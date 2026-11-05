"""Independent actual-app protocol check; requires a graphical session."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from resources import native_edit


def process_windows(process):
    """Read only the target process's native window metadata on Windows."""
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
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.EnumChildWindows.argtypes = [wintypes.HWND, callback_type, wintypes.LPARAM]
    result = []

    def metadata(handle):
        title = ctypes.create_unicode_buffer(2048)
        name = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(handle, title, len(title))
        user32.GetClassNameW(handle, name, len(name))
        return {"title": title.value, "class": name.value,
                "visible": bool(user32.IsWindowVisible(handle)),
                "enabled": bool(user32.IsWindowEnabled(handle))}

    @callback_type
    def visit(handle, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        if owner.value == process.pid:
            window = metadata(handle)
            children = []

            @callback_type
            def child(child_handle, _):
                children.append(metadata(child_handle))
                return True

            user32.EnumChildWindows(handle, child, 0)
            window["children"] = children
            result.append(window)
        return True

    user32.EnumWindows(visit, 0)
    return result


def observation_context(folder, process):
    files = {}
    for name in ("command", "state", "state.tmp"):
        try:
            files[name] = (folder / name).read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            files[name] = str(error)
    return {"pid": process.pid, "returncode": process.poll(),
            "files": files, "windows": process_windows(process)}


def wait_state(folder, process, predicate, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"App exited: {process.returncode}")
        try:
            state = dict(line.split("\t", 1) for line in
                         (folder / "state").read_text(encoding="utf-8").splitlines())
            if predicate(state):
                return state
        except (FileNotFoundError, PermissionError):
            pass
        time.sleep(.01)
    raise AssertionError("Actual app observation timed out")


def command(folder, text):
    pending = folder / "pending"
    pending.write_text(text, encoding="utf-8")
    pending.replace(folder / "command")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-process-") as name:
        folder = Path(name)
        sample = folder / "input.txt"
        original = b"visible app\nsecond line\n"
        sample.write_bytes(original)
        process = subprocess.Popen([str(args.executable.resolve()), "--observe", str(folder),
                                    "--startup-file", str(sample)])
        try:
            state = wait_state(folder, process, lambda s: s["sequence"] == "0" and
                               (s["backend"] != "GdkWaylandDisplay" or s["active"] == "1"))
            assert state["length"] == str(len(original)), state
            assert state["shown"] == "1" and state["dirty"] == "0", state
            assert state["active"] in ("0", "1"), state
            assert state["backend"] in ("Windows", "GdkWaylandDisplay", "GdkX11Display"), state
            native_edit(process, state)
            state = wait_state(folder, process, lambda s: s["dirty"] == "1")
            assert state["length"] == str(len(original) + 1), state
            command(folder, "1\nsave\n")
            state = wait_state(folder, process, lambda s: s["sequence"] == "1")
            assert state["result"] == "ok", state
            assert sample.read_bytes() == b"X" + original
        finally:
            process.terminate()
            process.wait(timeout=10)
    print("Actual app startup-file, shown state, native edit and saved bytes passed")


if __name__ == "__main__":
    main()
