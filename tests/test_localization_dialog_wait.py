"""Exercise native dialog discovery across transient accessibility reads."""

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

import localization_native


class NativeReadError(Exception):
    def __init__(self, message="timeout from dbind", domain="atspi_error", code=1):
        super().__init__(message)
        self.message = message
        self.domain = domain
        self.code = code


class DialogWait(unittest.TestCase):
    def setUp(self):
        self.now = 0.0
        self.window = Mock()
        self.window.get_name.return_value = "Owned chooser role readiness"
        self.window.get_role.return_value = "frame"
        self.window.get_state_set.return_value.contains.return_value = True
        self.process = SimpleNamespace(pid=123, poll=lambda: None)
        self.dialogs = localization_native.Dialogs.__new__(localization_native.Dialogs)
        self.dialogs.evidence = None
        self.dialogs.wait_sequence = 0
        self.dialogs.atspi = SimpleNamespace(
            Role=SimpleNamespace(ALERT="alert"), StateType=SimpleNamespace(SHOWING="showing")
        )
        self.dialogs.windows = Mock(return_value=[self.window])
        self.dialogs.wayland_mapping = Mock(return_value=(False, "not mapped yet"))
        self.dialogs.observe_wait_failure = Mock()
        repository = SimpleNamespace(GLib=SimpleNamespace(GError=NativeReadError))
        for replacement in (
            patch.dict("sys.modules", {"gi.repository": repository}),
            patch.object(localization_native.sys, "platform", "linux"),
            patch.dict(localization_native.os.environ, {"GDK_BACKEND": "wayland"}),
            patch.object(localization_native.time, "monotonic", side_effect=lambda: self.now),
            patch.object(localization_native.time, "sleep", side_effect=self.advance),
        ):
            replacement.start()
            self.addCleanup(replacement.stop)

    def advance(self, seconds):
        self.now += seconds

    def test_role_timeout_during_mapping_recovers_within_original_wait(self):
        self.dialogs.wayland_mapping.side_effect = [(False, "before map"), (True, "mapped")]
        self.window.get_role.side_effect = NativeReadError()
        self.assertIs(self.dialogs.wait(self.process, "Owned chooser role readiness"), self.window)
        self.assertEqual(self.window.get_role.call_count, 1)
        self.assertEqual(self.dialogs.wayland_mapping.call_count, 2)
        self.assertLess(self.now, 10)

    def test_available_owned_dialog_is_found_before_slow_unrelated_window_body(self):
        main = Mock()
        main.get_name.return_value = "Owned editor"
        main.get_role.return_value = "frame"

        def slow_native_children():
            self.advance(10)
            return 0

        main.get_child_count.side_effect = slow_native_children
        self.dialogs.windows.return_value = [main, self.window]
        self.dialogs.wayland_mapping.return_value = (True, "owned dialog is mapped")
        self.assertIs(
            self.dialogs.wait(self.process, "Owned chooser role readiness", "Expected native message"), self.window
        )
        self.assertLess(self.now, 10, "An already available dialog must retain the original discovery budget")

    def test_expired_mapping_budget_retains_native_failure_snapshot(self):
        title = "Owned chooser role readiness"

        def delayed_native_title():
            if self.now == 0:
                self.advance(10)
            return title

        self.window.get_name.side_effect = delayed_native_title
        self.window.get_state_set.return_value.get_states.return_value = ["showing"]
        self.dialogs.wayland_mapping = localization_native.Dialogs.wayland_mapping.__get__(self.dialogs)
        self.dialogs.observe_wait_failure = localization_native.Dialogs.observe_wait_failure.__get__(self.dialogs)
        folder = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.dialogs.evidence = folder
        self.enterContext(patch("subprocess.run", return_value=SimpleNamespace(stdout="Owned native scene", stderr="")))
        with self.assertRaisesRegex(AssertionError, "Native dialog mapping deadline expired"):
            self.dialogs.wait(self.process, title)
        retained = json.loads((folder / "native-wait-123-1.json").read_text(encoding="utf-8"))
        self.assertEqual(retained["expected"], title)
        self.assertEqual(retained["windows"][0]["title"], title)
        self.assertEqual((folder / "native-wait-123-1.scene.txt").read_text(encoding="utf-8"), "Owned native scene")
        self.assertEqual(self.now, 10, "Retention must not restart the discovery budget")

    def test_snapshot_error_does_not_replace_expired_mapping_verdict(self):
        title = "Owned chooser role readiness"

        def delayed_native_title():
            if self.now == 0:
                self.advance(10)
            return title

        self.window.get_name.side_effect = delayed_native_title
        self.window.get_state_set.return_value.get_states.return_value = ["showing"]
        self.dialogs.wayland_mapping = localization_native.Dialogs.wayland_mapping.__get__(self.dialogs)
        self.dialogs.observe_wait_failure = localization_native.Dialogs.observe_wait_failure.__get__(self.dialogs)
        folder = Path(self.enterContext(tempfile.TemporaryDirectory()))
        self.dialogs.evidence = folder
        self.enterContext(patch("subprocess.run", side_effect=OSError("Native scene service unavailable")))
        with self.assertRaisesRegex(AssertionError, "Native dialog mapping deadline expired") as raised:
            self.dialogs.wait(self.process, title)
        self.assertTrue((folder / "native-wait-123-1.json").is_file())
        self.assertIn("Native scene service unavailable", raised.exception.__notes__[0])
        self.assertEqual(self.now, 10)

    def test_alert_role_timeout_recovers_before_matching_message(self):
        self.window.get_name.return_value = "Question"
        self.window.get_role.side_effect = [NativeReadError(), "alert"]
        self.dialogs.texts = Mock(return_value=["Save changes to example.txt?"])
        self.assertIs(self.dialogs.wait(self.process, "Save changes?"), self.window)
        self.assertEqual(self.window.get_role.call_count, 2)
        self.dialogs.texts.assert_called_once_with(self.window)

    def test_other_native_errors_propagate(self):
        for error in (
            NativeReadError(domain="other"),
            NativeReadError(code=2),
            NativeReadError(message="Object does not exist"),
        ):
            with self.subTest(error=vars(error)):
                self.window.get_role.side_effect = error
                with self.assertRaises(NativeReadError) as raised:
                    self.dialogs.wait(self.process, "Owned chooser role readiness")
                self.assertIs(raised.exception, error)

    def test_persistent_timeout_exhausts_original_ten_second_budget(self):
        def busy_role():
            self.advance(1)
            raise NativeReadError()

        self.window.get_role.side_effect = busy_role
        with self.assertRaisesRegex(AssertionError, "timeout from dbind"):
            self.dialogs.wait(self.process, "Owned chooser role readiness")
        self.assertGreaterEqual(self.now, 10)
        self.assertLess(self.now, 11)

    def test_unmapped_window_keeps_original_deadline(self):
        with self.assertRaisesRegex(AssertionError, "not ready"):
            self.dialogs.wait(self.process, "Owned chooser role readiness")
        self.assertGreaterEqual(self.now, 10)
        self.assertLess(self.now, 10.1)

    def test_process_exit_is_reported_before_another_native_read(self):
        self.process.poll = lambda: 7
        self.process.returncode = 7
        with self.assertRaisesRegex(AssertionError, "7"):
            self.dialogs.wait(self.process, "Owned chooser role readiness")
        self.dialogs.windows.assert_not_called()


if __name__ == "__main__":
    unittest.main()
