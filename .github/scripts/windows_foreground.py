"""Prepare and restore foreground activation on an isolated hosted Windows desktop."""

import argparse
import ctypes
import json
import os
import sys
from contextlib import contextmanager
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from report_paths import report_path  # noqa: E402 -- The tools path must be initialized first.

SPI_GETFOREGROUNDLOCKTIMEOUT = 0x2000
SPI_SETFOREGROUNDLOCKTIMEOUT = 0x2001


def native_api():
    if os.name != "nt" or os.environ.get("GITHUB_ACTIONS") != "true":
        raise SystemExit("Foreground session changes are only supported on hosted Windows CI")
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.SystemParametersInfoW.argtypes = [wintypes.UINT, wintypes.UINT, wintypes.LPVOID, wintypes.UINT]
    return user


def current_timeout(user):
    value = wintypes.DWORD()
    if not user.SystemParametersInfoW(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, ctypes.byref(value), 0):
        raise ctypes.WinError(ctypes.get_last_error())
    return value.value


def publish(destination, record):
    destination.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")


def prepare(destination):
    user = native_api()
    original = current_timeout(user)
    record = {"original_milliseconds": original, "requested_milliseconds": 0}
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Persist the original value before changing the session, including when
    # setup later fails. Do not overwrite an earlier session's restoration data.
    with destination.open("x", encoding="utf-8") as output:
        output.write(json.dumps(record, indent=2) + "\n")
    ctypes.set_last_error(0)
    changed = bool(user.SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, None, 0))
    record.update(set_succeeded=changed, set_error=ctypes.get_last_error())
    publish(destination, record)
    if not changed:
        raise SystemExit("Foreground timeout setup was rejected; inspect " + str(destination))
    record["effective_milliseconds"] = current_timeout(user)
    publish(destination, record)
    if record["effective_milliseconds"] != 0:
        raise SystemExit("Foreground timeout setup did not take effect; inspect " + str(destination))
    print(f"Foreground lock timeout: {original}ms -> 0ms (session only)", flush=True)


def restore(destination):
    user = native_api()
    # Setup writes the original value before its first setter call. A failure
    # before that write has not changed this session.
    if not destination.exists():
        return
    record = json.loads(destination.read_text(encoding="utf-8"))
    if not record.get("set_succeeded"):
        return
    original = record["original_milliseconds"]
    ctypes.set_last_error(0)
    restored = bool(user.SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, ctypes.c_void_p(original), 0))
    record.update(restore_succeeded=restored, restore_error=ctypes.get_last_error())
    publish(destination, record)
    record["restored_milliseconds"] = current_timeout(user)
    publish(destination, record)
    if not restored or record["restored_milliseconds"] != original:
        raise SystemExit("Foreground timeout restoration failed; inspect " + str(destination))
    print(f"Foreground lock timeout restored to {original}ms", flush=True)


@contextmanager
def temporary(destination):
    if destination.exists():
        raise FileExistsError(destination)
    try:
        prepare(destination)
        yield
    finally:
        restore(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "restore"))
    parser.add_argument("--state-file", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        destination = report_path(arguments.state_file, Path.cwd().resolve())
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(str(error))
    if arguments.action == "prepare":
        prepare(destination)
    else:
        restore(destination)


if __name__ == "__main__":
    main()
