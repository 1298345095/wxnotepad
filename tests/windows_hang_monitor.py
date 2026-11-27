"""Capture the stalled Character Info child before its unchanged CTest timeout."""

import argparse
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

JOB_OBJECT_EXTENDED_LIMIT_INFORMATION = 9
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000
PROCESS_TERMINATE = 0x0001
PROCESS_VM_READ = 0x0010
PROCESS_SET_QUOTA = 0x0100
PROCESS_QUERY_INFORMATION = 0x0400
MINIDUMP_WITH_UNLOADED_MODULES = 0x0020
MINIDUMP_WITH_INDIRECTLY_REFERENCED_MEMORY = 0x0040
MINIDUMP_WITH_THREAD_INFO = 0x1000
LOAD_LIBRARY_SEARCH_SYSTEM32 = 0x0800
DEFAULT_DUMP_AFTER_SECONDS = 45


class JobBasicLimits(ctypes.Structure):
    _fields_ = [
        ("PerProcessUserTimeLimit", ctypes.c_longlong),
        ("PerJobUserTimeLimit", ctypes.c_longlong),
        ("LimitFlags", wintypes.DWORD),
        ("MinimumWorkingSetSize", ctypes.c_size_t),
        ("MaximumWorkingSetSize", ctypes.c_size_t),
        ("ActiveProcessLimit", wintypes.DWORD),
        ("Affinity", ctypes.c_size_t),
        ("PriorityClass", wintypes.DWORD),
        ("SchedulingClass", wintypes.DWORD),
    ]


class IoCounters(ctypes.Structure):
    _fields_ = [
        (name, ctypes.c_ulonglong)
        for name in (
            "ReadOperationCount",
            "WriteOperationCount",
            "OtherOperationCount",
            "ReadTransferCount",
            "WriteTransferCount",
            "OtherTransferCount",
        )
    ]


class JobExtendedLimits(ctypes.Structure):
    _fields_ = [
        ("BasicLimitInformation", JobBasicLimits),
        ("IoInfo", IoCounters),
        ("ProcessMemoryLimit", ctypes.c_size_t),
        ("JobMemoryLimit", ctypes.c_size_t),
        ("PeakProcessMemoryUsed", ctypes.c_size_t),
        ("PeakJobMemoryUsed", ctypes.c_size_t),
    ]


class OwnedChildJob:
    def __init__(self):
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        self.kernel.CreateJobObjectW.restype = wintypes.HANDLE
        self.kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        self.kernel.SetInformationJobObject.restype = wintypes.BOOL
        self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        self.kernel.AssignProcessToJobObject.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.process_handle = None
        self.handle = self.kernel.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        limits = JobExtendedLimits()
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.kernel.SetInformationJobObject(
            self.handle, JOB_OBJECT_EXTENDED_LIMIT_INFORMATION, ctypes.byref(limits), ctypes.sizeof(limits)
        ):
            error = ctypes.WinError(ctypes.get_last_error())
            self.close()
            raise error

    def assign(self, process):
        rights = PROCESS_TERMINATE | PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
        self.process_handle = self.kernel.OpenProcess(rights, False, process.pid)
        if not self.process_handle:
            error = ctypes.WinError(ctypes.get_last_error())
            if process.poll() is not None:
                return
            raise error
        if not self.kernel.AssignProcessToJobObject(self.handle, self.process_handle):
            # A child may have completed before assignment. Preserve that exit.
            error = ctypes.WinError(ctypes.get_last_error())
            if process.poll() is not None:
                return
            raise error

    def close(self):
        # This non-inherited handle also closes on hard CTest termination,
        # terminating the owned child and any descendants without an orphan.
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None
        if self.process_handle:
            self.kernel.CloseHandle(self.process_handle)
            self.process_handle = None


def capture_dump(process, process_handle, directory, command, after_seconds):
    import msvcrt

    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"character-info-{process.pid}-{time.time_ns()}.dmp"
    metadata = {
        "process_id": process.pid,
        "monitor_process_id": os.getpid(),
        "command": command,
        "cwd": str(Path.cwd()),
        "dump_after_seconds": after_seconds,
        "dump": str(path),
        "status": "capturing",
    }
    record = path.with_suffix(".json")
    record.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"Character Info hang capture: pid={process.pid} dump={path}", flush=True)
    try:
        dbghelp = ctypes.WinDLL("dbghelp", use_last_error=True, winmode=LOAD_LIBRARY_SEARCH_SYSTEM32)
        dbghelp.MiniDumpWriteDump.argtypes = [
            wintypes.HANDLE,
            wintypes.DWORD,
            wintypes.HANDLE,
            wintypes.DWORD,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
        ]
        dbghelp.MiniDumpWriteDump.restype = wintypes.BOOL
        flags = MINIDUMP_WITH_UNLOADED_MODULES | MINIDUMP_WITH_INDIRECTLY_REFERENCED_MEMORY | MINIDUMP_WITH_THREAD_INFO
        with path.open("xb") as output:
            if not dbghelp.MiniDumpWriteDump(
                process_handle, process.pid, msvcrt.get_osfhandle(output.fileno()), flags, None, None, None
            ):
                raise ctypes.WinError(ctypes.get_last_error())
        metadata.update(status="captured", bytes=path.stat().st_size)
    except Exception as error:
        metadata.update(status="failed", error=str(error))
        raise
    finally:
        record.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(f"Character Info hang capture completed: bytes={metadata['bytes']}", flush=True)


def run(command, directory, after_seconds):
    job = OwnedChildJob()
    process = None
    try:
        # Inherit the registered working directory, arguments and output streams.
        started = time.monotonic()
        process = subprocess.Popen(command, stdout=sys.stdout, stderr=sys.stderr)
        job.assign(process)
        print(f"Character Info monitor owns pid={process.pid}", flush=True)
        try:
            return process.wait(timeout=max(0, after_seconds - (time.monotonic() - started)))
        except subprocess.TimeoutExpired:
            capture_failed = False
            try:
                capture_dump(process, job.process_handle, directory, command, after_seconds)
            except Exception as error:
                print(f"FAIL: Character Info hang capture: {error}", file=sys.stderr, flush=True)
                capture_failed = True
            # Let CTest enforce the existing timeout without altering the child input.
            result = process.wait()
            return result if result else int(capture_failed)
    finally:
        job.close()
        if process is not None:
            if process.poll() is None:
                process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump-directory", required=True, type=Path)
    parser.add_argument("--dump-after", type=float, default=DEFAULT_DUMP_AFTER_SECONDS)
    parser.add_argument("program", nargs=argparse.REMAINDER)
    arguments = parser.parse_args()
    command = arguments.program[1:] if arguments.program[:1] == ["--"] else arguments.program
    if not command or not math.isfinite(arguments.dump_after) or arguments.dump_after <= 0:
        parser.error("provide a child command and a positive finite dump delay")
    if sys.platform != "win32":
        parser.error("Character Info hang capture requires Windows")
    return run(command, arguments.dump_directory.resolve(), arguments.dump_after)


if __name__ == "__main__":
    # Preserve the DWORD bits even when sys.exit converts through a signed C long.
    sys.exit(ctypes.c_int32(main()).value)
