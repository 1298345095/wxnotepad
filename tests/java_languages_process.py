"""Verify original language settings and Restart in actual packaged processes."""
import argparse
import json
from pathlib import Path
import tempfile

from localization_package import (Process, bundle_application, environment, menu_id, menus,
                                  settings, wait_child)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=Path)
parser.add_argument('--cmake-dir', required=True, type=Path)
parser.add_argument('--evidence', required=True, type=Path)
args = parser.parse_args()
args.evidence.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='java-languages-') as name:
    root = Path(name)
    executable = bundle_application(args.executable.resolve(), args.cmake_dir.resolve(), root / 'bundle',
                                     args.evidence / root.name / 'bundle')
    config = executable.parent / 'Notepad4.ini'
    children = root / 'children'
    children.mkdir()
    restart = automatic = None
    for code, preference in [('en', 1033), ('zh_CN', 2052), ('zh_TW', 1028)]:
        for scheme, suffix, section in [('Java', 'java', 'Java Source'), ('C#', 'csx', 'C# Source'),
                                         ('Kotlin', 'kts', 'Kotlin Source')]:
            extra = ('DetectIndentation=0\nFileWatchingMode=0\n'
                     f'[{section}]\nUseGlobalTabSettings=0\nTabWidth=6\nIndentWidth=2\nTabsAsSpaces=1\n')
            settings(config, preference, extra)
            path = root / f'{code}-document-界.{suffix}'
            original = b'\xef\xbb\xbf' + '// preserved 界\r\nclass Sample {}\r\n'.encode()
            path.write_bytes(original)
            app = Process(executable, root / f'app-{code}-{suffix}', environment(executable, children, code), path)
            child = None
            try:
                if code == 'en':
                    restart = menu_id(app.state, 'Restart')
                    automatic = menu_id(app.state, 'Automatic')
                chosen = menu_id(app.state, scheme)
                assert scheme in app.state['document_status'], app.state
                assert app.state['tab_width'] == '6' and app.state['indent_width'] == '2', app.state
                app.choose(chosen)
                before = dict(app.state)
                previous = {folder.name for folder in children.iterdir()}
                app.begin('menu', str(restart))
                assert app.process.wait(timeout=10) == 0
                child = wait_child(children, previous)
                for field in ('document_path', 'length', 'encoding', 'bom', 'eol_mode', 'tab_width', 'indent_width'):
                    assert child.state[field] == before[field], (field, before, child.state)
                assert child.process.pid != app.process.pid
                assert menus(child.state)[menu_id(child.state, scheme)]['checked'] == '1', child.state
                assert scheme in child.state['document_status'], child.state
                child.send('save')
                assert path.read_bytes() == original
                child.choose(automatic)
                assert scheme in child.state['document_status'], child.state
                assert menus(child.state)[automatic]['checked'] == '1'
                log = args.evidence / root.name / f'{code}-{suffix}.json'
                log.write_text(json.dumps({'parent': before, 'child': child.state}, ensure_ascii=False, indent=2),
                               encoding='utf-8')
                child.close()
            finally:
                app.stop()
                if child:
                    child.stop()
            print(f'PASS: packaged {code} {scheme} native menu, independent Restart, original settings, BOM/EOL and saved bytes',
                  flush=True)
