"""Exercise publication contention and clean shutdown in independent actual apps."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from resource_process import command
from settings_process import Application


def windows(process):
    if sys.platform != "win32":
        return []
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.IsWindowEnabled.argtypes = [wintypes.HWND]
    result = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def visit(handle, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        if owner.value == process.pid:
            title = ctypes.create_unicode_buffer(1024)
            name = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(handle, title, len(title))
            user32.GetClassNameW(handle, name, len(name))
            result.append({"title": title.value, "class": name.value,
                           "visible": bool(user32.IsWindowVisible(handle)),
                           "enabled": bool(user32.IsWindowEnabled(handle))})
        return True

    user32.EnumWindows(visit, 0)
    return result


def context(app):
    files = {}
    for name in ("command", "state", "state.tmp"):
        path = app.folder / name
        if path.is_file():
            files[name] = path.read_text(encoding="utf-8")
        elif path.is_dir():
            files[name] = "<directory>"
    return {"pid": app.process.pid, "returncode": app.process.poll(),
            "sequence": app.sequence, "files": files, "windows": windows(app.process)}


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-observed-shutdown-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        for contention in (False, True):
            app = Application(executable, folder, environment)
            try:
                if contention:
                    state = folder / "state"
                    if sys.platform == "win32":
                        # Same CRT sharing policy as wait_state's read_text, held
                        # long enough to make the publication collision repeatable.
                        reader = state.open("rb")
                    else:
                        state.unlink()
                        state.mkdir()
                    try:
                        command(folder, "1\nmenu\nView\tZoom In\n")
                        deadline = time.monotonic() + 5
                        while (folder / "command").exists() and time.monotonic() < deadline:
                            time.sleep(.01)
                        time.sleep(.25)
                        print("CONTENDED " + json.dumps(context(app)), flush=True)
                    finally:
                        if sys.platform == "win32":
                            reader.close()
                        else:
                            state.rmdir()
                    app.sequence = 1
                print("BEFORE CLOSE " + json.dumps(context(app)), flush=True)
                try:
                    app.close()
                except subprocess.TimeoutExpired:
                    print("EXIT TIMEOUT " + json.dumps(context(app)), flush=True)
                    raise
                print(f"PASS: contention={contention}, clean exit={app.process.returncode}", flush=True)
                state = dict(line.split("\t", 1) for line in
                             (folder / "state").read_text(encoding="utf-8").splitlines())
                assert state["sequence"] == str(app.sequence + 1) and state["result"] == "ok", state
                assert state["dirty"] == "0" and not (folder / "command").exists(), state
            finally:
                app.stop()


if __name__ == "__main__":
    main()
