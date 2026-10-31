"""Independent actual-app protocol check; requires a graphical session."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from resources import native_edit


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
