"""Check original character capture messages without launching a Windows desktop."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock, call, patch

sys.path.insert(0, str(Path(__file__).resolve().parent / "reference_capture"))
import character_capture
import original_windows


class CharacterCapture(unittest.TestCase):
    def setUp(self):
        self.user = Mock()
        self.kernel = Mock()
        self.kernel.OpenProcess.return_value = 500
        self.kernel.VirtualAllocEx.return_value = 600
        self.kernel.WriteProcessMemory.return_value = 1
        self.process = Mock(pid=42)
        self.frames = [100]
        self.editors = [200]
        self.user.EnumWindows.side_effect = lambda callback, arg: [callback(hwnd, arg) for hwnd in self.frames]
        self.user.EnumChildWindows.side_effect = lambda frame, callback, arg: [
            callback(hwnd, arg) for hwnd in self.editors
        ]

        def owner(hwnd, pid):
            pid._obj.value = 42

        def name(hwnd, buffer, size):
            buffer.value = "Notepad4" if hwnd < 200 else "Scintilla"

        self.user.GetWindowThreadProcessId.side_effect = owner
        self.user.GetClassNameW.side_effect = name
        self.enterContext(
            patch.object(
                character_capture.c,
                "WinDLL",
                create=True,
                side_effect=lambda name, **_: self.user if name == "user32" else self.kernel,
            )
        )
        self.enterContext(
            patch.object(character_capture.c, "WINFUNCTYPE", create=True, return_value=lambda callback: callback)
        )
        self.launch = self.enterContext(patch.object(character_capture.subprocess, "Popen", return_value=self.process))
        self.sleep = self.enterContext(patch.object(original_windows.time, "sleep"))

    def session(self):
        return character_capture.Session(Path("Notepad4.exe"), Path("input.html"))

    def test_launch_arguments_allocation_and_first_enumeration_delay(self):
        session = self.session()
        self.launch.assert_called_once_with(["Notepad4.exe", "/ns", "/n", "input.html"])
        self.kernel.OpenProcess.assert_called_once_with(0x38, False, 42)
        self.kernel.VirtualAllocEx.assert_called_once_with(500, None, 65536, 0x3000, 4)
        self.sleep.assert_called_once_with(0.05)
        self.assertEqual((session.frame, session.editor), (100, 200))

    def test_missing_frame_keeps_fifteen_second_assertion_policy(self):
        self.frames = []
        with patch.object(original_windows.time, "monotonic", side_effect=[0, 0, 15]):
            with self.assertRaises(AssertionError) as caught:
                self.session()
        self.assertEqual(str(caught.exception), "[]")
        self.user.EnumWindows.assert_called_once()
        self.sleep.assert_called_once_with(0.05)
        self.process.terminate.assert_not_called()
        self.kernel.OpenProcess.assert_not_called()

    def test_nonunique_frames_or_editors_keep_assertion_policy(self):
        for frames, editors in (([100, 101], [200]), ([100], [200, 201]), ([100], [])):
            with self.subTest(frames=frames, editors=editors):
                self.frames, self.editors = frames, editors
                with self.assertRaises(AssertionError):
                    self.session()
                self.process.terminate.assert_not_called()
                self.kernel.OpenProcess.assert_not_called()

    def test_case_keeps_reset_save_input_undo_redo_order(self):
        session = self.session()
        events = []
        can_undo, can_redo = iter((1, 0)), iter((1, 0))

        def message(hwnd, identifier, first, second):
            events.append((hwnd, identifier, first, second))
            if identifier == 2174:
                return next(can_undo)
            if identifier == 2016:
                return next(can_redo)
            return 0

        def write(handle, memory, data, size, count):
            events.append(("write", data, size))
            return 1

        snapshots = iter(("after", "undo", "redo"))

        def state():
            snapshot = next(snapshots)
            events.append(("state", snapshot))
            return [snapshot]

        self.user.SendMessageW.side_effect = message
        self.kernel.WriteProcessMemory.side_effect = write
        with patch.object(session, "state", side_effect=state):
            captured = session.capture_case("中\0x", 4, ">", True, 1, 5, 6, True)
        self.assertEqual(captured, {"after": ["after"], "undo": [["undo"]], "redo": [["redo"]]})
        self.assertEqual(
            events,
            [
                (200, 2171, 0, 0),
                (200, 2422, 0, 0),
                (200, 2782, 0, 0),
                ("write", "中\0x".encode() + b"\0", 6),
                (200, 2181, 0, 600),
                (200, 2160, 1, 4),
                (200, 2573, 6, 5),
                (200, 2186, 1, 0),
                (100, 0x111, 40004, 0),
                (200, 2175, 0, 0),
                (200, 2014, 0, 0),
                (200, 2782, 3, 0),
                (200, 4003, 0, -1),
                (200, 2171, 1, 0),
                (200, 0x102, ord(">"), 0),
                ("state", "after"),
                (200, 2174, 0, 0),
                (200, 2176, 0, 0),
                ("state", "undo"),
                (200, 2174, 0, 0),
                (200, 2016, 0, 0),
                (200, 2011, 0, 0),
                ("state", "redo"),
                (200, 2016, 0, 0),
                (100, 0x111, 40004, 0),
            ],
        )

    def test_empty_and_buffer_boundary_inputs_include_one_terminating_nul(self):
        session = self.session()
        self.user.SendMessageW.return_value = 0
        for text in ("", "中" * 21845):
            with self.subTest(bytes=len(text.encode())), patch.object(session, "state", return_value=[]):
                self.user.SendMessageW.reset_mock()
                captured = session.capture_case(text, 0, "(", False, 0, -1, -1, False)
                payload = text.encode() + b"\0"
                self.kernel.WriteProcessMemory.assert_called_with(500, 600, payload, len(payload), None)
                self.assertLessEqual(len(payload), 65536)
                self.assertEqual(captured["undo"], [])
                self.assertEqual(captured["redo"], [])
                self.assertFalse(any(args.args[1] == 2573 for args in self.user.SendMessageW.call_args_list))

    def test_state_preserves_utf8_bytes_and_all_selection_directions(self):
        session = self.session()
        text = "中x".encode()

        def message(hwnd, identifier, first, second):
            if identifier == 2006:
                return len(text)
            if identifier == 2007:
                return text[first] - 256 if text[first] >= 128 else text[first]
            if identifier == 2579:
                return (4, 1)[first]
            if identifier == 2577:
                return (0, 3)[first]
            return {2009: 4, 2008: 0, 2159: 1, 2174: 1, 2016: 0, 2570: 2}[identifier]

        self.user.SendMessageW.side_effect = message
        self.assertEqual(session.state(), ["中x", 4, 0, 1, 1, 0, [[4, 0], [1, 3]]])

    def test_close_waits_before_releasing_remote_memory(self):
        session = self.session()
        calls = Mock()
        calls.attach_mock(self.user.SendMessageW, "send")
        calls.attach_mock(self.process.wait, "wait")
        calls.attach_mock(self.kernel.VirtualFreeEx, "free")
        calls.attach_mock(self.kernel.CloseHandle, "close")
        session.close()
        self.assertEqual(
            calls.mock_calls,
            [
                call.send(200, 2171, 0, 0),
                call.send(200, 2175, 0, 0),
                call.send(100, 0x10, 0, 0),
                call.wait(timeout=10),
                call.free(500, 600, 0, 0x8000),
                call.close(500),
            ],
        )

    def test_close_timeout_terminates_and_preserves_failure(self):
        session = self.session()
        failure = subprocess.TimeoutExpired("Notepad4.exe", 10)
        self.process.wait.side_effect = failure
        with self.assertRaises(subprocess.TimeoutExpired) as caught:
            session.close()
        self.assertIs(caught.exception, failure)
        self.process.terminate.assert_called_once_with()
        self.kernel.VirtualFreeEx.assert_not_called()

    def test_portable_host_keeps_the_existing_settings(self):
        with tempfile.TemporaryDirectory() as name:
            folder = Path(name)
            executable = folder / "source.exe"
            executable.write_bytes(b"original")
            host = character_capture.prepare_host(executable, folder / "output")
            self.assertEqual((host / "Notepad4.exe").read_bytes(), b"original")
            self.assertEqual(
                (host / "Notepad4.ini").read_text(encoding="utf-8"),
                "[Notepad4]\n[Settings]\nSaveSettings=0\nLoadASCIIasUTF8=0\nLoadANSIasUTF8=0\n"
                "NoEncodingTags=0\nSkipUnicodeDetection=0\n",
            )


if __name__ == "__main__":
    unittest.main()
