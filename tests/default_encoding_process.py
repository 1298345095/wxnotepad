"""Native default choices persist through actual settings commands and exit."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(source, installed_directory):
    with tempfile.TemporaryDirectory(prefix="wxnotepad-defaults-") as name:
        folder = Path(name)
        executable_folder = folder
        installed = None
        if installed_directory:
            installed = tempfile.TemporaryDirectory(dir=installed_directory)
            executable_folder = Path(installed.name)
        try:
            executable = executable_folder / source.name
            shutil.copy2(source, executable)
            environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
            config_folder = folder
            if installed:
                environment["XDG_CONFIG_HOME"] = str(folder / "config")
                config_folder = folder / "config" / "Notepad4"
                config_folder.mkdir(parents=True)
            config = config_folder / "Notepad4.ini"
            choices = [("UTF-8", None), ("UTF-8 with BOM", 4), ("UTF-16LE with BOM", 1),
                       ("UTF-16LE", 6), ("UTF-16BE with BOM", 2), ("UTF-16BE", 7),
                       ("Simplified Chinese (GBK, GB2312)", 936),
                       ("Simplified Chinese (GB18030)", 54936),
                       ("Traditional Chinese (Big5, Big5-HKSCS)", 950)]
            for mode in ("disabled", "exit", "explicit"):
                for label, number in choices:
                    enabled = mode == "exit"
                    seed = (f"[Settings]\nSaveSettings={int(enabled)}\nSkipUnicodeDetection=0\n"
                            "Unrelated=keep\n[Settings2]\nNoFileVariables=1\nRelativeFileMRU=0\n"
                            "[Unrelated]\nKeep=保留\n")
                    config.write_text(seed, encoding="utf-16")
                    environment.update(WXNOTEPAD_DEFAULTS_PERSIST=mode, WXNOTEPAD_DEFAULTS_CHOICE=label)
                    result = subprocess.run([str(executable)], env=environment, timeout=20)
                    assert result.returncode == 0, (mode, label, result.returncode)
                    saved = config.read_text(encoding="utf-16")
                    for retained in ("NoFileVariables=1", "RelativeFileMRU=0", "Unrelated=keep", "Keep=保留"):
                        assert retained in saved, (mode, label, saved)
                    if mode == "disabled":
                        assert "SkipUnicodeDetection=0" in saved and "DefaultEncoding=" not in saved, saved
                        assert all(key + "=" not in saved for key in ("LoadASCIIasUTF8", "LoadANSIasUTF8", "LoadNFOasOEM", "NoEncodingTags")), saved
                    else:
                        assert "SkipUnicodeDetection=" not in saved, saved
                        for expected in ("LoadASCIIasUTF8=0", "LoadANSIasUTF8=0", "LoadNFOasOEM=0", "NoEncodingTags=1"):
                            assert expected in saved, saved
                        assert ("DefaultEncoding=" not in saved if number is None else f"DefaultEncoding={number}" in saved), saved
                    print(f"PASS: {mode} {label}: independent INI mapping and retained fields")
        finally:
            if installed:
                installed.cleanup()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--installed-directory", type=Path)
    args = parser.parse_args()
    run(args.executable.resolve(), args.installed_directory)
