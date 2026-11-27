"""Actual XRandR monitor movement, default positions and removed-monitor history."""

import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from localization_package import Process, menu_id, read_settings
from window_position_process import native_clipboard, native_rectangle, seed_clipboard


def run(*arguments):
    subprocess.run(arguments, check=True, capture_output=True, text=True)


def rectangle(app, expected):
    deadline = time.monotonic() + 4
    while True:
        actual = native_rectangle(app)
        if actual == expected:
            return
        assert time.monotonic() < deadline, (actual, expected)
        time.sleep(0.01)


def main():
    if sys.platform != "linux" or os.environ.get("GDK_BACKEND") != "x11":
        print("XRandR monitor acceptance requires the private X11 backend.")
        return 77
    assert os.environ.get("DISPLAY") not in (None, ":0", ":0.0") and os.environ.get("XAUTHORITY")
    with tempfile.TemporaryDirectory(prefix="wxnotepad-position-displays-") as temporary:
        folder = Path(temporary)
        source = Path(sys.argv[1]).resolve()
        executable = folder / source.name
        shutil.copy2(source, executable)
        config = folder / "Notepad4.ini"
        config.write_text("[Settings]\nSaveSettings=0\n[Settings2]\nUILanguage=1033\n", encoding="utf-16")
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        run("xrandr", "--setmonitor", "PositionLeft", "800/212x600/159+0+0", "none")
        run("xrandr", "--setmonitor", "PositionRight", "800/212x600/159+800+0", "none")
        try:
            app = Process(executable, folder / "right", environment, arguments=("-pos", "850,40,640,480,0"))
            try:
                rectangle(app, (850, 40, 640, 480))
                app.choose(menu_id(app.state, "Save Settings Now"))
                stored = read_settings(config)
                group = stored["Window Position 800x600"]
                assert tuple(int(group[key]) for key in ("WindowPosX", "WindowPosY", "WindowSizeX", "WindowSizeY")) == (
                    850,
                    40,
                    640,
                    480,
                )
                run("xdotool", "windowactivate", "--sync", app.state["frame_x11_window"])
                run("xdotool", "key", "--clearmodifiers", "ctrl+shift+p")
                rectangle(app, (1016, 16, 568, 568))
                seed_clipboard()
                app.choose(menu_id(app.state, "Window Position"))
                assert native_clipboard() == "-pos 1016,16,568,568,0"
                run("xdotool", "key", "--clearmodifiers", "ctrl+shift+p")
                rectangle(app, (816, 16, 568, 568))
                app.close()
            finally:
                app.stop()
            run("xrandr", "--delmonitor", "PositionRight")
            app = Process(executable, folder / "removed", environment)
            try:
                rectangle(app, (616, 16, 968, 968))
                app.close()
            finally:
                app.stop()
            print(
                "PASS: actual non-primary XRandR outer coordinates, right/left default, copy and removed-monitor history"
            )
        finally:
            monitors = subprocess.check_output(["xrandr", "--listmonitors"], text=True)
            for name in ("PositionRight", "PositionLeft"):
                if name in monitors:
                    run("xrandr", "--delmonitor", name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
