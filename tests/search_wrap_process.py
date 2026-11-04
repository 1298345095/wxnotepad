"""Verify original search-message suppression through independent native processes."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-search-messages-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        config.write_text("[Settings]\nSaveSettings=0\nUnrelated=keep\n", encoding="utf-16")
        environment["WXNOTEPAD_SEARCH_SETTINGS"] = "write"
        subprocess.run([str(executable)], env=environment, check=True, timeout=25)
        saved = config.read_text(encoding="utf-16")
        assert "[Suppressed Messages]" in saved and "MsgFindWrap1=1" in saved, saved
        assert "SaveSettings=0" in saved and "Unrelated=keep" in saved, saved
        config.write_text(saved + "\nMsgFindWrap2=2\nMsgNotFound=1\n", encoding="utf-16")
        environment["WXNOTEPAD_SEARCH_SETTINGS"] = "read"
        subprocess.run([str(executable)], env=environment, check=True, timeout=25)
        saved = config.read_text(encoding="utf-16")
        assert "MsgFindWrap2=2" in saved and "Unrelated=keep" in saved, saved
    print("PASS: immediate original suppression writes with SaveSettings off and next-process restoration")


if __name__ == "__main__":
    main()
