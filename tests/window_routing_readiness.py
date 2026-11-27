"""Keep the actual activation case behind a pending native WM map request."""

import argparse
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from app_observation import read_state

MAP_PUBLICATION_DELAY_SECONDS = 0.1
STARTUP_TIMEOUT_SECONDS = 15


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake-dir", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "linux" or os.environ.get("GDK_BACKEND") != "x11":
        print("SKIP: delayed X11 map requires the private X11 backend")
        return 77
    assert os.environ.get("DISPLAY") not in (None, ":0", ":0.0") and os.environ.get("XAUTHORITY")
    # managed_x11_session owns this sole sibling WM on our private display.
    # Its other processes and the host desktop are outside this fixture.
    parent = os.getppid()
    siblings = Path(f"/proc/{parent}/task/{parent}/children").read_text().split()
    managers = [int(pid) for pid in siblings if Path(f"/proc/{pid}/exe").resolve().name == "openbox"]
    assert len(managers) == 1, managers
    manager = managers[0]
    args.evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="owned-routing-map-") as directory:
        root = Path(directory)
        child = None
        try:
            os.kill(manager, signal.SIGSTOP)
            try:
                child = subprocess.Popen(
                    [
                        sys.executable,
                        "-X",
                        "utf8",
                        str(Path(__file__).with_name("window_routing_process.py")),
                        str(args.executable),
                        "--cmake-dir",
                        str(args.cmake_dir),
                        "--evidence",
                        str(args.evidence / "activation"),
                        "--scenario",
                        "activation",
                        "--language",
                        "en",
                    ],
                    env=dict(os.environ, TMPDIR=directory),
                )
                deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
                while not (states := list(root.rglob("state"))):
                    assert child.poll() is None, "Activation fixture exited before startup publication"
                    assert time.monotonic() < deadline, "Application startup must publish with its WM map pending"
                    time.sleep(0.005)
                state = read_state(states[0].parent)
                assert state["shown"] == "1" and state["backend"] == "GdkX11Display", state
                observation = {key: state[key] for key in ("shown", "active", "backend")}
                observation["wm_map_pending"] = True
                (args.evidence / "before-map.json").write_text(json.dumps(observation, indent=2), encoding="utf-8")
                # This is a controlled WM delay, not a readiness sleep in the driver.
                time.sleep(MAP_PUBLICATION_DELAY_SECONDS)
            finally:
                os.kill(manager, signal.SIGCONT)
            assert child.wait(timeout=120) == 0, "Original complete activation case must survive delayed native mapping"
        finally:
            if child is not None and child.poll() is None:
                child.terminate()
                child.wait(timeout=10)
    print("PASS: shown state precedes native mapping; original minimize, delivery and activation checks pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
