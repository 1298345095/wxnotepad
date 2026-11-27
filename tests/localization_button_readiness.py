"""Exercise native button publication through the packaged UI's native driver."""

import subprocess
import sys
import tempfile
import time
from pathlib import Path

from localization_native import Dialogs, plain

TITLE = "Native button readiness"
BUTTON_PUBLICATION_DELAY_MS = 250
COMMAND_POLL_MS = 10
PROPERTY_BUSY_SECONDS = 3.5  # A finite native property delay inside the existing ten-second readiness budget.
INPUT_NAME = "Line"
INPUT_TEXT = "native value 中文"


def publisher(root):
    if sys.platform == "win32":
        windows_publisher(root)
        return
    sys.path.append("/usr/lib/python3/dist-packages")
    import gi

    gi.require_version("Gtk", "3.0")
    gi.require_version("Atk", "1.0")
    from gi.repository import Atk, GLib, Gtk

    window = Gtk.Window(title=TITLE)
    # Wayland's client-side titlebar otherwise adds its own Close push button.
    window.set_decorated(False)
    contents = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
    window.add(contents)
    input_mode = (root / "input-mode").exists()
    if input_mode:

        class BusyAccessible(Gtk.EntryAccessible):
            __gtype_name__ = "NativeReadinessBusyAccessible"

            def do_get_role(self):
                if (root / "stall-role").exists():
                    (root / "stall-role").unlink()
                    (root / "blocked").touch()
                    time.sleep(PROPERTY_BUSY_SECONDS)
                    (root / "resumed").touch()
                return Atk.Role.TEXT

        class BusyEntry(Gtk.Entry):
            __gtype_name__ = "NativeReadinessBusyEntry"

        BusyEntry.set_accessible_type(BusyAccessible)
        field = BusyEntry()
        field.get_accessible().set_name(INPUT_NAME)
        field.set_text("original")
        contents.pack_start(field, True, True, 0)
        (root / "value").write_text("original", encoding="utf-8")
    else:
        # The caption is already visible; only a real native button may be clicked.
        placeholder = Gtk.Label(label="Close")
        contents.pack_start(placeholder, True, True, 0)
    window.show_all()
    actions = 0

    def clicked(button):
        nonlocal actions
        actions += 1
        pending = root / "actions.tmp"
        pending.write_text(str(actions), encoding="utf-8")
        pending.replace(root / "actions")

    if input_mode:

        def changed(entry):
            (root / "value").write_text(entry.get_text(), encoding="utf-8")
            clicked(entry)

        field.connect("changed", changed)

    def publish():
        placeholder.destroy()
        button = Gtk.Button(label="Close")
        button.connect("clicked", clicked)
        contents.pack_start(button, True, True, 0)
        button.show()
        (root / "published").touch()
        return GLib.SOURCE_REMOVE

    def commands():
        if (root / "stop").exists():
            Gtk.main_quit()
            return GLib.SOURCE_REMOVE
        if (root / "initial-observed").exists() and (root / "publish").exists():
            (root / "publish").unlink()
            GLib.timeout_add(BUTTON_PUBLICATION_DELAY_MS, publish)
        return GLib.SOURCE_CONTINUE

    GLib.timeout_add(COMMAND_POLL_MS, commands)
    Gtk.main()


def windows_publisher(root):
    import ctypes
    from ctypes import wintypes

    user = ctypes.WinDLL("user32", use_last_error=True)
    user.CreateWindowExW.argtypes = [
        wintypes.DWORD,
        wintypes.LPCWSTR,
        wintypes.LPCWSTR,
        wintypes.DWORD,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        wintypes.HWND,
        wintypes.HMENU,
        wintypes.HINSTANCE,
        ctypes.c_void_p,
    ]
    user.CreateWindowExW.restype = wintypes.HWND
    user.SetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_void_p]
    user.SetWindowLongPtrW.restype = wintypes.LPARAM
    user.DefWindowProcW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.DefWindowProcW.restype = wintypes.LPARAM
    user.DestroyWindow.argtypes = [wintypes.HWND]
    user.EnableWindow.argtypes = [wintypes.HWND, wintypes.BOOL]
    user.SetTimer.argtypes = [wintypes.HWND, ctypes.c_size_t, wintypes.UINT, ctypes.c_void_p]
    user.GetMessageW.argtypes = [ctypes.POINTER(wintypes.MSG), wintypes.HWND, wintypes.UINT, wintypes.UINT]
    visible_child = 0x40000000 | 0x10000000  # WS_CHILD | WS_VISIBLE.
    window = user.CreateWindowExW(
        0, "STATIC", TITLE, 0x00CF0000 | 0x10000000, 80, 80, 320, 160, None, None, None, None
    )  # WS_OVERLAPPEDWINDOW | WS_VISIBLE.
    assert window, ctypes.WinError(ctypes.get_last_error())
    placeholder = user.CreateWindowExW(0, "STATIC", "Close", visible_child, 20, 20, 180, 30, window, None, None, None)
    assert placeholder
    actions = 0
    publication = None
    button = None

    @ctypes.WINFUNCTYPE(wintypes.LPARAM, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
    def procedure(handle, message, first, second):
        nonlocal actions, publication, button
        if message == 0x0111 and second:  # WM_COMMAND / BN_CLICKED.
            actions += 1
            (root / "actions").write_text(str(actions), encoding="utf-8")
            return 0
        if message == 0x0113:  # WM_TIMER.
            if (root / "stop").exists():
                user.PostQuitMessage(0)
            if (root / "initial-observed").exists() and (root / "publish").exists():
                (root / "publish").unlink()
                publication = time.monotonic() + BUTTON_PUBLICATION_DELAY_MS / 1000
            if publication is not None and time.monotonic() >= publication:
                if button is None:
                    user.DestroyWindow(placeholder)
                    button = user.CreateWindowExW(
                        0, "BUTTON", "Close", visible_child | 0x08000000, 20, 20, 180, 30, window, None, None, None
                    )  # WS_DISABLED.
                    assert button
                    (root / "disabled-button").touch()
                    publication = time.monotonic() + BUTTON_PUBLICATION_DELAY_MS / 1000
                else:
                    publication = None
                    (root / "published").touch()
                    user.EnableWindow(button, True)
            return 0
        return user.DefWindowProcW(handle, message, first, second)

    assert user.SetWindowLongPtrW(window, -4, procedure)  # GWLP_WNDPROC.
    assert user.SetTimer(window, 1, COMMAND_POLL_MS, None)
    message = wintypes.MSG()
    while user.GetMessageW(ctypes.byref(message), None, 0, 0) > 0:
        user.TranslateMessage(ctypes.byref(message))
        user.DispatchMessageW(ctypes.byref(message))
    user.DestroyWindow(window)


def wait_marker(path):
    deadline = time.monotonic() + 10
    while not path.exists() and time.monotonic() < deadline:
        time.sleep(0.03)
    assert path.exists(), path


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--publisher":
        publisher(Path(sys.argv[2]))
        return
    with tempfile.TemporaryDirectory(prefix="native-button-readiness-") as name:
        root = Path(name)
        input_mode = sys.argv[1:] == ["--input"]
        if input_mode:
            (root / "input-mode").touch()
        # Windows venv python.exe redirects to a different process; the native
        # PID observer must own the stdlib-only publisher's actual process.
        publisher_python = Path(sys.base_prefix) / "python.exe" if sys.platform == "win32" else sys.executable
        child = subprocess.Popen([publisher_python, __file__, "--publisher", str(root)])
        try:
            dialogs = Dialogs()
            window = dialogs.wait(child, TITLE)
            if input_mode:
                from gi.repository import GLib

                dialogs.wait_text(window, INPUT_NAME)
                field = next(
                    node
                    for node in dialogs.children(window)
                    if node.get_role() in (dialogs.atspi.Role.TEXT, dialogs.atspi.Role.ENTRY)
                    and node.get_name() == INPUT_NAME
                )
                (root / "stall-role").touch()
                field.clear_cache()
                try:
                    dialogs.set_input(window, INPUT_NAME, INPUT_TEXT)
                except GLib.GError as error:
                    wait_marker(root / "resumed")
                    assert not (root / "actions").exists(), "A failed lookup must not mutate the input"
                    print("Read-only failure recovered:", repr(error), dialogs.title(window), flush=True)
                    raise
                assert (root / "resumed").exists()
                assert (root / "value").read_text(encoding="utf-8") == INPUT_TEXT
            else:
                dialogs.wait_text(window, "Close")
                if sys.platform == "win32":
                    import ctypes

                    def role(node):
                        name = ctypes.create_unicode_buffer(256)
                        dialogs.user.GetClassNameW(node, name, len(name))
                        return name.value

                    before = [(role(node), plain(dialogs.title(node))) for node in dialogs.children(window)]
                    assert ("Static", "Close") in before, before
                    assert ("Button", "Close") not in before, before
                else:
                    before = [(node.get_role_name(), plain(node.get_name())) for node in dialogs.children(window)]
                    assert ("label", "Close") in before, before
                    assert ("push button", "Close") not in before, before
                assert not (root / "published").exists()
                (root / "initial-observed").touch()
                (root / "publish").touch()
                try:
                    dialogs.click(window, "Close")
                except AssertionError:
                    print("Initial native tree:", before, flush=True)
                    raise
                assert (root / "published").exists(), "The native button must be published before activation"
                if sys.platform == "win32":
                    assert (root / "disabled-button").exists(), (
                        "The native button must become enabled before activation"
                    )
            wait_marker(root / "actions")
            assert (root / "actions").read_text(encoding="utf-8") == "1", "Exactly one native action is delivered"
            (root / "stop").touch()
            assert child.wait(timeout=10) == 0
            assert (root / "actions").read_text(encoding="utf-8") == "1"
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=10)
    print("PASS: exact native control readiness precedes one mutation")


if __name__ == "__main__":
    main()
