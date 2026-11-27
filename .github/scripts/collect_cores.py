"""Retain matching Linux crash executables and print their native backtraces."""

import argparse
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def crash_executable(executable, evidence):
    if executable.is_file():
        return executable
    # Packaged tests retain the exact failed fixture before removing its /tmp
    # directory. Match that fixture identity and executable-relative path.
    candidates = set()
    for failure in evidence.rglob("*-failure-*"):
        for ancestor in executable.parents:
            if ancestor.name and failure.name.endswith("-failure-" + ancestor.name):
                candidate = failure / executable.relative_to(ancestor)
                if candidate.is_file():
                    candidates.add(candidate)
    if len(candidates) != 1:
        raise FileNotFoundError(f"{executable}: expected one retained failure binary, found {len(candidates)}")
    return candidates.pop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-dir", type=Path, default=ROOT / ".ci/reports/cores")
    parser.add_argument("--evidence-dir", type=Path, default=ROOT / ".ci/build")
    args = parser.parse_args()
    directory = args.core_dir
    failed = False
    for core in sorted(directory.glob("core.*")):
        # Linux core_pattern is core.%p.%E; %E escapes executable path slashes as !.
        _, pid, encoded = core.name.split(".", 2)
        executable = Path(encoded.replace("!", "/"))
        source = crash_executable(executable, args.evidence_dir)
        retained = directory / f"executable-{pid}-{executable.name}"
        shutil.copy2(source, retained)
        report = directory / f"backtrace-{pid}.txt"
        with report.open("w", encoding="utf-8") as output:
            output.write(f"Core: {core}\nExecutable: {executable}\nCollected from: {source}\nRetained: {retained}\n\n")
            output.flush()
            result = subprocess.run(
                [
                    "gdb",
                    "--batch",
                    "--nx",
                    "-ex",
                    "set pagination off",
                    "-ex",
                    "info files",
                    "-ex",
                    "info sharedlibrary",
                    "-ex",
                    "thread apply all bt full",
                    retained,
                    core,
                ],
                env=dict(os.environ, DEBUGINFOD_URLS=""),
                stdout=output,
                stderr=subprocess.STDOUT,
            )
        print(report.read_text(encoding="utf-8"), flush=True)
        failed |= result.returncode != 0
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
