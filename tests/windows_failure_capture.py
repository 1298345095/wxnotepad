"""Preserve owned native windows immediately after a Windows test failure."""
import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path

from localization_layout import screenshot_windows


def capture(processes, destination, before_launch):
    destination.mkdir(parents=True, exist_ok=True)
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.SetProcessDpiAwarenessContext.argtypes = [wintypes.HANDLE]
    per_monitor_aware_v2 = wintypes.HANDLE(-4)
    if not user.SetProcessDpiAwarenessContext(per_monitor_aware_v2):
        raise ctypes.WinError(ctypes.get_last_error())
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowThreadProcessId.restype = wintypes.DWORD
    user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.IsIconic.argtypes = [wintypes.HWND]
    user.GetDpiForWindow.argtypes = [wintypes.HWND]
    user.GetForegroundWindow.restype = wintypes.HWND
    user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.IsWindow.argtypes = [wintypes.HWND]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR,
                                                ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]

    def identity(window):
        if not window:
            return None
        process = wintypes.DWORD()
        thread = user.GetWindowThreadProcessId(window, ctypes.byref(process))
        name = ctypes.create_unicode_buffer(128)
        user.GetClassNameW(window, name, len(name))
        record = {'window': window, 'exists': bool(user.IsWindow(window)), 'pid': process.value,
                  'thread': thread, 'class': name.value, 'process_image': None}
        process_query_limited_information = 0x1000
        handle = kernel.OpenProcess(process_query_limited_information, False, process.value)
        if handle:
            try:
                image = ctypes.create_unicode_buffer(32768)
                size = wintypes.DWORD(len(image))
                if kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(size)):
                    record['process_image'] = image.value
            finally:
                kernel.CloseHandle(handle)
        return record

    class GuiThreadInfo(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('flags', wintypes.DWORD),
                    ('active', wintypes.HWND), ('focus', wintypes.HWND),
                    ('capture', wintypes.HWND), ('menu_owner', wintypes.HWND),
                    ('move_size', wintypes.HWND), ('caret', wintypes.HWND),
                    ('caret_bounds', wintypes.RECT)]

    user.GetGUIThreadInfo.argtypes = [wintypes.DWORD, ctypes.POINTER(GuiThreadInfo)]
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
    foreground = user.GetForegroundWindow()
    foreground_identity = identity(foreground)
    before_foreground_identity = identity(before_launch.get('foreground'))
    records = []

    @callback
    def owned(window, unused):
        process = wintypes.DWORD()
        thread = user.GetWindowThreadProcessId(window, ctypes.byref(process))
        if process.value not in processes:
            return True
        bounds, client = wintypes.RECT(), wintypes.RECT()
        user.GetWindowRect(window, ctypes.byref(bounds))
        user.GetClientRect(window, ctypes.byref(client))
        gui = GuiThreadInfo()
        gui.size = ctypes.sizeof(gui)
        has_gui = user.GetGUIThreadInfo(thread, ctypes.byref(gui))
        record = {'pid': process.value, 'thread': thread, 'window': window,
                  'bounds': [bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top],
                  'client': [client.right - client.left, client.bottom - client.top],
                  'dpi': user.GetDpiForWindow(window), 'visible': bool(user.IsWindowVisible(window)),
                  'minimized': bool(user.IsIconic(window)), 'foreground': foreground == window,
                  'active': gui.active == window if has_gui else None,
                  'focus': gui.focus if has_gui else None, 'gui_flags': gui.flags if has_gui else None}
        if record['visible'] and not record['minimized'] and client.right > client.left and client.bottom > client.top:
            image = destination / f'process-{process.value}-window-{window}.png'
            try:
                screenshot_windows(user, window, image, presented=True)
                record['image'] = image.name
            except (AssertionError, OSError) as error:
                record['capture_error'] = str(error)
        records.append(record)
        return True

    if not user.EnumWindows(owned, 0):
        raise ctypes.WinError(ctypes.get_last_error())
    (destination / 'windows.json').write_text(json.dumps({'owned_processes': processes,
                                                        'before_helper_launch': before_launch,
                                                        'before_foreground_identity': before_foreground_identity,
                                                        'foreground_identity': foreground_identity,
                                                        'window_metadata_timing': 'after helper process started',
                                                        'capture': 'desktop DC pixels within owned window bounds; overlapping windows may cover them',
                                                        'windows': records}, indent=2) + '\n',
                                               encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--before-foreground', type=int)
    parser.add_argument('--before-active', type=int)
    parser.add_argument('--before-focus', type=int)
    arguments = parser.parse_args()
    before_launch = {'foreground': arguments.before_foreground, 'active': arguments.before_active,
                     'focus': arguments.before_focus}
    capture(arguments.pid, arguments.output, before_launch)


if __name__ == '__main__':
    main()
