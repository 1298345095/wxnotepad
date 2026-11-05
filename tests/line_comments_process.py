"""Verify native comments and AutoInsertMask lifecycle in owned portable processes."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="wxnotepad-line-comments-") as name:
    folder = Path(name)
    executable = folder / source.name
    shutil.copy2(source, executable)
    config = folder / "Notepad4.ini"
    for value, spaced in ((None, True), ("255suffix", False), ("1280", True)):
        entry = "" if value is None else f"AutoInsertMask={value}\n"
        config.write_text(f"[Settings]\n{entry}Unrelated=keep\n", encoding="utf-16")
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"],
                           WXNOTEPAD_COMMENT_SPACED=str(int(spaced)))
        for _ in range(2):
            result = subprocess.run([str(executable)], env=environment, timeout=25, check=False)
            assert result.returncode == 0, result.returncode
            saved = config.read_text(encoding="utf-16")
            expected = None if value is None else int(value.removesuffix("suffix"))
            assert ("AutoInsertMask=" not in saved if expected is None else f"AutoInsertMask={expected}\n" in saved), saved
            assert "Unrelated=keep" in saved, saved
print("PASS: default 511, space bit 256, original integer parsing, all other mask bits and reload/save preservation")
