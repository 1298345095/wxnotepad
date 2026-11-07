"""Import original wrap-symbol captures through real portable INIs and independent native processes.

Reference: Notepad4 source 89b8b36e0ee2c23f73be922315f17261ccfbbe1a.
The literal TSV was independently captured from the original Windows executable;
its legacy build log did not record a source revision. See issue #145 evidence.
"""
import csv
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


source = Path(sys.argv[1]).resolve()
reference = Path(sys.argv[2]).resolve()
environment = dict(os.environ, PATH=str(source.parent) + os.pathsep + os.environ['PATH'])
with tempfile.TemporaryDirectory(prefix='wxnotepad-line-symbols-') as name:
    folder = Path(name)
    executable = folder / source.name
    shutil.copy2(source, executable)
    config = folder / 'Notepad4.ini'

    def run(mode, row):
        result = subprocess.run([str(executable), mode, row['flags'], row['location'], row['numbers']],
                                env=environment, capture_output=True, timeout=15, check=False)
        print(result.stdout.decode('utf-8', errors='replace'), end='')
        print(result.stderr.decode('utf-8', errors='replace'), end='', file=sys.stderr)
        assert result.returncode == 0, (mode, row, result.returncode)

    def seed(row, save=0, visible=0):
        placement = '' if row['input'] == 'omit' else f"WordWrapSymbols={row['input']}\n"
        return (f'[Settings]\nSaveSettings={save}\nWordWrap=0\nShowLineNumbers=0\n'
                f'ViewEOLs={visible}\nShowWordWrapSymbols={visible}\nWarnLineEndings=0\n' + placement +
                'UnrelatedSetting=keep\n[Styles]\nUnrelatedStyle=保留\n')

    with reference.open(encoding='utf-8') as captured:
        rows = list(csv.DictReader(captured, delimiter='\t'))
    for row in rows:
        if row['input'] == 'omit':
            (folder / 'Notepad4.ini-default').write_text(seed(row), encoding='utf-16')
        else:
            config.write_text(seed(row), encoding='utf-16')
        run('enable', row)
        saved = config.read_text(encoding='utf-16')
        assert 'ViewEOLs=1' in saved and 'ShowWordWrapSymbols=1' in saved, (row, saved)
        placement = [line.split('=', 1)[1] for line in saved.splitlines() if line.startswith('WordWrapSymbols=')]
        assert placement == ([] if row['saved_placement'] == 'omit' else [row['saved_placement']]), (row, saved)
        assert 'SaveSettings=0' in saved and 'UnrelatedSetting=keep' in saved and 'UnrelatedStyle=保留' in saved, saved
        assert ('ShowLineNumbers=0' not in saved) == (row['numbers'] == '1'), (row, saved)
        run('restore', row)
        print('PASS: original setting', row['input'])

    row = next(row for row in rows if row['input'] == '31')
    for save in (0, 1):
        initial = seed(row, save)
        config.write_text(initial, encoding='utf-16')
        run('exit', row)
        saved = config.read_text(encoding='utf-16')
        if save:
            assert 'ViewEOLs=1' in saved and 'ShowWordWrapSymbols=1' in saved and 'WordWrapSymbols=31' in saved, saved
        else:
            assert 'ViewEOLs=0' in saved and 'ShowWordWrapSymbols=0' in saved and 'WordWrapSymbols=31' in saved, saved
        assert 'UnrelatedSetting=keep' in saved and 'UnrelatedStyle=保留' in saved, saved
    zero = next(row for row in rows if row['input'] == '0')
    config.write_text(seed(zero), encoding='utf-16')
    run('hidden-save', zero)
    saved = config.read_text(encoding='utf-16')
    assert 'WordWrapSymbols=0' in saved and 'ShowWordWrapSymbols=' not in saved and 'ViewEOLs=' not in saved, saved
    normal = next(row for row in rows if row['input'] == '1')
    for boolean, mode in (('invalid', 'enable'), ('2', 'enable'), ('-1', 'enable'),
                          ('0suffix', 'enable'), ('1suffix', 'restore')):
        initial = seed(normal).replace('ViewEOLs=0', f'ViewEOLs={boolean}').replace(
            'ShowWordWrapSymbols=0', f'ShowWordWrapSymbols={boolean}')
        config.write_text(initial, encoding='utf-16')
        run(mode, normal)
print('PASS: portable import, original placement normalization, explicit save, exit save and restart')
