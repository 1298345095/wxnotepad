"""Observe reviewed style fallbacks in an owned pinned original on a private desktop."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import shutil

import original_host as host

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exe', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
exe = root / 'Notepad4.exe'
shutil.copy2(args.exe, exe)
ini = root / 'Notepad4.ini'
sample = root / 'sample.cpp'
sample.write_bytes(b'int value;\r\n')
user = host.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
desktop_name = 'wxnotepad-original-style-review-269'
desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
assert desktop
records = []
cases = {
    'extension-default': '[File Extensions]\nPython Script=cpp\n',
    'extension-replaced': '[File Extensions]\nC/C++ Source=review\nPython Script=cpp\n',
    'extension-python-favorite': '[Styles]\nFavoriteSchemes=32 1\n[File Extensions]\nPython Script=cpp\n',
    'extension-cpp-favorite': '[Styles]\nFavoriteSchemes=1 32\n[File Extensions]\nPython Script=cpp\n',
    'caret-white': '[Global Styles]\nDefault Code Style=fore:#000000; back:#FFFFFF\nCaret=fore:#FFFFFF\n',
    'caret-additional-white': '[Global Styles]\nDefault Code Style=fore:#000000; back:#FFFFFF\nCaret=fore:#000000; back:#FFFFFF\n',
    'caret-additional-red': '[Global Styles]\nDefault Code Style=fore:#000000; back:#FFFFFF\nCaret=fore:#FFFFFF; back:#FF0000\n',
    'whitespace-alpha0': '[Global Styles]\nWhitespace=fore:#0000FF; back:#FF0000; alpha:0\n',
    'locale-ja': '[Global Styles]\nDefault Code Style=locale:ja-JP\n',
    'locale-invalid': '[Global Styles]\nDefault Code Style=locale:invalid-locale-name\n',
}
try:
    for name, settings in cases.items():
        ini.write_text('[Settings]\nSaveSettings=0\n' + settings, encoding='utf-16')
        app = host.Original(sample, exe, desktop, desktop_name)
        try:
            app.send(2761, 0, app.memory)
            locale = c.create_string_buffer(256)
            assert host.kernel.ReadProcessMemory(app.handle, app.memory, locale, len(locale), None)
            record = dict(name=name, ini=settings, lexer=app.send(4002), locale=locale.value.decode(),
                          caret=app.send(2754, 14) & 0xffffffff,
                          additional=app.send(2754, 15) & 0xffffffff,
                          whitespace=app.send(2754, 17) & 0xffffffff,
                          whitespace_back=app.send(2754, 18) & 0xffffffff)
            records.append(record)
            print(json.dumps(record), flush=True)
        finally:
            app.close()
finally:
    user.CloseDesktop(desktop)
(root / 'records.json').write_text(json.dumps(dict(
    revision='89b8b36e0ee2c23f73be922315f17261ccfbbe1a',
    executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), records=records), indent=2), encoding='utf-8')
