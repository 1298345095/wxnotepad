"""Original loss suppression persists immediately and reaches the next process."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-western-messages-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        config.write_text("[Settings]\nSaveSettings=0\n[Unrelated]\nKeep=1\n", encoding="utf-16")
        for mode in ("write", "read"):
            environment["WXNOTEPAD_WESTERN_SUPPRESSION"] = mode
            result = subprocess.run([str(executable)], env=environment, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, encoding="utf-8", timeout=40)
            print(result.stdout, end="")
            result.check_returncode()
            saved = config.read_text(encoding="utf-16")
            assert all(f"MsgConv{index}=1" in saved for index in (1, 2, 3)), saved
            assert "Keep=1" in saved and "SaveSettings=0" in saved, saved
        config.write_text("[Suppressed Messages]\nMsgConv3=2\n", encoding="utf-16")
        environment["WXNOTEPAD_WESTERN_SUPPRESSION"] = "never"
        result = subprocess.run([str(executable)], env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, encoding="utf-8", timeout=40)
        print(result.stdout, end="")
        result.check_returncode()
        assert "MsgConv3=2" in config.read_text(encoding="utf-16")
    print("PASS: original MsgConv3 writes with SaveSettings off and is restored by next process")


if __name__ == "__main__":
    main()
