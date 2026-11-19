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


def publisher(root):
    sys.path.append('/usr/lib/python3/dist-packages')
    import gi
    gi.require_version('Gtk', '3.0')
    from gi.repository import GLib, Gtk
    window = Gtk.Window(title=TITLE)
    contents = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
    window.add(contents)
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
        if (root / 'publish').exists():
            (root / 'publish').unlink()
            GLib.timeout_add(BUTTON_PUBLICATION_DELAY_MS, publish)
        return GLib.SOURCE_CONTINUE

    GLib.timeout_add(COMMAND_POLL_MS, commands)
    Gtk.main()


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--publisher':
        publisher(Path(sys.argv[2]))
        return
    with tempfile.TemporaryDirectory(prefix='native-button-readiness-') as name:
        root = Path(name)
        child = subprocess.Popen([sys.executable, __file__, '--publisher', str(root)])
        try:
            dialogs = Dialogs()
            window = dialogs.wait(child, TITLE)
            dialogs.wait_text(window, 'Close')
            before = [(node.get_role_name(), plain(node.get_name())) for node in dialogs.children(window)]
            assert ('label', 'Close') in before, before
            assert ('push button', 'Close') not in before, before
            (root / 'publish').touch()
            try:
                dialogs.click(window, 'Close')
            except AssertionError:
                print('Initial native tree:', before, flush=True)
                raise
            deadline = time.monotonic() + 10
            while not (root / 'actions').exists() and time.monotonic() < deadline:
                time.sleep(.03)
            assert (root / 'published').exists(), 'The native button must be published before activation'
            assert (root / 'actions').read_text(encoding='utf-8') == '1', 'Exactly one native action is delivered'
            (root / 'stop').touch()
            assert child.wait(timeout=10) == 0
            assert (root / 'actions').read_text(encoding='utf-8') == '1'
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait(timeout=10)
    print('PASS: exact native button role/caption waits for publication and activates once')


if __name__ == '__main__':
    main()
