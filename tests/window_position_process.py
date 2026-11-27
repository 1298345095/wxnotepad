"""Position commands through a real application process and its INI file."""

import argparse
import ctypes as c
import ctypes.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from contextlib import ExitStack
from ctypes import wintypes as w
from pathlib import Path

from localization_package import Process, bundle_application, menu_id, menus, read_settings

CAPTIONS = {
    1033: ("Sticky Window Position", "Clear Position History", "Window Position", "Save Settings Now"),
    2052: ("固定窗口位置(S)", "清除位置记录(H)", "窗口位置(W)", "立即保存设置(N)"),
    1028: ("固定視窗位置(S)", "清除位置記錄(H)", "視窗位置(W)", "立即儲存設定(N)"),
}


def native_rectangle(app):
    """Query the owned native outer rectangle independently of the app observer."""
    if sys.platform == "win32":
        from capture_window_positions import Placement

        user = c.WinDLL("user32", use_last_error=True)
        user.GetWindowPlacement.argtypes = [w.HWND, c.POINTER(Placement)]
        placement = Placement(c.sizeof(Placement))
        assert user.GetWindowPlacement(int(app.state["frame_handle"]), c.byref(placement))
        normal = placement.normal
        return (normal.left, normal.top, normal.right - normal.left, normal.bottom - normal.top)
    identity = app.state["frame_x11_window"]
    deadline = time.monotonic() + 4
    while True:
        extents = subprocess.check_output(["xprop", "-id", identity, "_NET_FRAME_EXTENTS"], text=True)
        if "=" in extents:
            break
        if time.monotonic() >= deadline:
            detail = subprocess.check_output(["xwininfo", "-id", identity, "-tree"], text=True)
            properties = subprocess.check_output(["xprop", "-id", identity], text=True)
            raise AssertionError(f"owned X11 frame extents unavailable: {extents}\n{detail}\n{properties}")
        time.sleep(0.01)
    info = subprocess.check_output(["xwininfo", "-id", identity], text=True)

    def number(label):
        return int(re.search(rf"{label}:\s+(-?\d+)", info)[1])

    left, right, top, bottom = map(int, extents.split("=", 1)[1].split(","))
    return (
        number("Absolute upper-left X") - left,
        number("Absolute upper-left Y") - top,
        number("Width") + left + right,
        number("Height") + top + bottom,
    )


def native_clipboard():
    if sys.platform == "win32":
        user, kernel = c.WinDLL("user32"), c.WinDLL("kernel32")
        user.GetClipboardData.restype = w.HANDLE
        kernel.GlobalLock.argtypes = [w.HANDLE]
        kernel.GlobalLock.restype = w.LPVOID
        kernel.GlobalUnlock.argtypes = [w.HANDLE]
        assert user.OpenClipboard(None)
        try:
            handle = user.GetClipboardData(13)
            pointer = kernel.GlobalLock(handle)
            assert pointer
            try:
                return c.wstring_at(pointer)
            finally:
                kernel.GlobalUnlock(handle)
        finally:
            user.CloseClipboard()
    gtk, glib, clipboard = gtk_clipboard()
    gtk.gtk_clipboard_wait_for_text.argtypes = [c.c_void_p]
    gtk.gtk_clipboard_wait_for_text.restype = c.c_void_p
    glib.g_free.argtypes = [c.c_void_p]
    pointer = gtk.gtk_clipboard_wait_for_text(clipboard)
    assert pointer
    try:
        return c.string_at(pointer).decode("utf-8")
    finally:
        glib.g_free(pointer)


def gtk_clipboard():
    gtk, gdk, glib = [c.CDLL(ctypes.util.find_library(name)) for name in ("gtk-3", "gdk-3", "glib-2.0")]
    gtk.gtk_init_check.argtypes = [c.c_void_p, c.c_void_p]
    assert gtk.gtk_init_check(None, None)
    gdk.gdk_atom_intern_static_string.argtypes = [c.c_char_p]
    gdk.gdk_atom_intern_static_string.restype = c.c_void_p
    gtk.gtk_clipboard_get.argtypes = [c.c_void_p]
    gtk.gtk_clipboard_get.restype = c.c_void_p
    return gtk, glib, gtk.gtk_clipboard_get(gdk.gdk_atom_intern_static_string(b"CLIPBOARD"))


def seed_clipboard():
    if sys.platform == "win32":
        user = c.WinDLL("user32")
        assert user.OpenClipboard(None)
        try:
            assert user.EmptyClipboard()
        finally:
            user.CloseClipboard()
    else:
        gtk, _, clipboard = gtk_clipboard()
        gtk.gtk_clipboard_set_text.argtypes = [c.c_void_p, c.c_char_p, c.c_int]
        gtk.gtk_clipboard_set_text(clipboard, b"owned window-position fixture seed", -1)


def native_work_area(app):
    if sys.platform == "win32":
        from capture_window_positions import Monitor

        user = c.WinDLL("user32")
        user.MonitorFromWindow.argtypes = [w.HWND, w.DWORD]
        user.MonitorFromWindow.restype = w.HANDLE
        user.GetMonitorInfoW.argtypes = [w.HANDLE, c.POINTER(Monitor)]
        monitor = Monitor(c.sizeof(Monitor))
        assert user.GetMonitorInfoW(user.MonitorFromWindow(int(app.state["frame_handle"]), 2), c.byref(monitor))
        return monitor.work.left, monitor.work.top, monitor.work.right, monitor.work.bottom
    values = subprocess.check_output(["xprop", "-root", "_NET_WORKAREA"], text=True).split("=", 1)[1]
    x, y, width, height = [int(value) for value in values.split(",")[:4]]
    return x, y, x + width, y + height


def within_work_area(app):
    x, y, width, height = native_rectangle(app)
    left, top, right, bottom = native_work_area(app)
    assert left <= x and top <= y and x + width <= right and y + height <= bottom, (
        (x, y, width, height),
        (left, top, right, bottom),
    )


def secondary_windows_positions():
    from capture_window_positions import Monitor

    user = c.WinDLL("user32")
    callback = c.WINFUNCTYPE(w.BOOL, w.HANDLE, w.HDC, c.POINTER(w.RECT), w.LPARAM)
    user.EnumDisplayMonitors.argtypes = [w.HDC, c.POINTER(w.RECT), callback, w.LPARAM]
    user.GetMonitorInfoW.argtypes = [w.HANDLE, c.POINTER(Monitor)]
    result = []

    def monitor(handle, _dc, _rect, _data):
        info = Monitor(c.sizeof(Monitor))
        assert user.GetMonitorInfoW(handle, c.byref(info))
        if not info.flags & 1:  # MONITORINFOF_PRIMARY.
            result.append(
                (
                    info.screen.left + 40,
                    info.screen.top + 40,
                    min(640, info.work.right - info.work.left - 80),
                    min(480, info.work.bottom - info.work.top - 80),
                )
            )
        return True

    assert user.EnumDisplayMonitors(None, None, callback(monitor), 0)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--installed-directory", type=Path)
    parser.add_argument("--cmake-dir", type=Path)
    args = parser.parse_args()
    if sys.platform == "win32":
        user = c.WinDLL("user32", use_last_error=True)
        user.SetProcessDpiAwarenessContext.argtypes = [w.HANDLE]
        assert user.SetProcessDpiAwarenessContext(w.HANDLE(-4)), "native measurements use physical PMv2 pixels"
    source = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="wxnotepad-position-") as temporary, ExitStack() as fixtures:
        folder = Path(temporary)
        if args.cmake_dir:
            source = bundle_application(source, args.cmake_dir.resolve(), folder / "distribution")
        executable_folder = folder
        installed = None
        if args.installed_directory:
            installed = fixtures.enter_context(
                tempfile.TemporaryDirectory(prefix="wxnotepad-position-", dir=args.installed_directory)
            )
            executable_folder = Path(installed)
        executable = executable_folder / source.name
        shutil.copy2(source, executable)
        if (source.parent / "locale").exists():
            shutil.copytree(source.parent / "locale", executable_folder / "locale")
        config = folder / "Notepad4.ini"
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ["PATH"])
        if installed:
            if sys.platform == "win32":
                native = c.create_unicode_buffer(32768)
                assert c.windll.shell32.SHGetFolderPathW(None, 0x1C, None, 0, native) == 0
                config = Path(native.value) / "Notepad4" / "Notepad4.ini"
                assert not config.exists(), "Windows installed tests use the approved isolated VM with no user INI"
                fixtures.callback(config.unlink, missing_ok=True)
            else:
                environment["XDG_CONFIG_HOME"] = str(folder / "user-config")
                config = folder / "user-config" / "Notepad4" / "Notepad4.ini"
            config.parent.mkdir(parents=True, exist_ok=True)
        run = 0
        tested_dpi = set()

        def launch(arguments=()):
            nonlocal run
            run += 1
            app = Process(executable, folder / f"observed-{run}", environment, arguments=arguments)
            tested_dpi.add(int(app.state["dpi"]))
            return app

        for language, captions in CAPTIONS.items():
            config.write_text(f"[Settings]\nSaveSettings=0\n[Settings2]\nUILanguage={language}\n", encoding="utf-16")
            app = launch(("-n", "-pos", "100,120,640,480,0"))
            wayland = app.state["backend"] == "GdkWaylandDisplay"
            try:
                sticky, clear, copy, save = [menu_id(app.state, label) for label in captions]
                entries = menus(app.state)
                assert entries[sticky]["path"].rsplit("/", 1)[0] == entries[clear]["path"].rsplit("/", 1)[0]
                assert entries[sticky]["checked"] == "0" and entries[clear]["enabled"] == "1"
                assert entries[copy]["enabled"] == str(int(not wayland))
                assert tuple(int(app.state[key]) for key in ("frame_width", "frame_height")) == (640, 480)
                if not wayland:
                    assert native_rectangle(app) == (100, 120, 640, 480)
                    seed_clipboard()
                    app.choose(copy)
                    copied = native_clipboard()
                    assert copied == ("/pos" if sys.platform == "win32" else "-pos") + " 100,120,640,480,0"
                app.choose(sticky)
                stored = read_settings(config)
                assert stored["Settings2"]["StickyWindowPosition"] == "1"
                assert menus(app.state)[clear]["enabled"] == "0"
                sections = [name for name in stored.sections() if name.startswith("Window Position ")]
                if wayland:
                    assert not sections
                    section = "wxnotepad Wayland Window"
                    assert "WindowPosX" not in stored[section] and "WindowPosY" not in stored[section]
                else:
                    assert len(sections) == 1
                    section = sections[0]
                assert stored[section]["WindowSizeX"] == "640" and stored[section]["WindowSizeY"] == "480"
                app.close()
            finally:
                app.stop()
            app = launch()
            try:
                assert tuple(int(app.state[key]) for key in ("frame_width", "frame_height")) == (640, 480)
                if not wayland:
                    assert native_rectangle(app) == (100, 120, 640, 480)
                app.choose(sticky)
                app.choose(clear)
                assert not any(name.startswith("Window Position") for name in read_settings(config).sections())
                app.close()
            finally:
                app.stop()
            if not wayland:
                app = launch(tuple(copied.split()))
                try:
                    assert native_rectangle(app) == (100, 120, 640, 480), "copied public parameters round-trip"
                    app.close()
                finally:
                    app.stop()
        if not wayland:
            # Imported history from an absent monitor/resolution must use the
            # available desktop's no-history layout, without changing the INI.
            config.write_text(
                "[Settings]\nSaveSettings=0\nWindowPosX=100000\nWindowPosY=100000\n"
                "[Settings2]\nUILanguage=1033\n[Window Position 12345x6789]\n"
                "WindowPosX=100000\nWindowPosY=100000\nWindowSizeX=640\nWindowSizeY=480\n",
                encoding="utf-16",
            )
            original = read_settings(config)
            app = launch()
            try:
                within_work_area(app)
                app.close()
                assert read_settings(config) == original
            finally:
                app.stop()
            app = launch(("-pos", "100000,100000,640,480,0"))
            try:
                within_work_area(app)
                assert native_rectangle(app)[2:] == (640, 480)
                app.close()
            finally:
                app.stop()
        config.write_text("[Settings]\nSaveSettings=1\n[Settings2]\nUILanguage=1033\n", encoding="utf-16")
        app = launch(("-pos", "100,120,640,480,1"))
        try:
            assert app.state["maximized"] == "1", app.state
            app.close()
        finally:
            app.stop()
        stored = read_settings(config)
        position_sections = [
            name
            for name in stored.sections()
            if name.startswith("Window Position ") or name == "wxnotepad Wayland Window"
        ]
        assert len(position_sections) == 1
        record = stored[position_sections[0]]
        assert tuple(int(record[key]) for key in ("WindowSizeX", "WindowSizeY", "WindowMaximized")) == (640, 480, 1), (
            dict(record)
        )
        app = launch()
        try:
            assert app.state["maximized"] == "1", "ordinary restart restores saved maximized state"
            app.close()
        finally:
            app.stop()
        if sys.platform == "win32":
            for position in secondary_windows_positions():
                config.write_text("[Settings]\nSaveSettings=0\n[Settings2]\nUILanguage=1033\n", encoding="utf-16")
                argument = ",".join(map(str, (*position, 0)))
                app = launch(("-pos", argument))
                try:
                    assert native_rectangle(app) == position, (native_rectangle(app), position)
                    seed_clipboard()
                    app.choose(menu_id(app.state, "Window Position"))
                    copied = native_clipboard()
                    assert copied == "/pos " + argument
                    app.close()
                finally:
                    app.stop()
                app = launch(tuple(copied.split()))
                try:
                    assert native_rectangle(app) == position, (
                        "non-primary copied launch preserves physical normal rectangle"
                    )
                    app.close()
                finally:
                    app.stop()
        print("PASS: English/Simplified/Traditional commands, real INI restore and copied public five-field launch")
        print(f"Observed native DPI: {sorted(tested_dpi)}")


if __name__ == "__main__":
    main()
