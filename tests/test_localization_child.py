"""Lock down the observed Windows exit-code-before-signaling race."""
import ctypes
from ctypes import wintypes
import gc
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

import localization_package

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
            CloseHandle=mock.Mock(return_value=True))
        self.enterContext(mock.patch.object(sys, 'platform', 'win32'))
        self.enterContext(mock.patch.object(ctypes, 'WinDLL', return_value=self.kernel, create=True))

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
        self.assertEqual(self.kernel.WaitForSingleObject.call_args_list,
                         [mock.call(OWNED_HANDLE, 0), mock.call(OWNED_HANDLE, 0)])
        self.kernel.OpenProcess.assert_called_once()
        del child
        gc.collect()
        self.kernel.CloseHandle.assert_called_once_with(OWNED_HANDLE)

    def test_failed_open_raises_without_closing_an_unowned_handle(self):
        self.kernel.OpenProcess.return_value = None
        with mock.patch.object(ctypes, 'get_last_error', return_value=5, create=True), \
                mock.patch.object(ctypes, 'WinError', side_effect=lambda code: OSError(code, 'OpenProcess'),
                                  create=True):
            with self.assertRaises(OSError):
                localization_package.Child(OWNED_PID)
        gc.collect()
        self.kernel.CloseHandle.assert_not_called()


if __name__ == '__main__':
    unittest.main()
