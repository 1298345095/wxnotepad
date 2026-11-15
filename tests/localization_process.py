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
from resource_process import native_edit, wait_state

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
        copy_parent = next(item['path'].rsplit('/', 1)[0] for item in self.reference.values()
                           if item['label'] == 'File Name and &Ext.\tShift+F9')
        copies = [identity for identity, item in self.reference.items()
                  if item['path'].rsplit('/', 1)[0] == copy_parent and
                  item['label'] in ('&File Name', 'File Name and &Ext.\tShift+F9', 'Full &Path Name\tAlt+Shift+F9')]
        assert len(copies) == 3
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
                for copying, identities in ((False, inserts), (True, copies)):
                    for index, identity in enumerate(identities):
                        if copying:
                            # Round-trip actual clipboard data through the same
                            # app's native Paste command, then compare disk bytes.
                            app.choose(identity)
                            app.choose(5033)  # wxID_PASTE.
                            destination = self.directory / f'copied-unnamed-{code}-{index}.txt'
                            app.send('save', str(destination))
                            assert destination.read_bytes() == TEXT[code]['untitled'].encode('utf-8')
                            app.choose(5002)
                        destination = self.directory / f'real 名字-{code}-{copying}-{index}.测试.txt'
                        app.send('save', str(destination))
                        app.choose(identity)
                        if copying:
                            app.choose(5033)
                        app.send('save')
                        expected = (destination.stem, destination.name, str(destination))[index]
                        assert destination.read_bytes() == expected.encode('utf-8'), (copying, index, destination)
                        app.choose(5002)
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
            print(f'PASS: {code} native accessible labels/stock captions, inserted/copied unnamed and real filename/path bytes, F10 and resized layout', flush=True)

    def dirty(self, app):
        length = int(app.state['length'])
        native_edit(app.process, app.state)
        app.state = wait_state(app.folder, app.process,
                               lambda state: state['dirty'] == '1' and int(state['length']) == length + 1)

    def discard(self, app):
        if app.process.poll() is not None:
            return
        app.begin('close')
        self.dialogs.answer(app.process, 'no', 'Save changes?')
        app.process.wait(timeout=10)

    def restarted(self, app, save_choice=None):
        previous = set(path.name for path in self.observation.iterdir())
        self.change(app, 2052, 'yes')
        if save_choice is not None:
            self.dialogs.answer(app.process, save_choice, 'Save changes?')
        assert app.process.wait(timeout=10) == 0
        child = wait_child(self.observation, previous)
        assert child.process.pid != app.process.pid
        assert child.state['menu_top.0.label'] == TEXT['zh_CN']['file']
        assert read_settings(self.config)['Settings2']['UILanguage'] == '2052'
        return child

    def restart(self):
        self.prepare_reference()
        document = self.directory / 'restart saved 中文.json'
        original = b'\xef\xbb\xbf{"a":1}\r\nsecond\tvalue\r\n'
        document.write_bytes(original)
        app = self.start(startup=document, arguments=('--read-only', '--startup-caret', '2,10'))
        child = None
        try:
            app.choose(menu_id(app.state, 'JSON'))
            before = dict(app.state)
            child = self.restarted(app)
            assert child.state['document_path'] == str(document)
            for field in ('encoding', 'bom', 'read_only', 'caret_position', 'frame_width', 'frame_height', 'maximized'):
                assert child.state[field] == before[field], (field, child.state[field], before[field])
            assert child.state['read_only'] == '1' and child.state['encoding'] == 'UTF-8' and child.state['bom'] == '1'
            assert 'JSON' in child.state['document_status']
            assert '行 2' in child.state['position_status']
            assert document.read_bytes() == original
            # A relative Open is an observable consequence of preserving the
            # original startup cwd across the independent relaunch.
            relative = self.working / 'relative-restart.txt'
            relative.write_bytes(b'original startup cwd')
            child.send('open', relative.name)
            assert child.state['length'] == '20'
            child.close()
        finally:
            app.stop()
            if child:
                child.stop()
        print('PASS: Yes restarts independent process with file, UTF-8 BOM, scheme, visual caret, read-only, geometry and startup cwd', flush=True)

        for unnamed in (False, True):
            document.write_bytes(b'disk original\n')
            app = self.start(startup=None if unnamed else document)
            child = None
            try:
                self.dirty(app)
                child = self.restarted(app)
                assert child.state['length'] == ('0' if unnamed else '14'), child.state
                assert child.state['dirty'] == '0'
                assert child.state['document_path'] == ('' if unnamed else str(document))
                assert document.read_bytes() == b'disk original\n'
                child.close()
            finally:
                app.stop()
                if child:
                    child.stop()
            print(f'PASS: default false save-before-tools discards dirty {"unnamed" if unnamed else "named"} buffer on Restart', flush=True)

        for bom in (False, True):
            original_text = '中文\r\nsecond'
            document.write_bytes(original_text.encode('utf-8' if bom else 'gbk'))
            app = self.start(extra='DefaultEncoding=936\nLoadASCIIasUTF8=0\n', startup=document)
            child = None
            try:
                app.choose(menu_id(app.state, 'UTF-8 BOM' if bom else 'UTF-8'))
                assert app.state['title'].startswith('*') and app.state['encoding'] == 'UTF-8', app.state
                child = self.restarted(app)
                assert child.state['encoding'] == 'UTF-8' and child.state['bom'] == str(int(bom))
                assert child.state['title'].startswith('*'), 'Original startup converts detected disk text after loading'
                child.send('save')
                assert document.read_bytes() == (b'\xef\xbb\xbf' if bom else b'') + original_text.encode('utf-8')
                child.close()
            finally:
                app.stop()
                if child:
                    child.stop()
            print(f'PASS: dirty selected UTF-8 BOM={bom} restarts from correctly decoded disk text and saves exact converted bytes', flush=True)

        for choice in ('cancel', 'no', 'yes'):
            document.write_bytes(b'disk original\n')
            app = self.start(extra='SaveBeforeRunningTools=1\n', startup=document)
            child = None
            try:
                self.dirty(app)
                if choice == 'cancel':
                    previous = set(self.observation.iterdir())
                    self.change(app, 2052, 'yes')
                    self.dialogs.answer(app.process, 'cancel', 'Save changes?')
                    app.finish()
                    assert app.process.poll() is None and app.state['dirty'] == '1'
                    assert set(self.observation.iterdir()) == previous
                    assert document.read_bytes() == b'disk original\n'
                    self.discard(app)
                else:
                    child = self.restarted(app, choice)
                    assert document.read_bytes() == (b'Xdisk original\n' if choice == 'yes' else b'disk original\n')
                    assert child.state['length'] == ('15' if choice == 'yes' else '14')
                    child.close()
            finally:
                app.stop()
                if child:
                    child.stop()
            print(f'PASS: enabled save-before-tools uses ordinary dirty-file {choice} outcome', flush=True)

        app = self.start(extra='SaveBeforeRunningTools=1\n')
        try:
            self.dirty(app)
            previous = set(self.observation.iterdir())
            self.change(app, 2052, 'yes')
            self.dialogs.answer(app.process, 'yes', 'Save changes?')
            chooser = self.dialogs.wait(app.process, 'Save file')
            self.dialogs.cancel(chooser)
            app.finish()
            assert app.process.poll() is None and app.state['dirty'] == '1' and app.state['length'] == '1'
            assert set(self.observation.iterdir()) == previous
            self.discard(app)
        finally:
            app.stop()
        print('PASS: enabled-policy unnamed Save As cancellation keeps the original dirty process', flush=True)

        document.write_bytes(b'launch failure original\n')
        app = self.start(startup=document)
        moved = self.executable.with_suffix('.running')
        mode = self.executable.stat().st_mode
        try:
            self.dirty(app)
            before = dict(app.state)
            previous = set(self.observation.iterdir())
            if sys.platform == 'win32':
                self.executable.rename(moved)
            else:
                self.executable.chmod(0)
            self.change(app, 2052, 'yes')
            self.dialogs.answer(app.process, 'ok', 'Restart failed')
            app.finish()
            assert app.process.poll() is None and app.state['dirty'] == '1'
            for field in ('length', 'document_path', 'caret_position', 'encoding'):
                assert app.state[field] == before[field]
            assert set(self.observation.iterdir()) == previous
            assert document.read_bytes() == b'launch failure original\n'
        finally:
            if sys.platform == 'win32':
                moved.rename(self.executable)
            else:
                self.executable.chmod(mode)
            try:
                self.discard(app)
            finally:
                app.stop()
        print('PASS: actual process launch failure retains original dirty window/document and reports failure', flush=True)


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
