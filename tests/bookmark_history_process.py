"""Original bookmark/history settings through independent application processes."""
import argparse
import os
from pathlib import Path
import shutil
import tempfile
from settings_process import Application


def state(app, bookmark, history):
    assert (app.state['bookmark_margin'], app.state['change_history']) == (str(bookmark), str(history)), app.state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    source = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix='wxnotepad-bookmark-history-') as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ['PATH'])
        config = folder / 'Notepad4.ini'
        seed = folder / 'Notepad4.ini-default'
        seed.write_text('; keep comment\n[Settings]\nSaveSettings=0\nShowBookmarkMargin=1suffix\n'
                        'ChangeHistoryMarker=7suffix\nUnrelated=keep\n[Styles]\nUnrelatedStyle=保留\n', encoding='utf-16')
        app = Application(executable, folder, environment)
        try:
            state(app, 1, 7)
            app.menu('Bookmark Margin')
            app.menu('Change History Marker')
            app.menu('Save Settings Now')
            app.menu('Bookmark Margin')
            app.menu('Change History Marker')
            app.close()
        finally:
            app.stop()
        saved = config.read_text(encoding='utf-16')
        assert 'ShowBookmarkMargin=' not in saved and 'ChangeHistoryMarker=' not in saved, saved
        assert 'Unrelated=keep' in saved and 'UnrelatedStyle=保留' in saved and '; keep comment' in saved, saved
        app = Application(executable, folder, environment)
        try:
            state(app, 0, 0)
            app.menu('Save Settings On Exit')
            app.menu('Bookmark Margin')
            app.menu('Change History Marker')
            app.close()
        finally:
            app.stop()
        saved = config.read_text(encoding='utf-16')
        assert 'ShowBookmarkMargin=1' in saved and 'ChangeHistoryMarker=3' in saved, saved
        app = Application(executable, folder, environment)
        try:
            state(app, 1, 3)
            app.close()
        finally:
            app.stop()
        for value, bookmark, history in [('0', 0, 0), ('1', 1, 1), ('2', 0, 2), ('3', 0, 3),
                                         ('4', 0, 4), ('5', 0, 5), ('7', 0, 7), ('99', 0, 99),
                                         ('-1', 0, -1), ('1suffix', 1, 1), ('3suffix', 0, 3), ('invalid', 0, 0)]:
            config.write_text(f'[Settings]\nSaveSettings=0\nShowBookmarkMargin={value}\nChangeHistoryMarker={value}\n',
                              encoding='utf-16')
            app = Application(executable, folder, environment)
            try:
                state(app, bookmark, history)
                app.close()
            finally:
                app.stop()
    print('PASS: original numeric import, seed, explicit/exit save, disabled save and independent restore')


if __name__ == '__main__':
    main()
