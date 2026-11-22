"""Capture original real titles/menu checks without using the clipboard.

Requires the serialized host GUI slot and an isolated pinned original executable.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import shutil
import time
from original_host import Original, CALLBACK


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--language', choices=('en', 'zh-Hans', 'zh-Hant'), default='en')
    parser.add_argument('--locale-dir', type=Path)
    parser.add_argument('--native-ansi', action='store_true')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    portable = args.output / 'portable'
    portable.mkdir()
    executable = portable / 'Notepad4.exe'
    shutil.copy2(args.executable, executable)
    if args.locale_dir:
        shutil.copytree(args.locale_dir, portable / 'locale')
    preference = {'en': 1033, 'zh-Hans': 2052, 'zh-Hant': 1028}[args.language]
    (portable / 'Notepad4.ini').write_text('[Settings]\nSaveSettings=0\nWordWrap=0\n'
                                         f'[Settings2]\nUILanguage={preference}\n', encoding='utf-16')
    user = c.WinDLL('user32', use_last_error=True)
    user.SendMessageW.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
    user.SendMessageW.restype = c.c_ssize_t
    user.PostMessageW.argtypes = user.SendMessageW.argtypes
    user.WaitForInputIdle.argtypes = [w.HANDLE, w.DWORD]
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.GetMenu.argtypes = [w.HWND]
    user.GetMenu.restype = w.HMENU
    user.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]
    user.GetDlgItem.argtypes = [w.HWND, c.c_int]
    user.GetDlgItem.restype = w.HWND
    user.GetDlgCtrlID.argtypes = [w.HWND]
    user.IsWindow.argtypes = [w.HWND]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.IsWindowEnabled.argtypes = [w.HWND]
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    desktop_name = f'wxnotepad-title-{os.getpid()}'
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x1ff, None)
    assert desktop
    ansi_path = args.output / 'native-ansi.txt'
    if args.native_ansi:
        ansi_path.write_bytes(('中' * 200).encode('mbcs', errors='replace'))
    original = Original(ansi_path if args.native_ansi else None, executable, desktop, desktop_name,
                        'ansi' if args.native_ansi else 'utf-8')
    result = {'revision': '89b8b36e0ee2c23f73be922315f17261ccfbbe1a',
              'language': args.language,
              'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(), 'cases': []}
    if args.locale_dir:
        result['locale_sha256'] = {str(path.relative_to(args.locale_dir)): hashlib.sha256(path.read_bytes()).hexdigest()
                                   for path in args.locale_dir.rglob('*.dll')}
    result['native_code_page'] = c.WinDLL('kernel32').GetACP()

    def record(name, before=None):
        user.SendMessageW(original.hwnd, 0x116, user.GetMenu(original.hwnd), 0)
        text = c.create_unicode_buffer(2048)
        user.GetWindowTextW(original.hwnd, text, len(text))
        row = {'name': name, 'title': text.value,
               'checks': [bool(user.GetMenuState(user.GetMenu(original.hwnd), command, 0) & 8)
                          for command in range(40430, 40434)], 'state': original.state()}
        if before is not None:
            assert before == row['state'], name + ' preserves text/selection/history'
        result['cases'].append(row)
        (args.output / 'samples.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        print(name, repr(text.value), flush=True)

    def file_dialog(command, path):
        filename_diagnostic = None
        user.PostMessageW(original.hwnd, 0x111, command, 0)
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            found = []
            @CALLBACK
            def visit(window, unused):
                pid = w.DWORD()
                user.GetWindowThreadProcessId(window, c.byref(pid))
                name = c.create_unicode_buffer(64)
                user.GetClassNameW(window, name, len(name))
                if pid.value == original.process.pid and name.value == '#32770':
                    found.append(window)
                return True
            user.EnumDesktopWindows(desktop, visit, 0)
            if found:
                dialog = found[0]
                edits = []
                @CALLBACK
                def child(control, unused):
                    name = c.create_unicode_buffer(64)
                    user.GetClassNameW(control, name, len(name))
                    if name.value == 'Edit' and user.IsWindowVisible(control) and user.IsWindowEnabled(control):
                        edits.append((control, user.GetDlgCtrlID(control)))
                    return True
                user.EnumChildWindows(dialog, child, 0)
                identifiers = tuple(identifier for _, identifier in edits)
                if filename_diagnostic != identifiers:
                    title = c.create_unicode_buffer(256)
                    user.GetWindowTextW(dialog, title, len(title))
                    print('filename edits', command, repr(title.value), edits, flush=True)
                    filename_diagnostic = identifiers
                # Original Save As uses IFileSaveDialog; Open uses GetOpenFileName.
                filename_control = 1001 if command == 40005 else 1148
                filename = [control for control, identifier in edits if identifier == filename_control]
                if not filename:
                    time.sleep(.02)
                    continue
                assert len(filename) == 1, ('one original filename edit', edits)
                edit = filename[0]
                value = c.create_unicode_buffer(str(path))
                user.SendMessageW(edit, 0xC, 0, c.addressof(value))
                readback = c.create_unicode_buffer(2048)
                user.SendMessageW(edit, 0xD, len(readback), c.addressof(readback))
                assert readback.value == str(path), ('original filename edit accepted the literal path',
                                                     readback.value, str(path), edits)
                user.PostMessageW(dialog, 0x111, 1, user.GetDlgItem(dialog, 1))
                while time.monotonic() < deadline and user.IsWindow(dialog):
                    time.sleep(.02)
                assert not user.IsWindow(dialog), 'original native file dialog completed'
                assert user.IsWindowEnabled(original.hwnd), 'original frame unblocked after file dialog'
                if command == 40005:
                    while time.monotonic() < deadline and not path.exists():
                        time.sleep(.02)
                    assert path.exists(), 'Save As wrote the actual target file'
                filename_window = user.GetDlgItem(original.hwnd, 0xFB05)
                loaded = c.create_unicode_buffer(2048)
                while time.monotonic() < deadline:
                    user.SendMessageW(filename_window, 0xD, len(loaded), c.addressof(loaded))
                    if os.path.normcase(loaded.value) == os.path.normcase(str(path)):
                        break
                    time.sleep(.02)
                assert os.path.normcase(loaded.value) == os.path.normcase(str(path)), 'original document identity changed'
                assert user.WaitForInputIdle(original.process.handle, 10000) == 0
                return
            time.sleep(.02)
        raise RuntimeError('original file dialog filename field did not appear')

    def launched_window(command, expected):
        original.command(command)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            found = []
            @CALLBACK
            def visit(window, unused):
                cls = c.create_unicode_buffer(64)
                user.GetClassNameW(window, cls, len(cls))
                if window != original.hwnd and cls.value.startswith('Notepad4'):
                    title = c.create_unicode_buffer(2048)
                    user.GetWindowTextW(window, title, len(title))
                    if title.value.startswith(expected):
                        found.append((window, title.value))
                return True
            user.EnumDesktopWindows(desktop, visit, 0)
            if found:
                assert len(found) == 1, 'one independently launched original window'
                window, title = found[0]
                result['cases'].append({'name': 'new-window' if command == 40011 else 'restart', 'title': title})
                print('launched', command, repr(title), flush=True)
                user.PostMessageW(window, 0x10, 0, 0)
                while time.monotonic() < deadline and user.IsWindow(window):
                    time.sleep(.02)
                assert not user.IsWindow(window), 'owned original child closed'
                return
            time.sleep(.02)
        raise RuntimeError('original child window did not appear')

    try:
        if args.native_ansi:
            original.send(2013)
            before = original.state()
            original.command(40433)
            record('native-ansi-byte-bound', before)
            return
        record('untitled-default')
        for name, text, anchor, caret, rectangle in [
                ('forward', '  中文\t😀\r\n  café  ', 0, None, False),
                ('reverse', '  中文\t😀\r\n  café  ', None, 0, False),
                ('whitespace', '\r\n \t', 0, None, False),
                ('empty', 'abc', 1, 1, False),
                ('rectangle', 'abc\ndef', 0, 5, True),
                *[(f'ascii-{n}', 'a' * n, 0, None, False) for n in (126, 127, 128, 129, 255, 300)],
                ('utf8-byte-limit', '中' * 100, 0, None, False),
                ('utf8-split', 'a' + '中' * 100, 0, None, False),
                ('supplementary-limit', '😀' * 80, 0, None, False)]:
            raw = text.encode('utf-8')
            original.reset(raw, len(raw) if anchor is None else anchor, len(raw) if caret is None else caret)
            if rectangle:
                original.send(2422, 1)
                original.send(2580, 0)
                original.send(2578, 5)
            before = original.state()
            original.command(40433)
            record(name, before)
        original.reset(b'captured', 0, 8)
        original.command(40433)
        original.send(2160, 0, 0)
        original.send(2001, 7, original.put(b'changed'))
        record('later-edit-and-selection')
        original.send(2176)
        record('undo')
        original.command(40024)
        record('editor-read-only')
        original.command(40024)
        original.send(2014)
        original.command(40000)
        record('new-clears')
        original.close()
        path = args.output / 'named 中文.txt'
        path.write_text('named captured', encoding='utf-8')
        original = Original(path, executable, desktop, desktop_name)
        for command in (40430, 40431, 40432):
            original.command(command)
            record(f'path-mode-{command - 40430}')
        original.send(2160, 0, 5)
        original.command(40433)
        original.command(40002)
        record('revert-retains')
        original.command(40430)
        record('path-mode-clears')
        original.send(2160, 0, 5)
        original.command(40433)
        file_dialog(40005, args.output / 'saved-as.txt')
        record('save-as-clears')
        original.send(2160, 0, 5)
        original.command(40433)
        file_dialog(40001, path)
        record('open-other-clears')
        original.send(2160, 0, 5)
        original.command(40433)
        file_dialog(40001, path)
        record('open-same-chooser-clears')
        original.send(2160, 0, 5)
        original.command(40433)
        record('before-new-window')
        launched_window(40011, 'Untitled' if args.language == 'en' else '未命名')
        launched_window(40027, path.name)
        (args.output / 'samples.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    finally:
        original.close()
        user.CloseDesktop(desktop)


if __name__ == '__main__':
    main()
