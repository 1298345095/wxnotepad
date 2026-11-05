"""Exercise publication contention and clean shutdown in independent actual apps."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from resource_process import command, observation_context
from settings_process import Application


@contextmanager
def obstruct_publication(state, mode):
    if mode == "crt-reader":
        # Control: ordinary Python readers deny delete but permit write;
        # wxRenameFile can still succeed through its CopyFile fallback.
        with state.open("rb"):
            yield
    elif mode == "restricted-reader":
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                        wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel32.CreateFileW.restype = wintypes.HANDLE
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        # GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING. This external reader
        # denies both replacement and the fallback overwrite while held.
        handle = kernel32.CreateFileW(str(state), 0x80000000, 1, None, 3, 0, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            yield
        finally:
            kernel32.CloseHandle(handle)
    elif mode == "directory":
        state.unlink()
        state.mkdir()
        try:
            yield
        finally:
            state.rmdir()


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-observed-shutdown-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        modes = ("none", "crt-reader", "restricted-reader") if sys.platform == "win32" else ("none", "directory")
        for mode in modes:
            app = Application(executable, folder, environment)
            try:
                if mode != "none":
                    with obstruct_publication(folder / "state", mode):
                        command(folder, "1\nmenu\nView\tZoom In\n")
                        deadline = time.monotonic() + 5
                        while (folder / "command").exists() and time.monotonic() < deadline:
                            time.sleep(.01)
                        time.sleep(.25)
                        print(f"CONTENDED {mode} " + json.dumps(observation_context(folder, app.process)), flush=True)
                    app.sequence = 1
                print("BEFORE CLOSE " + json.dumps(observation_context(folder, app.process)), flush=True)
                try:
                    app.close()
                except subprocess.TimeoutExpired:
                    print("EXIT TIMEOUT " + json.dumps(observation_context(folder, app.process)), flush=True)
                    raise
                print(f"PASS: mode={mode}, clean exit={app.process.returncode}", flush=True)
                state = dict(line.split("\t", 1) for line in
                             (folder / "state").read_text(encoding="utf-8").splitlines())
                assert state["sequence"] == str(app.sequence + 1) and state["result"] == "ok", state
                assert state["dirty"] == "0" and not (folder / "command").exists(), state
            finally:
                app.stop()


if __name__ == "__main__":
    main()
