"""Exercise the production diagnostic boundary with real stdio and allocation failures."""

import argparse
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    executable = parser.parse_args().executable.resolve()
    for scenario in ("success", "write-failure", "allocation-failure"):
        with tempfile.TemporaryDirectory(prefix="wxnotepad-diagnostic-print-") as directory:
            root = Path(directory)
            original = b"owned input remains unchanged\n"
            (root / "input.txt").write_bytes(original)
            result = subprocess.run(
                [str(executable), scenario, str(root)], capture_output=True, check=False, timeout=10
            )
            if result.returncode != 0:
                raise RuntimeError(f"{scenario}: exit {result.returncode}: {result.stderr!r}")
            expected = "print 42 002a 1\n中文\n".encode() if scenario == "success" else b"recovered\n"
            if scenario == "allocation-failure":
                expected = b"x" * 8192 + b"\nrecovered\n"
            if (root / "output.txt").read_bytes() != expected:
                raise RuntimeError(f"{scenario}: diagnostic output bytes changed")
            if (root / "input.txt").read_bytes() != original:
                raise RuntimeError(f"{scenario}: input-only fixture was modified")
            if scenario == "success":
                if result.stderr:
                    raise RuntimeError(f"valid diagnostics emitted fallback output: {result.stderr!r}")
            else:
                prefix = b"Diagnostic output failed: "
                if not (
                    result.stderr.startswith(prefix)
                    and result.stderr.endswith(b"\n")
                    and len(result.stderr) > len(prefix) + 1
                ):
                    raise RuntimeError(f"{scenario}: missing exception-specific fallback: {result.stderr!r}")
        print(f"PASS: production diagnostic print {scenario}", flush=True)


if __name__ == "__main__":
    main()
