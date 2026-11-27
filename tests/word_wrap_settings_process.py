"""Exercise native wrap settings and original portable INI normalization across restarts."""

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="wxnotepad-wrap-settings-") as name:
    folder = Path(name)
    executable = folder / source.name
    shutil.copy2(source, executable)
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    config = folder / "Notepad4.ini"

    def seed(extra=""):
        config.write_text(
            "; preserve comment\n[Settings]\nSaveSettings=0\nWarnLineEndings=0\n"
            + extra
            + "Unrelated=keep\n[Settings2]\nUILanguage=1033\n[Styles]\nUnrelatedStyle=保留\n",
            encoding="utf-16",
            newline="\r\n",
        )

    def run(*arguments):
        result = subprocess.run(
            [str(executable), *map(str, arguments)],
            cwd=folder,
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=60,
            check=False,
        )
        print(result.stdout.decode("utf-8", errors="replace"), end="", flush=True)
        result.check_returncode()

    seed()
    run()
    saved = config.read_text(encoding="utf-16")
    for line in (
        "WordWrapMode=3",
        "WordWrapIndent=4",
        "WordWrapSymbols=31",
        "ShowWordWrapSymbols=1",
        "WordWrapSelectSubLine=1",
        "HighlightCurrentLine=2",
        "SaveSettings=0",
        "Unrelated=keep",
        "UnrelatedStyle=保留",
        "; preserve comment",
    ):
        assert line in saved.splitlines(), (line, saved)
    run(3, 4, "complete")
    # Original LoadSettings clamps mode to Word..Auto and indent to None..DeepIndent;
    # ReadInt retains the existing prefix parsing, including signed values.
    for mode, indent, expected_mode, expected_indent in (
        ("0", "-1", 1, 0),
        ("-3", "99", 1, 7),
        ("99", "3suffix", 4, 3),
        ("2suffix", "invalid", 2, 0),
        ("invalid", "7", 4, 7),
    ):
        seed(f"WordWrapMode={mode}\nWordWrapIndent={indent}\n")
        run(expected_mode, expected_indent)
        run(expected_mode, expected_indent)
    seed("WordWrap=0\nWordWrapMode=2\nWordWrapIndent=4\n")
    run(0, 4)
    for save in (0, 1):
        seed()
        config.write_text(
            config.read_text(encoding="utf-16").replace("SaveSettings=0", f"SaveSettings={save}"),
            encoding="utf-16",
            newline="\r\n",
        )
        before = config.read_bytes()
        run("--exit")
        if save:
            saved = config.read_text(encoding="utf-16")
            for line in (
                "WordWrapMode=2",
                "WordWrapIndent=3",
                "WordWrapSymbols=31",
                "ShowWordWrapSymbols=1",
                "WordWrapSelectSubLine=1",
                "HighlightCurrentLine=2",
                "Unrelated=keep",
            ):
                assert line in saved.splitlines(), (line, saved)
            run(2, 3, "complete")
        else:
            assert config.read_bytes() == before, "disabled exit save preserves the INI byte for byte"
print("PASS: all wrap options, native dialog, original normalization, portable save and independent restart")
