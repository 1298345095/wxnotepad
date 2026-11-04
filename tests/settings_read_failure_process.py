"""Keep independent disk bytes while real settings writers cannot read an owned INI."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="wxnotepad-unreadable-settings-") as name:
    folder = Path(name)
    executable = folder / source.name
    shutil.copy2(source, executable)
    config = folder / "Notepad4.ini"
    original = "[Settings]\nSaveSettings=0\n; user comment\nUnrelated=keep\n[Custom]\nValue=preserved\n".encode("utf-16")
    config.write_bytes(original)
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    result = subprocess.run([str(executable)], env=environment, timeout=20, check=False)
    assert config.read_bytes() == original, "unreadable configuration was destructively replaced"
    assert result.returncode == 0, result.returncode
print("PASS: independent disk bytes survive all refused writes")
