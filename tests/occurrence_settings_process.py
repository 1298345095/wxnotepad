"""Original integer occurrence preferences across independent app processes."""
import os
import argparse
from pathlib import Path
import shutil
import tempfile
from settings_process import Application


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    source = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix='wxnotepad-occurrence-') as name:
        folder = Path(name)
        executable = folder / source.name
        shutil.copy2(source, executable)
        environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ['PATH'])
        config = folder / 'Notepad4.ini'
        (folder / 'Notepad4.ini-default').write_text(
            '; keep\n[Settings]\nSaveSettings=0\nMarkOccurrences=14suffix\nUnrelated=keep\n', encoding='utf-16')
        app = Application(executable, folder, environment)
        try:
            assert app.state['mark_occurrences'] == '14', app.state
            app.menu('Off')
            assert app.state['mark_occurrences'] == '15', app.state
            app.menu('Save Settings Now')
            app.menu('Match Case')
            app.close()
        finally:
            app.stop()
        saved = config.read_text(encoding='utf-16')
        assert 'MarkOccurrences=15' in saved and 'Unrelated=keep' in saved and '; keep' in saved, saved
        app = Application(executable, folder, environment)
        try:
            assert app.state['mark_occurrences'] == '15', app.state
            app.menu('Save Settings On Exit')
            app.menu('Match Whole Word Only')
            app.close()
        finally:
            app.stop()
        assert 'MarkOccurrences=11' in config.read_text(encoding='utf-16')
        for value, expected in [('0', '0'), ('1', '1'), ('2', '2'), ('15', '15'), ('31', '31'),
                                ('-1', '-1'), ('14suffix', '14'), ('invalid', '1')]:
            config.write_text(f'[Settings]\nSaveSettings=0\nMarkOccurrences={value}\n', encoding='utf-16')
            app = Application(executable, folder, environment)
            try:
                assert app.state['mark_occurrences'] == expected, (value, app.state)
                app.menu('Save Settings Now')
                app.close()
            finally:
                app.stop()
            saved = config.read_text(encoding='utf-16')
            assert ('MarkOccurrences=' not in saved if expected == '1' else f'MarkOccurrences={expected}\n' in saved), saved
    print('PASS: occurrence integer import, retained bits, explicit/exit persistence and preserved fields')


if __name__ == '__main__':
    main()
