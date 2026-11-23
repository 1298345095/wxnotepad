"""Record pinned original profiles, dirty theme switches and restart in owned copies."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import shutil
import time

import original_host as host

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exe', type=Path, required=True)
parser.add_argument('--dark', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
portable = root / 'original-portable'
portable.mkdir()
exe = portable / 'Notepad4.exe'
shutil.copy2(args.exe, exe)
shutil.copy2(args.dark, portable / 'Notepad4 DarkTheme.ini')
ini = portable / 'Notepad4.ini'
ini.write_text('[Settings]\nSaveSettings=0\nWordWrap=1\n'
               '[Global Styles]\nDefault Code Style=font:Consolas; size:13.5; weight:600\n'
               'Default Text Style=font:Consolas; size:16\n'
               '[C/C++ Source]\nDefault=size:+1.5\nComment=italic; fore:#123456\n', encoding='utf-16')
sample = portable / 'sample.cpp'
sample.write_bytes(b'int main() {\r\n  // comment\r\n  return 0;\r\n}\r\n')
user = host.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
desktop_name = 'wxnotepad-original-style-themes-263'
desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
assert desktop
records = []


def snapshot(app, label):
    print(label, flush=True)
    app.send(4003, 0, -1)
    styles = {}
    for identifier in (0, 1, 2, 3, 4, 5, 6, 7, 32, 33, 36, 37, 39):
        styles[identifier] = dict(fore=app.send(2481, identifier), back=app.send(2482, identifier),
                                  size=app.send(2062, identifier), weight=app.send(2064, identifier),
                                  italic=app.send(2484, identifier))
    records.append(dict(label=label, state=app.state(), styles=styles,
                        ini=ini.read_text(encoding='utf-16'),
                        dark=(portable / 'Notepad4 DarkTheme.ini').read_text(encoding='utf-16')))
    (root / 'snapshots.json').write_text(json.dumps(records, indent=2), encoding='utf-8')


def save_now(app):
    # The original Save Settings Now command confirms completion in an owned
    # modal dialog. Post it so this observer can acknowledge that dialog.
    user.PostMessageW(app.hwnd, 0x111, 40444, 0)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        dialogs = []
        def visit(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, len(name))
            if pid.value == app.process.pid and name.value == '#32770':
                dialogs.append(hwnd)
            return True
        user.EnumDesktopWindows(desktop, host.CALLBACK(visit), 0)
        if dialogs:
            user.SendMessageW(dialogs[0], 0x111, 1, 0)
            return
        time.sleep(.03)
    raise RuntimeError('original Save Settings Now completion dialog appeared')


try:
    app = host.Original(sample, exe, desktop, desktop_name)
    try:
        app.reset(sample.read_bytes(), 20, 3)
        app.send(2170, 0, app.put(b'edited\0'))
        app.send(2176)
        app.send(2160, 20, 3)
        snapshot(app, 'custom-default')
        app.command(40479)
        snapshot(app, 'dark')
        app.command(40479)
        snapshot(app, 'dark-again')
        app.command(40401)  # dirty original Use Default Code Style
        snapshot(app, 'dark-dirty-inheritance')
        app.command(40478)  # saves dirty dark styles before loading Default
        snapshot(app, 'default-after-dirty-dark')
        app.command(40479)
        snapshot(app, 'dark-restored')
        save_now(app)
    finally:
        app.close()
    app = host.Original(sample, exe, desktop, desktop_name)
    try:
        snapshot(app, 'restart')
    finally:
        app.close()
finally:
    user.CloseDesktop(desktop)
(root / 'records.json').write_text(json.dumps(dict(
    revision='89b8b36e0ee2c23f73be922315f17261ccfbbe1a',
    executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), records=records), indent=2), encoding='utf-8')
