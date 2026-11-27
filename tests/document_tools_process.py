"""Exercise native document tools with an owned desktop handler and configuration."""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake-dir", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-document-tools-") as directory:
        # Match TestFiles::TemporaryRoot: native document Open expands TEMP's 8.3 aliases.
        root = Path(directory).resolve(strict=True)
        environment = dict(
            os.environ,
            WXNOTEPAD_DOCUMENT_TOOLS_ROOT=str(root),
            XDG_DATA_HOME=str(root / "data"),
            XDG_CONFIG_HOME=str(root / "config"),
        )
        working = None
        if args.cmake_dir is not None:
            from localization_package import bundle_application
            from localization_package import environment as package_environment

            executable = bundle_application(executable, args.cmake_dir.resolve(), root / "bundle")
            environment.update(package_environment(executable, root / "children"))
            working = root / "unrelated working directory"
            working.mkdir()
        for name in ("data", "config"):
            (root / name).mkdir()
        if sys.platform != "win32":
            (root / "home" / "Desktop").mkdir(parents=True)
            environment["HOME"] = str(root / "home")
        (root / "Notepad4.ini").write_text(
            "[Settings]\nSaveBeforeRunningTools=0\nWarnLineEndings=0\n[Settings2]\nUILanguage=1033\nPortableMyDocs=1\n",
            encoding="utf-16",
        )
        try:
            subprocess.run([str(executable), "--fixture"], env=environment, cwd=working, check=True, timeout=55)
        finally:
            for log in sorted(root.glob("*.log")):
                print(log.name + ":\n" + log.read_text(encoding="utf-8"), file=sys.stderr)


if __name__ == "__main__":
    main()
