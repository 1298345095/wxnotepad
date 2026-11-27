"""Capture pinned Notepad4 search commands on a private Windows desktop."""

import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import threading
import time

from original_host import CALLBACK, Original, user

FIND, SAVE, NEXT, PREV, REPLACE, F4 = range(40364, 40370)
NEXT_SEL, PREV_SEL = 40567, 40568
QUERY, REPLACEMENT, REGEX, TRANSFORM, WILDCARD = 100, 101, 106, 107, 124

user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
user.GetDlgItem.argtypes = [w.HWND, c.c_int]
user.GetDlgItem.restype = w.HWND
user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
user.IsWindowVisible.argtypes = [w.HWND]
user.GetMenu.argtypes = [w.HWND]
user.GetMenu.restype = w.HMENU
user.GetSubMenu.argtypes = [w.HMENU, c.c_int]
user.GetSubMenu.restype = w.HMENU
user.GetMenuItemCount.argtypes = [w.HMENU]
user.GetMenuItemID.argtypes = [w.HMENU, c.c_int]
user.GetMenuStringW.argtypes = [w.HMENU, w.UINT, w.LPWSTR, c.c_int, w.UINT]


def search_menu(original):
    menu = user.GetMenu(original.hwnd)
    for position in range(user.GetMenuItemCount(menu)):
        submenu = user.GetSubMenu(menu, position)
        ids = [user.GetMenuItemID(submenu, index) for index in range(user.GetMenuItemCount(submenu))]
        if SAVE in ids:
            result = []
            for index, identifier in enumerate(ids):
                label = c.create_unicode_buffer(128)
                user.GetMenuStringW(submenu, index, label, len(label), 0x400)
                result.append([identifier, label.value])
            return result
    raise AssertionError("original Search menu not found")


def window_class(hwnd):
    name = c.create_unicode_buffer(64)
    user.GetClassNameW(hwnd, name, len(name))
    return name.value


def owned_dialogs(original, desktop):
    result = []

    @CALLBACK
    def inspect(hwnd, _):
        pid = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, c.byref(pid))
        if pid.value == original.process.pid and window_class(hwnd) == "#32770":
            result.append(hwnd)
        return True

    user.EnumDesktopWindows(desktop, inspect, 0)
    return result


def find_dialog(original, desktop):
    return next(
        (hwnd for hwnd in owned_dialogs(original, desktop) if window_class(user.GetDlgItem(hwnd, QUERY)) == "ComboBox"),
        None,
    )


def control_text(hwnd):
    buffer = c.create_unicode_buffer(256)
    user.SendMessageW(hwnd, 0xD, len(buffer), c.addressof(buffer))
    return buffer.value


def command(hwnd, identifier):
    user.SendMessageW(hwnd, 0x111, identifier, 0)


def set_text(dialog, identifier, value):
    control = user.GetDlgItem(dialog, identifier)
    buffer = c.create_unicode_buffer(value)
    user.SendMessageW(control, 0xC, 0, c.addressof(buffer))
    command(dialog, identifier)
    assert control_text(control) == value


def dialog_state(dialog):
    if not dialog:
        return None
    replace = user.GetDlgItem(dialog, REPLACEMENT)
    result = {"query": control_text(user.GetDlgItem(dialog, QUERY)), "mode": "replace" if replace else "find"}
    if replace:
        result["replacement"] = control_text(replace)
    for option, identifier in [
        ("case", 103),
        ("whole", 104),
        ("regex", REGEX),
        ("transform", TRANSFORM),
        ("wildcard", WILDCARD),
    ]:
        result[option] = bool(user.SendMessageW(user.GetDlgItem(dialog, identifier), 0xF0, 0, 0))
    return result


def document_state(original):
    result = {
        name: original.send(message)
        for name, message in [
            ("length", 2006),
            ("anchor", 2009),
            ("caret", 2008),
            ("undo", 2174),
            ("redo", 2016),
            ("readonly", 2140),
        ]
    }
    if result["length"] < 256:
        result["text"] = original.text().decode("utf-8")
    return result


def feedback_action(original, desktop, command_id, choices):
    worker = threading.Thread(target=original.command, args=(command_id,), daemon=True)
    worker.start()
    results = []
    for choice in choices:
        deadline = time.monotonic() + 4
        while worker.is_alive() and time.monotonic() < deadline:
            notice = next(
                (
                    hwnd
                    for hwnd in owned_dialogs(original, desktop)
                    if window_class(user.GetDlgItem(hwnd, 102)) == "Static"
                ),
                None,
            )
            if notice:
                results.append(
                    {
                        "message": control_text(user.GetDlgItem(notice, 102)),
                        "choice": choice,
                        "can_cancel": bool(user.IsWindowVisible(user.GetDlgItem(notice, 2))),
                    }
                )
                command(notice, choice)
                break
            time.sleep(0.02)
    worker.join(timeout=4)
    assert not worker.is_alive() and len(results) == len(choices), "expected original search feedback sequence"
    return results


def setup(original, content, anchor, caret, desktop, options):
    if len(content) < 256:
        original.reset(content, anchor, caret)
    else:
        original.reset(b"", 0, 0)
        for offset in range(0, len(content), 65536):
            chunk = content[offset : offset + 65536]
            original.send(2001, len(chunk), original.put(chunk))
        original.send(2160, anchor, caret)
        original.send(2175)
        original.send(2014)
    assert original.send(2006) == len(content)
    assert (original.send(2009), original.send(2008)) == (anchor, caret)
    if "query" in options:
        original.command(REPLACE if "replacement" in options else FIND)
        dialog = find_dialog(original, desktop)
        assert dialog
        set_text(dialog, QUERY, options["query"])
        if "replacement" in options:
            set_text(dialog, REPLACEMENT, options["replacement"])
        for identifier in options.get("checks", []):
            user.SendMessageW(user.GetDlgItem(dialog, identifier), 0xF1, 1, 0)
            command(dialog, identifier)


def capture(executable, directory, desktop, name, scenario):
    label, content, anchor, caret, steps, options = scenario
    directory.mkdir()
    shutil.copy2(executable, directory / "Notepad4.exe")
    ini = directory / "Notepad4.ini"
    ini.write_text(
        "[Notepad4]\n[Settings]\nSaveSettings=1\nSaveFindReplace=1\nWordWrap=0\n"
        f"SelectOption={options.get('seed', 2)}\n"
        f"FindReplaceFlag={options.get('flags', 0)}\n"
        + ("" if options.get("feedback") else "[Suppressed Messages]\nMsgFindWrap1=1\nMsgFindWrap2=1\nMsgNotFound=1\n")
        + options.get("history", ""),
        encoding="utf-16",
    )
    fixture = directory / "sample.txt"
    fixture.write_bytes(b"\xef\xbb\xbf")
    original = Original(fixture, directory / "Notepad4.exe", desktop, name)
    rows = []
    try:
        setup(original, content, anchor, caret, desktop, options)
        menu = search_menu(original) if label == "save-word" else None
        for step in steps:
            feedback = None
            if step == "close":
                dialog = find_dialog(original, desktop)
                assert dialog
                command(dialog, 2)
            elif step == "select":
                original.send(2160, anchor, caret)
            elif step == "readonly":
                original.send(2171, 1)
            elif step == "replace-document":
                original.reset(b"cat x", 0, 0)
            elif step == "clear-replacement":
                set_text(find_dialog(original, desktop), REPLACEMENT, "")
            elif step in ("undo", "redo"):
                original.send(2176 if step == "undo" else 2011)
            elif isinstance(step, tuple):
                if len(step) > 1:
                    feedback = feedback_action(original, desktop, step[0], step[1:])
                else:
                    dialog = find_dialog(original, desktop)
                    assert dialog
                    command(dialog, step[0])
                    time.sleep(0.05)
            else:
                original.command(step)
            rows.append(
                {
                    "action": step,
                    "document": document_state(original),
                    "dialog": dialog_state(find_dialog(original, desktop)),
                }
            )
            if feedback:
                rows[-1]["feedback"] = feedback
    finally:
        dialog = find_dialog(original, desktop)
        if dialog:
            command(dialog, 2)
        original.send(2175)
        original.close()
    raw = ini.read_bytes()
    saved = raw.decode("utf-16" if raw.startswith(b"\xff\xfe") else "utf-8-sig")
    return {"rows": rows, "search_menu": menu, "saved_ini": saved}


def main():
    executable, output = map(Path, sys.argv[1:3])
    output.mkdir(parents=True)
    name = "wxnotepad-search-reference-" + str(os.getpid())
    desktop = user.CreateDesktopW(name, None, None, 0, 0x1FF, None)
    assert desktop
    limit = 1024 * 1024
    samples = [
        ("save-word", b"  dog cat dog", 1, 1, [SAVE, NEXT, FIND], {}),
        ("save-punctuation", b"  ++  \n", 3, 3, [SAVE, FIND], {}),
        ("empty", b"", 0, 0, [NEXT, PREV, F4, NEXT_SEL], {}),
        ("save-disabled", b"dog cat dog", 3, 0, [SAVE, NEXT, FIND], {"seed": 0}),
        ("next-word", b"dog cat dog", 1, 1, [NEXT_SEL, FIND], {}),
        ("previous-selection", b"dog cat dog", 11, 8, [PREV_SEL, FIND], {}),
        ("unicode", "猫😀 x 猫😀".encode(), 0, 7, [NEXT_SEL, FIND], {}),
        ("multiline", b"a\r\nb x a\r\nb", 0, 4, [NEXT_SEL, FIND], {}),
        ("limit-minus-one", b"a" * limit, 0, limit - 1, [SAVE, NEXT], {}),
        ("limit-exact", b"a" * limit, 0, limit, [SAVE, NEXT], {}),
        ("implicit-next", b"dog cat dog", 0, 3, [NEXT, FIND], {"seed": 2}),
        ("implicit-prev", b"dog cat dog", 8, 11, [PREV, FIND], {"seed": 2}),
        ("implicit-disabled", b"dog cat dog", 0, 3, [NEXT], {"seed": 0}),
        ("implicit-no-word", b"dog cat dog", 1, 1, [NEXT], {"seed": 2}),
        ("implicit-flags", b"a[.] x a[.]", 0, 4, [NEXT, FIND], {"flags": 8294}),
        (
            "save-dialog-flags",
            b"dog cat DOG dog",
            0,
            3,
            [(132,), "select", (135,), NEXT],
            {"query": "dog", "checks": [103, 104, REGEX]},
        ),
        (
            "save-dialog-transform",
            b"\n x \n",
            0,
            2,
            [(132,), "select", (135,), NEXT],
            {"query": "x", "checks": [TRANSFORM]},
        ),
        (
            "save-dialog-wildcard",
            b"d*g x dog d*g",
            0,
            3,
            [(132,), "select", (135,), NEXT],
            {"query": "d*g", "checks": [WILDCARD]},
        ),
        ("save-menu-open-dialog", b"dog cat dog", 0, 3, [SAVE, NEXT], {"query": "draft"}),
        ("selection-with-dialog", b"dog cat dog", 0, 3, [NEXT_SEL], {"query": "draft"}),
        (
            "replace-uninitialized",
            b"dog cat dog",
            0,
            3,
            [F4],
            {"history": '\n[Recent Find]\n01="dog"\n[Recent Replace]\n01="fox"\n'},
        ),
        (
            "replace-after-find-only",
            b"dog cat dog",
            0,
            0,
            [(132,), "close", F4],
            {"query": "dog", "history": '\n[Recent Replace]\n01="fox"\n'},
        ),
        ("f4-in-find", b"dog cat dog", 0, 3, [(134,)], {"query": "dog"}),
        (
            "f4-empty-replacement",
            b"dog cat dog",
            0,
            0,
            [(132,), (134,), "close", F4, "undo", "redo", REPLACE],
            {"query": "dog", "replacement": ""},
        ),
        ("f4-empty-button", b"dog cat dog", 0, 0, [(132,), (112,), "close", F4], {"query": "dog", "replacement": ""}),
        (
            "f4-empty-after-nonempty",
            b"dog cat dog",
            0,
            0,
            [(132,), "clear-replacement", (134,), "close", F4, "undo", "redo"],
            {"query": "dog", "replacement": "fox"},
        ),
        (
            "f4-empty-after-seed",
            b"dog cat dog",
            0,
            3,
            [(132,), (134,), "close", F4, "undo", "redo"],
            {"query": "dog", "replacement": ""},
        ),
        (
            "f4-empty-after-flag",
            b"dog cat dog",
            0,
            0,
            [(132,), (134,), "close", F4, "undo", "redo"],
            {"query": "dog", "replacement": "", "checks": [103]},
        ),
        (
            "f4-literal",
            b"dog cat dog",
            0,
            0,
            [(132,), "close", F4, F4, "undo", "redo"],
            {"query": "dog", "replacement": "fox"},
        ),
        (
            "f4-regex-captures",
            b"dog cat dog",
            0,
            0,
            [(132,), "close", F4, "undo", "redo"],
            {"query": "(dog)", "replacement": "$1s", "checks": [REGEX]},
        ),
        (
            "f4-zero-width",
            b"dog cat dog",
            0,
            0,
            [(132,), "close", F4],
            {"query": "(?=dog)", "replacement": "!", "checks": [REGEX]},
        ),
        (
            "f4-zero-width-cxx",
            b"dog cat dog",
            0,
            0,
            [(132,), "close", F4, "undo", "redo"],
            {"query": "(?=dog)", "replacement": "!", "checks": [116, REGEX]},
        ),
        (
            "f4-readonly",
            b"dog cat dog",
            0,
            0,
            [(132,), "close", "readonly", F4],
            {"query": "dog", "replacement": "fox"},
        ),
        ("selection-wrap-cancel", b"dog cat dog", 11, 8, [(NEXT_SEL, 2)], {"feedback": True}),
        ("selection-wrap-accept", b"dog cat dog", 11, 8, [(NEXT_SEL, 1)], {"feedback": True}),
        ("selection-not-found", b"dog x", 0, 3, [SAVE, "replace-document", (NEXT, 1)], {"feedback": True}),
    ]
    result = {
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
        "cases": {},
    }
    try:
        for scenario in samples:
            label = scenario[0]
            if sys.argv[3:] and label not in sys.argv[3:]:
                continue
            print(label, flush=True)
            result["cases"][label] = capture(executable, output / label, desktop, name, scenario)
            (output / "samples.json").write_text(
                json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
            )
    finally:
        user.CloseDesktop(desktop)


if __name__ == "__main__":
    main()
