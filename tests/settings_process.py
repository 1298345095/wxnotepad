"""Verify original settings through independent application processes."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from contextlib import ExitStack

from resource_process import command, wait_state


class Application:
    def __init__(self, executable, folder, environment, startup=None):
        self.folder = folder
        self.sequence = 0
        (folder / "state").unlink(missing_ok=True)
        arguments = [str(executable), "--observe", str(folder)]
        if startup is not None:
            arguments += ["--startup-file", str(startup)]
        self.process = subprocess.Popen(arguments, env=environment)
        self.state = wait_state(folder, self.process, lambda s: s["sequence"] == "0")

    def menu(self, item):
        self.sequence += 1
        command(self.folder, f"{self.sequence}\nmenu\nView\t{item}\n")
        self.state = wait_state(self.folder, self.process, lambda s: s["sequence"] == str(self.sequence))
        assert self.state["result"] == "ok", item

    def close(self):
        command(self.folder, f"{self.sequence + 1}\nclose\n")
        assert self.process.wait(timeout=10) == 0

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=10)


def view(state, wrap, numbers, whitespace):
    assert (state["wrap_mode"] != "0", state["line_numbers"] == "1", state["whitespace"] != "0") == (
        wrap, numbers, whitespace), state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--installed-directory", type=Path,
                        help="Owned native Linux prefix used only for installation-discovery testing")
    args = parser.parse_args()
    source = args.executable.resolve()
    with ExitStack() as stack:
        name = stack.enter_context(tempfile.TemporaryDirectory(prefix="wxnotepad-settings-"))
        folder = Path(name)
        executable_folder = folder
        config_folder = folder
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        if args.installed_directory:
            executable_folder = Path(stack.enter_context(tempfile.TemporaryDirectory(dir=args.installed_directory)))
            config_folder = folder / "native-config" / "Notepad4"
            environment["XDG_CONFIG_HOME"] = str(folder / "native-config")
        executable = executable_folder / source.name
        shutil.copy2(source, executable)
        seed = executable_folder / "Notepad4.ini-default"
        seed.write_text("[Settings]\nWordWrap=invalid\nShowLineNumbers=1\nViewWhiteSpace=0\n"
                        "UnrelatedSetting=keep\n[Styles]\nUnrelatedStyle=保留\n", encoding="utf-16")
        startup = folder / "startup.txt"
        startup.write_bytes(b"settings survive startup file loading\n")
        app = Application(executable, folder, environment)
        try:
            view(app.state, True, True, False)
            for item in ("Word Wrap", "Line Numbers", "Show Whitespace"):
                app.menu(item)
            view(app.state, False, False, True)
            app.close()
            saved = (config_folder / "Notepad4.ini").read_bytes().decode("utf-16")
            assert "WordWrap=0" in saved and "ShowLineNumbers=0" in saved and "ViewWhiteSpace=1" in saved, repr(saved)
        finally:
            app.stop()
        app = Application(executable, folder, environment, startup)
        try:
            view(app.state, False, False, True)
            app.menu("Save Settings On Exit")
            for item in ("Word Wrap", "Line Numbers", "Show Whitespace"):
                app.menu(item)
            view(app.state, True, True, False)
            app.close()
        finally:
            app.stop()
        saved = (config_folder / "Notepad4.ini").read_text(encoding="utf-16")
        assert "SaveSettings=0" in saved and "WordWrap=0" in saved, saved
        assert "UnrelatedSetting=keep" in saved and "UnrelatedStyle=保留" in saved, saved
        app = Application(executable, folder, environment)
        try:
            view(app.state, False, False, True)
            for item in ("Word Wrap", "Line Numbers", "Show Whitespace"):
                app.menu(item)
            app.menu("Save Settings Now")
            for item in ("Word Wrap", "Line Numbers", "Show Whitespace"):
                app.menu(item)
            app.close()
        finally:
            app.stop()
        app = Application(executable, folder, environment)
        try:
            view(app.state, True, True, False)
            app.close()
        finally:
            app.stop()
        saved = (config_folder / "Notepad4.ini").read_text(encoding="utf-16")
        assert "SaveSettings=0" in saved, saved
        assert "WordWrap=" not in saved and "ShowLineNumbers=" not in saved and "ViewWhiteSpace=" not in saved, saved
        assert "UnrelatedSetting=keep" in saved and "UnrelatedStyle=保留" in saved, saved
    print("Original defaults, seed, restoration, disabled exit save, explicit save and unrelated settings passed")


if __name__ == "__main__":
    main()
