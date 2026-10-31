"""Check the DPI mode of the actual editor process, including its child window."""
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    user32.GetAncestor.argtypes = [wintypes.HWND, wintypes.UINT]
    user32.GetAncestor.restype = wintypes.HWND
    user32.GetWindowDpiAwarenessContext.argtypes = [wintypes.HWND]
    user32.GetWindowDpiAwarenessContext.restype = wintypes.HANDLE
    user32.AreDpiAwarenessContextsEqual.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    user32.AreDpiAwarenessContextsEqual.restype = wintypes.BOOL
    user32.GetDpiForWindow.argtypes = [wintypes.HWND]
    user32.GetDpiForWindow.restype = wintypes.UINT
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.PostMessageW.restype = wintypes.BOOL

    with tempfile.TemporaryDirectory(prefix='wxnotepad-dpi-') as folder:
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 4  # SW_SHOWNOACTIVATE
        process = subprocess.Popen([str(Path(sys.argv[1]).resolve()), '--observe', folder],
                                   startupinfo=startup, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        frame = None
        try:
            state_path = Path(folder) / 'state'
            deadline = time.monotonic() + 10
            while not state_path.exists():
                if process.poll() is not None:
                    raise RuntimeError(f'Editor exited before publishing its window: {process.returncode}')
                if time.monotonic() >= deadline:
                    raise RuntimeError('Editor did not publish its window within 10 seconds')
                time.sleep(.02)
            state = dict(line.split('\t', 1) for line in state_path.read_text(encoding='utf-8').splitlines())
            editor = int(state['editor_handle'])
            frame = user32.GetAncestor(editor, 2)  # GA_ROOT
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(frame, ctypes.byref(owner))
            if owner.value != process.pid:
                frame = None
                raise RuntimeError('Published editor window does not belong to the launched process')
            pmv2 = wintypes.HANDLE(-4)
            frame_aware = bool(user32.AreDpiAwarenessContextsEqual(
                user32.GetWindowDpiAwarenessContext(frame), pmv2))
            editor_aware = bool(user32.AreDpiAwarenessContextsEqual(
                user32.GetWindowDpiAwarenessContext(editor), pmv2))
            print(json.dumps(dict(frame_per_monitor_v2=frame_aware,
                                  editor_per_monitor_v2=editor_aware,
                                  frame_dpi=user32.GetDpiForWindow(frame),
                                  editor_dpi=user32.GetDpiForWindow(editor))), flush=True)
            if not frame_aware or not editor_aware:
                raise RuntimeError('Frame and editor must use Per-Monitor V2 DPI awareness; '
                                   'DPI-unaware windows are bitmap-scaled on high-DPI displays')
        finally:
            if process.poll() is None and frame:
                user32.PostMessageW(frame, 0x0010, 0, 0)  # WM_CLOSE; test document is blank.
            try:
                output, _ = process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                output, _ = process.communicate()
            if output:
                print(output.decode('utf-8', errors='replace'), end='')
        if process.returncode:
            raise RuntimeError(f'Editor exited with code {process.returncode}')
    print('PASS: actual editor frame and child use Per-Monitor V2 DPI awareness')


if __name__ == '__main__':
    main()
