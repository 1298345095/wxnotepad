"""Observe periodic backup eligibility, rotation and lifecycle on an isolated original."""

import ctypes as c
from ctypes import wintypes as w
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import sys
import time

from original_host import Original, user
from capture_save_backup import wait_ready


def wait_for(check, message, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(0.01)
    raise AssertionError(message)


def main():
    source, output = (Path(item).resolve() for item in sys.argv[1:3])
    portable = output / "original-portable"
    portable.mkdir(parents=True)
    executable = portable / "Notepad4.exe"
    shutil.copy2(source, executable)
    ini = portable / "Notepad4.ini"
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    desktop_name = "periodic-reference-" + str(os.getpid())
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x1FF, None)
    assert desktop
    rows = []
    try:
        for retention in (False, True):
            root = output / ("retained" if retention else "managed")
            root.mkdir()
            path = root / "source.txt"
            path.write_bytes(b"seed\n")
            ini.write_text(
                "[Settings]\nSaveSettings=0\nFileWatchingMode=0\nWarnLineEndings=0\n"
                f"AutoSaveOption={9 if retention else 1}\nAutoSavePeriod=120\n",
                encoding="utf-16",
            )
            original = Original(path, executable, desktop, desktop_name)
            try:
                wait_ready(original, path.name)
                backups = lambda: sorted(root.glob("source.txt *"))
                time.sleep(0.3)
                assert not backups(), "unchanged original has no backup"
                for index in range(7):
                    original.send(2001, 1, original.put(bytes([65 + index])))
                    wait_for(
                        lambda: any(
                            item.read_bytes() == bytes(range(65, 66 + index)) + b"seed\n" for item in backups()
                        ),
                        "edited original revision backed up",
                    )
                time.sleep(0.15)  # Read after the writer has finished rotation bookkeeping.
                observed = [{"name": item.name, "hex": item.read_bytes().hex()} for item in backups()]
                assert len(observed) == (7 if retention else 6), "six-entry rotation/manual retention"
                before = original.state()
                time.sleep(0.3)
                assert original.state() == before and len(backups()) == len(observed), "stable revision is skipped"
                original.command(40004)
                time.sleep(0.25)
                after_save = len(backups())
                assert after_save == (7 if retention else 0), "ordinary save cleans only managed entries"
                rows.append(
                    {
                        "retention": retention,
                        "backups": observed,
                        "after_save": after_save,
                        "state_before_save": before,
                        "source_hex": path.read_bytes().hex(),
                    }
                )
            finally:
                original.close()
    finally:
        user.CloseDesktop(desktop)
        (output / "original.json").write_text(
            json.dumps(
                {
                    "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
                    "executable_sha256": sha256(executable.read_bytes()).hexdigest(),
                    "fixtures": rows,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    print("PASS: original periodic eligibility, rotation, retention and save cleanup")


if __name__ == "__main__":
    main()
