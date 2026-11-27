"""Multi-file startup through real packaged processes, documents and settings."""

import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from contextlib import contextmanager, nullcontext
from pathlib import Path

from localization_native import Dialogs
from localization_package import Child, Process, bundle_application, environment, menu_id, settings
from resource_process import wait_state
from startup_options_process import start, unreadable


def children(root, count):
    # Match wait_child: each next independent child has its own discovery
    # deadline. There is no aggregate 15-second startup budget for 32 peers.
    deadline = time.monotonic() + 15
    started = time.monotonic()
    result = []
    seen = set()
    while len(result) < count and time.monotonic() < deadline:
        folders = [path for path in root.iterdir() if path.name.isdigit() and (path / "state").exists()]
        assert len(folders) <= count, f"Expected {count} editors, found an extra process: {folders}"
        for folder in folders:
            if folder.name in seen:
                continue
            app = Process.__new__(Process)
            app.folder = folder
            app.sequence = 0
            app.process = Child(int(folder.name))
            app.state = wait_state(folder, app.process, lambda state: state["shown"] == "1")
            result.append(app)
            seen.add(folder.name)
            deadline = time.monotonic() + 15
            if count > 2:
                print(
                    f"Discovered child {len(result)}/{count} at {time.monotonic() - started:.3f}s: PID {app.process.pid}",
                    flush=True,
                )
        if len(result) == count:
            return result
        time.sleep(0.01)
    raise AssertionError(f"Expected {count} independent documents, found {folders}")


@contextmanager
def launch_list(executable, folder, working, arguments, expected, language="en", overrides=None):
    folder.mkdir(parents=True)
    observed = folder / "children"
    observed.mkdir()
    apps = []
    env = environment(executable, observed, language)
    env.update(overrides or {})
    previous_pids = package_pids(executable)
    with (folder / "parent.log").open("wb") as log:
        parent = subprocess.Popen([str(executable), *arguments], cwd=working, env=env, stdout=log, stderr=log)
        try:
            if expected > 1:
                assert parent.wait(timeout=5) == 0, "Dispatcher exits without an empty editor"
            apps = children(observed, expected)
            for app in apps:
                if app.process.pid == parent.pid:
                    app.process = parent
            assert len({app.process.pid for app in apps}) == expected
            if expected == 1:
                assert apps[0].process.pid == parent.pid and parent.poll() is None
            else:
                assert all(app.process.pid != parent.pid for app in apps)
            yield apps
            for app in apps:
                app.close()
            assert parent.wait(timeout=5) == 0
            assert len(list(observed.iterdir())) == expected, "No extra blank editor"
        finally:
            for app in apps:
                app.stop()
            for pid in package_pids(executable) - previous_pids - {parent.pid}:
                peer = Child(pid)
                peer.terminate()
                peer.wait(timeout=5)
            if parent.poll() is None:
                parent.terminate()
                parent.wait(timeout=5)


def counts(executable, root):
    settings(executable.parent / "Notepad4.ini", 1033)
    reference = json.loads(Path(__file__).with_name("startup_multifile_reference.json").read_text(encoding="utf-8"))
    for expected in reference["explicit_counts"]:
        count = expected["input"]
        folder = root / f"count-{count}"
        working = root / f"caller-{count} 中文😀"
        working.mkdir()
        targets = [working / f"file {index} 中文😀.txt" for index in range(count)]
        data = [f"document {index} 中文😀\r\n".encode() for index in range(count)]
        for target, content in zip(targets, data, strict=True):
            target.write_bytes(content)
        with launch_list(
            executable, folder, working, ["+", *(target.name for target in targets)], expected["editors"]
        ) as apps:
            if count == 0:
                assert apps[0].state["document_path"] == "" and apps[0].state["length"] == "0"
            else:
                actual = {Path(app.state["document_path"]): app for app in apps}
                assert set(actual) == set(targets[:32]), actual
                for index, target in enumerate(targets[:32]):
                    app = actual[target]
                    assert app.state["encoding"] == "UTF-8" and app.state["length"] == str(len(data[index]))
                    saved = folder / f"saved-{index}.txt"
                    app.send("save", str(saved))
                    assert saved.read_bytes() == data[index]
        print(
            f"PASS: original {count} input paths -> {expected['editors']} editors, exact saved text and process exits",
            flush=True,
        )


def routing_matrix(executable, root, language, config=None, overrides=None):
    preference = {"en": 1033, "zh_CN": 2052, "zh_TW": 1028}[language]
    reference = json.loads(Path(__file__).with_name("startup_multifile_reference.json").read_text(encoding="utf-8"))
    expected = next(doc for doc in reference["options"]["documents"] if "file 0 " in doc["filename"])
    for default in (False, True):
        for reuse, single in ((0, 0), (0, 1), (1, 0), (1, 1)):
            folder = root / f"matrix-{int(default)}-{reuse}-{single}"
            folder.mkdir()
            config = config or executable.parent / "Notepad4.ini"
            settings(config, preference, "WordWrap=0\n")
            config.write_text(
                config.read_text(encoding="utf-16")
                + f"MultiFileArg={int(default)}\nReuseWindow={reuse}\nSingleFileInstance={single}\n",
                encoding="utf-16",
                newline="\r\n",
            )
            before_config = config.read_bytes()
            targets = [folder / "first space 中文😀.txt", folder / "second.txt"]
            original = b"first 0\r\ncaf\xe9\r\n"
            for target in targets:
                target.write_bytes(original)
            keeper_environment = environment(executable, folder / "unused", language)
            keeper_environment.update(overrides or {})
            keeper = start(
                executable,
                folder / "existing",
                keeper_environment,
                startup=targets[0],
                arguments=("-n", "-e", "cp1252"),
                cwd=folder,
            )
            before = dict(keeper.state)
            try:
                arguments = ["-rs", "-e", "cp1252", "-s", "json", "-ro", "-g", "2,2"]
                arguments += ([] if default else ["+"]) + [target.name for target in targets]
                with launch_list(executable, folder / "incoming", folder, arguments, 2, language, overrides) as apps:
                    assert {Path(app.state["document_path"]) for app in apps} == set(targets)
                    assert all(app.process.pid != keeper.process.pid for app in apps)
                    for index, app in enumerate(apps):
                        assert app.state["encoding"] == "CP1252" and app.state["length"] == str(
                            len(bytes.fromhex(expected["hex"]))
                        )
                        assert app.state["read_only"] == str(expected["readonly"])
                        assert app.state["caret_position"] == str(expected["caret"])
                        assert "JSON" in app.state["document_status"], app.state
                        assert (
                            app.state["menu_top.0.label"]
                            == {"en": "&File", "zh_CN": "文件(&F)", "zh_TW": "檔案(&F)"}[language]
                        )
                        app.choose(menu_id(app.state, "UTF-8"))
                        saved = folder / f"utf8-{index}.txt"
                        app.send("save", str(saved))
                        assert saved.read_bytes() == bytes.fromhex(expected["hex"]), app.state
                    keeper.send("snapshot")
                    for field in ("document_path", "length", "encoding", "caret_position", "read_only", "dirty"):
                        assert keeper.state[field] == before[field], (field, before, keeper.state)
                    assert all(target.read_bytes() == original for target in targets)
                keeper.close()
            finally:
                keeper.stop()
            assert config.read_bytes() == before_config, (
                "Temporary overrides preserve routing and MultiFileArg preferences",
                before_config.decode("utf-16"),
                config.read_text(encoding="utf-16"),
            )
    print(
        f"PASS: {language} source/text/format/scheme/readonly/caret across both modes and all routing combinations",
        flush=True,
    )


def single_and_duplicates(executable, root):
    config = executable.parent / "Notepad4.ini"
    settings(config, 1033)
    config.write_text(
        config.read_text(encoding="utf-16") + "MultiFileArg=1\nReuseWindow=1\nSingleFileInstance=1\n",
        encoding="utf-16",
        newline="\r\n",
    )
    before_config = config.read_bytes()
    target = root / "same 中文😀.txt"
    original = b"first\r\nsecond\r\n"
    target.write_bytes(original)
    keeper = Process(executable, root / "keeper", environment(executable, root / "unused"), startup=target, cwd=root)
    try:
        before = dict(keeper.state)
        observed = root / "one-child"
        observed.mkdir()
        with (root / "one.log").open("wb") as log:
            parent = subprocess.Popen(
                [str(executable), "-ro", "-g", "2,2", "+", target.name],
                cwd=root,
                env=environment(executable, observed),
                stdout=log,
                stderr=log,
            )
            try:
                assert parent.wait(timeout=5) == 0, "A single target keeps normal Single File Instance routing"
                assert not list(observed.iterdir()), "A single same-file request must not force a new window"
                keeper.send("snapshot")
                for field in ("document_path", "length", "caret_position", "read_only", "encoding", "dirty"):
                    assert keeper.state[field] == before[field], (field, before, keeper.state)
            finally:
                if parent.poll() is None:
                    parent.terminate()
                    parent.wait(timeout=5)
        with launch_list(executable, root / "duplicates", root, ["-rs", "+", target.name, target.name], 2) as apps:
            for index, app in enumerate(apps):
                assert Path(app.state["document_path"]) == target
                assert app.process.pid != keeper.process.pid
                saved = root / f"duplicate-{index}.txt"
                app.send("save", str(saved))
                assert saved.read_bytes() == original
        keeper.close()
    finally:
        keeper.stop()
    for marker in ("-", "--"):
        path = root / f"-ro forced {len(marker)} 中文😀.txt"
        path.write_bytes(original)
        with launch_list(
            executable, root / f"single-{len(marker)}", root, ["-n", marker, *path.name.split(" ")], 1
        ) as apps:
            app = apps[0]
            assert Path(app.state["document_path"]) == path
            assert app.state["read_only"] == "0" and app.state["length"] == str(len(original))
    switch = root / "-ro"
    switch.write_bytes(original)
    with launch_list(executable, root / "switch-list", root, ["+", switch.name, target.name], 2) as apps:
        assert {Path(app.state["document_path"]) for app in apps} == {switch, target}
        assert all(app.state["read_only"] == "0" for app in apps)
    assert config.read_bytes() == before_config
    print(
        "PASS: one-target normal routing, independent duplicates, -/-- joined tails and option-like list paths",
        flush=True,
    )


def installed(executable, root):
    if sys.platform == "win32":
        local = ctypes.create_unicode_buffer(32768)
        csidl_local_appdata = 0x1C
        assert ctypes.windll.shell32.SHGetFolderPathW(None, csidl_local_appdata, None, 0, local) == 0
        config = Path(local.value) / "Notepad4/Notepad4.ini"
        assert not config.exists(), "Installed Windows validation requires the coordinator's owned empty VM profile"
        owned_parent = not config.parent.exists()
        config.parent.mkdir(parents=True, exist_ok=True)
        parent = root
        prefix_name = "WinGet-multifile-"
    else:
        parent = Path("/opt/wxnotepad-file-workflows-test")
        assert parent.is_dir() and os.access(parent, os.W_OK)
        prefix_name = "multifile-"
    try:
        with tempfile.TemporaryDirectory(prefix=prefix_name, dir=parent) as name:
            prefix = Path(name)
            application = prefix / "bin" / executable.name
            shutil.copytree(executable.parent, application.parent, ignore=shutil.ignore_patterns("Notepad4.ini"))
            if sys.platform != "win32":
                (prefix / "share").mkdir()
                (application.parent / "locale").rename(prefix / "share/locale")
            for language in ("en", "zh_CN", "zh_TW"):
                folder = root / language
                folder.mkdir()
                overrides = {}
                if sys.platform != "win32":
                    config_home = folder / "config"
                    config = config_home / "Notepad4/Notepad4.ini"
                    config.parent.mkdir(parents=True)
                    overrides["XDG_CONFIG_HOME"] = str(config_home)
                routing_matrix(application, folder, language, config, overrides)
                assert not (application.parent / "Notepad4.ini").exists()
        print(
            "PASS: real installed configuration, both collection modes and all routing/language combinations",
            flush=True,
        )
    finally:
        if sys.platform == "win32":
            config.unlink(missing_ok=True)
            if owned_parent:
                config.parent.rmdir()


def two_files(executable, root, default=False):
    config = executable.parent / "Notepad4.ini"
    settings(config, 1033)
    if default:
        config.write_text(config.read_text(encoding="utf-16") + "MultiFileArg=1\n", encoding="utf-16")
    working = root / "caller 中文😀"
    working.mkdir()
    targets = [working / "first space.txt", working / "中文😀.txt"]
    data = [b"first\r\n", "second 中文😀\r\n".encode()]
    for target, content in zip(targets, data, strict=True):
        target.write_bytes(content)
    observed = root / "children"
    observed.mkdir()
    apps = []
    with (root / "parent.log").open("wb") as log:
        parent = subprocess.Popen(
            [str(executable), *([] if default else ["+"]), targets[0].name, str(targets[1])],
            cwd=working,
            env=environment(executable, observed),
            stdout=log,
            stderr=log,
        )
        try:
            assert parent.wait(timeout=5) == 0, "Multi-file dispatcher exits without an empty editor"
            apps = children(observed, 2)
            by_path = {Path(app.state["document_path"]): app for app in apps}
            assert set(by_path) == set(targets), by_path
            for index, (target, content) in enumerate(zip(targets, data, strict=True)):
                app = by_path[target]
                assert app.state["length"] == str(len(content)), app.state
                saved = root / f"saved-{index}.txt"
                app.send("save", str(saved))
                assert saved.read_bytes() == content
                app.close()
        finally:
            for app in apps:
                app.stop()
            if parent.poll() is None:
                parent.terminate()
                parent.wait(timeout=5)
    print("PASS: + dispatch, caller-relative Unicode paths, independent text and parent exit", flush=True)


def package_pids(executable):
    """Find only native processes running this exact disposable package."""
    pids = set()
    if sys.platform != "win32":
        for entry in Path("/proc").iterdir():
            if not entry.name.isdigit():
                continue
            try:
                if (entry / "exe").resolve(strict=True) == executable:
                    pids.add(int(entry.name))
            except FileNotFoundError, PermissionError:
                pass
    else:
        from ctypes import wintypes

        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.QueryFullProcessImageNameW.argtypes = [
            wintypes.HANDLE,
            wintypes.DWORD,
            wintypes.LPWSTR,
            ctypes.POINTER(wintypes.DWORD),
        ]

        user = ctypes.WinDLL("user32", use_last_error=True)
        callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
        user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]

        @callback
        def visit(window, unused):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(window, ctypes.byref(pid))
            handle = kernel.OpenProcess(0x1000, False, pid.value)
            if handle:
                try:
                    path = ctypes.create_unicode_buffer(32768)
                    size = wintypes.DWORD(len(path))
                    if (
                        kernel.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(size))
                        and Path(path.value) == executable
                    ):
                        pids.add(pid.value)
                finally:
                    kernel.CloseHandle(handle)
            return True

        user.EnumWindows(visit, 0)
    return pids


def pending_peer(dialogs, executable, exclude):
    """Identify only a visible peer belonging to this disposable package."""
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        pids = package_pids(executable)
        pending = pids - set(exclude)
        if len(pending) == 1:
            return Child(pending.pop())
        time.sleep(0.03)
    raise AssertionError(f"Expected one owned pending startup peer, found {pids}")


def partial_failures(executable, root, directory_only=False):
    settings(executable.parent / "Notepad4.ini", 1033)
    dialogs = Dialogs(root / "dialogs")
    choices = ("directory",) if directory_only else ("yes", "no", "cancel", "unreadable")
    for choice in choices:
        folder = root / choice
        folder.mkdir()
        observed = folder / "children"
        observed.mkdir()
        good = folder / "good.txt"
        original = b"first\r\nsecond\r\n"
        good.write_bytes(original)
        bad = folder / ("directory 中文😀" if choice == "directory" else "missing 中文😀.txt")
        if choice == "directory":
            bad.mkdir()
        elif choice == "unreadable":
            bad.write_bytes(original)
        good_app = bad_app = peer = None
        with (folder / "parent.log").open("wb") as log, unreadable(bad) if choice == "unreadable" else nullcontext():
            parent = subprocess.Popen(
                [str(executable), "-s", "json", "-ro", "+", good.name, bad.name],
                cwd=folder,
                env=environment(executable, observed),
                stdout=log,
                stderr=log,
            )
            try:
                assert parent.wait(timeout=5) == 0
                good_app = children(observed, 1)[0]
                assert Path(good_app.state["document_path"]) == good
                peer = pending_peer(dialogs, executable, [good_app.process.pid])
                title = (
                    "Open file"
                    if choice == "directory"
                    else "Could not open file"
                    if choice == "unreadable"
                    else "Create file?"
                )
                message = (
                    None
                    if choice == "directory"
                    else "Could not open the file for reading."
                    if choice == "unreadable"
                    else bad.name
                )
                window = dialogs.wait(peer, title, message=message)
                if choice == "directory":
                    assert bad.name in "\n".join(dialogs.texts(window)), dialogs.texts(window)
                    dialogs.cancel(window)
                else:
                    dialogs.answer(peer, "ok" if choice == "unreadable" else choice, window=window)
                if choice == "cancel":
                    assert peer.wait(timeout=10) == 0
                    assert not (observed / str(peer.pid) / "state").exists()
                else:
                    bad_app = Process.__new__(Process)
                    bad_app.folder = observed / str(peer.pid)
                    bad_app.sequence = 0
                    bad_app.process = peer
                    bad_app.state = wait_state(bad_app.folder, peer, lambda state: state["shown"] == "1")
                    assert bad_app.state["length"] == "0" and bad_app.state["dirty"] == "0"
                    assert bad_app.state["document_path"] == (str(bad) if choice == "yes" else "")
                    assert bad_app.state["read_only"] == ("1" if choice == "yes" else "0")
                    assert "JSON" in bad_app.state["document_status"]
                    bad_app.close()
                saved = folder / "good-saved.txt"
                good_app.send("save", str(saved))
                assert saved.read_bytes() == original and good.read_bytes() == original
                good_app.close()
                if choice == "yes":
                    assert bad.read_bytes() == b""
                elif choice in ("no", "cancel"):
                    assert not bad.exists()
            finally:
                for app in (bad_app, good_app):
                    if app:
                        app.stop()
                if peer and peer.poll() is None:
                    peer.terminate()
                    peer.wait(timeout=5)
                if parent.poll() is None:
                    parent.terminate()
                    parent.wait(timeout=5)
        print(
            f"PASS: independent {choice} startup path preserves the other actual document and process lifecycle",
            flush=True,
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake-dir", type=Path)
    parser.add_argument(
        "--packaged", action="store_true", help="Use an already bundled executable (owned VM validation)"
    )
    parser.add_argument(
        "--scenario",
        choices=("two", "default", "counts", "matrix", "single", "installed", "directory", "failures"),
        default="two",
    )
    parser.add_argument("--language", choices=("en", "zh_CN", "zh_TW"), default="en")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-multifile-") as name:
        root = Path(name).resolve()
        if args.packaged:
            executable = args.executable.resolve()
        else:
            if not args.cmake_dir:
                parser.error("--cmake-dir is required unless --packaged is used")
            executable = bundle_application(args.executable.resolve(), args.cmake_dir.resolve(), root / "bundle")
        if args.scenario == "counts":
            counts(executable, root)
        elif args.scenario == "matrix":
            routing_matrix(executable, root, args.language)
        elif args.scenario == "single":
            single_and_duplicates(executable, root)
        elif args.scenario == "installed":
            installed(executable, root)
        elif args.scenario in ("directory", "failures"):
            partial_failures(executable, root, directory_only=args.scenario == "directory")
        else:
            two_files(executable, root, default=args.scenario == "default")


if __name__ == "__main__":
    main()
