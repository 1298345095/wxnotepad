"""Verify original search-message suppression through independent native processes."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(executable, environment):
    result = subprocess.run([str(executable)], env=environment, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, encoding="utf-8", timeout=25)
    print(result.stdout, end="")
    result.check_returncode()


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
        run(executable, environment)
        saved = config.read_text(encoding="utf-16")
        assert "[Suppressed Messages]" in saved and "MsgFindWrap1=1" in saved, saved
        assert "SaveSettings=0" in saved and "Unrelated=keep" in saved, saved
        config.write_text(saved + "\nMsgFindWrap2=2\nMsgNotFound=1\n", encoding="utf-16")
        environment["WXNOTEPAD_SEARCH_SETTINGS"] = "read"
        run(executable, environment)
        saved = config.read_text(encoding="utf-16")
        assert "MsgFindWrap2=2" in saved and "Unrelated=keep" in saved, saved
        config.write_text("[Settings]\nFindReplaceOption=66\n", encoding="utf-16")
        environment["WXNOTEPAD_SEARCH_SETTINGS"] = "nowrap"
        run(executable, environment)
    print("PASS: immediate original suppression writes with SaveSettings off and next-process restoration")


if __name__ == "__main__":
    main()
