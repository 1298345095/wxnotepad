"""Check the Column Wrap boundary imported from original INI settings."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-wrap-boundary-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        for value, expected in (("91", 91), ("-1", 0), ("9999", 4096), ("invalid", 80)):
            config.write_text(f"[Settings]\nLongLinesLimit={value}\nUnrelated=keep\n", encoding="utf-16")
            subprocess.run([executable, "--boundary-probe", str(expected)], cwd=folder,
                           env=environment, check=True, timeout=30)
            saved = config.read_text(encoding="utf-16")
            assert "Unrelated=keep" in saved, saved
            if expected == 80:
                assert "LongLinesLimit=" not in saved, saved
            else:
                assert f"LongLinesLimit={expected}" in saved.splitlines(), saved
    print("PASS: independent processes import, bound and save LongLinesLimit with native dialog defaults")


if __name__ == "__main__":
    main()
