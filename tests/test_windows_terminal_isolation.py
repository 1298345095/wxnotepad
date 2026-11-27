"""Verify Terminal hiding receipts and process lifetime at the Win32 boundary."""

import ctypes
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".github/scripts"))
import windows_context_investigation as investigation

WAIT_TIMEOUT = 0x102
TERMINAL_CLASS = "CASCADIA_HOSTING_WINDOW_CLASS"


class TerminalAPI:
    def __init__(self):
        self.windows = {
            101: {"class": TERMINAL_CLASS, "pid": 501, "visible": True},
            102: {"class": TERMINAL_CLASS, "pid": 502, "visible": True},
            103: {"class": "OtherWindow", "pid": 503, "visible": True},
        }
        self.processes = {
            501: {"image": "C:/Program Files/WindowsTerminal.exe", "alive": True},
            502: {"image": "C:/tools/other.exe", "alive": True},
            503: {"image": "C:/Program Files/WindowsTerminal.exe", "alive": True},
        }
        self.error = 0
        self.enumerate_ok = True
        self.open_ok = True
        self.query_ok = True
        self.hide_ok = True
        self.handles = set()
        self.user = SimpleNamespace(
            **{
                name: mock.Mock(side_effect=method)
                for name, method in (
                    ("EnumWindows", self.enumerate),
                    ("GetClassNameW", self.class_name),
                    ("GetWindowThreadProcessId", self.process_id),
                    ("IsWindowVisible", lambda window: self.windows[window]["visible"]),
                    ("ShowWindow", self.hide),
                )
            }
        )
        self.kernel = SimpleNamespace(
            **{
                name: mock.Mock(side_effect=method)
                for name, method in (
                    ("OpenProcess", self.open),
                    ("QueryFullProcessImageNameW", self.image),
                    ("WaitForSingleObject", self.wait),
                    ("CloseHandle", self.close),
                )
            }
        )

    def enumerate(self, callback, unused):
        if not self.enumerate_ok:
            self.error = 5
            return False
        for window in self.windows:
            callback(window, 0)
        return True

    def class_name(self, window, value, unused):
        value.value = self.windows[window]["class"]

    def process_id(self, window, value):
        value._obj.value = self.windows[window]["pid"]

    def hide(self, window, command):
        if command != 0:
            raise AssertionError("Isolation must hide the window, not minimize or close it")
        if self.hide_ok:
            self.windows[window]["visible"] = False

    def open(self, unused_access, unused_inherit, process):
        if not self.open_ok:
            self.error = 5
            return None
        self.handles.add(process)
        return process

    def image(self, handle, unused, value, size):
        if not self.query_ok:
            self.error = 5
            return False
        value.value = self.processes[handle]["image"]
        size._obj.value = len(value.value)
        return True

    def wait(self, handle, unused):
        return WAIT_TIMEOUT if self.processes[handle]["alive"] else 0

    def close(self, handle):
        self.handles.remove(handle)
        return True


class TerminalIsolation(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wxnotepad terminal ")
        self.addCleanup(self.temporary.cleanup)
        self.reports = Path(self.temporary.name)
        self.api = TerminalAPI()
        libraries = {"user32": self.api.user, "kernel32": self.api.kernel}
        self.enterContext(
            mock.patch.object(ctypes, "WinDLL", side_effect=lambda name, **unused: libraries[name], create=True)
        )
        self.enterContext(
            mock.patch.object(ctypes, "WINFUNCTYPE", side_effect=lambda *unused: lambda callback: callback, create=True)
        )
        self.enterContext(mock.patch.object(ctypes, "get_last_error", side_effect=lambda: self.api.error, create=True))
        self.enterContext(
            mock.patch.object(ctypes, "WinError", side_effect=lambda code: OSError(code, "native API"), create=True)
        )

    def receipts(self):
        return json.loads((self.reports / "terminal-isolation.json").read_text(encoding="utf-8"))

    def test_only_verified_terminal_is_hidden_and_all_processes_stay_alive(self):
        investigation.hide_terminal_windows(self.reports)
        records = self.receipts()
        self.assertEqual(len(records), 1)
        self.assertEqual((records[0]["window"], records[0]["pid"]), (101, 501))
        self.assertTrue(records[0]["visible_before"])
        self.assertFalse(records[0]["visible_after"])
        self.assertTrue(records[0]["process_alive"])
        self.assertFalse(self.api.windows[101]["visible"])
        self.assertTrue(self.api.windows[102]["visible"] and self.api.windows[103]["visible"])
        self.assertTrue(all(process["alive"] for process in self.api.processes.values()))
        self.assertEqual(self.api.handles, set())

    def test_no_verified_terminal_is_not_a_successful_isolation(self):
        self.api.processes[501]["image"] = "C:/tools/other.exe"
        with self.assertRaisesRegex(SystemExit, "not established"):
            investigation.hide_terminal_windows(self.reports)
        self.assertEqual(self.receipts(), [])
        self.assertTrue(all(window["visible"] for window in self.api.windows.values()))
        self.assertEqual(self.api.handles, set())

    def test_ineffective_hiding_retains_the_failed_receipt(self):
        self.api.hide_ok = False
        with self.assertRaisesRegex(SystemExit, "not established"):
            investigation.hide_terminal_windows(self.reports)
        self.assertTrue(self.receipts()[0]["visible_after"])
        self.assertEqual(self.api.handles, set())

    def test_dead_process_is_not_reported_as_an_isolated_running_runner(self):
        self.api.processes[501]["alive"] = False
        with self.assertRaisesRegex(SystemExit, "not established"):
            investigation.hide_terminal_windows(self.reports)
        self.assertFalse(self.receipts()[0]["process_alive"])
        self.assertEqual(self.api.handles, set())

    def test_native_image_query_failure_releases_the_process_handle(self):
        self.api.query_ok = False
        with self.assertRaises(OSError):
            investigation.hide_terminal_windows(self.reports)
        self.assertEqual(self.api.handles, set())
        self.assertFalse((self.reports / "terminal-isolation.json").exists())

    def test_failed_window_enumeration_is_not_an_empty_success(self):
        self.api.enumerate_ok = False
        with self.assertRaises(OSError):
            investigation.hide_terminal_windows(self.reports)
        self.assertFalse((self.reports / "terminal-isolation.json").exists())

    def test_failed_process_open_does_not_hide_an_unverified_window(self):
        self.api.open_ok = False
        with self.assertRaises(OSError):
            investigation.hide_terminal_windows(self.reports)
        self.assertTrue(all(window["visible"] for window in self.api.windows.values()))
        self.assertEqual(self.api.handles, set())


if __name__ == "__main__":
    unittest.main()
