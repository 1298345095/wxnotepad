"""External native dialog/accessibility driver for owned packaged app processes."""

import ctypes
from ctypes import wintypes
import os
import sys
import time

from resource_process import wait_state
from weston_scene import mapped_toplevel

ATSPI_IPC_ERROR = 1  # ATSPI_ERROR_IPC; this native error enum is not exposed by the GI bindings.


def plain(text):
    return text.replace("&", "").replace("_", "")


class Dialogs:
    def __init__(self, evidence=None):
        self.evidence = evidence
        self.wait_sequence = 0
        self.key_sequence = 0
        if sys.platform == "win32":
            self.user = ctypes.WinDLL("user32", use_last_error=True)
            self.user.SetProcessDpiAwarenessContext.argtypes = [wintypes.HANDLE]
            assert self.user.SetProcessDpiAwarenessContext(wintypes.HANDLE(-4)), ctypes.WinError(
                ctypes.get_last_error()
            )
            self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
            self.user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
            self.user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
            self.user.IsWindowVisible.argtypes = [wintypes.HWND]
            self.user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
            self.user.SendMessageW.restype = wintypes.LPARAM
            self.user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
            self.callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            self.user.EnumWindows.argtypes = [self.callback, wintypes.LPARAM]
            self.user.EnumChildWindows.argtypes = [wintypes.HWND, self.callback, wintypes.LPARAM]
            ctypes.OleDLL("ole32").CoInitialize(None)
        else:
            # Ubuntu packages the native GI bindings here. Keep CMake's selected
            # interpreter (including coverage's isolated venv) and its packages.
            sys.path.append("/usr/lib/python3/dist-packages")
            import gi

            gi.require_version("Atspi", "2.0")
            from gi.repository import Atspi

            self.atspi = Atspi
            Atspi.set_timeout(1000, 1000)

    def windows(self, process):
        if sys.platform != "win32":
            result = []
            desktop = self.atspi.get_desktop(0)
            for index in range(desktop.get_child_count()):
                application = desktop.get_child_at_index(index)
                if application is not None and application.get_process_id() == process.pid:
                    for child_index in range(application.get_child_count()):
                        child = application.get_child_at_index(child_index)
                        # AT-SPI can lose a removed child between the count and
                        # indexed lookup; required-window checks stay with callers.
                        if child is not None:
                            result.append(child)
            return result
        result = []

        @self.callback
        def visit(window, unused):
            owner = wintypes.DWORD()
            self.user.GetWindowThreadProcessId(window, ctypes.byref(owner))
            if owner.value == process.pid and self.user.IsWindowVisible(window):
                result.append(window)
            return True

        self.user.EnumWindows(visit, 0)
        return result

    def title(self, window):
        if sys.platform != "win32":
            return window.get_name()
        text = ctypes.create_unicode_buffer(4096)
        self.user.GetWindowTextW(window, text, len(text))
        return text.value

    def children(self, window):
        if sys.platform != "win32":
            from gi.repository import GLib

            result = []
            try:
                for index in range(window.get_child_count()):
                    child = window.get_child_at_index(index)
                    if child is None:
                        continue
                    result.append(child)
                    result.extend(self.children(child))
            except GLib.GError as error:
                # GTK replaces file-list objects while a folder loads; removed
                # descendants are absent from this snapshot.
                if "does not exist" not in str(error):
                    raise
            return result
        result = []

        @self.callback
        def visit(child, unused):
            if self.user.IsWindowVisible(child):
                result.append(child)
            return True

        self.user.EnumChildWindows(window, visit, 0)
        return result

    def texts(self, window):
        if sys.platform != "win32":
            from gi.repository import GLib

            texts = []
            for child in self.children(window):
                try:
                    texts.append(self.title(child))
                except GLib.GError as error:
                    if "does not exist" not in str(error):
                        raise
            return texts
        texts = [self.title(child) for child in self.children(window)]
        texts.extend(self.accessible_names(window, entries_only=False))
        return texts

    def wait(self, process, title, message=None):
        read_errors = ()
        if sys.platform != "win32":
            from gi.repository import GLib

            read_errors = (GLib.GError,)
        deadline = time.monotonic() + 10
        observed = []
        observed_titles = []
        last_error = None
        mapping_observed = False
        self.wait_sequence += 1
        while time.monotonic() < deadline:
            assert process.poll() is None, process.returncode
            try:
                observed = self.windows(process)
                observed_titles = []
                for window in observed:
                    name = self.title(window)
                    observed_titles.append(name)
                    if name == title:
                        if sys.platform == "win32" or os.environ.get("GDK_BACKEND") != "wayland":
                            return window
                        mapped, scene = self.wayland_mapping(process.pid, title, deadline)
                        window.clear_cache()
                        if mapped and window.get_state_set().contains(self.atspi.StateType.SHOWING):
                            self.retain_native_scene(process.pid, "mapped", scene)
                            return window
                        if not mapping_observed:
                            self.retain_native_scene(process.pid, "before-map", scene)
                            mapping_observed = True
                    if message is not None and any(message in text for text in self.texts(window)):
                        return window
                    if sys.platform != "win32" and window.get_role() == self.atspi.Role.ALERT:
                        # GTK3 exposes the alert type (Question/Error) as its AT-SPI
                        # name. Select the actual visible application message.
                        fragments = {
                            "wxnotepad": "Changing the UI language requires a restart of wxnotepad, restart now?",
                            "Save changes?": "Save changes to ",
                            "Restart failed": "Could not restart wxnotepad.",
                            "Could not save settings": "Could not write ",
                        }
                        if title in fragments and any(fragments[title] in text for text in self.texts(window)):
                            return window
            except read_errors as error:
                if (
                    error.domain != "atspi_error"
                    or error.code != ATSPI_IPC_ERROR
                    or error.message != "timeout from dbind"
                ):
                    raise
                # These queries are read-only; retry discovery inside the same
                # deadline when the native publisher is temporarily busy.
                if last_error is None:
                    print(f"Native dialog {title!r} read-only lookup: {error}", file=sys.stderr, flush=True)
                last_error = str(error)
            time.sleep(0.03)
        self.observe_wait_failure(process, title, observed)
        raise AssertionError(
            f"Native dialog {title!r} not ready: {observed_titles}; last read-only error: {last_error}"
        )

    def wayland_mapping(self, pid, title, deadline):
        import subprocess

        remaining = deadline - time.monotonic()
        assert remaining > 0, "Native dialog mapping deadline expired"
        result = subprocess.run(
            ["weston-debug", "scene-graph"], capture_output=True, text=True, timeout=min(2, remaining), check=True
        )
        return mapped_toplevel(result.stdout, pid, title=title), result.stdout

    def retain_native_scene(self, pid, phase, scene):
        if self.evidence is not None:
            path = self.evidence / f"native-wait-{pid}-{self.wait_sequence}-{phase}.scene.txt"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(scene, encoding="utf-8")

    def observe_wait_failure(self, process, title, windows):
        if self.evidence is None or sys.platform == "win32":
            return
        import json
        import subprocess
        from localization_layout import capture_and_check

        prefix = self.evidence / f"native-wait-{process.pid}-{self.wait_sequence}"
        prefix.parent.mkdir(parents=True, exist_ok=True)
        states = []
        for window in windows:
            item = {
                "title": self.title(window),
                "cached_states": [str(state) for state in window.get_state_set().get_states()],
            }
            window.clear_cache()
            item["refreshed_states"] = [str(state) for state in window.get_state_set().get_states()]
            states.append(item)
        prefix.with_suffix(".json").write_text(
            json.dumps({"expected": title, "windows": states}, ensure_ascii=False), encoding="utf-8"
        )
        if os.environ.get("GDK_BACKEND") == "wayland":
            result = subprocess.run(["weston-debug", "scene-graph"], capture_output=True, text=True, timeout=2)
            prefix.with_suffix(".scene.txt").write_text(result.stdout + result.stderr, encoding="utf-8")
        display = getattr(capture_and_check, "_display", None)
        if display is not None:
            from gi.repository import Gdk

            root = display.get_default_screen().get_root_window()
            pixbuf = Gdk.pixbuf_get_from_window(root, 0, 0, root.get_width(), root.get_height())
            assert pixbuf and pixbuf.savev(str(prefix.with_suffix(".png")), "png", [], [])

    def click(self, window, caption, *, deadline=None):
        if deadline is None:
            deadline = time.monotonic() + 10
        if sys.platform == "win32":
            self.activate(window)
            self.user.IsWindowEnabled.argtypes = [wintypes.HWND]
            observed = []
            while time.monotonic() < deadline:
                observed = []
                for child in self.children(window):
                    kind = ctypes.create_unicode_buffer(256)
                    self.user.GetClassNameW(child, kind, len(kind))
                    if kind.value.lower() != "button":
                        continue
                    name = plain(self.title(child))
                    enabled = bool(self.user.IsWindowEnabled(child))
                    observed.append((name, enabled))
                    if name == caption and enabled:
                        # Wait for native publication; deliver the action once.
                        self.user.SendMessageW(child, 0x00F5, 0, 0)  # BM_CLICK
                        return
                time.sleep(0.03)
            raise AssertionError(f"Native button {caption!r} not ready; observed button names/enabled: {observed}")
        else:
            from gi.repository import GLib

            observed = []
            while time.monotonic() < deadline:
                # A removed descendant can leave children() with a partial tree.
                # Refresh native roles/names before deciding the button is absent.
                window.clear_cache()
                observed = []
                for child in self.children(window):
                    try:
                        role = child.get_role()
                        if role not in (self.atspi.Role.PUSH_BUTTON, self.atspi.Role.CHECK_BOX):
                            continue
                        name = plain(child.get_name())
                        observed.append((str(role), name))
                        if name != caption:
                            continue
                        action = child.get_action_iface()
                    except GLib.GError as error:
                        if "does not exist" not in str(error):
                            raise
                        continue
                    # Lookup waits for publication; activation is attempted once.
                    assert action and action.do_action(0), caption
                    return
                time.sleep(0.03)
            raise AssertionError(f"Native button {caption!r} not ready; observed button roles/names: {observed}")

    def wait_text(self, window, text):
        deadline = time.monotonic() + 10
        observed = []
        while time.monotonic() < deadline:
            observed = self.texts(window)
            if text in observed:
                return
            time.sleep(0.03)
        raise AssertionError((text, observed))

    def cancel(self, window):
        if sys.platform == "win32":
            self.user.SendMessageW(window, 0x0111, 2, 0)  # Native IDCANCEL.
        else:
            for caption in ("Cancel", "取消"):
                if any(plain(text) == caption for text in self.texts(window)):
                    self.click(window, caption)
                    return
            raise AssertionError(self.texts(window))

    def set_input(self, window, name, text):
        if sys.platform != "win32":
            from gi.repository import GLib

            deadline = time.monotonic() + 10
            last_error = None
            while time.monotonic() < deadline:
                target = None
                try:
                    window.clear_cache()
                    for child in self.children(window):
                        if (
                            child.get_role() in (self.atspi.Role.TEXT, self.atspi.Role.ENTRY)
                            and child.get_name() == name
                        ):
                            target = child.get_editable_text_iface()
                            break
                except GLib.GError as error:
                    if (
                        error.domain != "atspi_error"
                        or error.code != ATSPI_IPC_ERROR
                        or error.message != "timeout from dbind"
                    ):
                        raise
                    if last_error is None:
                        print(f"Native input {name!r} read-only lookup: {error}", file=sys.stderr, flush=True)
                    last_error = str(error)
                if target is not None:
                    # A timed-out write may already have happened; never repeat it.
                    assert target.set_text_contents(text)
                    return
                time.sleep(0.03)
            raise AssertionError(f"Accessible input {name!r} not ready; last read-only error: {last_error}")
        else:
            entries = []
            for child in self.children(window):
                kind = ctypes.create_unicode_buffer(256)
                self.user.GetClassNameW(child, kind, len(kind))
                if kind.value.lower() == "edit":
                    entries.append(child)
            names = self.accessible_names(window)
            for child, accessible_name in zip(entries, names):
                if accessible_name == name:
                    # Native character delivery also emits the real edit-change
                    # notifications used to enable the dialog's action buttons.
                    self.user.SendMessageW(child, 0x00B1, 0, -1)  # EM_SETSEL.
                    value = ctypes.create_unicode_buffer(text)
                    self.user.SendMessageW(
                        child, 0x00C2, 1, ctypes.cast(value, ctypes.c_void_p).value
                    )  # EM_REPLACESEL.
                    return
        observed = (
            [(child.get_role_name(), child.get_name()) for child in self.children(window)]
            if sys.platform != "win32"
            else self.accessible_names(window)
        )
        raise AssertionError(f"Accessible input {name!r} absent: {observed!r}")

    def activate(self, window):
        if sys.platform != "win32":
            return
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.user.GetForegroundWindow.restype = wintypes.HWND
        self.user.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
        self.user.SetForegroundWindow.argtypes = [wintypes.HWND]
        self.user.BringWindowToTop.argtypes = [wintypes.HWND]
        current = kernel.GetCurrentThreadId()
        foreground = self.user.GetWindowThreadProcessId(self.user.GetForegroundWindow(), None)
        target = self.user.GetWindowThreadProcessId(window, None)
        attached = []
        try:
            for thread in dict.fromkeys((foreground, target)):
                if thread != current and self.user.AttachThreadInput(current, thread, True):
                    attached.append(thread)
            self.user.BringWindowToTop(window)
            self.user.SetForegroundWindow(window)
        finally:
            for thread in attached:
                self.user.AttachThreadInput(current, thread, False)

    def key(self, app, key, window=None, expected_state=None):
        if sys.platform == "win32":
            self.user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
            handle = int(app.state["editor_handle"])
            if key == "ctrl+a":
                # Ctrl+A belongs to the editor's native key map, rather than an
                # application menu accelerator. Share only the two owned input
                # queues while delivering its synchronous native key messages.
                kernel = ctypes.WinDLL("kernel32", use_last_error=True)
                current = kernel.GetCurrentThreadId()
                target = self.user.GetWindowThreadProcessId(handle, None)
                message = wintypes.MSG()
                self.user.PeekMessageW(ctypes.byref(message), None, 0, 0, 0)
                self.user.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
                assert self.user.AttachThreadInput(current, target, True), ctypes.WinError(ctypes.get_last_error())
                previous = (wintypes.BYTE * 256)()
                try:
                    assert self.user.GetKeyboardState(previous)
                    keys = (wintypes.BYTE * 256).from_buffer_copy(previous)
                    vk_shift, vk_control, vk_menu = 0x10, 0x11, 0x12
                    keys[vk_shift] = keys[vk_menu] = 0
                    keys[vk_control] = 0x80
                    assert self.user.SetKeyboardState(keys)
                    self.user.SendMessageW(handle, 0x0100, ord("A"), 1)
                    self.user.SendMessageW(handle, 0x0101, ord("A"), 0xC0000001)
                finally:
                    self.user.SetKeyboardState(previous)
                    self.user.AttachThreadInput(current, target, False)
            else:
                assert key == "F10", key
                assert self.user.PostMessageW(handle, 0x0100, 0x79, 1)
                assert self.user.PostMessageW(handle, 0x0101, 0x79, 0xC0000001)
            deadline = time.monotonic() + 10
        else:
            import os
            import subprocess
            from pathlib import Path

            if window is not None and app.state["backend"] == "GdkWaylandDisplay":
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    window.clear_cache()
                    if window.get_state_set().contains(self.atspi.StateType.ACTIVE):
                        break
                    time.sleep(0.03)
                assert window.get_state_set().contains(self.atspi.StateType.ACTIVE), self.title(window)
            if app.state["backend"] == "GdkX11Display":
                search = ["xdotool", "search", "--all", "--onlyvisible", "--pid", str(app.process.pid)]
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    windows = subprocess.run(search, capture_output=True, text=True).stdout.splitlines()
                    if window is not None:
                        windows = [
                            identity
                            for identity in windows
                            if subprocess.check_output(["xdotool", "getwindowname", identity], text=True).strip()
                            == self.title(window)
                        ]
                    if windows:
                        break
                    time.sleep(0.03)
                assert windows, (
                    f"Owned native X11 window did not map: {self.title(window) if window else app.process.pid}"
                )
                subprocess.run(["xdotool", "windowfocus", "--sync", windows[0]], check=True)
            focus = Path(os.environ["WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS"])
            # The driver's retained log outlives each fixture process. Use a
            # fresh identity so an older reply cannot acknowledge this request.
            self.key_sequence = time.monotonic_ns()
            pending = focus.with_suffix(".localization-pending")
            pending.write_text(f"trace:{self.key_sequence}:key:{key}", encoding="utf-8")
            pending.replace(focus)
            # Claim acknowledgment precedes delivery. Wait for the existing
            # driver's completed native press/release before changing the field.
            diagnostics = Path(str(focus) + ".diagnostics")
            completed = f"	event=driver.inject-complete	gesture={self.key_sequence}	"
            deadline = time.monotonic() + 10
            observed = ""
            while time.monotonic() < deadline:
                observed = diagnostics.read_text(encoding="utf-8") if diagnostics.exists() else ""
                if completed in observed:
                    break
                time.sleep(0.01)
            Path(app.log.name).with_name("native-input.log").write_text(observed, encoding="utf-8")
            assert completed in observed, f"Native key press/release did not complete: {key}"
        if expected_state:
            latest = {}

            def matches(state):
                for field in ("sequence", "backend", "active", "focus", "length", "dirty", *expected_state):
                    latest[field] = state.get(field)
                return all(state.get(field) == value for field, value in expected_state.items())

            try:
                app.state = wait_state(app.folder, app.process, matches, timeout=max(0, deadline - time.monotonic()))
            except AssertionError as error:
                raise AssertionError(f"Native {key} expected {expected_state}; last app state: {latest}") from error

    def chooser_lookup(self, query, deadline, description):
        """Read a chooser snapshot; never repeat an action after an uncertain reply."""
        from gi.repository import GLib

        last_error = None
        while time.monotonic() < deadline:
            try:
                return query()
            except GLib.GError as error:
                if (
                    error.domain != "atspi_error"
                    or error.code != ATSPI_IPC_ERROR
                    or error.message != "timeout from dbind"
                ):
                    raise
                if last_error is None:
                    print(f"Native chooser {description} read-only lookup: {error}", file=sys.stderr, flush=True)
                last_error = str(error)
                time.sleep(0.03)
        raise AssertionError(f"Native chooser {description} lookup expired; last read-only error: {last_error}")

    def navigate_folder(self, app, window, folder):
        """Navigate the owned chooser to fixed disposable files before inspection."""
        import os

        if sys.platform == "win32":
            self.activate(window)
            for child in self.children(window):
                kind = ctypes.create_unicode_buffer(256)
                self.user.GetClassNameW(child, kind, len(kind))
                if kind.value == "Edit":
                    value = ctypes.create_unicode_buffer(str(folder) + os.sep)
                    self.user.SendMessageW(child, 0x00B1, 0, -1)
                    self.user.SendMessageW(child, 0x00C2, 1, ctypes.cast(value, ctypes.c_void_p).value)
                    self.user.PostMessageW(window, 0x0111, 1, 0)
                    break
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                for child in self.children(window):
                    kind = ctypes.create_unicode_buffer(256)
                    self.user.GetClassNameW(child, kind, len(kind))
                    if kind.value == "ToolbarWindow32" and str(folder) in self.title(child):
                        return
                time.sleep(0.03)
        else:
            self.key(app, "ctrl+l", window)
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                entries = [
                    child
                    for child in self.children(window)
                    if child.get_role() in (self.atspi.Role.TEXT, self.atspi.Role.ENTRY)
                    and child.get_editable_text_iface()
                    and child.get_state_set().contains(self.atspi.StateType.FOCUSED)
                ]
                if entries:
                    assert entries[0].get_editable_text_iface().set_text_contents(str(folder) + "/")
                    # GtkEntry activates the dialog's default button, which may
                    # still be disabled while the native folder model updates.
                    # Submit the actual chooser button once it can accept input.
                    while time.monotonic() < deadline:
                        buttons = self.chooser_lookup(
                            lambda: [
                                child
                                for child in self.children(window)
                                if child.get_role() == self.atspi.Role.PUSH_BUTTON
                                and child.get_name() == "Open"
                                and child.get_state_set().contains(self.atspi.StateType.SENSITIVE)
                            ],
                            deadline,
                            "Open",
                        )
                        if buttons:
                            # Read-only lookup can wait; this mutation stays outside
                            # its handler and is never retried after an uncertain reply.
                            assert buttons[0].get_action_iface().do_action(0)
                            break
                        time.sleep(0.03)
                    else:
                        raise AssertionError("Native chooser Open button did not become sensitive")
                    break
                time.sleep(0.03)
            while time.monotonic() < deadline:
                if folder.name in self.texts(window):
                    return
                time.sleep(0.03)
        raise AssertionError(f"Native file chooser did not navigate to owned folder {folder}: {self.texts(window)}")

    def resize(self, app, width, height):
        if sys.platform == "win32":
            self.user.SetWindowPos.argtypes = [
                wintypes.HWND,
                wintypes.HWND,
                ctypes.c_int,
                ctypes.c_int,
                ctypes.c_int,
                ctypes.c_int,
                wintypes.UINT,
            ]
            assert self.user.SetWindowPos(int(app.state["frame_handle"]), None, 0, 0, width, height, 0x0002 | 0x0004)
        else:
            windows = [window for window in self.windows(app.process) if self.title(window) == app.state["title"]]
            assert len(windows) == 1
            component = windows[0].get_component_iface()
            assert component.set_size(width, height)
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                size = component.get_size()
                if (size.x, size.y) == (width, height):
                    # X11 can report the native frame extents before GTK has
                    # delivered its configure/allocation event to wxWidgets.
                    from resource_process import wait_state

                    app.state = wait_state(
                        app.folder,
                        app.process,
                        lambda state: (int(state["frame_width"]), int(state["frame_height"])) == (width, height),
                        timeout=deadline - time.monotonic(),
                    )
                    return
                time.sleep(0.03)
            raise AssertionError(("Native resize did not complete", width, height, size.x, size.y))

    def wait_chooser_popup(self, control, showing, deadline):
        def popup_showing():
            control.clear_cache()
            popup = control.get_child_at_index(0)
            if popup is None:
                return False
            popup.clear_cache()
            return popup.get_state_set().contains(self.atspi.StateType.SHOWING)

        while time.monotonic() < deadline:
            observed = self.chooser_lookup(popup_showing, deadline, "filter popup")
            if observed == showing:
                return
            time.sleep(0.03)
        raise AssertionError(f"Native chooser filter popup showing={showing} not observed")

    def wait_chooser_filters(self, window, deadline):
        observed = []

        def actionable_controls():
            observed.clear()
            controls = []
            for child in self.children(window):
                child.clear_cache()
                role = child.get_role()
                action = child.get_action_iface() if role == self.atspi.Role.COMBO_BOX else None
                observed.append((str(role), action is not None))
                if action is not None:
                    controls.append((child, action))
            return controls

        waiting_reported = False
        while time.monotonic() < deadline:
            controls = self.chooser_lookup(actionable_controls, deadline, "filters")
            if controls:
                return controls
            if not waiting_reported:
                print(
                    f"Native chooser filters not yet actionable; roles/interfaces: {observed}",
                    file=sys.stderr,
                    flush=True,
                )
                waiting_reported = True
            time.sleep(0.03)
        raise AssertionError(f"Native chooser filter control not ready; last roles/interfaces: {observed}")

    def file_filters(self, window):
        if sys.platform == "win32":
            filters = []
            for child in self.children(window):
                kind = ctypes.create_unicode_buffer(256)
                self.user.GetClassNameW(child, kind, len(kind))
                if kind.value != "ComboBox":
                    continue
                count = self.user.SendMessageW(child, 0x0146, 0, 0)  # CB_GETCOUNT.
                for index in range(max(0, count)):
                    length = self.user.SendMessageW(child, 0x0149, index, 0)  # CB_GETLBTEXTLEN.
                    if length < 0:
                        continue
                    text = ctypes.create_unicode_buffer(length + 1)
                    self.user.SendMessageW(child, 0x0148, index, ctypes.cast(text, ctypes.c_void_p).value)
                    if "(*" in text.value:
                        filters.append(text.value)
            return filters
        # The chooser can still publish its filter controls after navigation.
        # Give only this read-only snapshot a bounded readiness wait; the outer
        # packaged-layout timeout and the native RPC timeout remain unchanged.
        deadline = time.monotonic() + 10
        controls = self.wait_chooser_filters(window, deadline)
        for child, action in controls:
            assert action.do_action(0), "Native chooser filter popup Open action"
            self.wait_chooser_popup(child, True, deadline)
            filters = [text for text in self.texts(window) if "(*" in text]
            assert action.do_action(0), "Native chooser filter popup Close action"
            self.wait_chooser_popup(child, False, deadline)
            if filters:
                return filters
        return [text for text in self.texts(window) if "(*" in text]

    def answer(self, process, choice, title="wxnotepad", window=None):
        window = window if window is not None else self.wait(process, title)
        if sys.platform == "win32":
            # Same native task-dialog buttons as FileCommandDriver, scoped to
            # the independently launched app's visible dialog.
            self.user.SendMessageW(window, 0x0400 + 102, {"yes": 6, "no": 7, "cancel": 2, "ok": 1}[choice], 0)
        else:
            captions = {
                "yes": ("Yes", "Save", "是(Y)", "是"),
                "no": ("No", "Discard", "否(N)", "否"),
                "cancel": ("Cancel", "取消"),
                "ok": ("OK", "确定", "確定"),
            }
            for child in self.children(window):
                if child.get_role() == self.atspi.Role.PUSH_BUTTON and plain(child.get_name()) in captions[choice]:
                    assert child.get_action_iface().do_action(0), choice
                    return window
            raise AssertionError((choice, self.texts(window)))
        return window

    def accessible_names(self, window, entries_only=True):
        if sys.platform != "win32":
            return [
                child.get_name()
                for child in self.children(window)
                if child.get_role() in (self.atspi.Role.TEXT, self.atspi.Role.ENTRY)
            ]

        # Query the actual native MSAA client accessible object. Its public
        # name is separate from wxWindow's stable internal SetName selector.
        class GUID(ctypes.Structure):
            _fields_ = [
                ("data1", wintypes.DWORD),
                ("data2", wintypes.WORD),
                ("data3", wintypes.WORD),
                ("data4", ctypes.c_ubyte * 8),
            ]

        class VariantData(ctypes.Union):
            _fields_ = [("integer", ctypes.c_long), ("storage", ctypes.c_void_p * 2)]

        class Variant(ctypes.Structure):
            _fields_ = [("type", ctypes.c_ushort), ("reserved", ctypes.c_ushort * 3), ("value", VariantData)]

        oleacc = ctypes.WinDLL("oleacc")
        oleaut = ctypes.OleDLL("oleaut32")
        oleacc.AccessibleObjectFromWindow.argtypes = [
            wintypes.HWND,
            wintypes.DWORD,
            ctypes.POINTER(GUID),
            ctypes.POINTER(ctypes.c_void_p),
        ]
        oleaut.SysFreeString.argtypes = [ctypes.c_void_p]
        oleaut.SysFreeString.restype = None
        iid = GUID(0x618736E0, 0x3C3D, 0x11CF, (ctypes.c_ubyte * 8)(0x81, 0x0C, 0, 0xAA, 0, 0x38, 0x9B, 0x71))
        result = []

        def table_of(accessible):
            return ctypes.cast(accessible, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents

        def release(accessible):
            ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(table_of(accessible)[2])(accessible)

        def names(accessible, descend):
            table = table_of(accessible)
            get_name = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, Variant, ctypes.POINTER(ctypes.c_void_p))(
                table[10]
            )
            get_count = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.POINTER(ctypes.c_long))(table[8])
            get_child = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, Variant, ctypes.POINTER(ctypes.c_void_p))(
                table[9]
            )
            count = ctypes.c_long()
            if descend:
                get_count(accessible, ctypes.byref(count))
            for identity in range(count.value + 1):
                value = Variant()
                value.type = 3  # VT_I4; zero is CHILDID_SELF.
                value.value.integer = identity
                returned = ctypes.c_void_p()
                if get_name(accessible, value, ctypes.byref(returned)) == 0 and returned:
                    result.append(ctypes.wstring_at(returned))
                    oleaut.SysFreeString(returned)
                if identity == 0:
                    continue
                dispatch = ctypes.c_void_p()
                if get_child(accessible, value, ctypes.byref(dispatch)) != 0 or not dispatch:
                    continue
                query = ctypes.WINFUNCTYPE(
                    ctypes.c_long, ctypes.c_void_p, ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p)
                )(table_of(dispatch)[0])
                child_accessible = ctypes.c_void_p()
                try:
                    if query(dispatch, ctypes.byref(iid), ctypes.byref(child_accessible)) == 0:
                        try:
                            names(child_accessible, True)
                        finally:
                            release(child_accessible)
                finally:
                    release(dispatch)

        for child in [window] + self.children(window):
            name = ctypes.create_unicode_buffer(256)
            self.user.GetClassNameW(child, name, len(name))
            if entries_only and name.value.lower() != "edit":
                continue
            accessible = ctypes.c_void_p()
            if oleacc.AccessibleObjectFromWindow(child, 0xFFFFFFFC, ctypes.byref(iid), ctypes.byref(accessible)) != 0:
                continue
            try:
                names(accessible, not entries_only)
            finally:
                release(accessible)
        return result
