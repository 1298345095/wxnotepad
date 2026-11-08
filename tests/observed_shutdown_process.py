"""Exercise publication contention and clean shutdown in independent actual apps."""
from contextlib import ExitStack, contextmanager
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
        # Ordinary Python readers deny delete but permit write. Publication
        # must wait for replacement instead of copying over the visible file.
        with state.open("rb"):
            yield
    elif mode in ("restricted-reader", "exclusive-reader"):
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                        wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel32.CreateFileW.restype = wintypes.HANDLE
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        # GENERIC_READ, OPEN_EXISTING: deny replacement and fallback overwrite.
        # The exclusive reader also denies the app's attempt to read a command.
        share = 0 if mode == "exclusive-reader" else 1
        handle = kernel32.CreateFileW(str(state), 0x80000000, share, None, 3, 0, None)
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


@contextmanager
def suspended_editor(app):
    """Stage the external lock before the real GUI thread can consume a packet."""
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    kernel32.OpenThread.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenThread.restype = wintypes.HANDLE
    kernel32.SuspendThread.argtypes = [wintypes.HANDLE]
    kernel32.SuspendThread.restype = wintypes.DWORD
    kernel32.ResumeThread.argtypes = [wintypes.HANDLE]
    kernel32.ResumeThread.restype = wintypes.DWORD
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    owner = wintypes.DWORD()
    thread_id = user32.GetWindowThreadProcessId(int(app.state["editor_handle"]), ctypes.byref(owner))
    assert owner.value == app.process.pid, "suspend only the launched app's editor thread"
    thread = kernel32.OpenThread(2, False, thread_id)
    if not thread:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        if kernel32.SuspendThread(thread) == 0xffffffff:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            yield
        finally:
            if kernel32.ResumeThread(thread) == 0xffffffff:
                raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel32.CloseHandle(thread)


def command_contention(executable, folder, environment):
    for mode in ("exclusive-reader", "restricted-reader"):
        app = Application(executable, folder, environment)
        requested = "1\nclose\n"
        try:
            with ExitStack() as reader:
                with suspended_editor(app):
                    command(folder, requested)
                    reader.enter_context(obstruct_publication(folder / "command", mode))
                time.sleep(.25)
                assert app.process.poll() is None, "pending command cannot close before its reader releases it"
                state = dict(line.split("\t", 1) for line in (folder / "state").read_text(encoding="utf-8").splitlines())
                assert state["sequence"] == "0", f"unconsumed command was acknowledged: {state}"
            app.wait_for_close(requested)
            state = dict(line.split("\t", 1) for line in (folder / "state").read_text(encoding="utf-8").splitlines())
            assert state["sequence"] == "1" and state["result"] == "ok", state
            assert not (folder / "command").exists(), "close command was consumed"
            print(f"PASS: command {mode}, clean exit={app.process.returncode}", flush=True)
        finally:
            app.stop()


def partial_command(executable, folder, environment):
    app = Application(executable, folder, environment)
    try:
        command(folder, "1\n")
        time.sleep(.25)
        state = dict(line.split("\t", 1) for line in (folder / "state").read_text(encoding="utf-8").splitlines())
        assert state["sequence"] == "0" and (folder / "command").exists(), state
        app.close()
        print("PASS: incomplete command retained until a complete close packet arrives", flush=True)
    finally:
        app.stop()


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
                        previous = (folder / "state").read_bytes() if mode != "directory" else None
                        command(folder, "1\nmenu\nView\tZoom In\n")
                        deadline = time.monotonic() + 5
                        while (folder / "command").exists() and time.monotonic() < deadline:
                            time.sleep(.01)
                        time.sleep(.25)
                        if mode != "directory":
                            assert (folder / "state").read_bytes() == previous, (
                                "a blocked snapshot replacement preserves the complete previous publication")
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
        if sys.platform == "win32":
            command_contention(executable, folder, environment)
        partial_command(executable, folder, environment)


if __name__ == "__main__":
    main()
