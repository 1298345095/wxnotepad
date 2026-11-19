"""Review document-tool captions and the directory chooser in packaged native apps."""
import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import sys
import tempfile
import time
from localization_process import Acceptance, PREFERENCE
from localization_package import menus, read_settings
from localization_layout import capture_and_check
from localization_native import plain

CAPTIONS = {
    'en': ('Execute &Document\tCtrl+L', 'O&pen Document With...', '&Save Before Running Tools',
           'Open Document With', 'Browse Directory...'),
    'zh_CN': ('执行文档(&D)\tCtrl+L', '打开文档方式(&P)...', '运行工具前保存(&S)', '打开文档方式', '浏览目录(B)...'),
    'zh_TW': ('執行文件(&D)\tCtrl+L', '開啟文件方式(&P)...', '執行工具前儲存(&S)', '開啟文件方式', '瀏覽目錄(B)...'),
}


def capture(fixture, app, window, path):
    if sys.platform != 'win32':
        capture_and_check(fixture.dialogs, app, window, path)
        return
    user = fixture.dialogs.user
    pid = wintypes.DWORD()
    user.GetWindowThreadProcessId(window, ctypes.byref(pid))
    assert pid.value == app.process.pid
    user.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
    user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                  ctypes.c_int, wintypes.UINT]
    topmost = bool(user.GetWindowLongW(window, -20) & 8)
    try:
        assert user.SetWindowPos(window, wintypes.HWND(-1), 0, 0, 0, 0, 0x13)
        ctypes.WinDLL('dwmapi').DwmFlush()
        capture_and_check(fixture.dialogs, app, window, path)
    finally:
        assert user.SetWindowPos(window, wintypes.HWND(-1 if topmost else -2), 0, 0, 0, 0, 0x13)


def change_directory(fixture, app, program_dialog, browse_caption, destination):
    dialogs = fixture.dialogs
    if sys.platform == 'win32':
        user = dialogs.user
        user.GetDlgCtrlID.argtypes = [wintypes.HWND]
        for child in dialogs.children(program_dialog):
            if plain(dialogs.title(child)) == browse_caption:
                user.PostMessageW(program_dialog, 0x111, user.GetDlgCtrlID(child), 0)
                break
        else:
            raise AssertionError(('browse button', dialogs.texts(program_dialog)))
    else:
        dialogs.click(program_dialog, browse_caption)
    deadline = time.monotonic() + 10
    folder = None
    while time.monotonic() < deadline:
        if sys.platform != 'win32':
            desktop = dialogs.atspi.get_desktop(0)
            desktop.clear_cache()
            for index in range(desktop.get_child_count()):
                application = desktop.get_child_at_index(index)
                if application is not None:
                    application.clear_cache()
        windows = [window for window in dialogs.windows(app.process) if dialogs.title(window) == dialogs.title(program_dialog)]
        if sys.platform == 'win32':
            windows = [window for window in windows if window != program_dialog]
        else:
            windows = [window for window in windows if window != program_dialog]
        if windows:
            folder = windows[-1]
            break
        time.sleep(.03)
    assert folder is not None, ('native program-directory picker appeared',
                               [(dialogs.title(window), window.get_role_name() if sys.platform != 'win32' else window)
                                for window in dialogs.windows(app.process)])
    if sys.platform == 'win32':
        for child in dialogs.children(folder):
            kind = ctypes.create_unicode_buffer(256)
            user.GetClassNameW(child, kind, len(kind))
            if kind.value == 'Edit':
                value = ctypes.create_unicode_buffer(str(destination))
                user.SendMessageW(child, 0xb1, 0, -1)
                user.SendMessageW(child, 0xc2, 1, ctypes.cast(value, ctypes.c_void_p).value)
                user.PostMessageW(folder, 0x111, 1, 0)
                break
        time.sleep(.3)
        if user.IsWindowVisible(folder):
            user.PostMessageW(folder, 0x111, 1, 0)
    else:
        dialogs.key(app, 'ctrl+l', folder)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            entries = [child for child in dialogs.children(folder)
                       if child.get_role() in (dialogs.atspi.Role.ENTRY, dialogs.atspi.Role.TEXT)
                       and child.get_editable_text_iface()
                       and child.get_state_set().contains(dialogs.atspi.StateType.FOCUSED)]
            if entries:
                assert entries[0].get_editable_text_iface().set_text_contents(str(destination))
                break
            time.sleep(.03)
        else:
            raise AssertionError('directory location entry must receive focus')
        dialogs.click(folder, 'Select')
    dialogs.wait_text(program_dialog, str(destination))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--cmake-dir', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='document-tools-localization-') as directory:
        fixture = Acceptance(args, Path(directory))
        programs = Path(directory) / 'available programs 中文'
        programs.mkdir()
        changed = Path(directory) / 'changed programs 中文'
        changed.mkdir()
        (programs / ('sample.lnk' if sys.platform == 'win32' else 'sample.desktop')).write_text('owned program entry')
        for code in ('en', 'zh_CN', 'zh_TW'):
            execute, open_with, save_tools, title, browse = CAPTIONS[code]
            app = fixture.start(PREFERENCE[code], preferred='en', extra=f'OpenWithDir={programs}\n', save=True)
            try:
                entries = menus(app.state)
                ids = {caption: next(identity for identity, item in entries.items() if item['label'] == caption)
                       for caption in (execute, open_with, save_tools)}
                assert entries[ids[execute]]['enabled'] == '0'
                assert entries[ids[open_with]]['enabled'] == '1'
                assert entries[ids[save_tools]]['checked'] == '0'
                app.choose(ids[save_tools])
                assert menus(app.state)[ids[save_tools]]['checked'] == '1'
                app.begin('menu', str(ids[open_with]))
                window = fixture.dialogs.wait(app.process, title)
                if code == 'en':
                    change_directory(fixture, app, window, browse, changed)
                capture(fixture, app, window, fixture.logs / f'open-with-{code}.png')
                fixture.dialogs.cancel(window)
                app.finish()
                app.close()
                config = read_settings(fixture.config)
                assert config['Settings']['SaveBeforeRunningTools'] == '1'
                assert config['Settings2']['UILanguage'] == str(PREFERENCE[code])
                if code == 'en':
                    assert 'changed programs' in config['Settings']['OpenWithDir']
            finally:
                app.stop()
    print('PASS: three native UI languages, directory Browse/Cancel persistence and shared preference round trips')


if __name__ == '__main__':
    main()
