"""Give the Escape native-window fixture a real WM inside its owned Xvfb session."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def main():
    if sys.platform != "linux" or os.environ.get("GDK_BACKEND") != "x11":
        return subprocess.call(sys.argv[1:], stdout=sys.stdout, stderr=sys.stderr)
    if os.environ.get("DISPLAY") in (None, ":0", ":0.0") or not os.environ.get("XAUTHORITY"):
        raise RuntimeError("Managed X11 tests require the existing private Xvfb session")
    executable = shutil.which("openbox")
    if not executable:
        raise RuntimeError("Native X11 minimize/restore requires test-only Openbox; install Ubuntu package openbox")
    with tempfile.TemporaryDirectory(prefix="wxnotepad-openbox-") as directory:
        config = Path(directory) / "rc.xml"
        config.write_text(
            '<?xml version="1.0"?><openbox_config xmlns="http://openbox.org/3.4/rc">'
            "<focus><focusNew>yes</focusNew></focus><keyboard/><mouse/></openbox_config>",
            encoding="utf-8",
        )
        with (Path(directory) / "openbox.log").open("w+", encoding="utf-8") as log:
            wm = subprocess.Popen(
                [executable, "--sm-disable", "--config-file", str(config)], stdout=log, stderr=subprocess.STDOUT
            )
            try:
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    state = subprocess.run(
                        ["xprop", "-root", "_NET_SUPPORTING_WM_CHECK"], capture_output=True, text=True, check=True
                    ).stdout
                    if "window id #" in state and wm.poll() is None:
                        return subprocess.call(sys.argv[1:], stdout=sys.stdout, stderr=sys.stderr)
                    if wm.poll() is not None:
                        break
                    time.sleep(0.01)
                raise RuntimeError("Owned Openbox did not acknowledge the private X11 root within 3 seconds")
            finally:
                if wm.poll() is None:
                    wm.terminate()
                    try:
                        wm.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        wm.kill()
                        wm.wait(timeout=3)
                log.seek(0)
                print(log.read(), end="")


if __name__ == "__main__":
    sys.exit(main())
