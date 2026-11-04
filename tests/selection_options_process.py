"""Import independent SelectOption bits through real INIs and frame processes."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="wxnotepad-selection-options-") as name:
    folder = Path(name)
    executable = folder / source.name
    shutil.copy2(source, executable)
    (folder / "sample.txt").write_bytes(b"01234567890123456789\n" * 100)
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    for option in (None, 0, 1, 8, 2, 4, 15, 31, -1):
        setting = "" if option is None else f"SelectOption={option}\n"
        (folder / "Notepad4.ini").write_text("[Settings]\nWordWrap=0\n" + setting, encoding="utf-16")
        result = subprocess.run([str(executable), str(15 if option is None else option)],
                                env=environment, capture_output=True, timeout=15, check=False)
        print(result.stdout.decode("utf-8", errors="replace"), end="")
        print(result.stderr.decode("utf-8", errors="replace"), end="", file=sys.stderr)
        assert result.returncode == 0, (option, result.returncode)
print("PASS: default, zero, independent and raw selection option bits across real INI/frame processes")
