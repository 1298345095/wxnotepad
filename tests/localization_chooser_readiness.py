"""Exercise the folder driver's role lookup using a controlled GTK publisher."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace

from localization_native import Dialogs
from localization_button_readiness import (
    BUTTON_PUBLICATION_DELAY_MS,
    COMMAND_POLL_MS,
    PROPERTY_BUSY_SECONDS,
    wait_marker,
)

TITLE = "Owned chooser role readiness"


class RoleDelayDialogs(Dialogs):
    def __init__(self, root):
        super().__init__()
        self.root = root
        self.role_delay_armed = False

    def chooser_lookup(self, query, deadline, description):
        if description == "Open" and not self.role_delay_armed:
            self.role_delay_armed = True
            # The input write has returned. Arm before the first read-only
            # role query; a GTK idle callback can run after that query.
            (self.root / "stall-role").touch()
        return super().chooser_lookup(query, deadline, description)


def publisher(root):
    sys.path.append("/usr/lib/python3/dist-packages")
    import gi

    gi.require_version("Gtk", "3.0")
    gi.require_version("Atk", "1.0")
    from gi.repository import Atk, Gdk, GLib, Gtk

    window = Gtk.Window(title=TITLE)
    window.set_decorated(False)
    contents = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
    window.add(contents)
    field = Gtk.Entry()
    contents.pack_start(field, True, True, 0)
    destination = root / "owned-target"
    delayed_role = (root / "delay-role").exists()

    class BusyButtonAccessible(Gtk.ButtonAccessible):
        __gtype_name__ = "NativeChooserReadinessBusyButtonAccessible"

        def do_get_role(self):
            if (root / "stall-role").exists():
                (root / "stall-role").unlink()
                (root / "blocked").touch()
                time.sleep(PROPERTY_BUSY_SECONDS)
                (root / "resumed").touch()
            return Atk.Role.PUSH_BUTTON

    class BusyButton(Gtk.Button):
        __gtype_name__ = "NativeChooserReadinessBusyButton"

    BusyButton.set_accessible_type(BusyButtonAccessible)
    button = BusyButton(label="Open")
    contents.pack_start(button, True, True, 0)
    label = Gtk.Label(label="initial folder")
    contents.pack_start(label, True, True, 0)

    class BusyFilterAccessible(Gtk.ComboBoxAccessible):
        __gtype_name__ = "NativeChooserReadinessBusyFilterAccessible"

        def do_get_role(self):
            if (root / "stall-filter").exists():
                (root / "stall-filter").unlink()
                (root / "filter-blocked").touch()
                time.sleep(PROPERTY_BUSY_SECONDS)
                (root / "filter-resumed").touch()
                if delayed_role:
                    (root / "filter-role-pending").touch()

                    def publish_role():
                        (root / "popup-before-role").write_text(str((root / "popup-changes").exists()))
                        (root / "filter-role-pending").unlink()
                        (root / "filter-role-published").touch()
                        return GLib.SOURCE_REMOVE

                    GLib.timeout_add(BUTTON_PUBLICATION_DELAY_MS, publish_role)
            return Atk.Role.UNKNOWN if (root / "filter-role-pending").exists() else Atk.Role.COMBO_BOX

    class BusyFilter(Gtk.ComboBoxText):
        __gtype_name__ = "NativeChooserReadinessBusyFilter"

    class DelayedFilterAccessible(BusyFilterAccessible, Atk.Action):
        __gtype_name__ = "NativeChooserReadinessDelayedFilterAccessible"

        def do_do_action(self, index):
            assert index == 0
            receipt = root / "filter-actions"
            count = int(receipt.read_text()) if receipt.exists() else 0
            receipt.write_text(str(count + 1))
            combo = self.get_widget()
            if combo.get_property("popup-shown"):
                combo.popdown()
            else:

                def publish():
                    combo.popup()
                    return GLib.SOURCE_REMOVE

                GLib.timeout_add(BUTTON_PUBLICATION_DELAY_MS, publish)
            return True

    delayed_popup = (root / "delay-popup").exists()
    BusyFilter.set_accessible_type(DelayedFilterAccessible if delayed_popup else BusyFilterAccessible)
    filters = BusyFilter()
    filters.append_text("All Files (*)")
    filters.append_text("Text files (*.txt)")
    filters.set_active(0)
    contents.pack_start(filters, True, True, 0)
    popup_changes = 0
    popup_visible = False

    def popup_changed(combo, unused):
        nonlocal popup_changes, popup_visible
        visible = combo.get_property("popup-shown")
        if visible == popup_visible:
            return
        popup_visible = visible
        popup_changes += 1
        (root / "popup-changes").write_text(str(popup_changes))
        if not combo.get_property("popup-shown"):
            (root / "popup-closed").touch()

    filters.connect("notify::popup-shown", popup_changed)
    changes = actions = 0

    def changed(entry):
        nonlocal changes
        if entry.get_text() == str(destination) + "/":
            changes += 1
            (root / "changes").write_text(str(changes))

    field.connect("changed", changed)

    def clicked(unused):
        nonlocal actions
        actions += 1
        (root / "actions").write_text(str(actions))
        label.set_text(destination.name)

    button.connect("clicked", clicked)

    def key(unused, event):
        if event.keyval == Gdk.KEY_l and event.state & Gdk.ModifierType.CONTROL_MASK:
            field.grab_focus()
            return True
        return False

    window.connect("key-press-event", key)
    window.show_all()
    field.grab_focus()

    def commands():
        if (root / "stop").exists():
            Gtk.main_quit()
            return GLib.SOURCE_REMOVE
        return GLib.SOURCE_CONTINUE

    GLib.timeout_add(COMMAND_POLL_MS, commands)
    Gtk.main()


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--publisher":
        publisher(Path(sys.argv[2]))
        return
    with tempfile.TemporaryDirectory(prefix="chooser-role-probe-") as folder:
        root = Path(folder)
        (root / "owned-target").mkdir()
        delayed_popup = sys.argv[1:] == ["--popup-delay"]
        if delayed_popup:
            (root / "delay-popup").touch()
        delayed_role = sys.argv[1:] == ["--role-delay"]
        if delayed_role:
            (root / "delay-role").touch()
        with (root / "publisher.log").open("w") as output:
            child = subprocess.Popen(
                [sys.executable, __file__, "--publisher", str(root)], stdout=output, stderr=subprocess.STDOUT
            )
            try:
                dialogs = RoleDelayDialogs(root)
                window = dialogs.wait(child, TITLE)
                window.get_application().set_cache_mask(dialogs.atspi.Cache.NONE)
                window.clear_cache()
                app = SimpleNamespace(
                    process=child,
                    log=output,
                    state={
                        "backend": "GdkWaylandDisplay"
                        if os.environ.get("GDK_BACKEND") == "wayland"
                        else "GdkX11Display"
                    },
                )
                try:
                    dialogs.navigate_folder(app, window, root / "owned-target")
                except Exception:
                    if (root / "blocked").exists():
                        wait_marker(root / "resumed")
                    print(
                        "Native chooser changes=",
                        (root / "changes").read_text() if (root / "changes").exists() else "0",
                        "actions=",
                        (root / "actions").read_text() if (root / "actions").exists() else "0",
                        "blocked=",
                        (root / "blocked").exists(),
                        "resumed=",
                        (root / "resumed").exists(),
                        flush=True,
                    )
                    raise
                assert (root / "blocked").exists() and (root / "resumed").exists()
                assert (root / "changes").read_text() == "1"
                assert (root / "actions").read_text() == "1"
                (root / "stall-filter").touch()
                try:
                    observed_filters = dialogs.file_filters(window)
                    assert set(observed_filters) == {"All Files (*)", "Text files (*.txt)"}, observed_filters
                except Exception:
                    if (root / "filter-blocked").exists():
                        wait_marker(root / "filter-resumed")
                    print(
                        "Native filter blocked=",
                        (root / "filter-blocked").exists(),
                        "resumed=",
                        (root / "filter-resumed").exists(),
                        "popup changes=",
                        (root / "popup-changes").read_text() if (root / "popup-changes").exists() else "0",
                        flush=True,
                    )
                    raise
                assert (root / "filter-blocked").exists() and (root / "filter-resumed").exists()
                wait_marker(root / "popup-closed")
                assert (root / "popup-changes").read_text() == "2", (root / "popup-changes").read_text()
                if delayed_popup:
                    assert (root / "filter-actions").read_text() == "2"
                if delayed_role:
                    assert (root / "filter-role-published").exists()
                    assert (root / "popup-before-role").read_text() == "False"
                (root / "stop").touch()
                assert child.wait(timeout=10) == 0
            except Exception:
                print("Chooser publisher log:", (root / "publisher.log").read_text(), file=sys.stderr, flush=True)
                names = ("changes", "actions", "popup-changes", "filter-actions", "popup-closed")
                print(
                    "Chooser receipts:",
                    {name: (root / name).read_text() for name in names if (root / name).exists()},
                    file=sys.stderr,
                    flush=True,
                )
                raise
            finally:
                if child.poll() is None:
                    child.terminate()
                    child.wait(timeout=10)
    print("PASS: native chooser read-only role recovery preserves one write, one Open and one filter toggle pair")


if __name__ == "__main__":
    main()
