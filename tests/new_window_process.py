"""Exercise New Window through real processes on an isolated desktop."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from contextlib import ExitStack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--installed-directory", type=Path)
    parser.add_argument("--scenario", choices=("all", "normal", "maximized"), default="all")
    args = parser.parse_args()
    source = args.executable.resolve()
    with ExitStack() as stack:
        name = stack.enter_context(tempfile.TemporaryDirectory(prefix="wxnotepad 新窗口 "))
        folder = Path(name)
        executable_folder = folder
        config_folder = folder
        environment = dict(os.environ, WXNOTEPAD_NEW_WINDOW_TEST=str(folder),
                           WXNOTEPAD_NEW_WINDOW_TEST_MODE=args.scenario,
                           PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        if args.installed_directory:
            executable_folder = Path(stack.enter_context(tempfile.TemporaryDirectory(
                prefix="新窗口 ", dir=args.installed_directory)))
            config_folder = folder / "user config 中文" / "Notepad4"
            config_folder.mkdir(parents=True)
            environment["XDG_CONFIG_HOME"] = str(config_folder.parent)
        executable = executable_folder / source.name
        shutil.copy2(source, executable)
        (config_folder / "Notepad4.ini").write_text(
            "[Settings]\nSaveSettings=0\nWordWrap=0\nWarnLineEndings=0\n", encoding="utf-16")
        (folder / "parent.txt").write_text("parent document", encoding="utf-16")
        (folder / "startup working 中文").mkdir()
        (folder / "changed working").mkdir()
        result = subprocess.run([str(executable), "--observe", str(folder / "parent-observation"),
                                 "--startup-file", str(folder / "parent.txt")],
                                env=environment, cwd=folder / "startup working 中文", timeout=30, check=False)
        failure = folder / "failure.txt"
        if result.returncode == 77:
            print("SKIP: " + (folder / "skip.txt").read_text(encoding="utf-8"), flush=True)
            raise SystemExit(77)
        assert result.returncode == 0, failure.read_text(encoding="utf-8") if failure.exists() else result.returncode
        scenarios = [] if args.scenario == "maximized" else ["normal", "accelerator"] + (
            ["edge"] if sys.platform == "win32" else [])
        if args.scenario != "normal":
            scenarios.append("maximized")
        for scenario in scenarios:
            assert (folder / f"{scenario}-child.txt").read_bytes() == b"independent child"
        assert (folder / "parent.txt").read_text(encoding="utf-16") == "parent document"
        assert not (folder / "parent-observation").exists()
        print("PASS: independent blank children, startup settings/cwd, geometry, dirty parent and failed launch", flush=True)


if __name__ == "__main__":
    main()
