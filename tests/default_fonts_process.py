"""Verify native default font changes across independent portable application launches."""

from pathlib import Path
import configparser
import os
import shutil
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="wxnotepad-fonts-") as temporary:
    folder = Path(temporary)
    executable = folder / source.name
    shutil.copy2(source, executable)
    environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
    ini = folder / "Notepad4.ini"
    sample = folder / "font.py"
    sample.write_text("print('字体')\n", encoding="utf-8")
    code = "font:$(Code); size:14; weight:600; italic; overline; locale:zh-CN; fore:#123456; back:#EEEEDD; alpha:77; outline:99; case:U; custom:保留"
    text = "font:$(Text); size:18; underline"
    scheme = "font:$(Text); size:+2; strike"

    def launch(action, expected_code, expected_cpp, open_file=False):
        arguments = [str(executable), action, str(expected_code), str(expected_cpp)]
        if open_file:
            arguments.append(str(sample))
        result = subprocess.run(arguments, env=environment, capture_output=True, text=True, timeout=25)
        assert result.returncode == 0, (action, expected_code, expected_cpp, result.stdout + result.stderr)

    for save_on_exit, action, changed in [
        (True, "change", True),
        (False, "change", False),
        (False, "explicit", True),
        (True, "cancel", False),
    ]:
        original = (
            f"[Settings]\nSaveSettings={int(save_on_exit)}\n"
            f"[Global Styles]\n; retain this comment\nDefault Code Style={code}\nDefault Text Style={text}\n"
            f"[C/C++ Source]\nUseDefaultCodeStyle=0\nDefault={scheme}\nUnknownScheme=unchanged\n"
            "[Other]\nDefault Code Style=unrelated\n"
        )
        ini.write_text(original, encoding="utf-16")
        launch(action, 14, 20)
        raw = ini.read_bytes()
        assert raw.startswith(b"\xff\xfe"), "font preferences retain original UTF-16LE BOM"
        saved = raw.decode("utf-16")
        parser = configparser.ConfigParser(interpolation=None)
        parser.read_string(saved)
        actual = parser["Global Styles"]["Default Code Style"]
        if changed:
            assert "; size:15;" in actual and "; bold" in actual and "; italic" in actual, actual
            if os.name == "nt":
                assert "; underline" in actual and "; strike" in actual, actual
            for attribute in [
                "overline",
                "locale:zh-CN",
                "fore:#123456",
                "back:#EEEEDD",
                "alpha:77",
                "outline:99",
                "case:U",
                "custom:保留",
            ]:
                assert attribute in actual, (attribute, actual)
            assert parser["C/C++ Source"].getboolean("UseDefaultCodeStyle", fallback=True)
        else:
            assert actual == code, (action, actual)
            assert not parser["C/C++ Source"].getboolean("UseDefaultCodeStyle")
        assert parser["Global Styles"]["Default Text Style"] == text
        assert parser["C/C++ Source"]["Default"] == scheme
        assert parser["C/C++ Source"]["UnknownScheme"] == "unchanged"
        assert parser["Other"]["Default Code Style"] == "unrelated"
        assert "; retain this comment" in saved
        launch("verify", 15 if changed else 14, 17 if changed else 20, open_file=True)
print(
    "PASS: font chooser, inheritance, original INI merge, disabled exit save, explicit save and independent reopening"
)
