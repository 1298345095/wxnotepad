"""Prepare owned real histories, then compare startup after manual monitor removal."""

import argparse
import ctypes as c
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from ctypes import wintypes as w
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from capture_window_positions import ORIGINAL_SHA256, REVISION, Placement  # noqa: E402 -- Bootstrap source tools first.
from localization_package import Process, read_settings  # noqa: E402 -- Bootstrap source tools first.
from original_windows import find_frames  # noqa: E402 -- Bootstrap source tools first.
from window_position_process import native_rectangle, secondary_windows_positions  # noqa: E402 -- Bootstrap source tools first.


def original(executable, arguments):
    user = c.WinDLL("user32", use_last_error=True)
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    user.GetWindowPlacement.argtypes = [w.HWND, c.POINTER(Placement)]
    user.IsWindowVisible.argtypes = [w.HWND]
    user.GetDpiForWindow.argtypes = [w.HWND]
    user.SendMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    process = subprocess.Popen([str(executable), "/n", *arguments])
    try:
        frames = find_frames(user, process, callback)
        assert len(frames) == 1
        frame = frames[0]
        deadline = time.monotonic() + 10
        while not user.IsWindowVisible(frame):
            assert process.poll() is None and time.monotonic() < deadline
            time.sleep(0.01)
        placement = Placement(c.sizeof(Placement))
        assert user.GetWindowPlacement(frame, c.byref(placement))
        rect = placement.normal
        result = dict(
            normal=[rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top],
            dpi=user.GetDpiForWindow(frame),
        )
        user.SendMessageW(frame, 0x10, 0, 0)
        assert process.wait(timeout=10) == 0
        return result
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)


def candidate(executable, directory, arguments):
    environment = dict(os.environ, PATH=str(executable.parent) + os.pathsep + os.environ["PATH"])
    app = Process(executable, directory, environment, arguments=("-n", *arguments))
    try:
        result = dict(normal=list(native_rectangle(app)), dpi=int(app.state["dpi"]))
        app.close()
        return result
    finally:
        app.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("prepare", "verify"))
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--original", type=Path)
    args = parser.parse_args()
    assert sys.platform == "win32"
    user = c.WinDLL("user32")
    user.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
    assert user.SetProcessDpiAwarenessContext(w.HANDLE(-4))
    root = args.root.resolve()
    stamp = str(time.time_ns())
    if args.phase == "prepare":
        positions = secondary_windows_positions()
        assert positions, "Prepare requires the user's actual extended secondary monitor"
        root.mkdir(parents=True, exist_ok=False)
        assert hashlib.sha256(args.original.read_bytes()).hexdigest() == ORIGINAL_SHA256
        for name, source in (("candidate", args.candidate), ("original", args.original)):
            folder = root / name
            folder.mkdir()
            shutil.copy2(source, folder / source.name)
            if (source.parent / "locale").exists():
                shutil.copytree(source.parent / "locale", folder / "locale")
            (folder / "Notepad4.ini").write_text(
                "[Settings]\nSaveSettings=1\n[Settings2]\nUILanguage=1033\n", encoding="utf-16"
            )
        argument = ",".join(map(str, (*positions[0], 0)))
        observed = dict(
            candidate=candidate(
                root / "candidate" / args.candidate.name, root / ("observed-" + stamp), ("-pos", argument)
            ),
            original=original(root / "original" / args.original.name, ("/pos", argument)),
        )
        assert observed["candidate"] == observed["original"], observed
        histories = {}
        for name in ("candidate", "original"):
            config = read_settings(root / name / "Notepad4.ini")
            histories[name] = {
                section: dict(config[section])
                for section in config.sections()
                if section.startswith("Window Position ") or section == "Settings"
            }
            assert any(section.startswith("Window Position ") for section in histories[name])
        report = dict(
            revision=REVISION,
            argument=argument,
            observed=observed,
            histories=histories,
            candidate_name=args.candidate.name,
            original_name=args.original.name,
        )
        (root / "prepared.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report))
        print("PASS: independent candidate/original secondary histories retained; both owned windows closed")
    else:
        assert not secondary_windows_positions(), "Verify waits for the user to restore a single active monitor"
        prepared = json.loads((root / "prepared.json").read_text(encoding="utf-8"))
        observed = dict(
            original=original(root / "original" / prepared["original_name"], ()),
            candidate=candidate(root / "candidate" / prepared["candidate_name"], root / ("observed-" + stamp), ()),
        )
        (root / ("verified-" + stamp + ".json")).write_text(json.dumps(observed, indent=2) + "\n", encoding="utf-8")
        assert observed["candidate"] == observed["original"], observed
        print(json.dumps(observed))
        print("PASS: removed-monitor startup matches independent original native rectangle and DPI")


if __name__ == "__main__":
    main()
