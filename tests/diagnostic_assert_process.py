"""Verify the real editor-runtime assertion still aborts when diagnostics fail."""

import argparse
import os
from pathlib import Path
import signal
import subprocess
import tempfile


def require_abort(result, scenario):
    expected = 3 if os.name == "nt" else -signal.SIGABRT
    if result.returncode != expected:
        raise RuntimeError(
            f"{scenario}: expected abort exit {expected}, got {result.returncode}: {result.stdout!r} {result.stderr!r}"
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    executable = str(parser.parse_args().executable.resolve())
    normal = subprocess.run([executable], capture_output=True, text=True, encoding="utf-8", check=False, timeout=10)
    require_abort(normal, "normal stderr")
    if normal.stderr != "diagnostic_assert.cpp:42: owned assertion\n":
        raise RuntimeError(f"assertion diagnostic changed: {normal.stderr!r}")
    with tempfile.TemporaryDirectory(prefix="wxnotepad-diagnostic-assert-") as directory:
        path = Path(directory) / "input.txt"
        original = b"owned read-only stderr handle\n"
        path.write_bytes(original)
        with path.open("rb") as unwritable:
            failed = subprocess.run([executable], stdout=subprocess.PIPE, stderr=unwritable, check=False, timeout=10)
        require_abort(failed, "unwritable stderr")
        if path.read_bytes() != original:
            raise RuntimeError("assertion modified the input-only stderr fixture")
    print("PASS: real Platform::Assert preserves its message and abort with normal or unwritable stderr", flush=True)


if __name__ == "__main__":
    main()
