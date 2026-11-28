"""Lock down the observed Windows exit-code-before-signaling race."""

import ctypes
import gc
import json
import os
import subprocess
import sys
import tempfile
import threading
import unittest
from ctypes import wintypes
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import localization_package
import startup_multifile_process

OWNED_PID = 1001
OWNED_HANDLE = 123
WAIT_OBJECT_0 = 0
WAIT_TIMEOUT = 258


class WindowsChildLifecycle(unittest.TestCase):
    def setUp(self):
        def exit_code(_handle, value):
            ctypes.cast(value, ctypes.POINTER(wintypes.DWORD)).contents.value = 0
            return True

        self.kernel = SimpleNamespace(
            OpenProcess=mock.Mock(return_value=OWNED_HANDLE),
            GetExitCodeProcess=mock.Mock(side_effect=exit_code),
            WaitForSingleObject=mock.Mock(side_effect=[WAIT_TIMEOUT, WAIT_OBJECT_0]),
            TerminateProcess=mock.Mock(return_value=True),
            CloseHandle=mock.Mock(return_value=True),
        )
        self.enterContext(mock.patch.object(sys, "platform", "win32"))
        self.enterContext(mock.patch.object(ctypes, "WinDLL", return_value=self.kernel, create=True))

    def test_available_exit_code_waits_for_signal_then_closes_once_and_caches(self):
        child = localization_package.Child(OWNED_PID)
        # The native capture observed exit code 0 while this handle timed out.
        self.assertIsNone(child.poll())
        self.assertIsNone(child.returncode)
        self.kernel.CloseHandle.assert_not_called()
        self.kernel.GetExitCodeProcess.assert_not_called()

        self.assertEqual(child.poll(), 0)
        self.assertEqual(child.returncode, 0)
        self.kernel.GetExitCodeProcess.assert_called_once()
        self.kernel.CloseHandle.assert_called_once_with(OWNED_HANDLE)
        self.assertEqual(child.poll(), 0)
        self.assertEqual(
            self.kernel.WaitForSingleObject.call_args_list, [mock.call(OWNED_HANDLE, 0), mock.call(OWNED_HANDLE, 0)]
        )
        self.kernel.OpenProcess.assert_called_once()
        del child
        gc.collect()
        self.kernel.CloseHandle.assert_called_once_with(OWNED_HANDLE)

    def test_failed_open_raises_without_closing_an_unowned_handle(self):
        self.kernel.OpenProcess.return_value = None
        with (
            mock.patch.object(ctypes, "get_last_error", return_value=5, create=True),
            mock.patch.object(ctypes, "WinError", side_effect=lambda code: OSError(code, "OpenProcess"), create=True),
        ):
            with self.assertRaises(OSError):
                localization_package.Child(OWNED_PID)
        gc.collect()
        self.kernel.CloseHandle.assert_not_called()


class ObservedStartupLifecycle(unittest.TestCase):
    def setUp(self):
        self.directory = Path(self.enterContext(tempfile.TemporaryDirectory(prefix="startup-lifecycle-")))
        self.real_popen = subprocess.Popen
        self.owned = []
        self.logs = []
        self.reapers = []
        self.addCleanup(self.cleanup)

    def spawn(self, arguments=None, **kwargs):
        kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        child = self.real_popen([sys.executable, "-c", "import time; time.sleep(120)"], **kwargs)
        self.owned.append(child)
        if "stdout" in kwargs:
            self.logs.append(kwargs["stdout"])
        return child

    def cleanup(self):
        for child in self.owned:
            if child.poll() is None:
                child.terminate()
            child.wait(timeout=5)
        for reaper in self.reapers:
            reaper.join(timeout=5)
        for log in self.logs:
            log.close()

    def constructor(self, observation):
        return (
            mock.patch.object(localization_package.subprocess, "Popen", side_effect=self.spawn),
            mock.patch.object(localization_package, "wait_state", **observation),
        )

    def test_failed_initial_observation_stops_real_child_and_closes_log(self):
        expected = AssertionError("controlled initial observation failure")
        launch, observe = self.constructor({"side_effect": expected})
        with launch, observe:
            with self.assertRaises(AssertionError) as caught:
                localization_package.Process(Path(sys.executable), self.directory / "app", dict(os.environ))
        self.assertIs(caught.exception, expected)
        self.assertIsNotNone(self.owned[0].poll(), "Constructor failure must not leave the actual child running")
        self.assertTrue(self.logs[0].closed)

    def test_success_keeps_real_child_and_log_until_explicit_stop(self):
        launch, observe = self.constructor({"return_value": {"shown": "1"}})
        with launch, observe:
            app = localization_package.Process(Path(sys.executable), self.directory / "app", dict(os.environ))
            self.assertIsNone(app.process.poll())
            self.assertFalse(app.log.closed)
            app.stop()
        self.assertIsNotNone(app.process.poll())
        self.assertTrue(app.log.closed)

    def test_cleanup_error_does_not_replace_initial_observation_error_or_leave_log_open(self):
        expected = AssertionError("controlled initial observation failure")
        launch, observe = self.constructor({"side_effect": expected})
        with (
            launch,
            observe,
            mock.patch.object(localization_package.Application, "stop", side_effect=OSError("cleanup")),
        ):
            with self.assertRaises(AssertionError) as caught:
                localization_package.Process(Path(sys.executable), self.directory / "app", dict(os.environ))
        self.assertIs(caught.exception, expected)
        self.assertTrue(self.logs[0].closed)
        self.assertTrue(any("cleanup" in note for note in getattr(expected, "__notes__", [])))

    def test_snapshot_error_preserves_original_failure_and_still_cleans_child(self):
        expected = AssertionError("controlled initial observation failure")
        launch, observe = self.constructor({"side_effect": expected})
        folder = self.directory / "window-routing-settings" / "app-002"
        with (
            launch,
            observe,
            mock.patch.object(localization_package, "observation_context", side_effect=OSError("capture"), create=True),
        ):
            with self.assertRaises(AssertionError) as caught:
                localization_package.Process(Path(sys.executable), folder, dict(os.environ))
        self.assertIs(caught.exception, expected)
        self.assertIsNotNone(self.owned[0].poll())
        self.assertTrue(self.logs[0].closed)
        self.assertTrue(any("capture" in note for note in getattr(expected, "__notes__", [])))

    def test_failed_startup_retains_last_raw_state_and_live_pid_before_cleanup(self):
        expected = AssertionError("controlled initial observation failure")
        raw = "sequence\t0\nshown\t1\nbackend\tGdkWaylandDisplay\nactive\t0\n"

        def failed_observation(folder, _process, _predicate):
            (folder / "state").write_text(raw, encoding="utf-8")
            raise expected

        launch, observe = self.constructor({"side_effect": failed_observation})
        folder = self.directory / "window-routing-settings" / "app-002"
        with launch, observe:
            with self.assertRaises(AssertionError) as caught:
                localization_package.Process(Path(sys.executable), folder, dict(os.environ))
        self.assertIs(caught.exception, expected)
        context = json.loads((folder / "startup-failure.json").read_text(encoding="utf-8"))[0]
        self.assertEqual(context["pid"], self.owned[0].pid)
        self.assertIsNone(context["returncode"])
        self.assertEqual(context["files"]["state"], raw)
        if sys.platform == "linux":
            self.assertEqual(int(context["linux"]["stat"].split(" ", 1)[0]), self.owned[0].pid)
            self.assertTrue(context["linux"]["wchan"].strip())
            host = json.loads((folder / "startup-host.json").read_text(encoding="utf-8"))
            self.assertGreater(host["available_cpu_count"], 0)
            self.assertEqual(host["affinity"], sorted(os.sched_getaffinity(0)))
            self.assertEqual(len(host["loadavg"].split()), 5)
            self.assertEqual(
                set(host),
                {"available_cpu_count", "affinity", "loadavg", "cgroup_cpu_max", "cpu_pressure", "io_pressure"},
            )
        else:
            self.assertNotIn("linux", context)
            self.assertFalse((folder / "startup-host.json").exists())
        self.assertIsNotNone(self.owned[0].poll())
        self.assertTrue(self.logs[0].closed)

    def multifile(self, cleanup_failure=False, capture_failure=False):
        executable = self.directory / "package" / "wxnotepad"
        executable.parent.mkdir()
        executable.touch()
        working = self.directory / "scenario"
        working.mkdir()
        previous = self.spawn()
        expected = AssertionError("controlled missing initial child observation")
        failed_cleanup_pids = []
        real_terminate = localization_package.Child.terminate

        def fail_first_cleanup(child):
            if not failed_cleanup_pids:
                failed_cleanup_pids.append(child.pid)
                raise OSError("cleanup")
            return real_terminate(child)

        def dispatch(_arguments, **kwargs):
            for _ in range(2):
                child = self.spawn(**kwargs)
                reaper = threading.Thread(target=child.wait, daemon=True)
                reaper.start()
                self.reapers.append(reaper)
            kwargs["creationflags"] = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
            return self.real_popen([sys.executable, "-c", "pass"], **kwargs)

        def owned_pids(_executable):
            return {child.pid for child in self.owned if child.poll() is None}

        with (
            mock.patch.object(startup_multifile_process.subprocess, "Popen", side_effect=dispatch),
            mock.patch.object(startup_multifile_process, "children", side_effect=expected),
            mock.patch.object(startup_multifile_process, "package_pids", side_effect=owned_pids),
            mock.patch.object(startup_multifile_process, "Dialogs", return_value=object()),
            mock.patch.dict(os.environ, {"WXNOTEPAD_DISPLAY_LOG_DIR": str(self.directory / "reports")}),
        ):
            if cleanup_failure:
                stop = fail_first_cleanup if cleanup_failure == "first" else mock.Mock(side_effect=OSError("cleanup"))
                with mock.patch.object(localization_package.Child, "terminate", autospec=True, side_effect=stop):
                    with self.assertRaises(AssertionError) as caught:
                        startup_multifile_process.partial_failures(executable, working, directory_only=True)
            else:
                with self.assertRaises(AssertionError) as caught:
                    startup_multifile_process.partial_failures(executable, working, directory_only=True)
        self.assertIs(caught.exception, expected)
        self.assertIsNone(
            previous.poll(), "A pre-existing process running the same package is not owned by this launch"
        )
        if cleanup_failure:
            self.assertTrue(any("cleanup" in note for note in getattr(expected, "__notes__", [])))
            if cleanup_failure == "first":
                alive = [child.pid for child in self.owned[1:] if child.poll() is None]
                self.assertEqual(alive, failed_cleanup_pids, "One failed cleanup must not skip the other owned child")
        else:
            self.assertTrue(all(child.poll() is not None for child in self.owned[1:]))
        if capture_failure:
            self.assertTrue(any("capture" in note for note in getattr(expected, "__notes__", [])))
        else:
            report = next((self.directory / "reports").glob("startup-directory-*"))
            contexts = json.loads((report / "startup-failure.json").read_text(encoding="utf-8"))
            self.assertEqual({row["pid"] for row in contexts[1:]}, {child.pid for child in self.owned[1:]})
            self.assertTrue(all(row["returncode"] is None for row in contexts[1:]))
            self.assertEqual(contexts[0]["returncode"], 0)
            if sys.platform == "linux":
                for field in ("stat", "wchan"):
                    self.assertIn("unavailable:", contexts[0]["linux"][field])
                self.assertTrue(all("state" in row["files"] for row in contexts))
                self.assertTrue(all(int(row["linux"]["stat"].split(" ", 1)[0]) == row["pid"] for row in contexts[1:]))
            self.assertEqual((report / "parent.log").read_bytes(), (working / "directory/parent.log").read_bytes())

    def test_multifile_failed_discovery_reaps_new_children_and_preserves_existing_same_package(self):
        self.multifile()

    def test_multifile_cleanup_error_does_not_replace_failed_discovery(self):
        self.multifile(cleanup_failure=True)

    def test_one_failed_child_cleanup_still_reaps_the_other_real_child(self):
        self.multifile(cleanup_failure="first")

    def test_multifile_capture_error_preserves_discovery_error_and_cleans_new_children(self):
        with mock.patch.object(startup_multifile_process, "_retain_startup_failure", side_effect=OSError("capture")):
            self.multifile(capture_failure=True)


if __name__ == "__main__":
    unittest.main()
