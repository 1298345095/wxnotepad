"""Original markup detection and canonical tab sections through real app processes."""
import os
from pathlib import Path
import shutil
import sys
import tempfile

from settings_process import Application, tabs


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-markup-settings-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        config = folder / "Notepad4.ini"
        startup = folder / "untyped"
        for settings, contents, expected in (
                ("", b"<Root/>", "XML Document"),
                ("[Settings2]\nNoHTMLGuess=1\n", b"<Root/>", "Plain Text"),
                ("[Settings2]\nNoHTMLGuess=1\n", b"<HTML/>", "Web Source Code"),
                ("[Settings2]\nNoHTMLGuess=1\n", b"<?xml version=\"1.0\"?><Root/>", "XML Document"),
                ("[Styles]\nAutoSelect=0\n", b"<!DOCTYPE html><html/>", "Plain Text"),
                ("[Settings2]\nNoHTMLGuess=invalid\n", b"<Root/>", "XML Document"),
                ("[Settings]\nNoHTMLGuess=1\n", b"<Root/>", "XML Document"),
                ("", b" " * 255 + b"<html/>", "Plain Text"),
                ("", b"<!--" + b" " * 300 + b"--><!DOCTYPE html><html/>", "Web Source Code")):
            startup.write_bytes(contents)
            config.write_text("[Settings]\nSaveSettings=0\nWarnLineEndings=0\nDetectIndentation=0\n" + settings,
                              encoding="utf-16")
            app = Application(executable, folder, environment, startup)
            try:
                assert f" | {expected} | " in app.state["document_status"], app.state
                app.named_menu("Scheme", "SVG")
                app.named_menu("Scheme", "Automatic")
                assert f" | {expected} | " in app.state["document_status"], app.state
                app.menu("Save Settings Now")
                app.close()
            finally:
                app.stop()
            saved = config.read_text(encoding="utf-16")
            assert contents == startup.read_bytes()
            if "NoHTMLGuess=" in settings:
                assert settings.split("NoHTMLGuess=", 1)[1].splitlines()[0] in saved.split("NoHTMLGuess=", 1)[1]
        config.write_text("[Settings]\nDetectIndentation=0\nTabWidth=7\nIndentWidth=3\n"
                          "[Web Source Code]\nTabWidth=5\nIndentWidth=2\nUnrelated=keep\n"
                          "[XML Document]\nTabWidth=9\nIndentWidth=6\nUseGlobalTabSettings=0\n"
                          "TabsAsSpaces=1\n[Settings2]\nNoHTMLGuess=1suffix\n", encoding="utf-16")
        app = Application(executable, folder, environment)
        try:
            for language in ("Web Source Code", "ASP VBScript", "ASP JavaScript", "ASP.NET C#", "ASP.NET VB", "JSP", "JSPX"):
                app.named_menu("Scheme", language)
                tabs(app.state, 5, 2, 0)
            for language in ("XML Document", "SVG", "XML Schema", "XSLT", "DTD", "Property List"):
                app.named_menu("Scheme", language)
                tabs(app.state, 9, 6, 0)
            app.menu("Save Settings Now")
            app.close()
        finally:
            app.stop()
        saved = config.read_text(encoding="utf-16")
        assert "NoHTMLGuess=1suffix" in saved and "Unrelated=keep" in saved, saved
        assert "[ASP VBScript]" not in saved and "[SVG]" not in saved, saved
        print("PASS: original markup prefix limits, load-only NoHTMLGuess and shared HTML/XML INI sections")


if __name__ == "__main__":
    main()
