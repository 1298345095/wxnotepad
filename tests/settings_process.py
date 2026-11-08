"""Verify original settings through independent application processes."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from contextlib import ExitStack

from resource_process import command, observation_context, wait_state


class Application:
    def __init__(self, executable, folder, environment, startup=None):
        self.folder = folder
        self.sequence = 0
        (folder / "state").unlink(missing_ok=True)
        arguments = [str(executable), "--observe", str(folder)]
        if startup is not None:
            arguments += ["--startup-file", str(startup)]
        self.process = subprocess.Popen(arguments, env=environment, stderr=sys.stderr)
        self.state = wait_state(folder, self.process, lambda s: s["sequence"] == "0")

    def menu(self, item, menu=None):
        if menu is None:
            menu = "Settings" if item in {"Auto Indent", "Auto-Shrink Horizontal Scrollbar", "Insert Tabs as Spaces",
                                          "Tab Settings...", "Long Line Settings...", "Save Settings On Exit",
                                          "Save Settings Now", "Remember Search Options"} else "View"
        self.named_menu(menu, item)

    def named_menu(self, menu, item):
        self.sequence += 1
        command(self.folder, f"{self.sequence}\nmenu\n{menu}\t{item}\n")
        self.state = wait_state(self.folder, self.process, lambda s: s["sequence"] == str(self.sequence))
        assert self.state["result"] == "ok", item

    def close(self):
        requested = f"{self.sequence + 1}\nclose\n"
        command(self.folder, requested)
        self.wait_for_close(requested)

    def wait_for_close(self, requested):
        try:
            result = self.process.wait(timeout=10)
        except subprocess.TimeoutExpired as error:
            error.add_note(f"Close transition: pid={self.process.pid}, requested={requested!r}, "
                           f"last_ack={self.state!r}, observed={observation_context(self.folder, self.process)!r}")
            raise
        assert result == 0, f"Close exited with {result}: requested={requested!r}, last_ack={self.state!r}"

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=10)


def view(state, wrap, numbers, whitespace):
    assert (state["wrap_mode"] != "0", state["line_numbers"] == "1", state["whitespace"] != "0") == (
        wrap, numbers, whitespace), state


def tabs(state, tab, indent, use_tabs, tab_indents=1, backspace=2):
    assert tuple(int(state[key]) for key in ("tab_width", "indent_width", "use_tabs", "tab_indents",
                                           "backspace_unindents")) == (tab, indent, use_tabs, tab_indents, backspace), state


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
        seed.write_text("[Settings]\nWordWrap=invalid\nAutoIndent=invalid\nShowLineNumbers=1\nViewWhiteSpace=0\n"
                        "DefaultEOLMode=1\nWarnLineEndings=0\n"
                        "UnrelatedSetting=keep\n[Styles]\nUnrelatedStyle=保留\n"
                        "[wxnotepad]\nAutoShrinkScrollWidth=invalid\nUnrelatedCustom=keep\n", encoding="utf-16")
        startup = folder / "startup.txt"
        startup.write_bytes(b"settings survive\r\nstartup file loading\nlast")
        app = Application(executable, folder, environment)
        try:
            view(app.state, True, True, False)
            assert app.state["auto_shrink_scroll_width"] == "0", app.state
            assert app.state["eol_mode"] == "2", app.state
            tabs(app.state, 4, 4, 1)
            assert app.state["auto_indent"] == "1", app.state
            app.named_menu("Scheme", "JSON")
            tabs(app.state, 2, 2, 0)
            for item in ("Word Wrap", "Line Numbers", "Show Whitespace", "Auto Indent", "Auto-Shrink Horizontal Scrollbar"):
                app.menu(item)
            view(app.state, False, False, True)
            assert app.state["auto_shrink_scroll_width"] == "1", app.state
            assert app.state["auto_indent"] == "0", app.state
            app.close()
            saved = (config_folder / "Notepad4.ini").read_bytes().decode("utf-16")
            assert "WordWrap=0" in saved and "ShowLineNumbers=0" in saved and "ViewWhiteSpace=1" in saved, repr(saved)
            assert "DefaultEOLMode=1" in saved and "WarnLineEndings=0" in saved, repr(saved)
            assert "AutoIndent=0" in saved, saved
            assert "AutoShrinkScrollWidth=1" in saved and "UnrelatedCustom=keep" in saved, saved
        finally:
            app.stop()
        app = Application(executable, folder, environment, startup)
        try:
            view(app.state, False, False, True)
            assert app.state["auto_shrink_scroll_width"] == "1", app.state
            assert app.state["eol_mode"] == "2", app.state
            assert app.state["auto_indent"] == "0", app.state
            app.menu("Save Settings On Exit")
            for item in ("Word Wrap", "Line Numbers", "Show Whitespace", "Auto Indent", "Auto-Shrink Horizontal Scrollbar"):
                app.menu(item)
            view(app.state, True, True, False)
            app.close()
        finally:
            app.stop()
        saved = (config_folder / "Notepad4.ini").read_text(encoding="utf-16")
        assert "SaveSettings=0" in saved and "WordWrap=0" in saved, saved
        assert "AutoShrinkScrollWidth=1" in saved, saved
        assert "AutoIndent=0" in saved, saved
        assert "UnrelatedSetting=keep" in saved and "UnrelatedStyle=保留" in saved, saved
        assert "DefaultEOLMode=1" in saved and "WarnLineEndings=0" in saved, saved
        app = Application(executable, folder, environment)
        try:
            view(app.state, False, False, True)
            for item in ("Word Wrap", "Line Numbers", "Show Whitespace", "Auto Indent", "Auto-Shrink Horizontal Scrollbar"):
                app.menu(item)
            app.menu("Save Settings Now")
            for item in ("Word Wrap", "Line Numbers", "Show Whitespace", "Auto Indent", "Auto-Shrink Horizontal Scrollbar"):
                app.menu(item)
            app.close()
        finally:
            app.stop()
        app = Application(executable, folder, environment)
        try:
            view(app.state, True, True, False)
            assert app.state["auto_indent"] == "1", app.state
            assert app.state["auto_shrink_scroll_width"] == "0", app.state
            app.close()
        finally:
            app.stop()
        saved = (config_folder / "Notepad4.ini").read_text(encoding="utf-16")
        assert "SaveSettings=0" in saved, saved
        assert "WordWrap=" not in saved and "ShowLineNumbers=" not in saved and "ViewWhiteSpace=" not in saved, saved
        assert "AutoIndent=" not in saved, saved
        assert "AutoShrinkScrollWidth=" not in saved and "UnrelatedCustom=keep" in saved, saved
        assert "UnrelatedSetting=keep" in saved and "UnrelatedStyle=保留" in saved, saved
        assert "DefaultEOLMode=1" in saved and "WarnLineEndings=0" in saved, saved
        for value, mode in (("0", "0"), ("1", "2"), ("2", "1"), ("-1", "0"), ("99", "1"), ("invalid", "0"),
                            ("4294967296", "1"), ("-4294967297", "0"), ("1suffix", "2")):
            (config_folder / "Notepad4.ini").write_text(
                "[Settings]\nSaveSettings=0\nWarnLineEndings=0\nDefaultEOLMode=" + value + "\n", encoding="utf-16")
            app = Application(executable, folder, environment)
            try:
                assert app.state["eol_mode"] == mode, (value, app.state)
                app.close()
            finally:
                app.stop()
        config = config_folder / "Notepad4.ini"
        config.write_text("[Settings]\nAutoIndent=0\nTabWidth=7\nIndentWidth=3\nTabsAsSpaces=1\nTabIndents=0\n"
                          "BackspaceUnindents=3\n[Python Script]\nTabWidth=9\nIndentWidth=0\n"
                          "TabsAsSpaces=0\nUseGlobalTabSettings=0\n[JSON Document]\nUseGlobalTabSettings=1\n"
                          "[C/C++ Source]\nTabWidth=999\nIndentWidth=-5\nUseGlobalTabSettings=0\n", encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            tabs(app.state, 7, 3, 0, 0, 3)
            assert app.state["auto_indent"] == "0", app.state
            app.named_menu("Scheme", "Python")
            tabs(app.state, 9, 0, 1, 0, 3)
            app.named_menu("Scheme", "JSON")
            tabs(app.state, 7, 3, 0, 0, 3)
            app.named_menu("Scheme", "C/C++")
            tabs(app.state, 256, 0, 1, 0, 3)
            app.close()
        finally:
            app.stop()
        saved = config.read_text(encoding="utf-16")
        assert "TabWidth=7" in saved and "IndentWidth=3" in saved and "BackspaceUnindents=3" in saved, saved
        assert "[Python Script]" in saved and "TabWidth=9" in saved and "IndentWidth=0" in saved, saved
    print("Original defaults, seed, restoration, disabled exit save, explicit save and unrelated settings passed")


if __name__ == "__main__":
    main()
