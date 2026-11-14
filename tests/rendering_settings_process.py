"""Observe renderer preferences in independent GUI processes and read saved INI bytes."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="wxnotepad-rendering-") as directory:
    folder = Path(directory)
    executable = folder / source.name
    shutil.copy2(source, executable)
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    ini = folder / "Notepad4.ini"
    base = "[Settings]\n; retained\nCustom=保留\n"

    def run(expected, action="keep", explicit=False):
        result = subprocess.run([str(executable), str(expected), action, "explicit" if explicit else "exit"],
                                env=environment, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stdout + result.stderr

    # raw preference, original imported value, native action, exit saving, explicit save, Windows persisted value
    cases = [(None, 1, "keep", True, False, None),
             ("0", 0, "keep", True, False, "0"),
             ("1", 1, "keep", True, False, None),
             ("2", 2, "keep", True, False, "0"),
             ("3", 3, "keep", True, False, "0"),
             ("4", 4, "keep", True, False, "0"),
             ("-7", 0, "keep", True, False, "0"),
             ("99", 4, "keep", True, False, "0"),
             ("invalid", 1, "keep", True, False, None),
             ("0suffix", 0, "keep", True, False, "0"),
             ("1", 1, "Legacy GDI", True, False, "0"),
             ("0", 0, "Direct2D", True, False, None),
             ("1", 1, "Legacy GDI", False, False, "1"),
             ("1", 1, "Legacy GDI", False, True, "0"),
             ("0", 0, "Direct2D", False, False, "0"),
             ("0", 0, "Direct2D", False, True, None)]
    for raw, imported, action, save_on_exit, explicit, windows_saved in cases:
        text = base + (f"RenderingTechnology={raw}\n" if raw is not None else "")
        if not save_on_exit:
            text += "SaveSettings=0\n"
        text += "[Other]\nRenderingTechnology=unrelated\n"
        ini.write_text(text, encoding="utf-16")
        run(imported, action, explicit)
        data = ini.read_bytes()
        assert data.startswith(b"\xff\xfe"), "original UTF-16LE BOM"
        saved = data.decode("utf-16").replace("\r\n", "\n")
        assert "; retained" in saved and "Custom=保留" in saved, repr(saved)
        assert "[Other]\nRenderingTechnology=unrelated\n" in saved
        expected = windows_saved if os.name == "nt" else raw
        setting = None if expected is None else f"RenderingTechnology={expected}"
        section = saved.split("[Other]", 1)[0]
        assert ([line for line in section.splitlines() if line.startswith("RenderingTechnology=")] ==
                ([] if setting is None else [setting])), (raw, action, save_on_exit, explicit, saved)
        # A second application independently imports the persisted preference.
        reopened = (1 if expected is None else int(expected)) if os.name == "nt" else imported
        run(reopened)
print("PASS: renderer defaults, original bounds/subset, native choices, explicit/disabled exit saves and reopening")
