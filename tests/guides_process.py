"""Exercise original guide/edge imports and saves in independent native processes."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(executable, folder, settings, expected, action='keep', fixture=None):
    ini = folder / 'Notepad4.ini'
    ini.write_text(settings, encoding='utf-16')
    arguments = [str(executable), '--preferences', *map(str, expected), action]
    if fixture is not None:
        source = folder / 'modeline.py'
        source.write_text(fixture, encoding='utf-8')
        arguments.append(str(source))
    result = subprocess.run(arguments,
                            capture_output=True, text=True, encoding='utf-8', timeout=20)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    saved = ini.read_text(encoding='utf-16')
    if '; preserved' not in saved or 'Custom=retained' not in saved:
        raise AssertionError('settings save must preserve unknown text')
    return saved


def main():
    with tempfile.TemporaryDirectory(prefix='wxnotepad-guides-') as directory:
        folder = Path(directory)
        executable = folder / Path(sys.argv[1]).name
        shutil.copy2(Path(sys.argv[1]).resolve(), executable)
        base = '[Settings]\n; preserved\nCustom=retained\n'
        run(executable, folder, base, (0, 0, 80))
        enabled = base + 'ShowIndentGuides=1\nMarkLongLines=1\nLongLineMode=2\nLongLinesLimit=91\n'
        saved = run(executable, folder, enabled, (2, 2, 91))
        for key in ('ShowIndentGuides=1', 'MarkLongLines=1', 'LongLineMode=2', 'LongLinesLimit=91'):
            assert key in saved, key
        advanced = enabled + '[Settings2]\nSimpleIndentGuides=1suffix\n'
        saved = run(executable, folder, advanced, (1, 2, 91), 'toggle')
        assert 'ShowIndentGuides=' not in saved and 'MarkLongLines=' not in saved
        assert 'SimpleIndentGuides=1suffix' in saved and 'LongLineMode=2' in saved
        saved = run(executable, folder, enabled + 'SaveSettings=0\n', (2, 2, 91), 'toggle')
        assert 'ShowIndentGuides=1' in saved and 'MarkLongLines=1' in saved
        saved = run(executable, folder, enabled + 'SaveSettings=0\n', (2, 2, 91), 'explicit')
        assert 'ShowIndentGuides=' not in saved and 'MarkLongLines=' not in saved
        for limit, mode, expected in [('-1', '-1', (0, 1, 0)), ('9999', '9', (0, 2, 4096)),
                                      ('invalid', 'invalid', (0, 1, 80)), ('175suffix', '2suffix', (0, 2, 175))]:
            run(executable, folder, base + f'MarkLongLines=1\nLongLinesLimit={limit}\nLongLineMode={mode}\n', expected)
        saved = run(executable, folder, base, (0, 0, 80), 'dialog')
        assert 'LongLinesLimit=35' in saved and 'LongLineMode=2' in saved and 'MarkLongLines=1' in saved
        assert 'SimpleIndentGuides=' not in saved, 'advanced load-only flag must not be created'
        for fixture, boundary in [('fill-column: 22\n', 22), ('fill-column: -1\n', 0), ('fill-column: 9999\n', 4096),
                                  ('tab-width: 3\n' + 'x' * 600 + '\nfill-column: 22\n', 91),
                                  ('unrecognized\n' + 'x' * 600 + '\nfill-column: 22\n', 22),
                                  ('enable-local-variables: 0; fill-column: 22\n', 91)]:
            saved = run(executable, folder, enabled, (2, 2, boundary), fixture=fixture)
            assert 'LongLinesLimit=91' in saved, 'opening modeline must not persist its boundary as the global limit'
        run(executable, folder, enabled + '[Settings2]\nNoFileVariables=1\n', (2, 2, 91), fixture='fill-column: 22\n')
    print('PASS: independent native guide/edge import, explicit/exit saves, defaults, bounds and advanced read-only flag')


if __name__ == '__main__':
    main()
