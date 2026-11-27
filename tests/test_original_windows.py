"""Preserve first-match and strict original-window capture policies."""

import sys
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import capture_number_radix
import capture_url_fixtures
import original_windows


class Windows:
    def __init__(self, frames, children):
        self.frames = frames
        self.children = children
        self.windows = {
            100: (42, "Notepad4"),
            101: (42, "Notepad4"),
            200: (42, "Scintilla"),
            201: (42, "Scintilla"),
            300: (99, "Notepad4"),
            301: (42, "Other"),
        }
        self.enumerations = 0

    def EnumWindows(self, callback, parameter):
        self.enumerations += 1
        for hwnd in self.frames:
            callback(hwnd, parameter)

    def EnumChildWindows(self, frame, callback, parameter):
        for hwnd in self.children.get(frame, []):
            callback(hwnd, parameter)

    def GetWindowThreadProcessId(self, hwnd, owner):
        owner._obj.value = self.windows[hwnd][0]

    def GetClassNameW(self, hwnd, name, size):
        name.value = self.windows[hwnd][1]


class OriginalWindows(unittest.TestCase):
    def setUp(self):
        self.process = Mock(pid=42)
        self.callback = lambda callback: callback
        self.sleep = self.enterContext(patch.object(original_windows.time, "sleep"))
        self.enterContext(patch.object(capture_url_fixtures.c, "WINFUNCTYPE", return_value=self.callback, create=True))

    def test_owned_classes_are_filtered_and_radix_keeps_first_matches(self):
        user = Windows([300, 301, 100, 101], {100: [301, 200, 201]})
        self.assertEqual(capture_number_radix.find_editor_windows(user, self.process, self.callback), (100, 200))
        self.sleep.assert_called_once_with(0.05)
        self.process.terminate.assert_not_called()

    def test_url_accepts_one_owned_frame_and_editor(self):
        user = Windows([300, 301, 100], {100: [301, 200]})
        self.assertEqual(capture_url_fixtures.find_editor_windows(user, self.process), (100, 200))
        self.process.terminate.assert_not_called()

    def test_url_rejects_multiple_frames_or_editors_and_terminates(self):
        for frames, children, message in (
            ([100, 101], {100: [200]}, "expected owned original Notepad4 window: [100, 101]"),
            ([100], {100: [200, 201]}, "expected owned original Scintilla: [200, 201]"),
            ([100], {}, "expected owned original Scintilla: []"),
        ):
            with self.subTest(frames=frames, children=children):
                self.process.reset_mock()
                with self.assertRaises(RuntimeError) as caught:
                    capture_url_fixtures.find_editor_windows(Windows(frames, children), self.process)
                self.assertEqual(str(caught.exception), message)
                self.process.terminate.assert_called_once_with()

    def test_missing_frame_keeps_ten_second_deadline_and_failure_policy(self):
        for strict in (False, True):
            self.process.reset_mock()
            self.sleep.reset_mock()
            user = Windows([], {})
            with self.subTest(strict=strict), patch.object(original_windows.time, "monotonic", side_effect=[0, 0, 10]):
                with self.assertRaises(RuntimeError) as caught:
                    if strict:
                        capture_url_fixtures.find_editor_windows(user, self.process)
                    else:
                        capture_number_radix.find_editor_windows(user, self.process, self.callback)
            self.assertEqual(
                str(caught.exception),
                "expected owned original Notepad4 window: []" if strict else "original Notepad4 window was not found",
            )
            self.assertEqual(user.enumerations, 1)
            self.sleep.assert_called_once_with(0.05)
            self.assertEqual(self.process.terminate.call_count, int(strict))

    def test_radix_keeps_missing_editor_failure_without_termination(self):
        with self.assertRaises(IndexError):
            capture_number_radix.find_editor_windows(Windows([100], {}), self.process, self.callback)
        self.process.terminate.assert_not_called()


if __name__ == "__main__":
    unittest.main()
