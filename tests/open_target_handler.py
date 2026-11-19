"""Owned desktop boundary: record exact native URI without opening an application."""
import os
from pathlib import Path
import sys

root = Path(os.environ["WXNOTEPAD_OPEN_TARGET_ROOT"])
temporary = root / "desktop-received.tmp"
temporary.write_text(sys.argv[1] + "\n", encoding="utf-8")
temporary.replace(root / "desktop-received")
