"""Observe Customize Schemes on an owned pinned original, on a private desktop."""
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
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--scheme', type=Path, help='Read an actual wxnotepad export in the pinned original')
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=False)
portable = root / 'original-portable'
portable.mkdir()
exe = portable / 'Notepad4.exe'
shutil.copy2(args.exe, exe)
ini = portable / 'Notepad4.ini'
ini.write_text('[Settings]\nSaveSettings=0\n[Global Styles]\n'
               'Default Code Style=font:Consolas; size:13\n'
               '[C/C++ Source]\nComment=fore:#123456; italic\n', encoding='utf-16')
sample = portable / 'sample.cpp'
sample.write_bytes(b'int main() {\r\n  // comment\r\n  return 0;\r\n}\r\n')
user = host.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
user.GetDlgItem.argtypes = [w.HWND, c.c_int]
user.GetDlgItem.restype = w.HWND
user.IsWindowEnabled.argtypes = [w.HWND]
desktop_name = 'wxnotepad-original-customize-264'
desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x10000000, None)
assert desktop
records = []


def wait_dialog(app, title=None):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        dialogs = []
        def visit(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(256)
            user.GetWindowTextW(hwnd, name, len(name))
            cls = c.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, cls, len(cls))
            if pid.value == app.process.pid and cls.value == '#32770' and (
                    ('Customize Schemes' not in name.value) if title is None else title in name.value):
                dialogs.append(hwnd)
            return True
        user.EnumDesktopWindows(desktop, host.CALLBACK(visit), 0)
        if dialogs:
            if title and 'Customize Schemes' in title:
                tree = user.GetDlgItem(dialogs[-1], 100)
                if not tree or not user.SendMessageW(tree, 0x110a, 0, 0):
                    time.sleep(.03)
                    continue
            return dialogs[-1]
        time.sleep(.03)
    raise RuntimeError(f'owned original dialog appeared: {title}')


class TVItem(c.Structure):
    _fields_ = [('mask', w.UINT), ('item', c.c_void_p), ('state', w.UINT),
                ('stateMask', w.UINT), ('text', c.c_void_p), ('textMax', c.c_int),
                ('image', c.c_int), ('selectedImage', c.c_int), ('children', c.c_int),
                ('param', c.c_ssize_t)]


def read_item(app, tree, item):
    data = TVItem(mask=1, item=item, text=app.memory + 1024, textMax=256)
    host.kernel.WriteProcessMemory(app.handle, app.memory, c.byref(data), c.sizeof(data), None)
    assert user.SendMessageW(tree, 0x113e, 0, app.memory)  # TVM_GETITEMW
    value = c.create_unicode_buffer(256)
    host.kernel.ReadProcessMemory(app.handle, app.memory + 1024, value, c.sizeof(value), None)
    return value.value


def find(app, tree, caption):
    def scan(item):
        while item:
            if read_item(app, tree, item) == caption:
                return item
            child = scan(user.SendMessageW(tree, 0x110a, 4, item))
            if child:
                return child
            item = user.SendMessageW(tree, 0x110a, 1, item)
        return 0
    item = scan(user.SendMessageW(tree, 0x110a, 0, 0))
    assert item, caption
    user.SendMessageW(tree, 0x110b, 9, item)
    return item


def find_style(app, tree, scheme, style):
    parent = find(app, tree, scheme)
    item = user.SendMessageW(tree, 0x110a, 4, parent)
    while item:
        if read_item(app, tree, item) == style:
            user.SendMessageW(tree, 0x110b, 9, item)
            return item
        item = user.SendMessageW(tree, 0x110a, 1, item)
    raise AssertionError((scheme, style))


def text(hwnd):
    buffer = c.create_unicode_buffer(2048)
    user.SendMessageW(hwnd, 0xd, len(buffer), c.addressof(buffer))
    return buffer.value


def set_text(hwnd, value):
    buffer = c.create_unicode_buffer(value)
    user.SendMessageW(hwnd, 0xc, 0, c.addressof(buffer))


def file_choice(app, dialog, command, path):
    user.PostMessageW(dialog, 0x111, command, 0)
    file_dialog = wait_dialog(app)
    edits = []
    def visit(child, _):
        name = c.create_unicode_buffer(100)
        user.GetClassNameW(child, name, 100)
        if name.value == 'Edit' and user.IsWindowVisible(child):
            edits.append(child)
        return True
    deadline = time.monotonic() + 10
    while not edits and time.monotonic() < deadline:
        user.EnumChildWindows(file_dialog, host.CALLBACK(visit), 0)
        time.sleep(.03)
    assert edits
    set_text(edits[0], str(path))
    user.PostMessageW(file_dialog, 0x111, 1, 0)
    deadline = time.monotonic() + 10
    while user.IsWindow(file_dialog) and time.monotonic() < deadline:
        time.sleep(.03)
    assert not user.IsWindow(file_dialog), 'owned original file choice accepted'


def snapshot(app, label, dialog=None, extra=None):
    record = dict(label=label, state=app.state(), comment_fore=app.send(2481, 1),
                  comment_italic=app.send(2484, 1), size=app.send(2062, 0),
                  comment_size=app.send(2062, 1),
                  foreground=app.send(2481, 0), weight=app.send(2064, 0),
                  italic=app.send(2484, 0), lexer=app.send(4002))
    if dialog:
        record['edit'] = text(user.GetDlgItem(dialog, 102))
        record['enabled'] = {str(i): bool(user.IsWindowEnabled(user.GetDlgItem(dialog, i)))
                             for i in (102, 104, 105, 106, 108)}
    user.GetMenu.restype = w.HMENU
    user.GetMenu.argtypes = [w.HWND]
    user.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]
    user.GetSubMenu.argtypes = [w.HMENU, c.c_int]
    user.GetSubMenu.restype = w.HMENU
    user.GetMenuItemCount.argtypes = [w.HMENU]
    user.GetMenuItemID.argtypes = [w.HMENU, c.c_int]
    def checked(menu):
        for index in range(user.GetMenuItemCount(menu)):
            if user.GetMenuItemID(menu, index) == 40401:
                user.SendMessageW(app.hwnd, 0x116, user.GetMenu(app.hwnd), 0)
                return bool(user.GetMenuState(menu, 40401, 0) & 8)
            sub = user.GetSubMenu(menu, index)
            if sub:
                result = checked(sub)
                if result is not None:
                    return result
        return None
    record['use_code'] = checked(user.GetMenu(app.hwnd))
    if extra:
        record.update(extra)
    records.append(record)
    (root / 'records.json').write_text(json.dumps(dict(
        revision='89b8b36e0ee2c23f73be922315f17261ccfbbe1a',
        executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), records=records), indent=2), encoding='utf-8')


try:
    app = host.Original(sample, exe, desktop, desktop_name)
    try:
        snapshot(app, 'before')
        inheritance = portable / 'inheritance-only.ini'
        inheritance.write_text('[C/C++ Source]\nUseDefaultCodeStyle=0\n', encoding='utf-16')
        user.PostMessageW(app.hwnd, 0x111, 40402, 0)
        dlg = wait_dialog(app, 'Customize Schemes')
        file_choice(app, dlg, 111, inheritance)
        user.SendMessageW(dlg, 0x111, 2, 0)
        snapshot(app, 'import-cancel-without-preview')
        app.command(40401)
        snapshot(app, 'restored-code-choice')
        user.PostMessageW(app.hwnd, 0x111, 40402, 0)
        dlg = wait_dialog(app, 'Customize Schemes')
        tree = user.GetDlgItem(dlg, 100)
        find(app, tree, 'C/C++ Source')
        snapshot(app, 'scheme-selection', dlg)
        find(app, tree, 'Default Code Style')
        snapshot(app, 'global-font-selection', dlg)
        set_text(user.GetDlgItem(dlg, 102), 'font:Consolas; size:19')
        user.SendMessageW(dlg, 0x111, 107, 0)
        snapshot(app, 'applied-font', dlg)
        user.SendMessageW(dlg, 0x111, 2, 0)
        snapshot(app, 'apply-then-cancel')
        user.PostMessageW(app.hwnd, 0x111, 40402, 0)
        dlg = wait_dialog(app, 'Customize Schemes')
        tree = user.GetDlgItem(dlg, 100)
        find(app, tree, 'Default Code Style')
        set_text(user.GetDlgItem(dlg, 102), 'font:Consolas; size:17.5')
        user.SendMessageW(dlg, 0x111, 1, 0)
        snapshot(app, 'accepted-font')
        imported = args.scheme.resolve() if args.scheme else portable / 'import.ini'
        if not args.scheme:
            imported.write_text('[File Extensions]\nC/C++ Source=style264\n'
                            '[C/C++ Source]\nUseDefaultCodeStyle=0\nComment=fore:#AA5533\n'
                            '[Global Styles]\nDefault Code Style=font:Consolas; size:21\n'
                                '[Custom Colors]\n01=#112233\n', encoding='utf-16')
        user.PostMessageW(app.hwnd, 0x111, 40402, 0)
        dlg = wait_dialog(app, 'Customize Schemes')
        file_choice(app, dlg, 111, imported)
        tree = user.GetDlgItem(dlg, 100)
        find(app, tree, 'C/C++ Source')
        snapshot(app, 'imported-extensions', dlg)
        user.SendMessageW(dlg, 0x111, 107, 0)
        snapshot(app, 'imported-applied', dlg)
        file_choice(app, dlg, 112, portable / 'export.ini')
        snapshot(app, 'exported-current-dialog-state', dlg)
        user.SendMessageW(dlg, 0x111, 2, 0)
        snapshot(app, 'import-apply-cancel')
        user.PostMessageW(app.hwnd, 0x111, 40402, 0)
        dlg = wait_dialog(app, 'Customize Schemes')
        tree = user.GetDlgItem(dlg, 100)
        find(app, tree, 'C/C++ Source')
        snapshot(app, 'extensions-after-cancel', dlg)
        user.SendMessageW(dlg, 0x111, 115, 0)
        snapshot(app, 'reset-all', dlg)
        user.SendMessageW(dlg, 0x111, 107, 0)
        snapshot(app, 'reset-all-applied', dlg)
        user.SendMessageW(dlg, 0x111, 2, 0)
        snapshot(app, 'reset-all-cancel')
        for value in ('size:+1.5; font:No such wxnotepad 264 font; bold; fore:#224466',
                      'size:bogus; weight:garbage; fore:invalid', '',
                      'size:12.5junk; weight:600junk; fore:#112233junk; italic'):
            user.PostMessageW(app.hwnd, 0x111, 40402, 0)
            dlg = wait_dialog(app, 'Customize Schemes')
            tree = user.GetDlgItem(dlg, 100)
            scheme = find(app, tree, 'C/C++ Source')
            child = user.SendMessageW(tree, 0x110a, 4, scheme)
            assert read_item(app, tree, child) == 'Default'
            user.SendMessageW(tree, 0x110b, 9, child)
            set_text(user.GetDlgItem(dlg, 102), value)
            user.SendMessageW(dlg, 0x111, 107, 0)
            snapshot(app, f'style-grammar:{value}', dlg)
            user.SendMessageW(dlg, 0x111, 2, 0)
        user.PostMessageW(app.hwnd, 0x111, 40402, 0)
        dlg = wait_dialog(app, 'Customize Schemes')
        tree = user.GetDlgItem(dlg, 100)
        scheme = find(app, tree, 'C/C++ Source')
        user.SendMessageW(tree, 0x110b, 9, user.SendMessageW(tree, 0x110a, 4, scheme))
        set_text(user.GetDlgItem(dlg, 102), 'size:15')
        find_style(app, tree, 'C/C++ Source', 'Comment')
        set_text(user.GetDlgItem(dlg, 102), 'size:bogus')
        user.SendMessageW(dlg, 0x111, 107, 0)
        snapshot(app, 'invalid-comment-size-inherits-scheme-default', dlg)
        user.SendMessageW(dlg, 0x111, 2, 0)
        app.command(40401)
        user.PostMessageW(app.hwnd, 0x111, 40402, 0)
        dlg = wait_dialog(app, 'Customize Schemes')
        tree = user.GetDlgItem(dlg, 100)
        find(app, tree, 'Default Code Style')
        set_text(user.GetDlgItem(dlg, 102), 'font:Consolas; size:18')
        find_style(app, tree, 'C/C++ Source', 'Comment')
        set_text(user.GetDlgItem(dlg, 102), 'size:+2; fore:#123456')
        user.SendMessageW(dlg, 0x111, 107, 0)
        user.PostMessageW(dlg, 0x111, 106, 0)
        font_dialog = wait_dialog(app)
        user.SendMessageW(font_dialog, 0x401, 0, app.memory)
        height = w.LONG()
        host.kernel.ReadProcessMemory(app.handle, app.memory, c.byref(height), c.sizeof(height), None)
        user.GetDpiForWindow.argtypes = [w.HWND]
        dpi = user.GetDpiForWindow(font_dialog)
        points = round(abs(height.value) * 72 / dpi)
        snapshot(app, 'relative-native-font-chooser', dlg, {'font_chooser_points': points, 'font_chooser_dpi': dpi})
        user.SendMessageW(font_dialog, 0x111, 2, 0)
        user.SendMessageW(dlg, 0x111, 2, 0)
    finally:
        app.close()
    ini.write_text(ini.read_text(encoding='utf-16') + '\n[File Extensions]\nC/C++ Source=style264\n', encoding='utf-16')
    associated = portable / 'sample.style264'
    associated.write_bytes(sample.read_bytes())
    for path, label in ((associated, 'extension-added'), (sample, 'extension-removed')):
        app = host.Original(path, exe, desktop, desktop_name)
        try:
            snapshot(app, label)
        finally:
            app.close()
finally:
    user.CloseDesktop(desktop)
