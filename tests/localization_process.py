"""Accept localization through the real packaged shown application and native UI."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from localization_native import Dialogs
from localization_package import (ROOT, Process, bundle_application, cache_values, environment, language_id,
                                  menu_id, menus, read_settings, settings, wait_child)
from resource_process import wait_state

# Independently reviewed wording from the pinned original resources and the two
# translation authors. These literals are not read from the delivered catalogs.
TEXT = {
    'en': {'file': '&File', 'untitled': 'Untitled', 'goto': 'Go To Line', 'line': 'Line', 'column': 'Column',
           'cancel': 'Cancel'},
    'zh_CN': {'file': '文件(&F)', 'untitled': '未命名', 'goto': '跳转到行', 'line': '行', 'column': '列', 'cancel': '取消'},
    'zh_TW': {'file': '檔案(&F)', 'untitled': '未命名', 'goto': '跳到行', 'line': '行', 'column': '欄', 'cancel': '取消'},
}
PREFERENCE = {'en': 1033, 'zh_CN': 2052, 'zh_TW': 1028}


class Acceptance:
    def __init__(self, args, directory):
        self.args = args
        self.directory = directory
        self.executable = bundle_application(args.executable.resolve(), args.cmake_dir.resolve(), directory / 'bundle')
        self.config = self.executable.parent / 'Notepad4.ini'
        self.observation = directory / 'children'
        self.observation.mkdir()
        self.working = directory / 'unrelated working 中文'
        self.working.mkdir()
        self.dialogs = Dialogs()
        self.count = 0
        self.reference = None

    def start(self, language=1033, preferred='en', extra='', save=False, startup=None, arguments=()):
        settings(self.config, language, extra, save)
        return self.launch(preferred, startup, arguments)

    def launch(self, preferred='en', startup=None, arguments=()):
        self.count += 1
        return Process(self.executable, self.directory / f'app-{self.count:03}',
                       environment(self.executable, self.observation, preferred), startup, arguments, self.working)

    def displayed(self, app, code, configured):
        text = TEXT[code]
        assert app.state['menu_top.0.label'] == text['file'], (code, app.state)
        assert app.state['title'] == text['untitled'] + ' - wxnotepad', app.state
        selected = [identity for identity in (language_id(app.state, value) for value in (1024, 1033, 2052, 1028))
                    if menus(app.state)[identity]['checked'] == '1']
        assert selected == ([] if configured is None else [language_id(app.state, configured)]), (configured, selected)
        if self.reference is not None:
            current = menus(app.state)
            assert set(current) == set(self.reference), 'Language must preserve menu command identities'
            for identity, original in self.reference.items():
                assert current[identity]['path'] == original['path'], (identity, current[identity], original)
                assert current[identity]['label'].partition('\t')[2] == original['label'].partition('\t')[2], (
                    'Translated menu retains accelerator', identity, current[identity], original)

    def goto(self, app, code):
        identity = next(identity for identity, item in self.reference.items()
                        if item['label'] == '&Goto Line...\tCtrl+G')
        app.begin('menu', str(identity))
        window = self.dialogs.wait(app.process, TEXT[code]['goto'])
        names = self.dialogs.accessible_names(window)
        assert TEXT[code]['line'] in names and TEXT[code]['column'] in names, (code, names)
        buttons = [text.replace('&', '').replace('_', '') for text in self.dialogs.texts(window)]
        assert TEXT[code]['cancel'] in buttons, (code, buttons)
        self.dialogs.click(window, TEXT[code]['cancel'])
        app.finish()

    def startup(self):
        for code in ('en', 'zh_CN', 'zh_TW'):
            app = self.start(PREFERENCE[code], preferred='zh_TW' if code == 'en' else 'en')
            try:
                self.displayed(app, code, PREFERENCE[code])
                if code == 'en':
                    self.reference = menus(app.state)
                self.goto(app, code)
                app.close()
            finally:
                app.stop()
            print(f'PASS: actual packaged {code}, relocated resources, menu IDs/accelerators, native accessible dialog and stock Cancel', flush=True)
        cases = [
            (None, 'zh_TW', 'zh_TW', 1024), (1024, 'zh_CN', 'zh_CN', 1024),
            (1024, 'en:zh_CN', 'en', 1024), (1024, 'zh_TW:zh_CN', 'zh_TW', 1024),
            (1024, 'fr:en:zh_CN', 'en', 1024), (1024, 'fr:zh_CN:en', 'zh_CN', 1024),
            (2057, 'zh_TW', 'en', 1033), (1041, 'zh_CN', 'en', None),
            (3076, 'en', 'zh_TW', 1028), (5124, 'en', 'zh_TW', 1028), (4100, 'en', 'zh_CN', 2052),
            (67588, 'en', 'zh_CN', 2052), (-63484, 'en', 'zh_CN', 2052),
            ('2052suffix', 'en', 'zh_CN', 2052), (0, 'en', 'en', 1024), (9999, 'en', 'en', 1024),
            ('invalid', 'en', 'en', 1024),
        ]
        for configured, preferred, code, checked in cases:
            app = self.start(configured, preferred)
            original = self.config.read_bytes()
            try:
                self.displayed(app, code, checked)
                app.close()
            finally:
                app.stop()
            assert self.config.read_bytes() == original, 'Startup fallback/normalization must not rewrite configured preference'
            print(f'PASS: original integer {configured!r}, preferred {preferred} -> {code}, checked {checked}', flush=True)
        cache = cache_values(self.args.cmake_dir.resolve())
        gettext = Path(cache['WXNOTEPAD_MSGFMT']).parent
        for code in ('zh_CN', 'zh_TW'):
            app_mo = self.executable.parent / 'locale' / code / 'LC_MESSAGES/wxnotepad.mo'
            original = app_mo.read_bytes()
            try:
                for failure in ('missing', 'corrupt'):
                    if failure == 'missing':
                        app_mo.unlink()
                    else:
                        app_mo.write_bytes(b'not an MO catalog')
                    app = self.start(PREFERENCE[code])
                    before = self.config.read_bytes()
                    try:
                        self.displayed(app, 'en', PREFERENCE[code])
                        self.goto(app, 'en')
                        app.close()
                    finally:
                        app.stop()
                    assert self.config.read_bytes() == before
                    print(f'PASS: {code} {failure} application MO -> complete English app and stock fallback', flush=True)
                app_mo.write_bytes(original)
                one = self.directory / f'{code}-one.po'
                one.write_text('msgid "&Goto Line...\\tCtrl+G"\nmsgstr ""\n', encoding='utf-8')
                partial = self.directory / f'{code}-partial.po'
                subprocess.run([str(gettext / ('msgattrib.exe' if sys.platform == 'win32' else 'msgattrib')),
                                '--set-fuzzy', f'--only-file={one}', '-o', partial, ROOT / 'po' / (code + '.po')], check=True)
                subprocess.run([cache['WXNOTEPAD_MSGFMT'], '--check', '--check-format', '-o', app_mo, partial], check=True)
                app = self.start(PREFERENCE[code])
                try:
                    identity = next(identity for identity, item in self.reference.items()
                                    if item['label'] == '&Goto Line...\tCtrl+G')
                    assert menus(app.state)[identity]['label'] == '&Goto Line...\tCtrl+G', app.state
                    assert app.state['title'] == TEXT[code]['untitled'] + ' - wxnotepad', app.state
                    self.goto(app, code)
                    app.close()
                finally:
                    app.stop()
                print(f'PASS: {code} individual missing message uses source fallback while app and stock remain translated', flush=True)
            finally:
                app_mo.write_bytes(original)

    def prepare_reference(self):
        app = self.start()
        try:
            self.reference = menus(app.state)
            app.close()
        finally:
            app.stop()

    def change(self, app, preference, answer):
        app.begin('menu', str(language_id(app.state, preference)))
        prompt = self.dialogs.wait(app.process, 'wxnotepad')
        texts = self.dialogs.texts(prompt)
        assert 'Changing the UI language requires a restart of wxnotepad, restart now?' in texts, texts
        self.dialogs.answer(app.process, answer)

    def lifecycle(self):
        self.prepare_reference()
        app = self.start()
        try:
            original = self.config.read_bytes()
            app.choose(language_id(app.state, 1033))
            assert self.config.read_bytes() == original, 'Same exact preference is a no-op'
            self.change(app, 2052, 'cancel')
            app.finish()
            self.displayed(app, 'en', 1033)
            assert self.config.read_bytes() == original, 'Cancel changes neither selection nor INI'
            before = read_settings(self.config)
            self.change(app, 2052, 'no')
            app.finish()
            self.displayed(app, 'en', 2052)
            after = read_settings(self.config)
            before['Settings2']['UILanguage'] = '2052'
            assert dict(before) == dict(after), 'No immediately writes only UILanguage with exit saving off'
            previous = set(path.name for path in self.observation.iterdir())
            app.choose(menu_id(app.state, 'New Window'))
            child = wait_child(self.observation, previous)
            try:
                self.displayed(child, 'zh_CN', 2052)
                child.close()
            finally:
                child.stop()
            app.close()
        finally:
            app.stop()
        print('PASS: same/Cancel/No, immediate single-key persistence, deferred current UI and real New Window application', flush=True)

        app = self.start(2057)
        try:
            original = self.config.read_bytes()
            self.change(app, 1033, 'cancel')
            app.finish()
            assert self.config.read_bytes() == original, 'Imported UK English differs numerically from canonical English'
            app.close()
        finally:
            app.stop()

        older = self.start(save=True)
        newer = self.launch()
        try:
            self.change(newer, 1028, 'no')
            newer.finish()
            older.choose(menu_id(older.state, 'Save Settings Now'))
            older.close()
            newer.close()
            assert read_settings(self.config)['Settings2']['UILanguage'] == '1028'
        finally:
            older.stop()
            newer.stop()
        print('PASS: older-window explicit and exit settings saves preserve a newer dedicated language choice', flush=True)

        app = self.start()
        preserved = self.config.with_suffix('.preserved')
        original = self.config.read_bytes()
        self.config.rename(preserved)
        self.config.mkdir()
        try:
            self.change(app, 2052, 'no')
            self.dialogs.answer(app.process, 'ok', 'Could not save settings')
            app.finish()
            self.displayed(app, 'en', 1033)
            assert preserved.read_bytes() == original
        finally:
            self.config.rmdir()
            preserved.rename(self.config)
            try:
                app.close()
            finally:
                app.stop()
        print('PASS: real settings commit failure reports an error and retains configured/current language', flush=True)

    def native(self):
        self.prepare_reference()
        inserts = [identity for identity, item in self.reference.items()
                   if item['label'] in ('&File Name', 'File Name and E&xtension\tCtrl+F9',
                                        'Full &Path Name\tCtrl+Shift+F9')]
        # The source File Name caption also occurs in Copy; select the insertion
        # siblings by the parent of its distinct Ctrl+F9 command.
        parent = next(item['path'].rsplit('/', 1)[0] for item in self.reference.values()
                      if item['label'] == 'File Name and E&xtension\tCtrl+F9')
        inserts = [identity for identity in inserts if self.reference[identity]['path'].rsplit('/', 1)[0] == parent]
        assert len(inserts) == 3
        for code in ('en', 'zh_CN', 'zh_TW'):
            app = self.start(PREFERENCE[code])
            try:
                self.goto(app, code)
                for index, identity in enumerate(inserts):
                    app.choose(identity)
                    destination = self.directory / f'unnamed-{code}-{index}.txt'
                    app.send('save', str(destination))
                    assert destination.read_bytes() == TEXT[code]['untitled'].encode('utf-8')
                    app.choose(5002)  # Same real New command as the numeric seam regression.
                readonly = next(identity for identity, item in self.reference.items()
                                if item['label'] == '&Read Only Mode\tF10')
                self.dialogs.key(app, 'F10')
                app.state = wait_state(app.folder, app.process, lambda state: state['read_only'] == '1')
                assert menus(app.state)[readonly]['checked'] == '1'
                self.dialogs.key(app, 'F10')
                app.state = wait_state(app.folder, app.process, lambda state: state['read_only'] == '0')
                # Exercise the actual shown native layout at the current DPI.
                self.dialogs.resize(app, 1100, 720)
                app.choose(readonly)
                assert int(app.state['frame_width']) == 1100 and int(app.state['frame_height']) == 720, app.state
                assert int(app.state['client_width']) > 0 and int(app.state['client_height']) > 0
                self.goto(app, code)
                app.choose(readonly)
                app.close()
            finally:
                app.stop()
            print(f'PASS: {code} native accessible labels, stock captions, unnamed filename/path bytes, F10 and resized layout', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--cmake-dir', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--scenario', choices=('startup', 'lifecycle', 'restart', 'native'), default='startup')
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=args.scenario + '-', dir=args.evidence) as name:
        fixture = Acceptance(args, Path(name))
        try:
            getattr(fixture, args.scenario)()
        except BaseException:
            failure = args.evidence / f'{args.scenario}-failure'
            shutil.copytree(name, failure, dirs_exist_ok=True)
            raise
    print(f'PASS: packaged localization {args.scenario}', flush=True)


if __name__ == '__main__':
    main()
