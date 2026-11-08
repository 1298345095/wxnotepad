"""Round-trip original file-watching preferences through independent applications."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile
import time

from settings_process import Application


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    source = args.executable.resolve()
    for mode, interval, delay in ((2, 1000, 1000), (0, 1373, 1459), (1, 1373, 1459), (2, 1373, 1459)):
        with tempfile.TemporaryDirectory(prefix="wxnotepad-watch-settings-") as name:
            folder = Path(name)
            executable = folder / source.name
            shutil.copy2(source, executable)
            configuration = folder / "Notepad4.ini"
            text = "[Settings]\nUnrelatedSetting=keep\n[Settings2]\nUnrelatedTimer=keep\n"
            if (interval, delay) != (1000, 1000):
                text = (f"[Settings]\nFileWatchingMode={mode}\nUnrelatedSetting=keep\n"
                        f"[Settings2]\nFileCheckInterval={interval}\nAutoReloadTimeout={delay}\n"
                        "UnrelatedTimer=keep\n")
            configuration.write_text(text, encoding="utf-16")
            environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
            for _ in range(2):
                app = Application(executable, folder, environment)
                try:
                    observed = tuple(int(app.state[key]) for key in
                                     ("file_watching_mode", "file_check_interval", "auto_reload_timeout"))
                    assert observed == (mode, interval, delay), app.state
                    app.close()
                finally:
                    app.stop()
            saved = configuration.read_text(encoding="utf-16")
            assert "UnrelatedSetting=keep" in saved and "UnrelatedTimer=keep" in saved, saved
    with tempfile.TemporaryDirectory(prefix="wxnotepad-watch-startup-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        (folder / "Notepad4.ini").write_text(
            "[Settings2]\nFileCheckInterval=20\nAutoReloadTimeout=50\n", encoding="utf-16")
        fixture = folder / "startup.txt"
        fixture.write_bytes(b"initial startup document\n")
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        app = Application(executable, folder, environment, startup=fixture)
        try:
            expected = b"independently changed startup document\n"
            fixture.write_bytes(expected)
            time.sleep(.3)
            app.named_menu("File", "Save")
            assert fixture.read_bytes() == expected and int(app.state["length"]) == len(expected), app.state
            app.close()
        finally:
            app.stop()
    print("PASS: original AutoReload/1000/1000 defaults and all three modes survive independent process restarts")
    print("PASS: startup-file Open installs monitoring when the native event loop starts")


if __name__ == "__main__":
    main()
