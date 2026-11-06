"""Observe opt-in diagnostics through independent actual application processes."""
import argparse
from contextlib import redirect_stderr
import os
from pathlib import Path
import shutil
import tempfile
import time

from settings_process import Application


def records(path, mixed=False):
    assert path.is_file(), "enabled diagnostic run must create its requested log"
    rows = [dict(field.split("=", 1) for field in line.split("\t"))
            for line in path.read_text(encoding="utf-8").splitlines() if not mixed or line.startswith("us=")]
    assert rows and rows[0]["event"] == "header", rows
    for field in ("build", "backend", "wx", "core", "compiler"):
        assert rows[0][field], (field, rows[0])
    assert [int(row["seq"]) for row in rows] == list(range(1, len(rows) + 1)), rows
    assert all(int(row["pid"]) > 0 and int(row["tid"]) > 0 for row in rows), rows
    stamps = [int(row["us"]) for row in rows]
    assert stamps == sorted(stamps), rows
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--compiled-out", action="store_true")
    parser.add_argument("--evidence", type=Path)
    args = parser.parse_args()
    source = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-diagnostics-") as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        base = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        base.pop("WXNOTEPAD_DIAGNOSTICS", None)
        base.pop("WXNOTEPAD_DIAGNOSTIC_LOG", None)
        logs = args.evidence.resolve() if args.evidence else folder
        logs.mkdir(parents=True, exist_ok=True)
        for enabled in (False, True):
            log = logs / ("enabled-诊断😀.log" if enabled else "disabled-诊断😀.log")
            log.unlink(missing_ok=True)
            environment = dict(base, WXNOTEPAD_DIAGNOSTIC_LOG=str(log))
            if enabled:
                environment["WXNOTEPAD_DIAGNOSTICS"] = "1"
            app = Application(executable, folder, environment)
            try:
                app.menu("Zoom In")
                app.close()
            finally:
                app.stop()
            if enabled and not args.compiled_out:
                rows = records(log)
                events = [row["event"] for row in rows]
                for expected in ("observer.request", "observer.ack", "close.request", "close.destroy-deferred",
                                 "frame.destroy", "process.exit"):
                    assert expected in events, (expected, events)
                assert events[-1] == "process.exit", events
            else:
                assert not log.exists(), "disabled or compiled-out run must leave no diagnostic file"
            print(f"PASS: runtime={'on' if enabled else 'off'} compiled_out={args.compiled_out}", flush=True)
        stderr_log = logs / "stderr.log"
        with stderr_log.open("w", encoding="utf-8") as captured, redirect_stderr(captured):
            app = Application(executable, folder, dict(base, WXNOTEPAD_DIAGNOSTICS="1",
                                                       WXNOTEPAD_DIAGNOSTIC_LOG="stderr"))
            try:
                app.close()
            finally:
                app.stop()
        if args.compiled_out:
            assert "event=header" not in stderr_log.read_text(encoding="utf-8"), "compiled-out build emitted tracing"
        else:
            assert records(stderr_log, mixed=True)[-1]["event"] == "process.exit"
        print(f"PASS: captured stderr channel compiled_out={args.compiled_out}", flush=True)
        if not args.compiled_out:
            log = logs / "terminated-诊断😀.log"
            log.unlink(missing_ok=True)
            app = Application(executable, folder, dict(base, WXNOTEPAD_DIAGNOSTICS="1",
                                                       WXNOTEPAD_DIAGNOSTIC_LOG=str(log)))
            try:
                app.menu("Zoom In")
                # Publication acknowledges the real menu first. Wait for its
                # diagnostic record to complete before testing prefix retention.
                deadline = time.monotonic() + 5
                while True:
                    lines = log.read_text(encoding="utf-8").splitlines(keepends=True) if log.exists() else []
                    if any(line.endswith("\n") and "\tevent=observer.ack\t" in line for line in lines):
                        break
                    assert time.monotonic() < deadline, "diagnostic acknowledgement record did not complete"
                    time.sleep(.01)
                app.process.terminate()
                status = app.process.wait(timeout=10)
                assert status != 0, f"controlled abnormal exit must retain the real failure status: {status}"
                rows = records(log)
                assert any(row["event"] == "observer.ack" for row in rows), rows
                assert rows[-1]["event"] != "process.exit", rows
                print(f"PASS: completed records survived abnormal exit={status}", flush=True)
            finally:
                app.stop()


if __name__ == "__main__":
    main()
