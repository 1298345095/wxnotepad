"""Exercise native button publication through the packaged UI's AT-SPI driver."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from localization_native import Dialogs, plain

TITLE = 'Native button readiness'
BUTTON_PUBLICATION_DELAY_MS = 250
COMMAND_POLL_MS = 10
PROPERTY_BUSY_SECONDS = 3.5  # A finite native property delay inside the existing ten-second readiness budget.
INPUT_NAME = 'Line'
INPUT_TEXT = 'native value 中文'


def publisher(root):
    sys.path.append('/usr/lib/python3/dist-packages')
    import gi
    gi.require_version('Gtk', '3.0')
    gi.require_version('Atk', '1.0')
    from gi.repository import Atk, GLib, Gtk
    window = Gtk.Window(title=TITLE)
    # Wayland's client-side titlebar otherwise adds its own Close push button.
    window.set_decorated(False)
    contents = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
    window.add(contents)
    input_mode = (root / 'input-mode').exists()
    if input_mode:
        class BusyAccessible(Gtk.EntryAccessible):
            __gtype_name__ = 'NativeReadinessBusyAccessible'

            def do_get_role(self):
                if (root / 'stall-role').exists():
                    (root / 'stall-role').unlink()
                    (root / 'blocked').touch()
                    time.sleep(PROPERTY_BUSY_SECONDS)
                    (root / 'resumed').touch()
                return Atk.Role.TEXT

        class BusyEntry(Gtk.Entry):
            __gtype_name__ = 'NativeReadinessBusyEntry'

        BusyEntry.set_accessible_type(BusyAccessible)
        field = BusyEntry()
        field.get_accessible().set_name(INPUT_NAME)
        field.set_text('original')
        contents.pack_start(field, True, True, 0)
        (root / 'value').write_text('original', encoding='utf-8')
    else:
        # The caption is already visible; only a real native button may be clicked.
        placeholder = Gtk.Label(label='Close')
        contents.pack_start(placeholder, True, True, 0)
    window.show_all()
    actions = 0

    def clicked(button):
        nonlocal actions
        actions += 1
        pending = root / 'actions.tmp'
        pending.write_text(str(actions), encoding='utf-8')
        pending.replace(root / 'actions')

    if input_mode:
        def changed(entry):
            (root / 'value').write_text(entry.get_text(), encoding='utf-8')
            clicked(entry)
        field.connect('changed', changed)

    def publish():
        placeholder.destroy()
        button = Gtk.Button(label='Close')
        button.connect('clicked', clicked)
        contents.pack_start(button, True, True, 0)
        button.show()
        (root / 'published').touch()
        return GLib.SOURCE_REMOVE

    def commands():
        if (root / 'stop').exists():
            Gtk.main_quit()
            return GLib.SOURCE_REMOVE
        if (root / 'initial-observed').exists() and (root / 'publish').exists():
            (root / 'publish').unlink()
            GLib.timeout_add(BUTTON_PUBLICATION_DELAY_MS, publish)
        return GLib.SOURCE_CONTINUE

    GLib.timeout_add(COMMAND_POLL_MS, commands)
    Gtk.main()


def wait_marker(path):
    deadline = time.monotonic() + 10
    while not path.exists() and time.monotonic() < deadline:
        time.sleep(.03)
    assert path.exists(), path


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--publisher':
        publisher(Path(sys.argv[2]))
        return
    with tempfile.TemporaryDirectory(prefix='native-button-readiness-') as name:
        root = Path(name)
        input_mode = sys.argv[1:] == ['--input']
        if input_mode:
            (root / 'input-mode').touch()
        child = subprocess.Popen([sys.executable, __file__, '--publisher', str(root)])
        try:
            dialogs = Dialogs()
            window = dialogs.wait(child, TITLE)
            if input_mode:
                from gi.repository import GLib
                dialogs.wait_text(window, INPUT_NAME)
                field = next(node for node in dialogs.children(window)
                             if node.get_role() in (dialogs.atspi.Role.TEXT, dialogs.atspi.Role.ENTRY)
                             and node.get_name() == INPUT_NAME)
                (root / 'stall-role').touch()
                field.clear_cache()
                try:
                    dialogs.set_input(window, INPUT_NAME, INPUT_TEXT)
                except GLib.GError as error:
                    wait_marker(root / 'resumed')
                    assert not (root / 'actions').exists(), 'A failed lookup must not mutate the input'
                    print('Read-only failure recovered:', repr(error), dialogs.title(window), flush=True)
                    raise
                assert (root / 'resumed').exists()
                assert (root / 'value').read_text(encoding='utf-8') == INPUT_TEXT
            else:
                dialogs.wait_text(window, 'Close')
                before = [(node.get_role_name(), plain(node.get_name())) for node in dialogs.children(window)]
                assert ('label', 'Close') in before, before
                assert ('push button', 'Close') not in before, before
                assert not (root / 'published').exists()
                (root / 'initial-observed').touch()
                (root / 'publish').touch()
                try:
                    dialogs.click(window, 'Close')
                except AssertionError:
                    print('Initial native tree:', before, flush=True)
                    raise
                assert (root / 'published').exists(), 'The native button must be published before activation'
            wait_marker(root / 'actions')
            assert (root / 'actions').read_text(encoding='utf-8') == '1', 'Exactly one native action is delivered'
            (root / 'stop').touch()
            assert child.wait(timeout=10) == 0
            assert (root / 'actions').read_text(encoding='utf-8') == '1'
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=10)
    print('PASS: exact native control readiness precedes one mutation')


if __name__ == '__main__':
    main()
