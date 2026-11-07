"""Keep native assertions while checking opt-in input/clipboard/close evidence."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

from diagnostics_process import records


def check_native(log, driver):
    rows = records(log)
    names = {row["event"] for row in rows}
    for event in ("application.key-down", "application.key-up", "editor.key-down", "editor.key-up",
                  "editor.focus-in", "clipboard.write", "clipboard.owned", "clipboard.read", "clipboard.received"):
        assert event in names, (event, names)
    for row in rows:
        if row["event"].startswith("editor.key-"):
            assert all(field in row for field in ("target", "focus", "length", "anchor", "caret", "key", "modifiers")), row
        if row["event"] == "clipboard.received":
            assert int(row["length"]) > 0 and row["format"], row
    driver_rows = records(driver) if driver.exists() else rows
    for gesture in {row["gesture"] for row in rows if row["gesture"] != "0"}:
        events = {row["event"] for row in rows if row["gesture"] == gesture}
        delivered = {row["event"] for row in driver_rows if row["gesture"] == gesture}
        if {"application.key-down", "application.key-up", "editor.key-down", "editor.key-up"} <= events:
            assert {"driver.request", "driver.ack"} <= delivered, (gesture, delivered)
            break
    else:
        raise AssertionError("one native gesture must correlate driver acknowledgement and application/editor receipt")
    # Known independent fixture literals must stay out of the diagnostic channel.
    contents = log.read_text(encoding="utf-8")
    for payload in ("abc tail", "first\\r\\nsecond", "Unsaved", "original bytes", "continued"):
        assert payload not in contents, payload


def check_close(log):
    rows = records(log)
    choices = {row["choice"] for row in rows if row["event"] == "close.choice"}
    assert {"Save", "Discard", "Cancel"} <= choices, choices
    events = [row["event"] for row in rows]
    for expected in ("close.confirm", "close.veto", "close.save-result", "close.destroy-deferred", "frame.destroy"):
        assert expected in events, (expected, events)
    assert events[-1] == "process.exit", events


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("editing_keys", type=Path)
    parser.add_argument("close_smoke", type=Path)
    parser.add_argument("--compiled-out", action="store_true")
    parser.add_argument("--runtime", choices=("off", "on", "both"), default="both")
    parser.add_argument("--evidence", required=True, type=Path)
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    for enabled in (False, True):
        mode = "on" if enabled else "off"
        if args.runtime != "both" and args.runtime != mode:
            continue
        for executable, check in ((args.editing_keys, check_native), (args.close_smoke, check_close)):
            log = args.evidence.resolve() / f"{mode}-{executable.stem}.log"
            log.unlink(missing_ok=True)
            environment = dict(os.environ, WXNOTEPAD_DIAGNOSTIC_LOG=str(log),
                               WXNOTEPAD_DIAGNOSTICS="1" if enabled else "0")
            focus = environment.get("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS")
            driver = Path(focus + ".diagnostics") if focus else log.with_suffix(".driver")
            driver.unlink(missing_ok=True)
            try:
                # Keep the fixtures' own deadlines, assertions and genuine status.
                result = subprocess.run([str(executable.resolve())], env=environment, timeout=30)
                assert result.returncode == 0, (executable, result.returncode)
                if enabled and not args.compiled_out:
                    if executable == args.editing_keys:
                        check(log, driver)
                    else:
                        check(log)
                else:
                    assert not log.exists() and not driver.exists(), "disabled native run must leave no diagnostic file"
                print(f"PASS: native {executable.stem} runtime={mode} compiled_out={args.compiled_out}", flush=True)
            finally:
                if driver.exists():
                    shutil.copy2(driver, args.evidence / f"{mode}-{executable.stem}.driver")


if __name__ == "__main__":
    main()
