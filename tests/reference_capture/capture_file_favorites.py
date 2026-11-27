"""Capture the untouched pinned original's File Favorites on an owned copy."""

import argparse
import ctypes as c
import hashlib
import json
import shutil
import subprocess
import sys
from ctypes import wintypes as w
from pathlib import Path

from capture_printing import Native, add_unsaved_text

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from file_favorites_packaged import shortcut  # noqa: E402 -- Shared independent OS shortcut reader.
from localization_layout import screenshot_windows  # noqa: E402 -- Use owned-window capture.

OPEN, ADD, MANAGE = 40017, 40018, 40019
EXPECTED_HASH = "1425c9a4b5ab2ea1fa3ba088f38dd987b763b7b8feff23dde6243f7855e86a78"


def read_editor(native, editor):
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.VirtualAllocEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD]
    kernel.VirtualAllocEx.restype = c.c_void_p
    kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
    kernel.VirtualFreeEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    length = native.send(editor, 2006, 0, 0)
    process = kernel.OpenProcess(0x38, False, native.process.pid)
    remote = kernel.VirtualAllocEx(process, None, length + 1, 0x3000, 4)
    try:
        assert remote
        native.send(editor, 2182, length + 1, remote)
        buffer = c.create_string_buffer(length + 1)
        assert kernel.ReadProcessMemory(process, remote, buffer, length + 1, None)
        return buffer.raw[:length]
    finally:
        if remote:
            kernel.VirtualFreeEx(process, remote, 0, 0x8000)
        kernel.CloseHandle(process)


def select(native, dialog, index=0):
    control = native.item(dialog, 100)
    native.send(control, 0x100, 0x24, 1)
    for _ in range(index):
        native.send(control, 0x100, 0x28, 1)
    native.respond(dialog, 1)


def answer(native, dialog, caption):
    for button in native.windows(dialog):
        if native.class_name(button) == "Button" and native.text(button).replace("&", "") == caption:
            native.post(button, 0xF5, 0, 0)
            return
    raise AssertionError((caption, native.controls(dialog)))


def observe_manage(native, frame, favorites, output):
    with (output / "manage-explorer.log").open("wb") as log:
        observer = subprocess.Popen(
            [
                "pwsh",
                "-NoProfile",
                "-File",
                str(Path(__file__).resolve().parents[1] / "open_target_explorer.ps1"),
                "-Root",
                str(output),
                "-Evidence",
                str(output),
                "-Requests",
                "1",
            ],
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        try:
            native.wait((output / "explorer-ready").exists, "Explorer baseline")
            (output / "folder-request-1").write_text(f"{favorites}\n\n{native.process.pid}\n", encoding="utf-8")
            native.post(frame, 0x111, MANAGE, 0)
            assert observer.wait(timeout=25) == 0
        finally:
            if observer.poll() is None:
                observer.terminate()
                observer.wait(timeout=10)


def main():
    user = c.WinDLL("user32", use_last_error=True)
    user.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
    assert user.SetProcessDpiAwarenessContext(w.HANDLE(-4))
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    binary = output / "Notepad4.exe"
    shutil.copy2(args.executable, binary)
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    assert digest == EXPECTED_HASH, digest
    favorites = output / "favorites"
    favorites.mkdir()
    target = output / "reference 中文.txt"
    original_bytes = "independent favorite 文档\n".encode()
    target.write_bytes(original_bytes)
    ini = output / "Notepad4.ini"
    ini.write_text(
        f"[Settings]\nSaveSettings=1\nFavorites={favorites}\n[Settings2]\nUILanguage=1033\n", encoding="utf-16"
    )
    process = subprocess.Popen([str(binary), "/n", "/e", "utf-8", str(target)], cwd=output)
    native = Native(process)
    rows = {"revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a", "executable_sha256": digest}
    frame = editor = None
    try:
        frame = native.wait(
            lambda: next((window for window in native.windows() if native.user.GetMenu(window)), None),
            "owned original frame",
        )
        editor = next(window for window in native.windows(frame) if native.class_name(window) == "Scintilla")
        rows["file_menu"] = native.menu(frame)
        file_menu = native.user.GetSubMenu(native.user.GetMenu(frame), 0)
        favorites_menu = None
        for index in range(native.user.GetMenuItemCount(file_menu)):
            label = c.create_unicode_buffer(256)
            native.user.GetMenuStringW(file_menu, index, label, len(label), 0x400)
            if label.value == "&Favorites":
                rows["favorites_menu_index"] = index
                favorites_menu = native.user.GetSubMenu(file_menu, index)
        assert favorites_menu
        rows["commands"] = []
        for index in range(native.user.GetMenuItemCount(favorites_menu)):
            label = c.create_unicode_buffer(256)
            native.user.GetMenuStringW(favorites_menu, index, label, len(label), 0x400)
            rows["commands"].append([native.user.GetMenuItemID(favorites_menu, index), label.value])
        assert rows["commands"] == [
            [OPEN, "&Open Favorites...\tAlt+I"],
            [ADD, "&Add Current File...\tAlt+K"],
            [MANAGE, "&Manage...\tAlt+F9"],
        ]
        dialog = native.open(frame, ADD)
        rows["add_controls"] = native.controls(dialog)
        rows["default_name"] = native.text(native.item(dialog, 100))
        screenshot_windows(native.user, dialog, output / "original-add.png")
        native.respond(dialog, 2)
        native.closed(dialog)
        assert not list(favorites.iterdir())
        dialog = native.open(frame, ADD)
        native.set_text(native.item(dialog, 100), "文档 favorite")
        native.respond(dialog, 1)
        native.closed(dialog)
        response = native.dialog()
        rows["add_success"] = native.controls(response)
        answer(native, response, "OK")
        native.closed(response)
        link = favorites / "文档 favorite.lnk"
        assert Path(shortcut(link)) == target
        rows["shortcut_target"] = shortcut(link)
        before_duplicate = link.read_bytes()
        dialog = native.open(frame, ADD)
        native.set_text(native.item(dialog, 100), "文档 favorite")
        native.respond(dialog, 1)
        native.closed(dialog)
        response = native.dialog()
        rows["duplicate_failure"] = native.controls(response)
        answer(native, response, "OK")
        native.closed(response)
        assert link.read_bytes() == before_duplicate
        dialog = native.open(frame, OPEN)
        rows["favorites_controls"] = native.controls(dialog)
        screenshot_windows(native.user, dialog, output / "original-favorites.png")
        select(native, dialog)
        native.closed(dialog)
        assert read_editor(native, editor) == original_bytes
        rows["file_open_hex"] = read_editor(native, editor).hex()
        add_unsaved_text(native, editor, "X")
        question = native.open(frame, OPEN)
        rows["save_question_before_picker"] = native.controls(question)
        answer(native, question, "Cancel")
        native.closed(question)
        assert native.send(editor, 2159, 0, 0)
        question = native.open(frame, OPEN)
        answer(native, question, "Yes")
        native.closed(question)
        dialog = native.dialog()
        rows["after_save_picker"] = native.controls(dialog)
        native.respond(dialog, 2)
        native.closed(dialog)
        rows["saved_target_hex"] = target.read_bytes().hex()
        assert not native.send(editor, 2159, 0, 0)
        add_unsaved_text(native, editor, "discard")
        question = native.open(frame, OPEN)
        answer(native, question, "No")
        native.closed(question)
        dialog = native.dialog()
        select(native, dialog)
        native.closed(dialog)
        assert read_editor(native, editor) == target.read_bytes()
        directory = output / "directory target 中文"
        directory.mkdir()
        shortcut(favorites / "A directory.lnk", directory)
        dialog = native.open(frame, OPEN)
        select(native, dialog)
        native.closed(dialog)
        chooser = native.dialog()
        rows["directory_file_chooser"] = native.controls(chooser)
        native.respond(chooser, 2)
        native.closed(chooser)
        observe_manage(native, frame, favorites, output)
        rows["manage_directory"] = str(favorites)
        shortcut(favorites / "B broken.lnk", output / "missing-target.txt")
        dialog = native.open(frame, OPEN)
        select(native, dialog, 1)
        native.closed(dialog)
        response = native.dialog()
        rows["broken_target"] = native.controls(response)
        rows["broken_target_editor_hex"] = read_editor(native, editor).hex()
        answer(native, response, "No")
        native.closed(response)
        rows["after_broken_process_exit"] = process.poll()
        rows["missing_choices"] = {
            "No": {"hex": read_editor(native, editor).hex(), "exists": (output / "missing-target.txt").exists()}
        }
        for caption in ("Yes",):
            dialog = native.open(frame, OPEN)
            select(native, dialog, 1)
            native.closed(dialog)
            response = native.dialog()
            rows["missing_choices"][caption] = {"controls": native.controls(response)}
            answer(native, response, caption)
            native.closed(response)
            if caption == "Yes":
                native.wait(lambda: native.send(editor, 2006, 0, 0) == 0, "original missing target opens empty")
            rows["missing_choices"][caption].update(
                hex=read_editor(native, editor).hex(),
                exists=(output / "missing-target.txt").exists(),
                title=native.text(frame),
            )
        assert rows["missing_choices"]["No"]["hex"] == target.read_bytes().hex()
        assert not rows["missing_choices"]["No"]["exists"]
        assert rows["missing_choices"]["Yes"]["exists"] and rows["missing_choices"]["Yes"]["hex"] == ""
        dialog = native.open(frame, OPEN)
        select(native, dialog, 2)
        native.closed(dialog)
        native.wait(lambda: read_editor(native, editor) == target.read_bytes(), "original returns to document")
        shortcut(favorites / "B broken.lnk", output / "missing-cancel.txt")
        dialog = native.open(frame, OPEN)
        select(native, dialog, 1)
        native.closed(dialog)
        response = native.dialog()
        rows["missing_choices"]["Cancel"] = {
            "controls": native.controls(response),
            "before_hex": read_editor(native, editor).hex(),
        }
        answer(native, response, "Cancel")
        native.closed(response)
        rows["missing_choices"]["Cancel"].update(
            exit_code=process.wait(timeout=10), exists=(output / "missing-cancel.txt").exists()
        )
        assert rows["missing_choices"]["Cancel"]["exit_code"] == 0
        rows["complete"] = True
    finally:
        (output / "original-file-favorites.json").write_text(
            json.dumps(rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        if process.poll() is None:
            for window in native.windows():
                if native.class_name(window) == "#32770":
                    native.post(window, 0x10, 0, 0)
            if editor:
                native.post(editor, 2014, 0, 0)
            if frame:
                native.post(frame, 0x10, 0, 0)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    main()
