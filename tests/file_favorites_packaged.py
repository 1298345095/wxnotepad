"""Exercise File Favorites through the packaged application's public menu seam."""

import argparse
import ctypes
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from localization_layout import capture_and_check, owned_topmost
from localization_native import Dialogs, plain
from localization_package import Process, bundle_application, environment, menu_id, menus, settings
from resource_process import native_edit, process_windows, wait_state

CAPTIONS = {
    1033: (
        "en",
        "&Open Favorites...\tAlt+I",
        "&Add Current File...\tAlt+K",
        "&Manage...\tAlt+F9",
        "Favorites",
        "Add to Favorites",
        "Favorite name",
    ),
    2052: (
        "zh_CN",
        "打开收藏夹(&O)...\tAlt+I",
        "添加当前文件(&A)...\tAlt+K",
        "管理(&M)...\tAlt+F9",
        "收藏夹",
        "添加到收藏夹",
        "收藏名称",
    ),
    1028: (
        "zh_TW",
        "開啟我的最愛(&O)...\tAlt+I",
        "加入目前檔案(&A)...\tAlt+K",
        "管理(&M)...\tAlt+F9",
        "我的最愛",
        "加入我的最愛",
        "我的最愛名稱",
    ),
}


def wait_for(predicate, description):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if result := predicate():
            return result
        time.sleep(0.03)
    raise AssertionError(description)


def cancel_dialog(dialogs, window):
    captions = [plain(text) for text in dialogs.texts(window)]
    caption = next((text for text in ("Cancel", "取消") if text in captions), None)
    assert caption, captions
    dialogs.click(window, caption)


def accept_dialog(dialogs, window):
    captions = [plain(text) for text in dialogs.texts(window)]
    caption = next((text for text in ("OK", "确定", "確定", "確認") if text in captions), None)
    assert caption, captions
    dialogs.click(window, caption)


def default_name(dialogs, window, name_label, target):
    expected = target.name
    if sys.platform == "win32":
        import winreg

        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced"
        ) as key:
            hidden = winreg.QueryValueEx(key, "HideFileExt")[0]
        if hidden:
            expected = target.stem
        entries = []
        for child in dialogs.children(window):
            kind = ctypes.create_unicode_buffer(256)
            dialogs.user.GetClassNameW(child, kind, len(kind))
            if kind.value == "Edit":
                entries.append(child)
        assert len(entries) == 1, entries
        value = ctypes.create_unicode_buffer(1024)
        dialogs.user.SendMessageW(entries[0], 0xD, len(value), ctypes.addressof(value))
        observed = value.value
    else:
        entries = [
            child
            for child in dialogs.children(window)
            if child.get_role() in (dialogs.atspi.Role.TEXT, dialogs.atspi.Role.ENTRY)
            and child.get_name() == name_label
        ]
        assert len(entries) == 1, entries
        observed = dialogs.atspi.Text.get_text(entries[0], 0, -1)
    assert observed == expected, (observed, expected)


def shortcut(path, target=None):
    """Create/read an independent native WSH shortcut, or a POSIX symbolic link."""
    if sys.platform != "win32":
        if target is not None:
            path.symlink_to(target, target_is_directory=Path(target).is_dir())
        return os.readlink(path)
    script = (
        "$ErrorActionPreference='Stop';[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);"
        "$link=(New-Object -ComObject WScript.Shell).CreateShortcut($env:WXNOTEPAD_FAVORITE_LINK);"
        "if($env:WXNOTEPAD_FAVORITE_TARGET){$link.TargetPath=$env:WXNOTEPAD_FAVORITE_TARGET;$link.Save()};"
        "$link.TargetPath"
    )
    result = subprocess.run(
        ["pwsh", "-NoLogo", "-NoProfile", "-Command", script],
        env=dict(os.environ, WXNOTEPAD_FAVORITE_LINK=str(path), WXNOTEPAD_FAVORITE_TARGET=str(target or "")),
        capture_output=True,
        encoding="utf-8",
        check=True,
        timeout=10,
    )
    return result.stdout.strip()


def select_favorite(dialogs, app, window, index=0):
    if sys.platform == "win32":
        controls = []
        for child in dialogs.children(window):
            kind = ctypes.create_unicode_buffer(256)
            dialogs.user.GetClassNameW(child, kind, len(kind))
            if kind.value == "SysListView32":
                controls.append(child)
        assert len(controls) == 1, controls
        # Deliver the control's native navigation keys, preserving its real
        # selection notifications and the dialog's ordinary OK enablement.
        dialogs.user.SendMessageW(controls[0], 0x100, 0x24, 1)  # WM_KEYDOWN, VK_HOME
        for _ in range(index):
            dialogs.user.SendMessageW(controls[0], 0x100, 0x28, 1)  # VK_DOWN
        for child in dialogs.children(window):
            kind = ctypes.create_unicode_buffer(256)
            dialogs.user.GetClassNameW(child, kind, len(kind))
            if kind.value == "Button" and dialogs.title(child).replace("&", "") == "OK":
                # A failed resolution opens its message inside the button event.
                # Queue the native click so the external driver can answer it.
                assert dialogs.user.PostMessageW(child, 0xF5, 0, 0)
                return
        raise AssertionError("Favorites OK button")
    else:
        dialogs.key(app, "Home", window)
        for _ in range(index):
            dialogs.key(app, "Down", window)
        dialogs.key(app, "Return", window)


def choose_file(dialogs, app, window, target):
    if sys.platform == "win32":
        for child in dialogs.children(window):
            kind = ctypes.create_unicode_buffer(256)
            dialogs.user.GetClassNameW(child, kind, len(kind))
            if kind.value == "Edit":
                value = ctypes.create_unicode_buffer(str(target))
                dialogs.user.SendMessageW(child, 0xC, 0, ctypes.cast(value, ctypes.c_void_p).value)
                dialogs.user.PostMessageW(window, 0x111, 1, 0)
                return
        raise AssertionError("Native file chooser filename input")
    dialogs.key(app, "ctrl+l", window)
    entries = wait_for(
        lambda: [
            child
            for child in dialogs.children(window)
            if child.get_role() in (dialogs.atspi.Role.TEXT, dialogs.atspi.Role.ENTRY)
            and child.get_state_set().contains(dialogs.atspi.StateType.FOCUSED)
            and child.get_editable_text_iface()
        ],
        "Native file chooser location entry",
    )
    assert entries[0].get_editable_text_iface().set_text_contents(str(target))
    dialogs.click(window, "Open")


def favorite_key(dialogs, app, key):
    if sys.platform != "win32":
        dialogs.key(app, "alt+" + key)
        return
    user = dialogs.user
    user.GetAncestor.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    user.GetAncestor.restype = ctypes.c_void_p
    root = user.GetAncestor(int(app.state["editor_handle"]), 2)
    dialogs.activate(root)
    assert user.GetForegroundWindow() == root, "Native shortcut targets the owned foreground application"
    user.keybd_event.argtypes = [ctypes.c_ubyte, ctypes.c_ubyte, ctypes.c_uint, ctypes.c_size_t]
    virtual = 0x78 if key == "F9" else ord(key.upper())
    user.keybd_event(0x12, 0, 0, 0)
    try:
        user.keybd_event(virtual, 0, 0, 0)
        user.keybd_event(virtual, 0, 2, 0)
    finally:
        user.keybd_event(0x12, 0, 2, 0)


def resize_policy(dialogs, app, window, horizontal_only, destination):
    if sys.platform == "win32":

        class Point(ctypes.Structure):
            _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

        class MinMax(ctypes.Structure):
            _fields_ = [(name, Point) for name in ("reserved", "max_size", "max_position", "minimum", "maximum")]

        bounds = MinMax()
        bounds.maximum.x = bounds.maximum.y = 32767
        dialogs.user.SendMessageW(window, 0x24, 0, ctypes.addressof(bounds))  # WM_GETMINMAXINFO
        assert bounds.maximum.x > bounds.minimum.x
        assert (bounds.maximum.y == bounds.minimum.y) == horizontal_only
        result = {"minimum": [bounds.minimum.x, bounds.minimum.y], "maximum": [bounds.maximum.x, bounds.maximum.y]}
    elif app.state["backend"] == "GdkX11Display":
        windows = subprocess.check_output(
            ["xdotool", "search", "--onlyvisible", "--pid", str(app.process.pid), "--name", dialogs.title(window)],
            text=True,
        ).splitlines()
        identities = [
            identity
            for identity in windows
            if subprocess.check_output(["xdotool", "getwindowname", identity], text=True).strip()
            == dialogs.title(window)
        ]
        assert len(identities) == 1, identities
        hints = subprocess.check_output(["xprop", "-id", identities[0], "WM_NORMAL_HINTS"], text=True)
        minimum = re.search(r"minimum size: (\d+) by (\d+)", hints)
        maximum = re.search(r"maximum size: (\d+) by (\d+)", hints)
        assert minimum, hints
        assert bool(maximum and maximum[2] == minimum[2]) == horizontal_only, hints
        result = {"native_x11_size_hints": hints}
    else:
        trace = Path(app.log.name).read_text(encoding="utf-8", errors="replace")
        objects = re.findall(r'xdg_toplevel[@#](\d+)\.set_title\("' + re.escape(dialogs.title(window)) + r'"\)', trace)
        assert objects, "Owned Wayland toplevel's title protocol request"
        object_id = objects[-1]
        minimum = re.findall(r"xdg_toplevel[@#]" + object_id + r"\.set_min_size\((\d+), (\d+)\)", trace)
        maximum = re.findall(r"xdg_toplevel[@#]" + object_id + r"\.set_max_size\((\d+), (\d+)\)", trace)
        assert minimum and maximum, (object_id, minimum, maximum)
        assert (maximum[-1][1] == minimum[-1][1] and maximum[-1][1] != "0") == horizontal_only
        result = {"minimum": minimum[-1], "maximum": maximum[-1], "wayland_toplevel": object_id}
    destination.write_text(json.dumps(result, indent=2), encoding="utf-8")


def check_document(app, expected):
    app.send("snapshot")
    assert Path(app.state["document_path"]) == expected, app.state
    copy = app.folder / "loaded-copy.txt"
    app.send("save", str(copy))
    assert copy.read_bytes() == expected.read_bytes(), (copy.read_bytes(), expected.read_bytes())


def manager_environment(root, application_environment):
    if sys.platform == "win32":
        return application_environment
    applications = root / "data" / "applications"
    applications.mkdir(parents=True)
    config = root / "config"
    config.mkdir()
    recorder = root / "record-manager.py"
    recorder.write_text(
        "import os,sys\nfrom pathlib import Path\n"
        "Path(os.environ['WXNOTEPAD_FAVORITE_MANAGER_LOG']).write_text(sys.argv[1],encoding='utf-8')\n",
        encoding="utf-8",
    )
    (applications / "owned-favorites.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=Owned Favorites Manager\n"
        f'Exec={sys.executable} "{recorder}" %f\nMimeType=inode/directory;\n',
        encoding="utf-8",
    )
    (config / "mimeapps.list").write_text(
        "[Default Applications]\ninode/directory=owned-favorites.desktop;\n",
        encoding="utf-8",
    )
    return dict(
        application_environment,
        XDG_DATA_HOME=str(applications.parent),
        XDG_CONFIG_HOME=str(config),
        WXNOTEPAD_FAVORITE_MANAGER_LOG=str(root / "manager-target.txt"),
    )


def manage(app, dialogs, root, favorite_directory, evidence):
    if sys.platform != "win32":
        marker = root / "manager-target.txt"
        assert not marker.exists(), "Adding and opening favorites never dispatches a desktop association"
        favorite_key(dialogs, app, "F9")
        wait_for(marker.exists, "Owned native directory handler receives Manage")
        assert marker.read_text(encoding="utf-8") == str(favorite_directory)
        app.send("snapshot")
        return
    log = (evidence / "explorer.log").open("wb")
    observer = subprocess.Popen(
        [
            "pwsh",
            "-NoProfile",
            "-File",
            str(Path(__file__).with_name("open_target_explorer.ps1")),
            "-Root",
            str(root),
            "-Evidence",
            str(evidence),
            "-Requests",
            "1",
        ],
        stdout=log,
        stderr=subprocess.STDOUT,
    )
    try:
        wait_for((root / "explorer-ready").exists, "Independent native Explorer observer baseline")
        (root / "folder-request-1").write_text(f"{favorite_directory}\n\n{app.process.pid}\n", encoding="utf-8")
        favorite_key(dialogs, app, "F9")
        assert observer.wait(timeout=25) == 0, "Native Explorer opens exactly the owned favorites directory"
        assert (root / "folder-done-1").read_text(encoding="utf-8") == "ok\n"
        app.send("snapshot")
        native_windows = process_windows(app.process)
        (evidence / "manage-windows.json").write_text(
            json.dumps(
                {"pid": app.process.pid, "image": app.process.args[0], "windows": native_windows},
                ensure_ascii=False,
                indent=2,
            ),
            encoding="utf-8",
        )
        assert not [window for window in native_windows if window["visible"] and window["class"] == "#32770"], (
            "Alt+F9 opens only the favorites directory, without falling through to the F9 encoding chooser",
            native_windows,
        )
    finally:
        if observer.poll() is None:
            observer.terminate()
            observer.wait(timeout=10)
        log.close()


def linux_configuration_modes(executable, package, root, dialogs, target):
    if sys.platform == "win32":
        return
    # The default Documents fallback remains inside an owned HOME/XDG profile.
    home = root / "owned home"
    documents = home / "Documents"
    documents.mkdir(parents=True)
    xdg = root / "default-documents-config"
    xdg.mkdir()
    (xdg / "user-dirs.dirs").write_text(f'XDG_DOCUMENTS_DIR="{documents}"\n', encoding="utf-8")
    (documents / "documents marker").symlink_to(target)
    settings(package / "Notepad4.ini", 1033, save=True)
    env = dict(environment(executable, root / "children", "en"), HOME=str(home), XDG_CONFIG_HOME=str(xdg))
    app = Process(executable, root / "documents-default", env)
    try:
        app.begin("menu", str(menu_id(app.state, "Open Favorites...")))
        window = dialogs.wait(app.process, "Favorites")
        wait_for(
            lambda: any("documents marker" in text for text in dialogs.texts(window)),
            "Missing Favorites setting uses owned current-user Documents",
        )
        cancel_dialog(dialogs, window)
        app.finish()
        app.close()
    finally:
        app.stop()

    # Use the same prepared /opt fixture parent as existing installed UI tests.
    parent = Path("/opt/wxnotepad-file-workflows-test")
    assert parent.is_dir() and os.access(parent, os.W_OK), "Prepared native installed-test parent"
    with tempfile.TemporaryDirectory(prefix="favorites-", dir=parent) as name:
        prefix = Path(name)
        binary_directory = prefix / "bin"
        shutil.copytree(package, binary_directory, ignore=shutil.ignore_patterns("locale", "Notepad4.ini"))
        shutil.copytree(package / "locale", prefix / "share" / "locale")
        installed = binary_directory / executable.name
        (binary_directory / "favorites" / "installed marker").symlink_to(target)
        config = root / "installed-config" / "Notepad4" / "Notepad4.ini"
        config.parent.mkdir(parents=True)
        settings(config, 1033, "Favorites=favorites\n", save=True)
        env = dict(environment(installed, root / "children", "en"), XDG_CONFIG_HOME=str(config.parent.parent))
        for attempt in range(2):
            app = Process(installed, root / f"installed-{attempt}", env, cwd=root)
            try:
                app.begin("menu", str(menu_id(app.state, "Open Favorites...")))
                window = dialogs.wait(app.process, "Favorites")
                wait_for(
                    lambda: any("installed marker" in text for text in dialogs.texts(window)),
                    "Installed XDG configuration resolves Favorites relative to executable, not config/cwd",
                )
                cancel_dialog(dialogs, window)
                app.finish()
                app.close()
                assert not (binary_directory / "Notepad4.ini").exists()
                saved = config.read_text(encoding="utf-16")
                assert "Favorites=" in saved and "UnrelatedSetting=keep" in saved, saved
            finally:
                app.stop()


def exercise(app, dialogs, favorite_directory, target, other, evidence):
    link = favorite_directory / ("文档 favorite.lnk" if sys.platform == "win32" else "文档 favorite")
    add = menu_id(app.state, "Add Current File...")
    open_favorites = menu_id(app.state, "Open Favorites...")

    def recent_paths():
        app.begin("menu", str(menu_id(app.state, "Recent (History)...")))
        deadline = time.monotonic() + 10
        window = dialogs.wait(app.process, "Recent Files")
        while True:
            paths = tuple(text for text in dialogs.texts(window) if str(target.parent) in text)
            if any(app.state["document_path"] in text for text in paths):
                break
            assert time.monotonic() < deadline, paths
            time.sleep(0.03)
        assert any(app.state["document_path"] in text for text in paths), paths
        cancel_dialog(dialogs, window)
        app.finish()
        return paths

    def add_name(name, message):
        app.begin("menu", str(add))
        window = dialogs.wait(app.process, "Add to Favorites")
        dialogs.set_input(window, "Favorite name", name)
        dialogs.click(window, "OK")
        response = dialogs.wait(app.process, "Add to Favorites", message=message)
        # The name dialog has closed; the native message contains the result.
        if message not in "\n".join(dialogs.texts(response)):
            response = dialogs.wait(app.process, "", message=message)
        assert message in "\n".join(dialogs.texts(response)), dialogs.texts(response)
        dialogs.answer(app.process, "ok", window=response)
        app.finish()

    add_name("文档 favorite", "The favorite was added successfully.")
    assert Path(shortcut(link)) == target, shortcut(link)
    original = link.read_bytes() if sys.platform == "win32" else os.readlink(link)
    add_name("文档 favorite", "A favorite with this name already exists.")
    assert (link.read_bytes() if sys.platform == "win32" else os.readlink(link)) == original
    add_name("invalid/name", "Enter a valid name for the favorite.")
    assert len(list(favorite_directory.iterdir())) == 1
    if sys.platform == "win32":
        account = subprocess.check_output(["whoami"], text=True).strip()
        subprocess.run(
            ["icacls", str(favorite_directory), "/deny", account + ":(W)"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
    else:
        favorite_directory.chmod(0o555)
    try:
        add_name("unwritable", "Could not create the favorite. Check the name and directory permissions.")
    finally:
        if sys.platform == "win32":
            subprocess.run(
                ["icacls", str(favorite_directory), "/remove:d", account],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
        else:
            favorite_directory.chmod(0o755)
    assert len(list(favorite_directory.iterdir())) == 1

    # Save question is first, and Cancel prevents the Favorites chooser.
    app.send("open", str(other))
    native_edit(app.process, app.state)
    app.state = wait_state(app.folder, app.process, lambda state: state["dirty"] == "1")
    app.begin("menu", str(open_favorites))
    question = dialogs.wait(app.process, "Save changes?")
    dialogs.answer(app.process, "cancel", window=question)
    app.finish()
    assert Path(app.state["document_path"]) == other and app.state["dirty"] == "1", app.state
    assert not any(dialogs.title(window) == "Favorites" for window in dialogs.windows(app.process))

    # Save takes effect even when the subsequent Favorites picker is cancelled.
    app.begin("menu", str(open_favorites))
    question = dialogs.wait(app.process, "Save changes?")
    dialogs.answer(app.process, "yes", window=question)
    window = dialogs.wait(app.process, "Favorites")
    cancel_dialog(dialogs, window)
    app.finish()
    assert other.read_bytes() == b"Xsecondary document\n", other.read_bytes()
    assert app.state["dirty"] == "0" and Path(app.state["document_path"]) == other, app.state

    # Discard and load use the normal file pipeline without a second save question.
    native_edit(app.process, app.state)
    app.state = wait_state(app.folder, app.process, lambda state: state["dirty"] == "1")
    app.begin("menu", str(open_favorites))
    question = dialogs.wait(app.process, "Save changes?")
    dialogs.answer(app.process, "no", window=question)
    window = dialogs.wait(app.process, "Favorites")
    select_favorite(dialogs, app, window)
    app.finish()
    check_document(app, target)
    assert other.read_bytes() == b"Xsecondary document\n"

    # A directory favorite shows the native file picker at its actual target.
    directory = target.parent / "folder target 中文"
    directory.mkdir()
    nested = directory / "nested.txt"
    nested.write_bytes(b"directory favorite content\n")
    directory_link = favorite_directory / ("A directory.lnk" if sys.platform == "win32" else "A directory")
    shortcut(directory_link, directory)
    app.begin("menu", str(open_favorites))
    window = dialogs.wait(app.process, "Favorites")
    select_favorite(dialogs, app, window)
    chooser = dialogs.wait(app.process, "Open file")
    wait_for(
        lambda: any(directory.name in text or str(directory) in text for text in dialogs.texts(chooser)),
        "Directory favorite initializes native file picker at actual target",
    )
    choose_file(dialogs, app, chooser, nested)
    app.finish()
    check_document(app, nested)

    # A parsed link's missing target keeps the original Create choices. The
    # approved Cancel correction retains this window instead of exiting.
    broken = favorite_directory / ("B broken.lnk" if sys.platform == "win32" else "B broken")
    missing = target.parent / "missing-target.txt"
    if sys.platform != "win32":
        project = target.parent / "project 中文"
        (project / "subdirectory").mkdir(parents=True)
        alias = target.parent / "directory alias"
        alias.symlink_to(project / "subdirectory", target_is_directory=True)
        missing = project / "missing-target.txt"
        # The filesystem follows alias before '..'; lexical collapse would
        # incorrectly point at target.parent/missing-target.txt.
        shortcut(broken, alias / ".." / missing.name)
    else:
        shortcut(broken, missing)
    before_missing = dict(app.state)
    history_before_missing = recent_paths()
    for choice in ("no", "cancel", "yes"):
        app.begin("menu", str(open_favorites))
        window = dialogs.wait(app.process, "Favorites")
        select_favorite(dialogs, app, window, 1)
        prompt = dialogs.wait(app.process, "Create file?", message=f'"{missing}" not found.')
        assert str(missing) in "\n".join(dialogs.texts(prompt))
        dialogs.answer(app.process, choice, window=prompt)
        app.finish()
        assert app.process.poll() is None
        if choice == "yes":
            assert missing.read_bytes() == b"" and Path(app.state["document_path"]) == missing
            assert app.state["length"] == "0" and app.state["dirty"] == "0"
        else:
            assert not missing.exists()
            for field in ("document_path", "length", "dirty", "encoding", "bom"):
                assert app.state[field] == before_missing[field], (field, before_missing, app.state)
            assert recent_paths() == history_before_missing, "No/Cancel keeps the original recent-file list"
    if sys.platform != "win32":
        relative = favorite_directory / "C relative"
        resolved = project / "existing-target.txt"
        resolved.write_bytes(b"filesystem-resolved directory symlink target\n")
        relative_target = str(Path(os.path.relpath(alias, favorite_directory)) / ".." / resolved.name)
        relative.symlink_to(relative_target)
        assert os.readlink(relative) == relative_target
        app.begin("menu", str(open_favorites))
        window = dialogs.wait(app.process, "Favorites")
        select_favorite(dialogs, app, window, 2)
        app.finish()
        check_document(app, resolved)
    damaged = favorite_directory / "D damaged.lnk"
    damaged.write_bytes(b"L\0\0\0unsupported Windows shell link")
    before_damaged = dict(app.state)
    app.begin("menu", str(open_favorites))
    window = dialogs.wait(app.process, "Favorites")
    select_favorite(dialogs, app, window, 2 if sys.platform == "win32" else 3)
    message = "Could not resolve the favorite. The link may be damaged or unsupported."
    error = dialogs.wait(app.process, "", message=message)
    dialogs.answer(app.process, "ok", window=error)
    cancel_dialog(dialogs, window)
    app.finish()
    for field in ("document_path", "length", "dirty", "encoding", "bom"):
        assert app.state[field] == before_damaged[field], (field, before_damaged, app.state)
    (evidence / "workflow.json").write_text(
        json.dumps(
            {
                "link": str(link),
                "target": shortcut(link),
                "directory_target": shortcut(directory_link),
                "save_before_chooser": True,
                "duplicate_preserved": True,
                "missing_target_no_cancel_preserved_document": True,
                "missing_target_yes_created_empty": True,
                "malformed_link_preserved_document": True,
            },
            ensure_ascii=False,
            indent=2,
        ),
        encoding="utf-8",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake-dir", type=Path)
    parser.add_argument("--evidence", type=Path, required=True)
    args = parser.parse_args()
    source = args.executable.resolve()
    args.evidence.mkdir(parents=True, exist_ok=True)
    dialogs = Dialogs(args.evidence)
    with tempfile.TemporaryDirectory(prefix="wxnotepad-favorites-package-") as temporary:
        root = Path(temporary)
        package = root / "package"
        package.mkdir()
        if args.cmake_dir:
            executable = bundle_application(source, args.cmake_dir.resolve(), package)
            package = executable.parent
        else:
            executable = package / source.name
            shutil.copy2(source, executable)
            for directory in ("locale", "lib", "libexec"):
                if (source.parent / directory).exists():
                    shutil.copytree(source.parent / directory, package / directory)
            for dependency in source.parent.glob("*.dll"):
                shutil.copy2(dependency, package / dependency.name)
        favorite_directory = package / "favorites"
        favorite_directory.mkdir()
        target = root / "当前文档.txt"
        target.write_bytes("favorite content 中文\n".encode())
        other = root / "secondary.txt"
        other.write_bytes(b"secondary document\n")
        for preference, captions in CAPTIONS.items():
            code, open_label, add_label, manage_label, picker_title, add_title, name_label = captions
            settings(package / "Notepad4.ini", preference, "Favorites=favorites\nSaveRecentFiles=65\n", save=True)
            application_environment = environment(executable, root / "children", code)
            if application_environment.get("GDK_BACKEND") == "wayland":
                application_environment["WAYLAND_DEBUG"] = "client"
            if preference == 1033:
                application_environment = manager_environment(root, application_environment)
            app = Process(executable, root / code, application_environment, log_path=args.evidence / (code + ".log"))
            try:
                items = menus(app.state)
                expected = (open_label, add_label, manage_label)
                favorites = [item for item in items.values() if item["label"] in expected]
                assert [item["label"] for item in favorites] == list(expected), favorites
                assert [item["enabled"] for item in favorites] == ["1", "0", "1"], favorites
                assert len({item["path"].rsplit("/", 1)[0] for item in favorites}) == 1, favorites
                app.send("open", str(target))
                identity = next(identity for identity, item in menus(app.state).items() if item["label"] == add_label)
                assert menus(app.state)[identity]["enabled"] == "1"
                if preference == 1033:
                    favorite_key(dialogs, app, "k")
                else:
                    app.begin("menu", str(identity))
                window = dialogs.wait(app.process, add_title)
                assert name_label in dialogs.accessible_names(window), dialogs.accessible_names(window)
                default_name(dialogs, window, name_label, target)
                with owned_topmost(dialogs, app.process, window):
                    capture_and_check(dialogs, app, window, args.evidence / f"add-{code}.png")
                    resize_policy(dialogs, app, window, True, args.evidence / f"add-resize-{code}.json")
                cancel_dialog(dialogs, window)
                if preference == 1033:
                    app.send("snapshot")
                else:
                    app.finish()
                assert not list(favorite_directory.iterdir()), "Cancelling Add creates no link"
                if preference != 1033:
                    app.begin("menu", str(identity))
                    window = dialogs.wait(app.process, add_title)
                    dialogs.set_input(window, name_label, "invalid/name")
                    accept_dialog(dialogs, window)
                    message = {2052: "请输入有效的收藏名称。", 1028: "請輸入有效的我的最愛名稱。"}[preference]
                    response = dialogs.wait(app.process, "", message=message)
                    assert message in "\n".join(dialogs.texts(response))
                    dialogs.answer(app.process, "ok", window=response)
                    app.finish()
                    assert not list(favorite_directory.iterdir()), "Localized failure creates no link"
                open_identity = next(identity for identity, item in items.items() if item["label"] == open_label)
                if preference == 1033:
                    favorite_key(dialogs, app, "i")
                else:
                    app.begin("menu", str(open_identity))
                window = dialogs.wait(app.process, picker_title)
                with owned_topmost(dialogs, app.process, window):
                    capture_and_check(dialogs, app, window, args.evidence / f"favorites-{code}.png")
                    resize_policy(dialogs, app, window, False, args.evidence / f"favorites-resize-{code}.json")
                cancel_dialog(dialogs, window)
                if preference == 1033:
                    app.send("snapshot")
                else:
                    app.finish()
                if preference == 1033:
                    exercise(app, dialogs, favorite_directory, target, other, args.evidence)
                    manage(app, dialogs, root, favorite_directory, args.evidence)
                    for entry in favorite_directory.iterdir():
                        entry.unlink()
                app.close()
                saved = (package / "Notepad4.ini").read_text(encoding="utf-16")
                assert "Favorites=" in saved and "UnrelatedSetting=keep" in saved, saved
            finally:
                app.stop()
            # Restart reads the path saved by the real application. A marker
            # distinguishes the configured directory from the user's Documents.
            marker = favorite_directory / ("restart marker.lnk" if sys.platform == "win32" else "restart marker")
            shortcut(marker, target)
            restarted = Process(executable, root / (code + "-restart"), application_environment)
            try:
                identity = next(
                    identity for identity, item in menus(restarted.state).items() if item["label"] == open_label
                )
                restarted.begin("menu", str(identity))
                window = dialogs.wait(restarted.process, picker_title)
                wait_for(
                    lambda: any("restart marker" in text for text in dialogs.texts(window)),
                    "Configuration restart retains actual favorites directory and link name",
                )
                cancel_dialog(dialogs, window)
                restarted.finish()
                restarted.close()
            finally:
                restarted.stop()
                marker.unlink()
        marker = favorite_directory / ("environment marker.lnk" if sys.platform == "win32" else "environment marker")
        shortcut(marker, target)
        settings(package / "Notepad4.ini", 1033, "Favorites=%WXNOTEPAD_FAVORITES_TEST%\n", save=True)
        app = Process(
            executable,
            root / "environment",
            dict(environment(executable, root / "children", "en"), WXNOTEPAD_FAVORITES_TEST=str(favorite_directory)),
        )
        try:
            app.begin("menu", str(menu_id(app.state, "Open Favorites...")))
            window = dialogs.wait(app.process, "Favorites")
            wait_for(
                lambda: any("environment marker" in text for text in dialogs.texts(window)),
                "Original percent environment expansion resolves to actual favorites directory",
            )
            cancel_dialog(dialogs, window)
            app.finish()
            app.close()
            saved = (package / "Notepad4.ini").read_text(encoding="utf-16")
            assert "UnrelatedSetting=keep" in saved
        finally:
            app.stop()
        linux_configuration_modes(executable, package, root, dialogs, target)
    print("PASS: original Favorites menu grouping, shortcuts, availability and configuration")


if __name__ == "__main__":
    main()
