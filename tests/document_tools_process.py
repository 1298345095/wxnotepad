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
        if sys.platform != "win32":
            (root / "home" / "Desktop").mkdir(parents=True)
            environment["HOME"] = str(root / "home")
        (root / "Notepad4.ini").write_text("[Settings]\nSaveBeforeRunningTools=0\nWarnLineEndings=0\n[Settings2]\nUILanguage=1033\nPortableMyDocs=1\n",
                                          encoding="utf-16")
        try:
            subprocess.run([str(executable), "--fixture"], env=environment, check=True, timeout=55)
        finally:
            for log in sorted(root.glob("*.log")):
                print(log.name + ":\n" + log.read_text(encoding="utf-8"), file=sys.stderr)


if __name__ == "__main__":
    main()
