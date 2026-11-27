"""Compare native dialog results with independently captured pinned-original rows."""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

source = Path(sys.argv[1]).resolve()
reference = Path(sys.argv[2]).resolve()
if "WXNOTEPAD_ORIGINAL_WRAP_REFERENCE" in os.environ:
    variant = "dpi96" if os.environ.get("WXNOTEPAD_WRAP_DPI96") else "current"
    reference = Path(os.environ["WXNOTEPAD_ORIGINAL_WRAP_REFERENCE"]).resolve() / variant
with tempfile.TemporaryDirectory(prefix="wxnotepad-wrap-reference-") as name:
    folder = Path(name)
    executable = folder / source.name
    shutil.copy2(source, executable)
    (folder / "Notepad4.ini").write_text(
        "[Settings]\nSaveSettings=0\nWordWrap=1\nWordWrapMode=4\n[Settings2]\nUILanguage=1033\n", encoding="utf-16"
    )
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    result = subprocess.run(
        [str(executable), "--reference", str(reference)],
        cwd=folder,
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
        timeout=90,
    )
    print(result.stdout.decode("utf-8", errors="replace"), end="", flush=True)
    result.check_returncode()
