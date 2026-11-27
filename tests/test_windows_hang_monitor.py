"""Verify the Character Info parent monitor with real Windows processes and dumps."""

import ctypes
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from ctypes import wintypes
from pathlib import Path

MONITOR = Path(__file__).with_name("windows_hang_monitor.py")
PROCESS_SYNCHRONIZE = 0x00100000
CHILD_EXIT_WAIT_MILLISECONDS = 3000
WAIT_OBJECT_0 = 0
ERROR_INVALID_PARAMETER = 87


@unittest.skipUnless(sys.platform == "win32", "Windows parent-process hang evidence")
class CharacterInfoHangMonitor(unittest.TestCase):
    def setUp(self):
        evidence = os.environ.get("WXNOTEPAD_HANG_MONITOR_EVIDENCE")
        if evidence:
            Path(evidence).mkdir(parents=True, exist_ok=True)
            self.directory = Path(tempfile.mkdtemp(prefix="attempt-", dir=evidence))
            print("Hang monitor regression evidence:", self.directory, flush=True)
        else:
            temporary = tempfile.TemporaryDirectory(prefix="wxnotepad hang 测试 ")
            self.addCleanup(temporary.cleanup)
            self.directory = Path(temporary.name)
        self.dumps = self.directory / "dumps"

    def command(self, script, *arguments, after="45"):
        return [
            sys.executable,
            "-X",
            "utf8",
            str(MONITOR),
            "--dump-directory",
            str(self.dumps),
            "--dump-after",
            after,
            "--",
            sys.executable,
            "-X",
            "utf8",
            "-c",
            script,
            *map(str, arguments),
        ]

    def execute(self, command):
        return subprocess.run(command, cwd=self.directory, capture_output=True, text=True, encoding="utf-8", timeout=15)

    def wait_for_child_exit(self, pid):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        kernel.WaitForSingleObject.restype = wintypes.DWORD
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.OpenProcess(PROCESS_SYNCHRONIZE, False, pid)
        if handle:
            try:
                self.assertEqual(
                    kernel.WaitForSingleObject(handle, CHILD_EXIT_WAIT_MILLISECONDS),
                    WAIT_OBJECT_0,
                    "owned child must not remain alive",
                )
            finally:
                kernel.CloseHandle(handle)
        else:
            self.assertEqual(
                ctypes.get_last_error(), ERROR_INVALID_PARAMETER, "only an already exited PID is acceptable"
            )

    def test_ordinary_exit_preserves_numeric_status_arguments_cwd_and_output(self):
        arguments = ["space value", 'literal $value & "quotes"', "中文"]
        script = (
            'import json,os,sys; print(json.dumps({"cwd":os.getcwd(), "args":sys.argv[1:]})); '
            'print("child stderr", file=sys.stderr); raise SystemExit(7)'
        )
        result = self.execute(self.command(script, *arguments))
        self.assertEqual(result.returncode, 7, repr(result))
        payload = json.loads(next(line for line in result.stdout.splitlines() if line.startswith("{")))
        self.assertEqual(Path(payload["cwd"]), self.directory)
        self.assertEqual(payload["args"], arguments)
        self.assertIn("child stderr", result.stderr)
        self.assertFalse(self.dumps.exists(), "ordinary child must not trigger diagnostic work")

    def test_high_bit_windows_exit_dword_is_preserved(self):
        access_violation_exit = 0xC0000005
        script = (
            "import ctypes; from ctypes import wintypes; "
            'exit_process=ctypes.WinDLL("kernel32").ExitProcess; '
            "exit_process.argtypes=[wintypes.UINT]; exit_process.restype=None; "
            f"exit_process({access_violation_exit})"
        )
        result = self.execute(self.command(script))
        self.assertEqual(result.returncode, access_violation_exit, repr(result))
        self.assertFalse(self.dumps.exists())

    def test_parent_captures_thread_and_module_streams_without_changing_child_exit(self):
        result = self.execute(self.command("import time; time.sleep(2); raise SystemExit(9)", after=".1"))
        self.assertEqual(result.returncode, 9, repr(result))
        dumps = list(self.dumps.glob("*.dmp"))
        self.assertEqual(len(dumps), 1, repr(result))
        record = json.loads(dumps[0].with_suffix(".json").read_text(encoding="utf-8"))
        self.assertEqual(record["status"], "captured", record)
        self.assertNotEqual(record["process_id"], record["monitor_process_id"])
        self.assertEqual(Path(record["cwd"]), self.directory)
        data = dumps[0].read_bytes()
        signature, _, stream_count, directory_offset = struct.unpack_from("<4sIII", data)
        self.assertEqual(signature, b"MDMP")
        streams = {
            struct.unpack_from("<III", data, directory_offset + index * 12)[0]: struct.unpack_from(
                "<III", data, directory_offset + index * 12
            )[1:]
            for index in range(stream_count)
        }
        thread_list_stream, module_list_stream, thread_info_stream = 3, 4, 17
        for required in (thread_list_stream, module_list_stream, thread_info_stream):
            self.assertIn(required, streams)
        _, thread_offset = streams[thread_list_stream]
        self.assertGreater(struct.unpack_from("<I", data, thread_offset)[0], 0)
        self.assertIn("hang capture completed", result.stdout)

    @unittest.skipUnless(os.environ.get("WXNOTEPAD_GUI_PROBE"), "Native GUI executable supplied by CTest")
    def test_gui_child_output_is_forwarded(self):
        command = [
            sys.executable,
            "-X",
            "utf8",
            str(MONITOR),
            "--dump-directory",
            str(self.dumps),
            "--",
            os.environ["WXNOTEPAD_GUI_PROBE"],
        ]
        result = self.execute(command)
        self.assertEqual(result.returncode, 0, repr(result))
        self.assertIn("PASS: retained SDK capabilities", result.stdout)
        self.assertFalse(self.dumps.exists())

    def test_capture_failure_is_visible_and_does_not_shorten_child_lifetime(self):
        self.dumps.write_text("blocked diagnostic directory", encoding="utf-8")
        finished = self.directory / "finished"
        script = 'import sys,time; from pathlib import Path; time.sleep(.4); Path(sys.argv[1]).write_text("done")'
        result = self.execute(self.command(script, finished, after=".1"))
        self.assertEqual(result.returncode, 1, repr(result))
        self.assertIn("FAIL: Character Info hang capture:", result.stderr)
        self.assertEqual(finished.read_text(encoding="utf-8"), "done")

    def sleeping_child(self, marker):
        return self.command(
            "import os,sys,time; from pathlib import Path; "
            "Path(sys.argv[1]).write_text(str(os.getpid())); time.sleep(30)",
            marker,
        )

    def test_hard_monitor_termination_closes_job_and_owned_child(self):
        marker = self.directory / "child-pid"
        parent = subprocess.Popen(
            self.sleeping_child(marker),
            cwd=self.directory,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
        )
        try:
            self.assertIn("monitor owns pid=", parent.stdout.readline())
            until = time.monotonic() + 3
            while not marker.exists() and time.monotonic() < until:
                time.sleep(0.01)
            self.assertTrue(marker.exists(), "real child publishes its PID")
            child_pid = int(marker.read_text(encoding="utf-8"))
            parent.kill()
            parent.wait(timeout=5)
            self.wait_for_child_exit(child_pid)
        finally:
            if parent.poll() is None:
                parent.kill()
            parent.communicate(timeout=5)

    def test_original_ctest_timeout_terminates_monitor_and_child(self):
        self.assertIsNotNone(shutil.which("ctest"), "registered CTest regression requires CTest")
        marker = self.directory / "timeout-child-pid"

        def literal(value):
            return "[=[" + str(value) + "]=]"

        registration = "add_test(owned_child " + " ".join(map(literal, self.sleeping_child(marker))) + ")\n"
        registration += "set_tests_properties(owned_child PROPERTIES TIMEOUT 1)\n"
        (self.directory / "CTestTestfile.cmake").write_text(registration, encoding="utf-8")
        result = self.execute(["ctest", "--test-dir", str(self.directory), "--output-on-failure"])
        (self.directory / "timeout-ctest.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        self.assertEqual(result.returncode, 8, repr(result))
        self.assertIn("Timeout", result.stdout)
        self.assertTrue(marker.exists(), repr(result))
        self.wait_for_child_exit(int(marker.read_text(encoding="utf-8")))


if __name__ == "__main__":
    unittest.main()
