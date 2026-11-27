"""Independent native child observation; no application conversion or parsing code."""

import os
from pathlib import Path
import sys

output = Path(os.environ["WXNOTEPAD_RUN_RECORD"])
pending = output.with_suffix(".pending")
pending.write_bytes(("\n".join([os.getcwd(), *sys.argv[1:]]) + "\n").encode("utf-8"))
pending.replace(output)
