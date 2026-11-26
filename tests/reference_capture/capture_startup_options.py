"""Capture command-line startup from a separate pinned original executable."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import shutil

from original_host import Original, user

TEXT = 'head\r\n\tA中😀Z\nlast\rtail\n'
POSITIONS = ('0', '2', '2,1', '2,2', '2,3', '2,4', '2,5', '2,6', '2,7',
             '2,6,1', '2,7,1', '2,8,1', '2,-4', '-1', '999', '2,6,4', '2,6,8', '2,6,-1')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('g', 'G'), required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    portable = args.output / 'portable'
    portable.mkdir()
    executable = portable / 'Notepad4.exe'
    shutil.copy2(args.executable, executable)
    (portable / 'Notepad4.ini').write_text(
        '[Settings]\nSaveSettings=0\nWordWrap=0\nWarnLineEndings=0\nTabWidth=4\n'
        '[Settings2]\nUILanguage=1033\n', encoding='utf-16')
    target = portable / 'target.txt'
    target.write_bytes(TEXT.encode('utf-8'))
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    name = 'wxnotepad-startup-reference'
    desktop = user.CreateDesktopW(name, None, None, 0, 0x1ff, None)
    assert desktop
    result = {'revision': '89b8b36e0ee2c23f73be922315f17261ccfbbe1a',
              'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
              'text': TEXT, 'positions': []}
    try:
        for option in ('/' + args.mode,):
            for location in POSITIONS:
                app = Original(target, executable, desktop, name, arguments=(option, location))
                try:
                    row = {'option': option, 'location': location, **app.state(),
                           'first_visible_line': app.send(2152), 'x_offset': app.send(2398)}
                    result['positions'].append(row)
                    assert bytes.fromhex(row['hex']) == target.read_bytes()
                    (args.output / 'startup_reference.json').write_text(
                        json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
                    print(option, location, row['caret'], flush=True)
                finally:
                    app.close()
    finally:
        user.CloseDesktop(desktop)


if __name__ == '__main__':
    main()
