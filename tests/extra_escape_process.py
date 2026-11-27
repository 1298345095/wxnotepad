"""Round-trip original EscFunction values through native menus, saving and restart."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="wxnotepad-escape-") as directory:
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
            timeout=20,
        )
        assert result.returncode == 0, result.stdout + result.stderr

    cases = [
        (None, 0, "keep", True, False),
        ("0", 0, "keep", True, False),
        ("1", 1, "keep", True, False),
        ("2", 2, "keep", True, False),
        ("-7", 0, "keep", True, False),
        ("99", 2, "keep", True, False),
        ("invalid", 0, "keep", True, False),
        ("1suffix", 1, "keep", True, False),
    ]
    cases += [
        ("1", 1, chosen, policy, explicit)
        for chosen in range(3)
        for policy, explicit in [(True, False), (False, False), (False, True)]
    ]
    for raw, imported, action, save_on_exit, explicit in cases:
        text = "[Settings]\n; retained\nCustom=保留\n"
        if raw is not None:
            text += f"EscFunction={raw}\n"
        if not save_on_exit:
            text += "SaveSettings=0\n"
        text += "[Other]\nEscFunction=unrelated\n"
        ini.write_text(text, encoding="utf-16")
        run(imported, action, explicit)
        saved = ini.read_text(encoding="utf-16").replace("\r\n", "\n")
        assert "; retained" in saved and "Custom=保留" in saved, saved
        assert "[Other]\nEscFunction=unrelated\n" in saved, saved
        chosen = imported if action == "keep" else action
        expected = (None if chosen == 0 else str(chosen)) if save_on_exit or explicit else raw
        section = saved.split("[Other]", 1)[0]
        assert [line for line in section.splitlines() if line.startswith("EscFunction=")] == (
            [] if expected is None else [f"EscFunction={expected}"]
        ), saved
        run(chosen if save_on_exit or explicit else imported)
print("PASS: original EscFunction default/values/bounds, Save Settings On Exit/Now, unrelated fields and restart")
