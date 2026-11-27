"""Capture named CSV startup from a separate pinned original executable."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import shutil
import time

from original_host import Original, user, CALLBACK


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    executable = args.output / 'Notepad4.exe'
    shutil.copy2(args.executable, executable)
    (args.output / 'Notepad4.ini').write_text(
        '[Settings]\nSaveSettings=0\nWordWrap=0\nWarnLineEndings=0\n'
        '[Settings2]\nUILanguage=1033\n', encoding='utf-16')
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.GetDlgItem.argtypes = [w.HWND, c.c_int]
    user.GetDlgItem.restype = w.HWND
    name = 'wxnotepad-startup-csv-reference'
    desktop = user.CreateDesktopW(name, None, None, 0, 0x1ff, None)
    assert desktop
    result = {'revision': '89b8b36e0ee2c23f73be922315f17261ccfbbe1a',
              'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(), 'cases': []}
    try:
        for extension in ('txt', 'tsv'):
            for scheme in ('csv', 'tsv'):
                target = args.output / ('semicolon.' + extension)
                target.write_bytes(b'a;b\nc;d\n')
                app = Original(target, executable, desktop, name, arguments=('/s', scheme))
                try:
                    app.send(4003, 0, -1)
                    row = {'extension': extension, 'scheme': scheme, **app.state(),
                           'lexer': app.send(4002),
                           'styles': bytes(app.send(2038, i) & 255 for i in range(app.send(2006))).hex()}
                    user.PostMessageW(app.hwnd, 0x111, 40702, 0)  # original IDM_STYLE_CSV_OPTIONS
                    dialog = None
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline and not dialog:
                        def window(hwnd, _):
                            nonlocal dialog
                            pid = w.DWORD()
                            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
                            title = c.create_unicode_buffer(100)
                            user.GetWindowTextW(hwnd, title, 100)
                            if pid.value == app.process.pid and title.value == 'CSV Options':
                                dialog = hwnd
                            return True
                        user.EnumDesktopWindows(desktop, CALLBACK(window), 0)
                        time.sleep(.02)
                    assert dialog
                    row['semicolon_checked'] = user.SendMessageW(user.GetDlgItem(dialog, 101), 0xF0, 0, 0) == 1
                    row['double_quote_checked'] = user.SendMessageW(user.GetDlgItem(dialog, 110), 0xF0, 0, 0) == 1
                    user.SendMessageW(dialog, 0x111, 2, 0)
                    assert target.read_bytes().hex() == row['hex']
                    result['cases'].append(row)
                    print(extension, scheme, row['styles'], row['semicolon_checked'], flush=True)
                finally:
                    app.close()
    finally:
        user.CloseDesktop(desktop)
    (args.output / 'startup_csv_reference.json').write_text(
        json.dumps(result, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
