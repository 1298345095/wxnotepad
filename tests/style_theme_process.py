"""Observe theme/profile lifecycle in the real relocated packaged application."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import sys
import tempfile

from localization_process import Acceptance
from localization_package import menu_id, menus, read_settings
from localization_native import Dialogs


CAPTIONS = {
    'en': ('Default', 'Dark'),
    'zh_CN': ('默认(D)', '深色(K)'),
    'zh_TW': ('預設(D)', '深色(K)'),
}
LANGUAGES = {'en': 1033, 'zh_CN': 2052, 'zh_TW': 1028}
DOCUMENT_FIELDS = ('length', 'dirty', 'encoding', 'bom', 'eol_mode', 'caret_position',
                   'selection_start', 'selection_end', 'zoom')


class ThemeAcceptance(Acceptance):
    def __init__(self, args, directory):
        if not args.prebuilt_package:
            super().__init__(args, directory)
        else:
            # VM validation consumes the actual frozen artifact: no synthetic
            # CMake cache or SDK stands in for its original native build.
            self.args = args
            self.directory = directory
            self.logs = args.evidence / 'runs' / directory.name
            self.executable = directory / 'bundle/artifact' / args.executable.name
            shutil.copytree(args.executable.parent, self.executable.parent)
            self.config = self.executable.parent / 'Notepad4.ini'
            self.observation = directory / 'children'
            self.observation.mkdir()
            self.working = directory / 'unrelated working 中文'
            self.working.mkdir()
            self.dialogs = Dialogs(self.logs)
            self.count = 0
            self.reference = None
        self.logs.mkdir(parents=True, exist_ok=True)
        artifact = self.executable.parent
        manifest = {
            'build': json.loads((artifact / 'build.json').read_text(encoding='utf-8')),
            'files': {str(path.relative_to(artifact)): hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in artifact.rglob('*') if path.is_file()},
        }
        (self.logs / 'tested-artifact.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')


def checked(app, code, dark):
    entries = menus(app.state)
    assert entries[menu_id(app.state, CAPTIONS[code][dark])]['checked'] == '1', app.state
    assert entries[menu_id(app.state, CAPTIONS[code][not dark])]['checked'] == '0', app.state


def record(fixture, name, app):
    destination = fixture.logs / (name + '.json')
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(app.state, ensure_ascii=False, indent=2), encoding='utf-8')


def launch(fixture, document):
    return fixture.launch(startup=document, arguments=('--startup-caret', '2,5'))


def close(app):
    try:
        app.close()
    finally:
        app.stop()


def change(fixture, app, code, dark, document, original):
    before = dict(app.state)
    app.choose(menu_id(app.state, CAPTIONS[code][dark]))
    checked(app, code, dark)
    for field in DOCUMENT_FIELDS:
        assert app.state[field] == before[field], (field, before[field], app.state[field])
    app.send('save')
    assert document.read_bytes() == original, 'Theme changes retain actual saved UTF-8 BOM/CRLF bytes'


def basic(fixture):
    document = fixture.directory / 'theme saved 中文.cpp'
    original = b'\xef\xbb\xbfint main() {\r\n  // style profiles\r\n  return 0;\r\n}\r\n'
    document.write_bytes(original)
    dark_file = fixture.config.with_name('Notepad4 DarkTheme.ini')
    fixture.config.unlink(missing_ok=True)
    dark_file.unlink(missing_ok=True)
    app = launch(fixture, document)
    try:
        system_code = {'&File': 'en', '文件(&F)': 'zh_CN', '檔案(&F)': 'zh_TW'}[app.state['menu_top.0.label']]
        checked(app, system_code, False)
        record(fixture, 'unconfigured-startup', app)
    finally:
        close(app)
    print('PASS: actual unconfigured packaged startup chooses Default with the native system UI language', flush=True)
    for code, preference in LANGUAGES.items():
        fixture.config.unlink(missing_ok=True)
        dark_file.unlink(missing_ok=True)
        fixture.config.write_text(f'[Settings]\nWarnLineEndings=0\n[Settings2]\nUILanguage={preference}\n',
                                  encoding='utf-16')
        app = launch(fixture, document)
        try:
            checked(app, code, False)
            change(fixture, app, code, True, document, original)
            assert not dark_file.exists(), 'A missing user Dark profile needs no writable copy for selection'
            main_before = fixture.config.read_bytes()
            app.choose(menu_id(app.state, CAPTIONS[code][True]))
            assert fixture.config.read_bytes() == main_before and not dark_file.exists(), 'Same theme is a no-op'
            record(fixture, 'missing-dark-' + code, app)
        finally:
            close(app)
        assert read_settings(fixture.config)['Styles'].getint('StyleTheme') == 1
        app = launch(fixture, document)
        try:
            checked(app, code, True)
            record(fixture, 'restart-dark-' + code, app)
            change(fixture, app, code, False, document, original)
        finally:
            close(app)
        app = launch(fixture, document)
        try:
            checked(app, code, False)
            record(fixture, 'restart-default-' + code, app)
        finally:
            close(app)
        assert document.read_bytes() == original
        print(f'PASS: actual packaged {code} default startup, missing Dark, same-theme no-op, save/exit and two independent restarts', flush=True)

    for save_on_exit in (True, False):
        default_text = (
            f'[Settings]\nSaveSettings={int(save_on_exit)}\nWarnLineEndings=0\nFileWatchingMode=0\n'
            '[Settings2]\nUILanguage=1033\n'
            '[Styles]\nStyleTheme=0\n'
            '[Global Styles]\nDefault Code Style=font:$(Code); size:13.5; weight:600\n'
            'Default Text Style=font:$(Text); size:16\n'
            '[C/C++ Source]\nUseDefaultCodeStyle=1\nDefault=size:+1.5\nComment=  italic; fore:#123456\n'
            '[Unavailable Language]\nUnknown=preserved-default\n')
        fixture.config.write_text(default_text, encoding='utf-16')
        dark_file.write_text(
            '[Global Styles]\nDefault Code Style=font:$(Code); size:14.5; fore:#D4D4D4; back:#1E1E1E\n'
            'Default Text Style=font:$(Text); size:19; fore:#D4D4D4; back:#1E1E1E\n'
            '[C/C++ Source]\nUseDefaultCodeStyle=1\nDefault=size:+2.5\n'
            '[Unavailable Language]\nUnknown=preserved-dark\n', encoding='utf-16')
        app = launch(fixture, document)
        try:
            checked(app, 'en', False)
            assert app.state['font_size_fractional'] == '1500', app.state
            change(fixture, app, 'en', True, document, original)
            assert app.state['font_size_fractional'] == '1700', app.state
            app.choose(menu_id(app.state, 'Use Default Code Style'))
            assert app.state['font_size_fractional'] == '2150', app.state
            change(fixture, app, 'en', False, document, original)
            assert app.state['font_size_fractional'] == '1500', app.state
            main = read_settings(fixture.config)
            dark = read_settings(dark_file)
            assert main['Styles'].getint('StyleTheme') == 1, 'Dirty switch saves the old theme before loading Default'
            assert main['Global Styles']['Default Code Style'] == 'font:$(Code); size:13.5; weight:600'
            assert main['C/C++ Source']['Default'] == 'size:+1.5'
            assert not dark['C/C++ Source'].getboolean('UseDefaultCodeStyle')
            assert dark['C/C++ Source']['Default'] == 'size:+2.5'
            assert main['Unavailable Language']['Unknown'] == 'preserved-default'
            assert dark['Unavailable Language']['Unknown'] == 'preserved-dark'
            assert 'Comment=  italic; fore:#123456' in fixture.config.read_text(encoding='utf-16')
            record(fixture, f'custom-default-save-{int(save_on_exit)}', app)
        finally:
            close(app)
        app = launch(fixture, document)
        try:
            checked(app, 'en', not save_on_exit)
            assert app.state['font_size_fractional'] == ('1500' if save_on_exit else '2150'), app.state
            change(fixture, app, 'en', True, document, original)
            assert app.state['font_size_fractional'] == '2150', app.state
            record(fixture, f'custom-dark-restart-save-{int(save_on_exit)}', app)
        finally:
            close(app)
        print(f'PASS: actual packaged customized profiles, fractional inheritance, dirty save-before-switch and restart SaveSettings={int(save_on_exit)}', flush=True)


@contextmanager
def readonly(directory):
    modes = {path: stat.S_IMODE(path.stat().st_mode) for path in (directory, *directory.rglob('*'))}
    try:
        for path, mode in modes.items():
            path.chmod(mode & ~0o222)
        assert not os.access(directory / 'bin', os.W_OK), 'Native installation directory is actually read-only'
        yield
    finally:
        for path, mode in modes.items():
            path.chmod(mode)


def installed(fixture):
    assert sys.platform != 'win32'
    parent = Path('/opt/wxnotepad-file-workflows-test')
    assert parent.is_dir() and os.access(parent, os.W_OK), 'Use the existing owned installation-test prefix'
    with tempfile.TemporaryDirectory(prefix='style-theme-', dir=parent) as name:
        prefix = Path(name)
        executable = prefix / 'bin' / fixture.executable.name
        shutil.copytree(fixture.executable.parent, executable.parent)
        (prefix / 'share').mkdir()
        (executable.parent / 'locale').rename(prefix / 'share/locale')
        fixture.executable = executable
        config_home = fixture.directory / 'isolated user config 中文'
        fixture.config = config_home / 'Notepad4/Notepad4.ini'
        fixture.config.parent.mkdir(parents=True)
        original_launch = fixture.launch
        fixture.launch = lambda *args, **kwargs: original_launch(
            *args, **kwargs, overrides={'XDG_CONFIG_HOME': str(config_home)})
        before = {str(path.relative_to(prefix)): path.read_bytes() for path in prefix.rglob('*') if path.is_file()}
        with readonly(prefix):
            basic(fixture)
            assert not (executable.parent / 'Notepad4.ini').exists()
            assert not (executable.parent / 'Notepad4 DarkTheme.ini').exists()
            after = {str(path.relative_to(prefix)): path.read_bytes() for path in prefix.rglob('*') if path.is_file()}
            assert after == before, 'Read-only installation output is untouched while user profiles persist'
        print('PASS: actual installed read-only /opt package, share/locale discovery, separate writable XDG profiles and restart', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--cmake-dir', type=Path)
    parser.add_argument('--prebuilt-package', action='store_true')
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--scenario', choices=('portable', 'installed'), default='portable')
    args = parser.parse_args()
    if not args.prebuilt_package and args.cmake_dir is None:
        parser.error('Use the real --cmake-dir, or --prebuilt-package for an already bundled artifact')
    args.evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='style-theme-process-') as name:
        fixture = ThemeAcceptance(args, Path(name))
        (basic if args.scenario == 'portable' else installed)(fixture)


if __name__ == '__main__':
    main()
