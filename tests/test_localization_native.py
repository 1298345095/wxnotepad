"""Verify Wayland alert readiness, Linux button state checks and alert dismissal synchronization."""
from pathlib import Path
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import localization_native as LN


class FakeGError(Exception):
    def __init__(self, message, domain='g-io-error-quark', code=0):
        super().__init__(message)
        self.message = message
        self.domain = domain
        self.code = code


class FakeStateSet:
    def __init__(self, states):
        self._states = set(states)

    def contains(self, state):
        return state in self._states


class FakeAction:
    def __init__(self, callback):
        self._callback = callback

    def do_action(self, index):
        assert index == 0
        self._callback()
        return True


class FakeNode:
    def __init__(self, role, name, states=(), children=(), action=None, on_clear=None):
        self._role = role
        self._name = name
        self._states = set(states)
        self._children = list(children)
        self._action = action
        self._on_clear = on_clear
        self.clear_calls = 0

    def get_role(self):
        return self._role

    def get_name(self):
        return self._name

    def get_state_set(self):
        return FakeStateSet(self._states)

    def clear_cache(self):
        self.clear_calls += 1
        if self._on_clear is not None:
            self._on_clear(self)

    def get_action_iface(self):
        return self._action


def make_linux_dialogs(evidence=None):
    dialogs = LN.Dialogs.__new__(LN.Dialogs)
    dialogs.evidence = evidence
    dialogs.wait_sequence = 0
    dialogs.key_sequence = 0
    dialogs.atspi = SimpleNamespace(
        Role=SimpleNamespace(ALERT='alert', DIALOG='dialog', PUSH_BUTTON='push-button',
                             CHECK_BOX='check-box', LABEL='label'),
        StateType=SimpleNamespace(SHOWING='showing', VISIBLE='visible', SENSITIVE='sensitive'),
    )
    dialogs.glib = SimpleNamespace(GError=FakeGError)
    return dialogs


class LocalizationNativeReadiness(unittest.TestCase):
    def test_wayland_wait_gates_alert_matched_by_message_or_fragment_on_mapping_and_showing(self):
        for title, message, label_text in (
                ('保存修改？', '打开另一个文件前', '打开另一个文件前保存对 policy-A.txt 的修改？'),
                ('Save changes?', None, 'Save changes to policy-A.txt before opening another file?'),
        ):
            with self.subTest(title=title, message=message), tempfile.TemporaryDirectory() as folder:
                evidence = Path(folder)
                dialogs = make_linux_dialogs(evidence)
                label = FakeNode(dialogs.atspi.Role.LABEL, label_text)
                alert = FakeNode(dialogs.atspi.Role.ALERT, 'Question', states=(), children=[label])
                mapping_calls = []

                def fake_mapping(pid, expected_title, deadline):
                    mapping_calls.append((pid, expected_title))
                    if len(mapping_calls) == 1:
                        return False, 'unmapped scene'
                    if len(mapping_calls) == 2:
                        return True, 'mapped but not showing'
                    alert._states.add(dialogs.atspi.StateType.SHOWING)
                    return True, 'mapped and showing'

                process = SimpleNamespace(pid=4242, poll=lambda: None, returncode=None)
                with mock.patch.object(LN.sys, 'platform', 'linux'), \
                        mock.patch.dict(LN.os.environ, {'GDK_BACKEND': 'wayland'}), \
                        mock.patch.object(dialogs, 'windows', return_value=[alert]), \
                        mock.patch.object(dialogs, 'children', return_value=[label]), \
                        mock.patch.object(dialogs, 'wayland_mapping', side_effect=fake_mapping), \
                        mock.patch.object(LN.time, 'sleep'):
                    matched = dialogs.wait(process, title, message=message)

                self.assertIs(matched, alert)
                self.assertEqual(mapping_calls, [(4242, title)] * 3)
                self.assertEqual(alert.clear_calls, 3)
                self.assertEqual((evidence / 'native-wait-4242-1-before-map.scene.txt').read_text(encoding='utf-8'),
                                 'unmapped scene')
                self.assertEqual((evidence / 'native-wait-4242-1-mapped.scene.txt').read_text(encoding='utf-8'),
                                 'mapped and showing')

    def test_linux_click_waits_for_visible_showing_sensitive_and_alert_dismissal(self):
        dialogs = make_linux_dialogs()
        state_steps = [
            {dialogs.atspi.StateType.VISIBLE, dialogs.atspi.StateType.SHOWING},
            {dialogs.atspi.StateType.VISIBLE, dialogs.atspi.StateType.SENSITIVE},
            {dialogs.atspi.StateType.VISIBLE, dialogs.atspi.StateType.SHOWING, dialogs.atspi.StateType.SENSITIVE},
        ]
        dismissal_checks = 0
        clicked = []

        def advance_button(node):
            if state_steps:
                node._states = state_steps.pop(0)

        def on_click():
            clicked.append('clicked')

        button = FakeNode(dialogs.atspi.Role.PUSH_BUTTON, '保存(_S)',
                          action=FakeAction(on_click), on_clear=advance_button)

        def advance_alert(node):
            nonlocal dismissal_checks
            if clicked:
                dismissal_checks += 1
                if dismissal_checks == 2:
                    raise FakeGError('timeout from dbind', domain='atspi_error', code=LN.ATSPI_IPC_ERROR)
                if dismissal_checks >= 3:
                    node._states.discard(dialogs.atspi.StateType.SHOWING)

        alert = FakeNode(dialogs.atspi.Role.ALERT, 'Question',
                         states={dialogs.atspi.StateType.SHOWING}, children=[button], on_clear=advance_alert)

        with mock.patch.object(LN.sys, 'platform', 'linux'), \
                mock.patch.object(dialogs, 'children', return_value=[button]), \
                mock.patch.object(LN.time, 'sleep'):
            dialogs.click(alert, '保存(S)', deadline=time.monotonic() + 5)

        self.assertEqual(clicked, ['clicked'])
        self.assertEqual(button.clear_calls, 3)
        self.assertEqual(dismissal_checks, 3)

    def test_linux_answer_handles_destroyed_alert_and_non_alert_click_returns_immediately(self):
        dialogs = make_linux_dialogs()
        clicked = []

        def on_alert_click():
            clicked.append('alert')

        alert_button = FakeNode(
            dialogs.atspi.Role.PUSH_BUTTON, 'Save',
            states={dialogs.atspi.StateType.VISIBLE, dialogs.atspi.StateType.SHOWING,
                    dialogs.atspi.StateType.SENSITIVE},
            action=FakeAction(on_alert_click),
        )

        def alert_destroyed(node):
            if clicked:
                raise FakeGError('Object does not exist')

        alert = FakeNode(dialogs.atspi.Role.ALERT, 'Question',
                         states={dialogs.atspi.StateType.SHOWING}, children=[alert_button],
                         on_clear=alert_destroyed)
        process = SimpleNamespace(pid=77)
        with mock.patch.object(LN.sys, 'platform', 'linux'), \
                mock.patch.object(dialogs, 'children', return_value=[alert_button]):
            returned = dialogs.answer(process, 'yes', window=alert)
        self.assertIs(returned, alert)
        self.assertEqual(clicked, ['alert'])

        # Non-alert dialogs (such as Find/Replace) stay open after clicking a button.
        dialog_clicks = []
        find_button = FakeNode(
            dialogs.atspi.Role.PUSH_BUTTON, 'Find All',
            states={dialogs.atspi.StateType.VISIBLE, dialogs.atspi.StateType.SHOWING,
                    dialogs.atspi.StateType.SENSITIVE},
            action=FakeAction(lambda: dialog_clicks.append('find')),
        )
        dialog = FakeNode(dialogs.atspi.Role.DIALOG, 'Find and Replace',
                          states={dialogs.atspi.StateType.SHOWING}, children=[find_button])
        with mock.patch.object(LN.sys, 'platform', 'linux'), \
                mock.patch.object(dialogs, 'children', return_value=[find_button]):
            dialogs.click(dialog, 'Find All', deadline=time.monotonic() + 5)
        self.assertEqual(dialog_clicks, ['find'])
        self.assertTrue(dialog.get_state_set().contains(dialogs.atspi.StateType.SHOWING))


if __name__ == '__main__':
    unittest.main()
