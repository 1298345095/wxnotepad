"""Verify original Settings2 insertion templates through independent application processes and saved bytes."""
from datetime import datetime
import os
from pathlib import Path
import shutil
import sys
import tempfile

from resource_process import command, wait_state
from settings_process import Application


def default_text(now, short):
    if sys.platform != "win32":
        return now.strftime("%H:%M %m/%d/%y" if short else "%H:%M %a %b %e %Y")
    import ctypes
    from ctypes import wintypes

    class SystemTime(ctypes.Structure):
        _fields_ = [(field, wintypes.WORD) for field in
                    ("year", "month", "weekday", "day", "hour", "minute", "second", "millisecond")]

    local = SystemTime(now.year, now.month, 0, now.day, now.hour, now.minute, now.second, 0)
    date = ctypes.create_unicode_buffer(128)
    clock = ctypes.create_unicode_buffer(128)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    assert kernel.GetDateFormatEx(None, 1 if short else 2, ctypes.byref(local), None, date, 128, None)
    assert kernel.GetTimeFormatEx(None, 2, ctypes.byref(local), None, clock, 128)
    return clock.value + " " + date.value


def main():
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-date-time-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        if sys.platform != "win32":
            environment["LC_ALL"] = "C.UTF-8"
        config = folder / "Notepad4.ini"
        document = folder / "insert.txt"
        cases = [
            ('  "短-%Y-%m-%d"  ', "long-%H:%M", "date"),
            ("S" * 255 + "ignored", "L" * 255 + "ignored", "bounded"),
            ("😀" * 127 + "Aignored", "字" * 255 + "ignored", "utf16"),
        ]
        for short, long, mode in cases:
            original = ("[Settings]\nUnrelated=keep\n[Settings2]\n"
                        f"DateTimeShort={short}\nDateTimeLong={long}\nOther=preserved\n")
            config.write_text(original, encoding="utf-16")
            for label, is_short in (("Time/Date (Short Form)", True), ("Time/Date (Long Form)", False)):
                document.write_bytes(b"")
                app = Application(executable, folder, environment, document)
                try:
                    before = datetime.now()
                    app.named_menu("Edit", label)
                    after = datetime.now()
                    assert app.state["dirty"] == "1", app.state
                    app.sequence += 1
                    command(folder, f"{app.sequence}\nsave\n")
                    app.state = wait_state(folder, app.process, lambda s: s["sequence"] == str(app.sequence))
                    assert app.state["result"] == "ok", app.state
                    actual = document.read_bytes()
                    if mode == "date":
                        pattern = "短-%Y-%m-%d" if is_short else "long-%H:%M"
                        assert actual in (before.strftime(pattern).encode(), after.strftime(pattern).encode()), actual
                    else:
                        expected = ("S" if is_short else "L") * 255 if mode == "bounded" else (
                            "😀" * 127 + "A" if is_short else "字" * 255)
                        assert actual == expected.encode("utf-8"), (mode, actual)
                    app.menu("Save Settings Now")
                    app.close()
                finally:
                    app.stop()
                saved = config.read_text(encoding="utf-16")
                assert f"DateTimeShort={short}" in saved and f"DateTimeLong={long}" in saved, saved
                assert "Other=preserved" in saved and "Unrelated=keep" in saved, saved
        # Missing and empty load-only fields retain the host defaults without creating keys.
        for fields in ("", "DateTimeShort=\nDateTimeLong=\n", 'DateTimeShort=""\nDateTimeLong=\'\'\n'):
            config.write_text("[Settings2]\n" + fields, encoding="utf-16")
            for label, short in (("Time/Date (Short Form)", True), ("Time/Date (Long Form)", False)):
                document.write_bytes(b"")
                app = Application(executable, folder, environment, document)
                try:
                    before = datetime.now()
                    app.named_menu("Edit", label)
                    after = datetime.now()
                    app.sequence += 1
                    command(folder, f"{app.sequence}\nsave\n")
                    app.state = wait_state(folder, app.process, lambda s: s["sequence"] == str(app.sequence))
                    assert app.state["result"] == "ok", app.state
                    actual = document.read_bytes()
                    assert actual in (default_text(before, short).encode(), default_text(after, short).encode()), actual
                    app.menu("Save Settings Now")
                    app.close()
                finally:
                    app.stop()
            saved = config.read_text(encoding="utf-16")
            assert ("DateTimeShort=" in saved) == bool(fields), saved
            assert ("DateTimeLong=" in saved) == bool(fields), saved
    print("PASS: Settings2 short/long templates, Unicode, original 255 UTF-16-unit limit and load-only persistence")


if __name__ == "__main__":
    main()
