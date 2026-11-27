"""Capture Copy as RTF from the pinned Notepad4 application's native menu and clipboard."""

import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import shutil

from original_host import Original, user, kernel


COPY_RTF = 40377
TABS_AS_SPACES = 40417
WM_INITMENU = 0x116
RICH_TEXT_FORMAT = "Rich Text Format"
REFERENCE_REVISION = "89b8b36e0ee2c23f73be922315f17261ccfbbe1a"


def winapi(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = arguments
    return function


def clipboard_rtf():
    if not user.OpenClipboard(None):
        raise c.WinError(c.get_last_error())
    try:
        fmt = user.RegisterClipboardFormatW(RICH_TEXT_FORMAT)
        assert fmt
        formats = []
        identifier = user.EnumClipboardFormats(0)
        while identifier:
            formats.append(identifier)
            identifier = user.EnumClipboardFormats(identifier)
        assert formats == [fmt], formats
        memory = user.GetClipboardData(fmt)
        assert memory
        data = kernel.GlobalLock(memory)
        assert data
        try:
            raw = c.string_at(data, kernel.GlobalSize(memory))
            result, terminator, _ = raw.partition(b"\0")
            assert terminator and result.startswith(rb"{\rtf1") and result.endswith(b"}"), raw
            return result
        finally:
            kernel.GlobalUnlock(memory)
    finally:
        user.CloseClipboard()


def menu_item(menu, path=()):
    for index in range(user.GetMenuItemCount(menu)):
        label = c.create_unicode_buffer(256)
        user.GetMenuStringW(menu, index, label, len(label), 0x400)
        if user.GetMenuItemID(menu, index) == COPY_RTF:
            return path + (label.value,), index, menu
        submenu = user.GetSubMenu(menu, index)
        if submenu:
            result = menu_item(submenu, path + (label.value,))
            if result:
                return result
    return None


def selection(original):
    send = original.send
    return dict(
        original.state(),
        start=send(2143),
        end=send(2145),
        ranges=[[send(message, index) for message in (2579, 2577, 2583, 2581)] for index in range(send(2570))],
    )


def record(original, directory, name):
    menu = user.GetMenu(original.hwnd)
    assert menu
    user.SendMessageW(original.hwnd, WM_INITMENU, menu, 0)
    item = menu_item(menu)
    assert item and "RTF" in item[0][-1], item
    flags = user.GetMenuState(item[2], COPY_RTF, 0)
    assert flags != 0xFFFFFFFF
    before = selection(original)
    old_sequence = user.GetClipboardSequenceNumber()
    original.command(COPY_RTF)
    changed = old_sequence != user.GetClipboardSequenceNumber()
    assert changed == bool(before["hex"]), (name, before, changed)
    if changed:
        (directory / f"{name}.rtf").write_bytes(clipboard_rtf())
    after = selection(original)
    assert before == after, (name, before, after)
    return dict(
        name=name, menu=item[0], menuIndex=item[1], enabled=not bool(flags & 3), state=before, clipboardChanged=changed
    )


def configure_api():
    winapi(user, "OpenInputDesktop", w.HANDLE, w.DWORD, w.BOOL, w.DWORD)
    winapi(user, "CloseDesktop", w.BOOL, w.HANDLE)
    winapi(user, "GetMenu", w.HMENU, w.HWND)
    winapi(user, "GetMenuItemCount", c.c_int, w.HMENU)
    winapi(user, "GetMenuItemID", w.UINT, w.HMENU, c.c_int)
    winapi(user, "GetMenuStringW", c.c_int, w.HMENU, w.UINT, w.LPWSTR, c.c_int, w.UINT)
    winapi(user, "GetSubMenu", w.HMENU, w.HMENU, c.c_int)
    winapi(user, "GetMenuState", w.UINT, w.HMENU, w.UINT, w.UINT)
    winapi(user, "OpenClipboard", w.BOOL, w.HWND)
    winapi(user, "GetClipboardSequenceNumber", w.DWORD)
    winapi(user, "RegisterClipboardFormatW", w.UINT, w.LPCWSTR)
    winapi(user, "EnumClipboardFormats", w.UINT, w.UINT)
    winapi(user, "GetClipboardData", w.HANDLE, w.UINT)
    winapi(kernel, "GlobalLock", c.c_void_p, w.HANDLE)
    winapi(kernel, "GlobalUnlock", w.BOOL, w.HANDLE)
    winapi(kernel, "GlobalSize", c.c_size_t, w.HANDLE)


def capture_ranges(original, output):
    full_text = "\t{ alpha \\ 中文 😀 }\r\nBeta\nthird\rlast".encode()
    cases = []
    for name, text, anchor, caret in (
        ("empty", b"", 0, 0),
        ("all", full_text, 2, 2),
        ("selected", full_text, 22, 1),
        ("rectangle", b"ab12\ncd34\nef56", 1, 12),
        ("multiple", b"ab12\ncd34\nef56", 1, 2),
        ("multiple-empty-main", b"ab12\ncd34\nef56", 1, 2),
    ):
        original.reset(text, anchor, caret)
        if name == "rectangle":
            original.send(2422, 1)
            original.send(2590, anchor)
            original.send(2588, caret)
        elif name.startswith("multiple"):
            original.send(2573, 10 if name.endswith("empty-main") else 12, 10)
        cases.append(record(original, output, name))
    original.reset(b"", 0, 0)
    cases.append(record(original, output, "empty-after-copy"))
    original.reset(b"x\tY\tZ", 0, 5)
    original.command(TABS_AS_SPACES)
    assert not original.send(2125), "original case expands tab characters"
    cases.append(record(original, output, "expanded-tabs"))
    return cases


def capture_style_overrides(original, output):
    send = original.send
    keyword = 8
    comment = 2
    send(2056, keyword, original.put(b"Consolas\0"))
    for message, value in ((2061, 1350), (2063, 600), (2054, 1), (2059, 1), (2041, 1), (2052, 0xD0D0FF), (2066, 204)):
        send(message, keyword, value)
    send(2052, comment, 0xF0FFF0)
    send(2057, comment, 1)
    return record(original, output, "cpp-overrides")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    configure_api()
    output = args.output.resolve()
    portable = output / "original-portable"
    portable.mkdir(parents=True)
    executable = portable / "Notepad4.exe"
    shutil.copy2(args.executable, executable)
    (portable / "Notepad4.ini").write_text("[Settings]\nSaveSettings=0\nWordWrap=0\n", encoding="utf-16")
    cpp_text = 'int value = 42;\r\n// 中文 😀\r\nconst char* path = "\\{foo}";\r\n'
    cpp = portable / "styled.cpp"
    cpp.write_bytes(cpp_text.encode())
    (portable / "legacy.txt").write_bytes("Déjà vu £\r\n\tfin".encode("cp1252"))
    (portable / "ansi.txt").write_bytes("plain 中文\r\n\tend".encode(f"cp{kernel.GetACP()}", errors="replace"))
    desktop = user.OpenInputDesktop(0, False, 0x0001 | 0x0040)
    if not desktop:
        raise c.WinError(c.get_last_error())
    cases = []
    try:
        for source, name, encoding in (
            (None, None, "utf-8"),
            (cpp, "cpp", "utf-8"),
            (portable / "legacy.txt", "legacy", "windows-1252"),
            (portable / "ansi.txt", "ansi", "ansi"),
        ):
            original = Original(source, executable, desktop, "default", encoding)
            try:
                if name:
                    original.send(2160, 0, original.send(2006))
                    cases.append(record(original, output, name))
                    if name == "cpp":
                        cases.append(capture_style_overrides(original, output))
                else:
                    cases.extend(capture_ranges(original, output))
            finally:
                original.close()
    finally:
        user.CloseDesktop(desktop)
    session = dict(
        revision=REFERENCE_REVISION,
        executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
        windowsAnsiCodePage=kernel.GetACP(),
        cases=cases,
    )
    (output / "original.json").write_text(json.dumps(session, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
