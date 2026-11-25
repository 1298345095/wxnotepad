"""Exercise session restoration at the native API boundary without changing the host."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.github/scripts'))
import windows_foreground


class ForegroundAPI:
    def __init__(self):
        self.timeout = 200_000
        self.error = 0
        self.reject_value = None
        self.ignore_zero = False
        self.read_error = False

    def SystemParametersInfoW(self, action, unused, value, flags):
        if flags:
            raise AssertionError('Session preparation must not persist or broadcast the setting')
        if action == windows_foreground.SPI_GETFOREGROUNDLOCKTIMEOUT:
            if self.read_error:
                self.error = 5
                return False
            ctypes.cast(value, ctypes.POINTER(wintypes.DWORD)).contents.value = self.timeout
            return True
        if action != windows_foreground.SPI_SETFOREGROUNDLOCKTIMEOUT:
            raise AssertionError('Unexpected native foreground action')
        requested = value.value if value is not None else 0
        if requested == self.reject_value:
            self.error = 5
            return False
        if requested or not self.ignore_zero:
            self.timeout = requested
        return True


class ForegroundSession(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='wxnotepad foreground ')
        self.addCleanup(self.temporary.cleanup)
        self.state = Path(self.temporary.name) / 'session.json'
        self.api = ForegroundAPI()
        self.original = self.api.timeout
        self.enterContext(mock.patch.object(windows_foreground, 'native_api', return_value=self.api))
        self.enterContext(mock.patch.object(ctypes, 'set_last_error',
                                           side_effect=lambda value: setattr(self.api, 'error', value), create=True))
        self.enterContext(mock.patch.object(ctypes, 'get_last_error', side_effect=lambda: self.api.error, create=True))
        self.enterContext(mock.patch.object(ctypes, 'WinError', side_effect=lambda code: OSError(code, 'native API'),
                                           create=True))

    def record(self):
        return json.loads(self.state.read_text(encoding='utf-8'))

    def test_success_restores_the_original_setting_and_records_readback(self):
        with windows_foreground.temporary(self.state):
            self.assertEqual(self.api.timeout, 0)
        self.assertEqual(self.api.timeout, self.original)
        record = self.record()
        self.assertTrue(record['set_succeeded'] and record['restore_succeeded'])
        self.assertEqual(record['effective_milliseconds'], 0)
        self.assertEqual(record['restored_milliseconds'], self.original)

    def test_failed_operation_propagates_after_restoring_the_session(self):
        observed = []

        def failed_operation():
            with windows_foreground.temporary(self.state):
                observed.append(self.api.timeout)
                raise RuntimeError('owned operation failed')

        with self.assertRaisesRegex(RuntimeError, 'owned operation failed'):
            failed_operation()
        self.assertEqual(observed, [0])
        self.assertEqual(self.api.timeout, self.original)
        self.assertTrue(self.record()['restore_succeeded'])

    def test_rejected_setup_does_not_enter_the_operation(self):
        self.api.reject_value = 0
        observed = []

        def operation():
            with windows_foreground.temporary(self.state):
                observed.append('entered')

        with self.assertRaisesRegex(SystemExit, 'setup was rejected'):
            operation()
        self.assertEqual(observed, [])
        self.assertEqual(self.api.timeout, self.original)
        self.assertFalse(self.record()['set_succeeded'])

    def test_ineffective_setup_does_not_run_and_restores_original_policy(self):
        self.api.ignore_zero = True
        observed = []

        def operation():
            with windows_foreground.temporary(self.state):
                observed.append('entered')

        with self.assertRaisesRegex(SystemExit, 'did not take effect'):
            operation()
        self.assertEqual(observed, [])
        self.assertEqual(self.api.timeout, self.original)
        self.assertEqual(self.record()['effective_milliseconds'], self.original)
        self.assertTrue(self.record()['restore_succeeded'])

    def test_failed_restoration_reports_failure_and_retains_original_value(self):
        self.api.reject_value = self.original

        def operation():
            with windows_foreground.temporary(self.state):
                pass

        with self.assertRaisesRegex(SystemExit, 'restoration failed'):
            operation()
        record = self.record()
        self.assertFalse(record['restore_succeeded'])
        self.assertEqual(record['original_milliseconds'], self.original)
        self.assertEqual(record['restored_milliseconds'], 0)
        self.assertEqual(self.api.timeout, 0)

    def test_initial_read_failure_preserves_session_and_creates_no_state(self):
        self.api.read_error = True
        with self.assertRaises(OSError):
            windows_foreground.prepare(self.state)
        self.assertEqual(self.api.timeout, self.original)
        self.assertFalse(self.state.exists())

    def test_existing_state_is_preserved_without_mutating_the_session(self):
        before = b'preserve prior session\n'
        self.state.write_bytes(before)
        with self.assertRaises(FileExistsError):
            windows_foreground.prepare(self.state)
        self.assertEqual(self.state.read_bytes(), before)
        self.assertEqual(self.api.timeout, self.original)

    def test_missing_restoration_state_does_not_invent_a_setting(self):
        windows_foreground.restore(self.state)
        self.assertEqual(self.api.timeout, self.original)
        self.assertFalse(self.state.exists())


if __name__ == '__main__':
    unittest.main()
