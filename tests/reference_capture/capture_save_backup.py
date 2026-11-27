"""Capture the original File > Save Backup command on a private desktop."""

import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timedelta
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import sys
import time
from uuid import UUID, uuid4

from original_host import Original, kernel, user


SAVE_BACKUP = 40028


def local_backup_directory():
    shell = c.WinDLL("shell32")
    shell.SHGetKnownFolderPath.argtypes = [c.c_void_p, w.DWORD, w.HANDLE, c.POINTER(w.LPWSTR)]
    free = c.WinDLL("ole32").CoTaskMemFree
    free.argtypes = [c.c_void_p]
    key = c.create_string_buffer(UUID("f1b32785-6fba-4fcf-9d55-7b8e7f157091").bytes_le)
    result = w.LPWSTR()
    assert shell.SHGetKnownFolderPath(key, 0, None, c.byref(result)) == 0
    try:
        return Path(result.value) / "Notepad4" / "AutoSave"
    finally:
        free(result)


def menu_state(original):
    user.GetMenu.argtypes = [w.HWND]
    user.GetMenu.restype = w.HMENU
    user.GetSubMenu.argtypes = [w.HMENU, c.c_int]
    user.GetSubMenu.restype = w.HMENU
    user.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]
    user.GetMenuState.restype = w.UINT
    user.GetMenuItemCount.argtypes = [w.HMENU]
    user.GetMenuItemID.argtypes = [w.HMENU, c.c_int]
    user.GetMenuStringW.argtypes = [w.HMENU, w.UINT, w.LPWSTR, c.c_int, w.UINT]
    menu = user.GetSubMenu(user.GetMenu(original.hwnd), 0)
    assert menu, "original File menu appeared"
    user.SendMessageW(original.hwnd, 0x117, menu, 0)
    index = next(i for i in range(user.GetMenuItemCount(menu)) if user.GetMenuItemID(menu, i) == SAVE_BACKUP)
    labels = []
    for position in range(index - 1, index + 2):
        label = c.create_unicode_buffer(128)
        assert user.GetMenuStringW(menu, position, label, len(label), 0x400)
        labels.append(label.value)
    return {"labels": labels, "enabled": (user.GetMenuState(menu, SAVE_BACKUP, 0) & 3) == 0}


def window_title(original):
    user.GetWindowTextW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
    title = c.create_unicode_buffer(1024)
    assert user.GetWindowTextW(original.hwnd, title, len(title))
    return title.value


def wait_ready(original, basename):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        if window_title(original).startswith(basename):
            return
        time.sleep(0.03)
    raise TimeoutError(f"original never opened {basename}: {window_title(original)}")


def state(original):
    return {**original.state(), "title": window_title(original)}


def capture_case(executable, desktop, desktop_name, output, fallback, run_id, case):
    label, name, encoding, source, scenario = case
    root = output / "inputs" / label
    if scenario == "dotted-directory":
        root /= "parent.dot"
    root.mkdir(parents=True)
    path = root / f"{run_id}-{name}" if name else None
    if path:
        path.write_bytes(source)
    original = Original(path, executable, desktop, desktop_name, encoding)
    owned = []
    try:
        wait_ready(original, path.name if path else "Untitled")
        if scenario == "untitled":
            original.send(2001, len(source), original.put(source))
        if scenario == "dirty":
            for text in (b"!", b"?"):
                original.send(2078)
                original.send(2001, 1, original.put(text))
                original.send(2079)
            original.send(2176)
            original.send(2160, 4, 1)
        if scenario == "missing-directory":
            root.rename(root.with_name(root.name + "-moved"))

        timer_start = datetime.now()
        collisions = {}
        if scenario.startswith("collision"):
            if scenario == "collision-both":
                fallback.mkdir(parents=True, exist_ok=True)
            for second in range(-1, 4):
                stamp = (timer_start + timedelta(seconds=second)).strftime(" %Y%m%d %H%M%S")
                sibling = path.with_name(f"{path.name}{stamp}.txt")
                collisions[sibling] = b"existing same-directory backup"
                if scenario == "collision-both":
                    collisions[fallback / sibling.name] = b"existing fallback backup"
            for item, payload in collisions.items():
                with item.open("xb") as file:
                    if item.parent == fallback:
                        owned.append(item)
                    file.write(payload)
        directories = [("sibling", root), ("fallback", fallback)]
        before_paths = {item for _, directory in directories if directory.is_dir() for item in directory.iterdir()}
        before = state(original)
        menu = menu_state(original)
        original.command(SAVE_BACKUP)
        after = state(original)
        observed = []
        for location, directory in directories:
            for item in directory.iterdir() if directory.is_dir() else ():
                if item in before_paths or not item.is_file():
                    continue
                data = item.read_bytes()
                if not (item.name.startswith(run_id) if path else item.name.startswith("Untitled ") and data == source):
                    continue
                observed.append({"name": item.name, "hex": data.hex(), "location": location})
                if location == "fallback":
                    owned.append(item)
                archive = output / "backups" / label / location / item.name
                archive.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(item, archive)
        assert all(item.read_bytes() == expected for item, expected in collisions.items())
        if path and path.exists():
            assert path.read_bytes() == source
        return {
            "case": label,
            "path": str(path) if path else None,
            "source_hex": source.hex(),
            "before": before,
            "after": after,
            "menu": menu,
            "backups": observed,
            "local_start": timer_start.isoformat(),
            "local_end": datetime.now().isoformat(),
        }
    finally:
        try:
            original.close()
        finally:
            for item in owned:
                item.unlink()


def main():
    source, output = (Path(item).resolve() for item in sys.argv[1:3])
    portable = output / "original-portable"
    portable.mkdir(parents=True)
    executable = portable / "Notepad4.exe"
    shutil.copy2(source, executable)
    (portable / "Notepad4.ini").write_text(
        "[Settings]\nSaveSettings=0\nFileWatchingMode=0\nAutoSaveOption=0\nWarnLineEndings=0\n"
        "LoadASCIIasUTF8=0\nLoadANSIasUTF8=0\n",
        encoding="utf-16",
    )
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    desktop_name = "save-backup-reference-" + str(os.getpid())
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x1FF, None)
    if not desktop:
        raise c.WinError(c.get_last_error())
    fallback = local_backup_directory()
    run_id = "wxnp232-" + uuid4().hex[:12]
    mixed = "中é😀\r\nA\0B\rC\nD"
    cases = [
        ("empty", "empty.txt", "utf-8", b"", "none"),
        ("empty-untitled", None, "utf-8", b"", "none"),
        ("untitled", None, "utf-8", b"Untitled sample " + run_id.encode(), "untitled"),
        ("native-ansi", "ansi.txt", "ansi", "aé中\nend".encode("mbcs", "replace"), "none"),
        ("native-oem", "oem.txt", "oem", "aé中\nend".encode(f"cp{kernel.GetOEMCP()}", "replace"), "none"),
        ("windows-1252", "european.txt", "cp1252", b"caf\xe9\r\nlast", "none"),
        ("gbk", "chinese", "gbk", "中\n国".encode("gbk"), "none"),
        ("gb18030", "supplementary.txt", "gb18030", "中😀\n字".encode("gb18030"), "none"),
        ("big5", "traditional.txt", "big5", "漢\n字".encode("big5"), "none"),
        ("dos437", "dos.txt", "cp437", "café\nline".encode("cp437"), "none"),
        ("utf8-mixed-nul", "sample.tar.gz", "utf-8", mixed.encode(), "dirty"),
        ("utf8-bom", "sample.txt", "utf-8", b"\xef\xbb\xbf" + mixed.encode(), "none"),
        ("utf16-le", "sample.txt", "utf-16", b"\xff\xfe" + mixed.encode("utf-16-le"), "none"),
        ("utf16-be", "sample.txt", "utf-16be", b"\xfe\xff" + mixed.encode("utf-16-be"), "none"),
        ("utf16-le-no-bom", "sample.txt", "utf-16", mixed.encode("utf-16-le"), "none"),
        ("utf16-be-no-bom", "sample.txt", "utf-16be", mixed.encode("utf-16-be"), "none"),
        ("space-in-extension", "file.my suffix", "utf-8", b"space", "none"),
        ("dot-in-parent", "file.txt", "utf-8", b"dot in parent", "dotted-directory"),
        ("fallback-crlf", "é中🙂.txt", "ansi", "oneé\r\ntwo".encode("mbcs", "replace"), "missing-directory"),
        ("fallback-lf", "é中🙂.txt", "cp1252", b"caf\xe9\nlast", "missing-directory"),
        ("fallback-cr", "é中🙂.txt", "utf-8", "one🙂\rtwo".encode(), "missing-directory"),
        ("collision-sibling", "collision.txt", "utf-8", "one🙂\ntwo".encode(), "collision"),
        ("collision-both", "collision.txt", "utf-8", "one🙂\ntwo".encode(), "collision-both"),
    ]
    results = []
    try:
        for case in cases:
            row = capture_case(executable, desktop, desktop_name, output, fallback, run_id, case)
            results.append(row)
            print(f"Captured {case[0]}: {row['backups']}", flush=True)
    finally:
        user.CloseDesktop(desktop)
        summary = {
            "original_revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
            "executable_sha256": sha256(executable.read_bytes()).hexdigest(),
            "ansi_cp": kernel.GetACP(),
            "oem_cp": kernel.GetOEMCP(),
            "fallback": str(fallback),
            "fixtures": results,
        }
        (output / "original.json").write_text(
            json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    assert all(row["before"] == row["after"] for row in results)
    assert all(
        len(row["backups"]) == int(row["case"] not in {"empty", "empty-untitled", "collision-both"}) for row in results
    )


if __name__ == "__main__":
    main()
