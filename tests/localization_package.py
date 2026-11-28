"""Real artifact bundling and the existing independent shown-app observer seam."""

import configparser
import ctypes
import importlib.util
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

from resource_process import command, observation_context, wait_state
from settings_process import Application

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from cmake_cache import read_cmake_cache as read_cmake_cache  # noqa: E402 -- Initialize tools, then re-export for callers.


def bundle_application(source, cmake, folder, reports=None):
    """Feed actual build outputs through the distribution's real artifact bundler."""
    spec = importlib.util.spec_from_file_location("localization_ci", ROOT / ".github/scripts/ci.py")
    ci = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ci)
    executable = ci.bundle(cmake, source, folder, report_dir=reports)
    package = executable.parent
    for code in ("zh_CN", "zh_TW"):
        for domain in ("wxnotepad", "wxstd"):
            relative = Path("locale") / code / "LC_MESSAGES" / (domain + ".mo")
            assert (package / relative).read_bytes() == (source.parent / relative).read_bytes(), relative
    assert not (package / "locale/en").exists(), "Source English needs no MO"
    return executable


def settings(path, language, extra="", save=False):
    language_line = "" if language is None else f"UILanguage={language}\n"
    text = (
        f"[Settings]\nSaveSettings={int(save)}\nWarnLineEndings=0\nUnrelatedSetting=keep\n"
        + extra
        + "\n[Settings2]\n"
        + language_line
        + "UnrelatedLanguage=preserved\n"
    )
    path.write_text(text, encoding="utf-16", newline="\r\n")
    return path.read_bytes()


def read_settings(path):
    config = configparser.ConfigParser(interpolation=None)
    config.read(path, encoding="utf-16")
    return config


def menus(state):
    result = {}
    for key, value in state.items():
        if key.startswith("menu."):
            _, identity, field = key.split(".", 2)
            result.setdefault(int(identity), {})[field] = value
    return result


def menu_id(state, caption):
    found = [
        identity
        for identity, item in menus(state).items()
        if item["label"].split("\t", 1)[0].replace("&", "") == caption
    ]
    assert len(found) == 1, (caption, found)
    return found[0]


def language_id(state, preference):
    if preference != 1024:
        return menu_id(state, {1033: "English", 2052: "中文(简体)", 1028: "中文(繁體)"}[preference])
    english = menus(state)[menu_id(state, "English")]["path"]
    system_path = english.rsplit("/", 1)[0] + "/0"
    return next(identity for identity, item in menus(state).items() if item["path"] == system_path)


def _linux_startup_resources(contexts):
    def read(path):
        try:
            return path.read_text(encoding="utf-8")
        except OSError as error:
            return f"unavailable: {error!r}"

    for context in contexts:
        context["linux"] = {name: read(Path("/proc") / str(context["pid"]) / name) for name in ("stat", "wchan")}
    try:
        cpu_count = os.process_cpu_count() or "unavailable: no available CPU count"
    except OSError as error:
        cpu_count = f"unavailable: {error!r}"
    try:
        affinity = sorted(os.sched_getaffinity(0))
    except OSError as error:
        affinity = f"unavailable: {error!r}"
    cgroup = read(Path("/proc/self/cgroup"))
    if cgroup.startswith("unavailable:"):
        quota = cgroup
    else:
        unified = next((line[3:] for line in cgroup.splitlines() if line.startswith("0::")), None)
        quota = (
            read(Path("/sys/fs/cgroup") / unified.lstrip("/") / "cpu.max")
            if unified is not None
            else "unavailable: no unified cgroup in /proc/self/cgroup"
        )
    return {
        "available_cpu_count": cpu_count,
        "affinity": affinity,
        "loadavg": read(Path("/proc/loadavg")),
        "cgroup_cpu_max": quota,
        "cpu_pressure": read(Path("/proc/pressure/cpu")),
        "io_pressure": read(Path("/proc/pressure/io")),
    }


def _retain_startup_failure(error, report, processes, log_path):
    try:
        report.mkdir(parents=True, exist_ok=True)
        if log_path.parent != report:
            shutil.copy2(log_path, report / log_path.name)
        contexts = [observation_context(folder, process) for folder, process in processes]
        host = _linux_startup_resources(contexts) if sys.platform == "linux" else None
        (report / "startup-failure.json").write_text(json.dumps(contexts, indent=2), encoding="utf-8")
        if host is not None:
            (report / "startup-host.json").write_text(json.dumps(host, indent=2), encoding="utf-8")
    except Exception as capture_error:
        error.add_note(f"Startup failure capture: {capture_error!r}")


class Process(Application):
    def __init__(self, executable, folder, environment, startup=None, arguments=(), cwd=None, log_path=None):
        self.folder = folder
        self.folder.mkdir(parents=True, exist_ok=True)
        self.sequence = 0
        (folder / "state").unlink(missing_ok=True)
        args = [str(executable), "--observe", str(folder), *arguments]
        if startup is not None:
            args += ["--startup-file", str(startup)]
        log_path = log_path if log_path is not None else folder / "stderr.log"
        log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log = log_path.open("wb")
        try:
            self.process = subprocess.Popen(args, env=environment, cwd=cwd, stdout=self.log, stderr=self.log)
            self.state = wait_state(
                folder,
                self.process,
                lambda state: (
                    state["sequence"] == "0" and (state["backend"] != "GdkWaylandDisplay" or state["active"] == "1")
                ),
            )
            assert self.state["shown"] == "1", self.state
        except BaseException as error:
            if hasattr(self, "process") and "window-routing-settings" in log_path.parts and folder.name == "app-002":
                _retain_startup_failure(error, log_path.parent, [(folder, self.process)], log_path)
            try:
                if hasattr(self, "process"):
                    self.stop()
                else:
                    self.log.close()
            except Exception as cleanup_error:
                error.add_note(f"Startup failure cleanup: {cleanup_error!r}")
            raise

    def begin(self, operation, source=""):
        self.sequence += 1
        command(self.folder, f"{self.sequence}\n{operation}\n{source}\n")

    def finish(self):
        self.state = wait_state(self.folder, self.process, lambda state: state["sequence"] == str(self.sequence))
        assert self.state["result"] == "ok", self.state
        return self.state

    def send(self, operation, source=""):
        self.begin(operation, source)
        return self.finish()

    def choose(self, identity):
        return self.send("menu", str(identity))

    def stop(self):
        try:
            super().stop()
        finally:
            if hasattr(self, "log"):
                self.log.close()


class Child:
    def __init__(self, pid):
        self.pid = pid
        self.returncode = None
        self._handle = None
        if sys.platform == "win32":
            self._kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            self._kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            self._kernel.OpenProcess.restype = wintypes.HANDLE
            self._kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
            self._kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
            self._kernel.WaitForSingleObject.restype = wintypes.DWORD
            self._kernel.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
            self._kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            process_query_limited_information = 0x1000
            process_synchronize = 0x100000
            process_terminate = 0x0001
            self._handle = self._kernel.OpenProcess(
                process_query_limited_information | process_synchronize | process_terminate, False, pid
            )
            if not self._handle:
                raise ctypes.WinError(ctypes.get_last_error())

    def _close_handle(self):
        if self._handle:
            self._kernel.CloseHandle(self._handle)
            self._handle = None

    def __del__(self):
        self._close_handle()

    def poll(self):
        if self.returncode is not None:
            return self.returncode
        if sys.platform == "win32":
            wait_object_0 = 0
            wait_timeout = 258
            state = self._kernel.WaitForSingleObject(self._handle, 0)
            if state == wait_timeout:
                return None
            if state != wait_object_0:
                raise ctypes.WinError(ctypes.get_last_error())
            # Windows can publish the exit code before DLL teardown finishes.
            # Match Popen: cache a result only after this process is signaled.
            code = wintypes.DWORD()
            assert self._kernel.GetExitCodeProcess(self._handle, ctypes.byref(code))
            self.returncode = code.value
            self._close_handle()
        else:
            try:
                os.kill(self.pid, 0)
            except ProcessLookupError:
                self.returncode = 0
        return self.returncode

    def terminate(self):
        if sys.platform == "win32":
            if self.poll() is not None:
                return
            if not self._kernel.TerminateProcess(self._handle, 1):
                error = ctypes.get_last_error()
                # As with Popen, termination may race a child already exiting.
                code = wintypes.DWORD()
                still_active = 259
                if not self._kernel.GetExitCodeProcess(self._handle, ctypes.byref(code)) or code.value == still_active:
                    raise ctypes.WinError(error)
            return
        os.kill(self.pid, signal.SIGTERM)

    def wait(self, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.poll() is not None:
                return self.returncode
            time.sleep(0.01)
        raise subprocess.TimeoutExpired(f"child {self.pid}", timeout)


def wait_child(root, previous=()):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        for path in root.iterdir():
            if path.name not in previous and path.name.isdigit() and (path / "state").exists():
                app = Process.__new__(Process)
                app.folder = path
                app.sequence = 0
                app.process = Child(int(path.name))
                # Match Process startup: the first Wayland snapshot can precede
                # the native decoration/layout configuration.
                app.state = wait_state(
                    path,
                    app.process,
                    lambda state: (
                        state["sequence"] == "0" and (state["backend"] != "GdkWaylandDisplay" or state["active"] == "1")
                    ),
                )
                return app
        time.sleep(0.01)
    raise AssertionError("Restart/New Window did not produce an observed independent child")


def environment(package, root, preferred="en"):
    result = dict(os.environ, WXLANGUAGE=preferred, WXNOTEPAD_OBSERVE_ROOT=str(root))
    # The process must find resources in the delivered package, independently of
    # the build tree, working directory and document directory.
    if sys.platform != "win32":
        result["LD_LIBRARY_PATH"] = str(package.parent / "lib")
        result["GIO_LAUNCH_DESKTOP"] = str(package.parent / "libexec/gio-launch-desktop")
        result["LC_ALL"] = "C.UTF-8"
        result.pop("NO_AT_BRIDGE", None)
    return result
