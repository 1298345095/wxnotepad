"""Observe the actual Release app, with independent processes and native edits.

Requires a graphical session. Linux: run inside clipboard_isolated.py's private
Xvfb/Weston session. All output/scratch files are intentionally disposable.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
import time

POLL = .01
QUIET = 1.0


def digest(path, prefix=b""):
    result = hashlib.sha256(prefix)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def memory(pid):
    if sys.platform != "win32":
        status = dict(line.split(":", 1) for line in Path(f"/proc/{pid}/status").read_text().splitlines() if ":" in line)
        return {"rss_bytes": int(status["VmRSS"].split()[0]) * 1024}

    class Counters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize",
            "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
            "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
    handle = kernel.OpenProcess(0x410, False, pid)
    counters = Counters()
    counters.cb = ctypes.sizeof(counters)
    ok = psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
    kernel.CloseHandle(handle)
    if not ok:
        raise ctypes.WinError(ctypes.get_last_error())
    return {"working_set_bytes": counters.WorkingSetSize, "private_bytes": counters.PrivateUsage}


def wait_state(folder, process, predicate):
    deadline = time.perf_counter() + 30
    while time.perf_counter() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"App exited {process.returncode}")
        try:
            state = dict(line.split("\t", 1) for line in
                         (folder / "state").read_text(encoding="utf-8").splitlines())
            if predicate(state):
                return state
        except (FileNotFoundError, PermissionError):
            pass
        time.sleep(POLL)
    raise RuntimeError("App state observation timed out")


def command(folder, sequence, operation, path=""):
    pending = folder / "pending"
    pending.write_text(f"{sequence}\n{operation}\n{path}\n", encoding="utf-8")
    pending.replace(folder / "command")


def native_edit(process, state):
    if sys.platform == "win32":
        user = ctypes.WinDLL("user32", use_last_error=True)
        user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        handle = int(state["editor_handle"])
        owner = wintypes.DWORD()
        user.GetWindowThreadProcessId(handle, ctypes.byref(owner))
        if owner.value != process.pid:
            raise RuntimeError("Editor native handle does not belong to the launched process")
        if not user.PostMessageW(handle, 0x0102, ord("X"), 1):
            raise ctypes.WinError(ctypes.get_last_error())
    else:
        if os.environ.get("DISPLAY") in (None, ":0", ":0.0") or not os.environ.get("XAUTHORITY"):
            raise RuntimeError("Native input requires the private Xvfb display")
        if state["backend"] == "GdkX11Display":
            windows = subprocess.check_output(["xdotool", "search", "--onlyvisible", "--pid", str(process.pid)], text=True).splitlines()
            if not windows:
                raise RuntimeError("No owned visible X11 window")
            subprocess.run(["xdotool", "windowfocus", "--sync", windows[0]], check=True)
        subprocess.run(["xdotool", "key", "--clearmodifiers", "X"], check=True)


def show_owned_topmost(process, state):
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.GetAncestor.argtypes = [wintypes.HWND, wintypes.UINT]
    user.GetAncestor.restype = wintypes.HWND
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                                 ctypes.c_int, ctypes.c_int, wintypes.UINT]
    user.RedrawWindow.argtypes = [wintypes.HWND, ctypes.c_void_p, wintypes.HANDLE, wintypes.UINT]
    handle = user.GetAncestor(int(state["editor_handle"]), 2)
    owner = wintypes.DWORD()
    user.GetWindowThreadProcessId(handle, ctypes.byref(owner))
    if owner.value != process.pid:
        raise RuntimeError("Topmost frame does not belong to launched process")
    if not user.SetWindowPos(handle, wintypes.HWND(-1), 0, 0, 0, 0, 0x0053):
        raise ctypes.WinError(ctypes.get_last_error())
    user.RedrawWindow(handle, None, None, 0x0181)  # invalidate, all children, update now


def prepare(directory):
    directory.mkdir(parents=True, exist_ok=True)
    line = (b"plain text 0123456789 abcdefghijklmnopqrstuvwxyz " * 3)[:127] + b"\n"
    block = bytearray(line * 8192)
    block[-128:-122] = b"needle"
    paths = {}
    for name, count in (("small", 0), ("10MiB", 10), ("100MiB", 100)):
        path = directory / f"{name}.txt"
        with path.open("wb") as stream:
            if count:
                for _ in range(count):
                    stream.write(block)
            else:
                stream.write(line * 32)
        paths[name] = path
    return paths


def observe(executable, work, kind, sample, run, topmost):
    folder = work / f"{kind}-{sample.stem if sample else 'empty'}-{run}"
    folder.mkdir()
    scratch = folder / "document.txt"
    if sample:
        shutil.copyfile(sample, scratch)  # Outside timing; can warm OS file cache.
    args = [str(executable), "--observe", str(folder)]
    if kind != "operations" and sample:
        args += ["--startup-file", str(scratch)]
    row = {"kind": kind, "case": sample.stem if sample else "empty", "run": run,
           "folder": str(folder), "memory": {}, "timestamps": {}}
    stamp = row["timestamps"]
    stamp["spawn_begin"] = time.perf_counter()
    process = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    row["pid"] = process.pid
    try:
        state = wait_state(folder, process, lambda s: s["sequence"] == "0" and
                           (s["backend"] != "GdkWaylandDisplay" or s["active"] == "1"))
        if topmost:
            show_owned_topmost(process, state)
        stamp["ready_observed"] = time.perf_counter()
        row["initial_state"] = state
        assert state["shown"] == "1" and state["dirty"] == "0", state
        row["launch_to_update_observed_ms"] = (stamp["ready_observed"] - stamp["spawn_begin"]) * 1000
        if kind == "idle":
            time.sleep(QUIET)
            row["memory"]["idle"] = memory(process.pid)
            stamp["idle_memory"] = time.perf_counter()
        else:
            if kind == "operations":
                stamp["open_begin"] = time.perf_counter()
                command(folder, 1, "open", scratch)
                state = wait_state(folder, process, lambda s: s["sequence"] == "1")
                stamp["open_observed"] = time.perf_counter()
                assert state["result"] == "ok" and int(state["length"]) == sample.stat().st_size, state
                row["open_observed_ms"] = (stamp["open_observed"] - stamp["open_begin"]) * 1000
                time.sleep(QUIET)
                row["memory"]["opened"] = memory(process.pid)
                stamp["opened_memory"] = time.perf_counter()
            stamp["edit_begin"] = time.perf_counter()
            native_edit(process, state)
            state = wait_state(folder, process, lambda s: s["dirty"] == "1")
            stamp["edit_observed"] = time.perf_counter()
            assert int(state["length"]) == (sample.stat().st_size if sample else 0) + 1, state
            row["edit_observed_ms"] = (stamp["edit_observed"] - stamp["edit_begin"]) * 1000
            if kind == "startup":
                row["launch_to_native_edit_observed_ms"] = (stamp["edit_observed"] - stamp["spawn_begin"]) * 1000
            if kind == "operations":
                time.sleep(QUIET)
                row["memory"]["edited"] = memory(process.pid)
                stamp["edited_memory"] = time.perf_counter()
                stamp["save_begin"] = time.perf_counter()
                command(folder, 2, "save")
                state = wait_state(folder, process, lambda s: s["sequence"] == "2")
                stamp["save_observed"] = time.perf_counter()
                assert state["result"] == "ok" and state["dirty"] == "0", state
                row["save_observed_ms"] = (stamp["save_observed"] - stamp["save_begin"]) * 1000
                time.sleep(QUIET)
                row["memory"]["saved"] = memory(process.pid)
                stamp["saved_memory"] = time.perf_counter()
                # Independent full-content comparison only after all timing/memory.
                with scratch.open("rb") as actual, sample.open("rb") as original:
                    assert actual.read(1) == b"X"
                    while True:
                        block = original.read(1024 * 1024)
                        assert actual.read(len(block)) == block
                        if not block:
                            assert actual.read(1) == b""
                            break
                row["output_sha256"] = digest(scratch)
                row["output_bytes"] = scratch.stat().st_size
                row["content_verified"] = True
        row["final_state"] = state
        row["status"] = "ok"
    except Exception as error:
        row["status"] = "failed"
        row["error"] = repr(error)
    finally:
        process.terminate()
        process.wait(timeout=10)
    return row


def summarize(rows):
    groups = {}
    for row in rows:
        if row["status"] != "ok":
            continue
        group = groups.setdefault(row["kind"] + "/" + row["case"], [])
        group.append(row)
    result = {}
    for name, values in groups.items():
        entry = {"successful_processes": len(values)}
        for key in values[0]:
            if key.endswith("_ms"):
                entry[key] = statistics.median(row[key] for row in values)
        entry["memory"] = {stage: {counter: statistics.median(row["memory"][stage][counter] for row in values)
                                   for counter in counters}
                           for stage, counters in values[0]["memory"].items()}
        result[name] = entry
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--startup-count", type=int, default=5)
    parser.add_argument("--idle-count", type=int, default=3)
    parser.add_argument("--operation-count", type=int, default=3)
    parser.add_argument("--owned-topmost", action="store_true",
                        help="Windows: show only each launched app above normal windows without global input")
    args = parser.parse_args()
    if args.owned_topmost and sys.platform != "win32":
        parser.error("--owned-topmost is Windows-only")
    executable = args.executable.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True)
    samples = prepare(output / "samples")
    report = {"source_commit": args.source_commit, "executable": str(executable),
              "exe_sha256": digest(executable), "platform": platform.platform(),
              "python": sys.version, "processor": platform.processor(), "logical_cpus": os.cpu_count(),
              "environment": {k: os.environ.get(k) for k in ("DISPLAY", "GDK_BACKEND", "WAYLAND_DISPLAY")},
              "poll_seconds": POLL, "hook_timer_ms": 10, "quiet_seconds": QUIET,
              "owned_topmost": args.owned_topmost,
              "planned_counts": {"startup_per_case": args.startup_count,
                                 "idle_per_case": args.idle_count,
                                 "operations_per_size": args.operation_count},
              "cache": "OS caches retained; samples generated/hashed/copied before timing; fresh process each row",
              "definitions": "External perf_counter observed upper bounds including command-file/input dispatch and polling. Actual shown frame; Open/Save frame commands, native one-byte X at zero. Hook calls Update before publishing; no compositor-present timestamp. Startup Update marker followed by native character acceptance; pristine idle uses separate processes. Windows WM_CHAR is process-local native character delivery, not a hardware/IME latency test. Linux xdotool input and focus subprocess overhead included in edit/startup endpoints. Memory snapshots after 1 s quiet, not peaks.",
              "samples": {name: {"bytes": p.stat().st_size, "sha256": digest(p),
                                 "expected_saved_sha256": digest(p, b"X")} for name, p in samples.items()},
              "rows": []}
    for parent in executable.parents:
        record = parent / "original-core/wxnotepad-source.json"
        if record.exists():
            report["core_build_record"] = json.loads(record.read_text(encoding="utf-8"))
            cache = parent / "cmake/CMakeCache.txt"
            report["cmake_build"] = [line for line in cache.read_text(encoding="utf-8").splitlines()
                                     if line.startswith(("CMAKE_BUILD_TYPE:", "CMAKE_CXX_COMPILER:",
                                         "CMAKE_GENERATOR:", "CMAKE_GENERATOR_TOOLSET:", "wxWidgets_DIR:"))]
            break
    if sys.platform != "win32":
        report["cpu_info"] = next(line for line in Path("/proc/cpuinfo").read_text().splitlines()
                                  if line.startswith("model name"))
        report["mem_total"] = Path("/proc/meminfo").read_text().splitlines()[0]
        report["os_release"] = Path("/etc/os-release").read_text()
    for kind, count, cases in (("startup", args.startup_count, [None, samples["small"]]),
                               ("idle", args.idle_count, [None, samples["small"]]),
                               ("operations", args.operation_count, [samples["10MiB"], samples["100MiB"]])):
        for sample in cases:
            for run in range(1, count + 1):
                row = observe(executable, output, kind, sample, run, args.owned_topmost)
                report["rows"].append(row)
                print(kind, row["case"], run, row["status"], row.get("error", ""), flush=True)
                report["summary"] = summarize(report["rows"])
                (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
                if row["status"] != "ok":
                    raise SystemExit("Batch stopped on failed observation; raw row retained")
    raise SystemExit(any(row["status"] != "ok" for row in report["rows"]))


if __name__ == "__main__":
    main()
