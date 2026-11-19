"""Exercise native document tools with an owned desktop handler and configuration."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-document-tools-") as directory:
        root = Path(directory)
        environment = dict(os.environ, WXNOTEPAD_DOCUMENT_TOOLS_ROOT=str(root),
                           XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"))
        for name in ("data", "config"):
            (root / name).mkdir()
        (root / "Notepad4.ini").write_text("[Settings]\nSaveBeforeRunningTools=0\n[Settings2]\nUILanguage=1033\n",
                                          encoding="utf-16")
        try:
            subprocess.run([str(executable)], env=environment, check=True, timeout=55)
        finally:
            if (root / "fixture.log").exists():
                print((root / "fixture.log").read_text(encoding="utf-8"), file=sys.stderr)


if __name__ == "__main__":
    main()
