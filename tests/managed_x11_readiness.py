"""A stale WM acknowledgement cannot release the next real managed X11 command."""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def verify_native_wm():
    state = subprocess.check_output(["xprop", "-root", "_NET_SUPPORTING_WM_CHECK"], text=True)
    match = re.search(r"window id # (0x[0-9a-fA-F]+)", state)
    assert match, state
    supporting = subprocess.run(["xprop", "-id", match[1], "_NET_SUPPORTING_WM_CHECK"], capture_output=True, text=True)
    assert supporting.returncode == 0 and match[1] in supporting.stdout, supporting.stdout + supporting.stderr
    supported = subprocess.check_output(["xprop", "-root", "_NET_SUPPORTED"], text=True)
    assert "_NET_WM_STATE_ABOVE" in supported, supported


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--session-helper", type=Path, default=Path(__file__).with_name("managed_x11_session.py"))
    args = parser.parse_args()
    if sys.platform != "linux" or os.environ.get("GDK_BACKEND") != "x11":
        print("SKIP: native managed-WM readiness applies to X11")
        return 77
    if args.verify:
        verify_native_wm()
        return 0
    real_openbox = shutil.which("openbox")
    assert real_openbox, "native fixture requires actual Openbox"
    command = [sys.executable, str(args.session_helper), sys.executable, str(Path(__file__).resolve()), "--verify"]
    subprocess.run(command, check=True, timeout=15)
    stale = subprocess.check_output(["xprop", "-root", "_NET_SUPPORTING_WM_CHECK"], text=True)
    assert "window id #" in stale, "the first actual WM leaves its stale root acknowledgement"
    with tempfile.TemporaryDirectory(prefix="delayed-native-wm-") as directory:
        delayed = Path(directory) / "openbox"
        delayed.write_text(
            f"#!{sys.executable}\nimport os, sys, time\ntime.sleep(0.1)\n"
            f"os.execv({real_openbox!r}, [{real_openbox!r}, *sys.argv[1:]])\n",
            encoding="utf-8",
        )
        delayed.chmod(0o755)
        environment = dict(os.environ, PATH=directory + os.pathsep + os.environ["PATH"])
        subprocess.run(command, check=True, timeout=15, env=environment)
    print("PASS: serial native WM sessions reject stale acknowledgement and wait for actual delayed Openbox")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
