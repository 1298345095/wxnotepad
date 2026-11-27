"""Observe original FontQuality lifecycle through independent GUI processes and INI bytes."""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="wxnotepad-font-quality-") as directory:
    folder = Path(directory)
    executable = folder / source.name
    shutil.copy2(source, executable)
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    ini = folder / "Notepad4.ini"

    def run(expected, action="keep", explicit=False):
        result = subprocess.run(
            [str(executable), str(expected), str(action), "explicit" if explicit else "exit"],
            env=environment,
            capture_output=True,
            text=True,
            timeout=30,
        )
        assert result.returncode == 0, result.stdout + result.stderr

    # Values and clamping from pinned Notepad4 Notepad4.cpp LoadSettings/SaveSettings.
    cases = [
        (None, 3, "keep", True, False, None),
        ("0", 0, "keep", True, False, "0"),
        ("1", 1, "keep", True, False, "1"),
        ("2", 2, "keep", True, False, "2"),
        ("3", 3, "keep", True, False, None),
        ("-7", 0, "keep", True, False, "0"),
        ("99", 3, "keep", True, False, None),
        ("invalid", 3, "keep", True, False, None),
        ("2suffix", 2, "keep", True, False, "2"),
        ("3", 3, 1, True, False, "1"),
        ("1", 1, 3, True, False, None),
        ("2", 2, 0, False, False, "2"),
        ("2", 2, 0, False, True, "0"),
        ("1", 1, 3, False, False, "1"),
        ("1", 1, 3, False, True, None),
    ]
    for raw, imported, action, save_on_exit, explicit, windows_saved in cases:
        text = "[Settings]\n; retained\nCustom=保留\n"
        if raw is not None:
            text += f"FontQuality={raw}\n"
        if not save_on_exit:
            text += "SaveSettings=0\n"
        text += "[Other]\nFontQuality=unrelated\n"
        ini.write_text(text, encoding="utf-16")
        run(imported, action, explicit)
        data = ini.read_bytes()
        assert data.startswith(b"\xff\xfe"), "original UTF-16LE BOM"
        saved = data.decode("utf-16").replace("\r\n", "\n")
        assert "; retained" in saved and "Custom=保留" in saved, repr(saved)
        assert "[Other]\nFontQuality=unrelated\n" in saved
        expected = windows_saved if os.name == "nt" else raw
        section = saved.split("[Other]", 1)[0]
        assert [line for line in section.splitlines() if line.startswith("FontQuality=")] == (
            [] if expected is None else [f"FontQuality={expected}"]
        ), (raw, action, save_on_exit, explicit, saved)
        reopened = (3 if expected is None else int(expected)) if os.name == "nt" else imported
        run(reopened)
print("PASS: font quality default, original bounds, native choices, explicit/disabled saves and reopening")
