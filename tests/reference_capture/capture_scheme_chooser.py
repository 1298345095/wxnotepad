"""Observe the pinned original scheme chooser on an isolated owned desktop.

No clipboard or global input is used. The output includes the complete native
tree, checkbox state, selection/default transitions and document/history state.
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

from original_host import Original, CALLBACK, user, kernel
import scheme_native_constants as win


class TreeItem(c.Structure):
    _fields_ = [
        ("mask", w.UINT),
        ("item", w.HANDLE),
        ("state", w.UINT),
        ("stateMask", w.UINT),
        ("text", c.c_void_p),
        ("textLength", c.c_int),
        ("image", c.c_int),
        ("selectedImage", c.c_int),
        ("children", c.c_int),
        ("data", c.c_ssize_t),
    ]


def capture(original, output, visible=False):
    output.mkdir(parents=True, exist_ok=False)
    portable = output / "portable"
    portable.mkdir()
    executable = portable / "Notepad4.exe"
    shutil.copy2(original, executable)
    (portable / "Notepad4.ini").write_text(
        "[Settings]\nSaveSettings=0\n[Settings2]\nUILanguage=1033\n"
        "[Styles]\nFavoriteSchemes=67 1 32\nDefaultScheme=32\nAutoSelect=1\n",
        encoding="utf-16",
    )
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.GetDlgItem.argtypes = [w.HWND, c.c_int]
    user.GetDlgItem.restype = w.HWND
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.IsWindowEnabled.argtypes = [w.HWND]
    name = f"wxnotepad-schemes-reference-{os.getpid()}"
    user.GetThreadDesktop.argtypes = [w.DWORD]
    user.GetThreadDesktop.restype = w.HANDLE
    kernel.GetCurrentThreadId.restype = w.DWORD
    user.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
    user.SetWindowPos.argtypes = [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT]
    desktop = (
        user.GetThreadDesktop(kernel.GetCurrentThreadId())
        if visible
        else user.CreateDesktopW(name, None, None, 0, win.DESKTOP_ALL_ACCESS, None)
    )
    assert desktop
    app = None
    records = []
    try:
        app = Original(None, executable, desktop, "Default" if visible else name)

        def dialog(favorite=False):
            user.PostMessageW(
                app.hwnd, win.WM_COMMAND, win.IDM_VIEW_SCHEME_FAVORITE if favorite else win.IDM_VIEW_SCHEME, 0
            )
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                found = []

                @CALLBACK
                def visit(hwnd, _):
                    pid = w.DWORD()
                    user.GetWindowThreadProcessId(hwnd, c.byref(pid))
                    kind = c.create_unicode_buffer(128)
                    user.GetClassNameW(hwnd, kind, 128)
                    if pid.value == app.process.pid and kind.value == "#32770" and user.IsWindowVisible(hwnd):
                        found.append(hwnd)
                    return True

                user.EnumDesktopWindows(desktop, visit, 0)
                if found:
                    assert len(found) == 1
                    return found[0]
                time.sleep(0.02)
            raise RuntimeError("owned original scheme dialog appears")

        def item(tree, identifier):
            data = TreeItem(
                mask=win.TVIF_TEXT | win.TVIF_PARAM | win.TVIF_STATE | win.TVIF_CHILDREN,
                item=identifier,
                stateMask=win.TVIS_STATEIMAGEMASK,
                text=app.memory + win.REMOTE_TREE_TEXT_OFFSET_BYTES,
                textLength=256,
            )
            assert kernel.WriteProcessMemory(app.handle, app.memory, c.byref(data), c.sizeof(data), None)
            assert user.SendMessageW(tree, win.TVM_GETITEMW, 0, app.memory)
            assert kernel.ReadProcessMemory(app.handle, app.memory, c.byref(data), c.sizeof(data), None)
            text = c.create_unicode_buffer(256)
            assert kernel.ReadProcessMemory(
                app.handle, app.memory + win.REMOTE_TREE_TEXT_OFFSET_BYTES, text, c.sizeof(text), None
            )
            return {
                "label": text.value,
                "check": (data.state >> win.TREE_STATE_IMAGE_SHIFT) & win.TREE_STATE_IMAGE_BITS,
                "identityPointer": bool(data.data),
            }

        def children(tree, parent=0):
            identifier = user.SendMessageW(
                tree, win.TVM_GETNEXTITEM, win.TVGN_CHILD if parent else win.TVGN_ROOT, parent
            )
            result = []
            while identifier:
                row = item(tree, identifier)
                row["children"] = children(tree, identifier)
                result.append(row)
                identifier = user.SendMessageW(tree, win.TVM_GETNEXTITEM, win.TVGN_NEXT, identifier)
            return result

        def select(tree, label, parent=0):
            identifier = user.SendMessageW(
                tree, win.TVM_GETNEXTITEM, win.TVGN_CHILD if parent else win.TVGN_ROOT, parent
            )
            while identifier:
                if item(tree, identifier)["label"] == label:
                    user.SendMessageW(tree, win.TVM_SELECTITEM, win.TVGN_CARET, identifier)
                    return True
                if select(tree, label, identifier):
                    return True
                identifier = user.SendMessageW(tree, win.TVM_GETNEXTITEM, win.TVGN_NEXT, identifier)
            return False

        def snapshot(d, phase):
            tree = user.GetDlgItem(d, win.IDC_STYLELIST)
            selected = user.SendMessageW(tree, win.TVM_GETNEXTITEM, win.TVGN_CARET, 0)
            records.append(
                {
                    "phase": phase,
                    "selected": item(tree, selected)["label"] if selected else None,
                    "default": user.SendMessageW(user.GetDlgItem(d, win.IDC_DEFAULTSCHEME), win.BM_GETCHECK, 0, 0),
                    "defaultEnabled": bool(user.IsWindowEnabled(user.GetDlgItem(d, win.IDC_DEFAULTSCHEME))),
                    "autoSelect": user.SendMessageW(user.GetDlgItem(d, win.IDC_AUTOSELECT), win.BM_GETCHECK, 0, 0),
                    "tree": children(tree),
                    "document": app.state(),
                }
            )
            if visible:
                from PIL import ImageGrab

                flags = win.SWP_NOSIZE | win.SWP_NOMOVE | win.SWP_NOACTIVATE
                assert user.SetWindowPos(d, w.HWND(win.HWND_TOPMOST), 0, 0, 0, 0, flags)
                user.RedrawWindow(
                    d, None, None, win.RDW_INVALIDATE | win.RDW_ERASE | win.RDW_ALLCHILDREN | win.RDW_UPDATENOW
                )
                time.sleep(0.1)
                rect = w.RECT()
                assert user.GetWindowRect(d, c.byref(rect))
                ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom)).save(output / f"{phase}.png")
                assert user.SetWindowPos(d, w.HWND(win.HWND_NOTOPMOST), 0, 0, 0, 0, flags)

        app.reset(b"class Example {};\n", 1, 9)
        before = app.state()
        d = dialog()
        snapshot(d, "normal-initial")
        tree = user.GetDlgItem(d, win.IDC_STYLELIST)
        assert select(tree, "Python Script")
        snapshot(d, "selected-existing-default")
        user.SendMessageW(user.GetDlgItem(d, win.IDC_DEFAULTSCHEME), win.BM_CLICK, 0, 0)
        assert select(tree, "C/C++ Source")
        snapshot(d, "default-unchecked-then-selection")
        user.SendMessageW(user.GetDlgItem(d, win.IDC_DEFAULTSCHEME), win.BM_CLICK, 0, 0)
        assert select(tree, "C")
        snapshot(d, "group-selection")
        user.SendMessageW(user.GetDlgItem(d, win.IDC_AUTOSELECT), win.BM_CLICK, 0, 0)
        user.SendMessageW(d, win.WM_COMMAND, win.IDCANCEL, 0)
        d = dialog()
        snapshot(d, "cancel-reopened")
        assert select(user.GetDlgItem(d, win.IDC_STYLELIST), "C/C++ Source")
        user.SendMessageW(d, win.WM_COMMAND, win.IDOK, 0)
        assert app.state() == before
        d = dialog(True)
        snapshot(d, "favorite-initial")
        assert select(user.GetDlgItem(d, win.IDC_STYLELIST), "Rust Source")
        user.SendMessageW(user.GetDlgItem(d, win.IDC_DEFAULTSCHEME), win.BM_CLICK, 0, 0)
        user.SendMessageW(user.GetDlgItem(d, win.IDC_AUTOSELECT), win.BM_CLICK, 0, 0)
        user.SendMessageW(d, win.WM_COMMAND, win.IDOK, 0)
        assert app.state() == before
        d = dialog()
        snapshot(d, "favorite-accepted-normal-reopened")
        assert select(user.GetDlgItem(d, win.IDC_STYLELIST), "Rust Source")
        snapshot(d, "favorite-default-preserved")
        user.SendMessageW(d, win.WM_COMMAND, win.IDCANCEL, 0)
        (output / "observations.json").write_text(
            json.dumps(
                {
                    "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
                    "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
                    "records": records,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        print("PASS: independent original native scheme selection/default/cancel/favorite observations")
    finally:
        if app:
            app.close()
        if not visible:
            user.CloseDesktop(desktop)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--visible", action="store_true", help="Owned Default-desktop windows and screenshots")
    arguments = parser.parse_args()
    capture(arguments.original.resolve(), arguments.output.resolve(), arguments.visible)
