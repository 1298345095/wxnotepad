"""Import, save and reopen original integer scroll modes in independent GUI processes."""
import argparse
from contextlib import ExitStack
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--installed-directory", type=Path,
                        default=os.environ.get("WXNOTEPAD_INSTALLED_TEST_DIR"))
    args = parser.parse_args()
    source = args.executable.resolve()
    with ExitStack() as stack:
        folder = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix="wxnotepad-scroll-")))
        binary_folder = folder
        config_folder = folder
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        if args.installed_directory:
            binary_folder = Path(stack.enter_context(tempfile.TemporaryDirectory(dir=args.installed_directory)))
            config_folder = folder / "native-config" / "Notepad4"
            environment["XDG_CONFIG_HOME"] = str(folder / "native-config")
            config_folder.mkdir(parents=True)
        executable = binary_folder / source.name
        shutil.copy2(source, executable)
        ini = config_folder / "Notepad4.ini"

        def run(expected, action="keep", explicit=False):
            result = subprocess.run([str(executable), str(expected), str(action), "explicit" if explicit else "exit"],
                                    env=environment, capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr

        # raw INI value, original normalized value, native menu mode, save-on-exit, explicit save
        cases = [(None, 1, "keep", True, False), ("0", 0, "keep", True, False),
                 ("1", 1, "keep", True, False), ("2", 2, "keep", True, False),
                 ("3", 3, "keep", True, False), ("4", 4, "keep", True, False),
                 ("-7", 0, "keep", True, False), ("99", 4, "keep", True, False),
                 ("invalid", 1, "keep", True, False), ("3suffix", 3, "keep", True, False)]
        cases += [("1", 1, mode, True, False) for mode in range(5)]
        cases += [("2", 2, 4, False, False), ("2", 2, 4, False, True),
                  ("3", 3, 1, False, False), ("3", 3, 1, False, True)]
        for raw, imported, action, save_on_exit, explicit in cases:
            text = "[Settings]\n; retained\nCustom=保留\n"
            if raw is not None:
                text += f"EndAtLastLine={raw}\n"
            if not save_on_exit:
                text += "SaveSettings=0\n"
            text += "[Other]\nEndAtLastLine=unrelated\n"
            ini.write_text(text, encoding="utf-16")
            run(imported, action, explicit)
            saved = ini.read_bytes()
            assert saved.startswith(b"\xff\xfe"), "original UTF-16LE BOM"
            saved = saved.decode("utf-16").replace("\r\n", "\n")
            assert "; retained" in saved and "Custom=保留" in saved, saved
            assert "[Other]\nEndAtLastLine=unrelated\n" in saved, saved
            result = imported if action == "keep" else action
            expected = (None if result == 1 else str(result)) if save_on_exit or explicit else raw
            section = saved.split("[Other]", 1)[0]
            setting = [] if expected is None else [f"EndAtLastLine={expected}"]
            assert [line for line in section.splitlines() if line.startswith("EndAtLastLine=")] == setting, saved
            run(result if save_on_exit or explicit else imported)
    print("PASS: original five integer modes, bounds, INI, save-on-exit, explicit save and restart")


if __name__ == "__main__":
    main()
