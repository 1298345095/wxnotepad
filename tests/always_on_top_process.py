"""Exercise Always On Top through the packaged application's menus and INI."""

import argparse
import os
import shutil
import sys
import tempfile
import time
from contextlib import ExitStack
from pathlib import Path

from localization_package import (
    Process,
    bundle_application,
    environment,
    menu_id,
    menus,
    read_settings,
    settings,
    wait_child,
)


def checked(app, identity, expected):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        app.send("snapshot")
        if menus(app.state)[identity]["checked"] == str(int(expected)):
            return
        time.sleep(0.01)
    raise AssertionError(menus(app.state)[identity])


def lifecycle(executable, root, ini=None, environment_overrides=None):
    ini = ini or executable.parent / "Notepad4.ini"
    children = root / "children"
    children.mkdir()
    env = environment(executable, children)
    env.update(environment_overrides or {})
    count = 0
    for preference in (False, True):
        for option, forced in ((None, None), ("o", True), ("o0", False)):
            for save_on_exit, explicit in ((True, False), (False, False), (False, True)):
                count += 1
                settings(ini, 1033, f"AlwaysOnTop={int(preference)}\n", save=save_on_exit)
                app = Process(
                    executable, root / f"matrix-{count}", env, arguments=(() if option is None else ("-" + option,))
                )
                try:
                    identity = menu_id(app.state, "Always On Top")
                    supported = app.state["backend"] != "GdkWaylandDisplay"
                    effective = preference if forced is None else forced
                    checked(app, identity, supported and effective)
                    app.choose(menu_id(app.state, "Save Settings Now"))
                    assert read_settings(ini).getboolean("Settings", "AlwaysOnTop", fallback=False) == preference
                    if save_on_exit:
                        previous = {path.name for path in children.iterdir()}
                        app.choose(menu_id(app.state, "New Window"))
                        child = wait_child(children, previous)
                        try:
                            checked(child, identity, supported and preference)
                            child.close()
                        finally:
                            child.stop()
                    if supported:
                        app.choose(identity)
                        checked(app, identity, not effective)
                    if explicit:
                        app.choose(menu_id(app.state, "Save Settings Now"))
                    app.close()
                finally:
                    app.stop()
                saved = (not effective) if supported and (save_on_exit or explicit) else preference
                assert read_settings(ini).getboolean("Settings", "AlwaysOnTop", fallback=False) == saved
                config = read_settings(ini)
                assert config["Settings"]["UnrelatedSetting"] == "keep"
                assert config["Settings2"]["UnrelatedLanguage"] == "preserved"
                assert config["Settings"].get("ToolbarButtons") is not None
                app = Process(executable, root / f"restart-{count}", env)
                try:
                    checked(app, identity, supported and saved)
                    app.close()
                finally:
                    app.stop()
    prefixes = ("/", "-", "--") if sys.platform == "win32" else ("-", "--")
    for index, (prefix, option) in enumerate(
        (prefix, option) for prefix in prefixes for option in ("o", "o0", "o-", "oo")
    ):
        enabled = option == "o"
        settings(ini, 1033, f"AlwaysOnTop={int(not enabled)}\n")
        app = Process(executable, root / f"prefix-{index}", env, arguments=(prefix + option,))
        try:
            checked(app, menu_id(app.state, "Always On Top"), app.state["backend"] != "GdkWaylandDisplay" and enabled)
            app.close()
        finally:
            app.stop()
    target = root / "o"
    target.write_bytes(b"absolute path is not an option\n")
    settings(ini, 1033)
    app = Process(executable, root / "absolute-path", env, arguments=(str(target),))
    try:
        assert Path(app.state["document_path"]) == target
        app.close()
    finally:
        app.stop()
    print(
        "PASS: packaged preference/override/save matrix, manual clearing, New Window, restart, prefixes and absolute path"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--cmake-dir", type=Path)
    parser.add_argument("--scenario", choices=("command", "lifecycle"), default="command")
    parser.add_argument(
        "--packaged", action="store_true", help="Use an already bundled application without rebuilding it"
    )
    parser.add_argument("--installed", action="store_true", help="Exercise native installed configuration discovery")
    args = parser.parse_args()
    if not args.packaged and args.cmake_dir is None:
        parser.error("--cmake-dir is required unless --packaged is used")
    with ExitStack() as stack:
        root = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix="always-on-top-")))
        executable = (
            args.executable.resolve()
            if args.packaged
            else bundle_application(args.executable, args.cmake_dir, root / "package")
        )
        if args.installed:
            if sys.platform == "win32":
                assert "WinGet" in str(executable), (
                    "owned installed-mode Windows package must use original discovery prefix"
                )
                ini = Path(os.environ["LOCALAPPDATA"]) / "Notepad4" / "Notepad4.ini"
                assert not ini.exists(), (
                    "installed-mode test requires an isolated profile without preexisting configuration"
                )
                created_parent = not ini.parent.exists()
                ini.parent.mkdir(exist_ok=True)
                try:
                    lifecycle(executable, root, ini)
                finally:
                    ini.unlink(missing_ok=True)
                    if created_parent:
                        ini.parent.rmdir()
            else:
                prefix = Path(
                    stack.enter_context(
                        tempfile.TemporaryDirectory(prefix="topmost-", dir="/opt/wxnotepad-file-workflows-test")
                    )
                )
                installed = prefix / "bin" / executable.name
                shutil.copytree(executable.parent, installed.parent)
                config_home = root / "native-user"
                ini = config_home / "Notepad4" / "Notepad4.ini"
                ini.parent.mkdir(parents=True)
                lifecycle(installed, root, ini, {"XDG_CONFIG_HOME": str(config_home)})
                assert not (installed.parent / "Notepad4.ini").exists()
            print("PASS: actual installed package uses native original-format configuration discovery")
            return
        if args.scenario == "lifecycle":
            lifecycle(executable, root)
            return
        for language, caption, explanation in (
            (1033, "Always On &Top", "Always On Top is unavailable on this desktop."),
            (2052, "始终置顶(&T)", "当前桌面不支持窗口置顶。"),
            (1028, "始終置頂(&T)", "目前桌面不支援視窗置頂。"),
        ):
            settings(executable.parent / "Notepad4.ini", language)
            app = Process(executable, root / f"app-{language}", environment(executable, root / "children"))
            try:
                identity = menu_id(app.state, caption.replace("&", ""))
                entry = menus(app.state)[identity]
                assert entry["label"] == caption + "\tAlt+Shift+T", entry
                assert entry["checked"] == "0", entry
                supported = app.state["backend"] != "GdkWaylandDisplay"
                assert entry["enabled"] == str(int(supported)), entry
                if supported:
                    app.choose(identity)
                    checked(app, identity, True)
                    app.choose(identity)
                    checked(app, identity, False)
                else:
                    assert any(
                        item["label"] == explanation and item["enabled"] == "0" for item in menus(app.state).values()
                    )
                app.close()
            finally:
                app.stop()
    print("PASS: packaged original Always On Top command and backend capability")


if __name__ == "__main__":
    main()
