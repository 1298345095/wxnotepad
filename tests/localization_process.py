"""Accept localization through the real packaged shown application and native UI."""
import argparse
from contextlib import ExitStack
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from localization_native import Dialogs
from localization_layout import capture_and_check
from localization_package import (ROOT, Process, bundle_application, read_cmake_cache, environment, language_id,
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
DYNAMIC = {
    'en': ('Replace', 'Find what', 'Replace with', 'Find All', 'Replace All', 'Close',
           'Regular expression', 'Invalid regular expression'),
    'zh_CN': ('替换', '搜索字符串', '替换为', '查找全部(F)', '全部替换(A)', '关闭',
              '正则表达式搜索(E)', '正则表达式无效'),
    'zh_TW': ('取代', '搜尋字串', '取代為', '尋找全部(F)', '全部取代(A)', '關閉',
              '正規表示式(E)', '無效的正規表示式'),
}

# Reviewed command captions for #209. Check the deployed application rather than
# deriving expected translations from the catalog being tested.
EDITOR_CONTROLS = {
    'en': ('Default &Code Font...\tAlt+F12', 'Word Wrap Settings...', 'Scroll Past &Last Line',
           'Select &Word\tCtrl+Alt+Space', 'Show Character &Info', 'Un&icode Control Character',
           'Show &Menu\tAlt+F11', 'Standard &Encode'),
    'zh_CN': ('默认代码字体(&C)...\tAlt+F12', '自动换行设置...', '滚动超出末行(&L)',
              '选择单词(&W)\tCtrl+Alt+Space', '显示字符信息(&I)', '显示 Unicode 控制字符(&I)',
              '显示菜单(&M)\tAlt+F11', '标准编码(&E)'),
    'zh_TW': ('預設程式碼字型(&C)...\tAlt+F12', '自動換行設定...', '捲動超出最後一行(&L)',
              '選取單字(&W)\tCtrl+Alt+Space', '顯示字元資訊(&I)', 'Un&icode 控制字元',
              '顯示功能表(&M)\tAlt+F11', '標準編碼(&E)'),
}


class Acceptance:
    def __init__(self, args, directory):
        self.args = args
        self.directory = directory
        self.logs = args.evidence / 'runs' / directory.name
        self.executable = bundle_application(args.executable.resolve(), args.cmake_dir.resolve(), directory / 'bundle',
                                             self.logs / 'bundle')
        self.config = self.executable.parent / 'Notepad4.ini'
        self.observation = directory / 'children'
        self.observation.mkdir()
        self.working = directory / 'unrelated working 中文'
        self.working.mkdir()
        self.dialogs = Dialogs(self.logs)
        self.count = 0
        self.reference = None

    def start(self, language=1033, preferred='en', extra='', save=False, startup=None, arguments=()):
        settings(self.config, language, extra, save)
        return self.launch(preferred, startup, arguments)

    def launch(self, preferred='en', startup=None, arguments=(), overrides=None):
        self.count += 1
        child_environment = environment(self.executable, self.observation, preferred)
        child_environment.update(overrides or {})
        return Process(self.executable, self.directory / f'app-{self.count:03}',
                       child_environment, startup, arguments, self.working,
                       self.logs / f'app-{self.count:03}' / 'stderr.log')

    def displayed(self, app, code, configured):
        text = TEXT[code]
        assert app.state['menu_top.0.label'] == text['file'], (code, app.state)
        assert app.state['title'] == text['untitled'] + ' - wxnotepad', app.state
        captions = {item['label'] for item in menus(app.state).values()}
        for caption in EDITOR_CONTROLS[code]:
            assert caption in captions, ('Packaged native editor control caption', code, caption, captions)
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
            unindent = next(identity for identity, item in self.reference.items() if item['label'] == '&Unindent')
            assert current[unindent]['label'] == {'en': '&Unindent', 'zh_CN': '减少缩进(&U)',
                                                   'zh_TW': '減少縮排(&U)'}[code], current[unindent]

    def goto(self, app, code, snapshot=None):
        identity = next(identity for identity, item in self.reference.items()
                        if item['label'] == '&Goto Line...\tCtrl+G')
        app.begin('menu', str(identity))
        window = self.dialogs.wait(app.process, TEXT[code]['goto'])
        names = self.dialogs.accessible_names(window)
        assert TEXT[code]['line'] in names and TEXT[code]['column'] in names, (code, names)
        buttons = [text.replace('&', '').replace('_', '') for text in self.dialogs.texts(window)]
        # GTK3 maps default stock-ID buttons to GTK's regional stock labels;
        # those native labels keep the regional language independently of UI.
        native_cancel = TEXT[code]['cancel'] if sys.platform == 'win32' else 'Cancel'
        assert native_cancel in buttons, (code, buttons)
        if snapshot is not None:
            capture_and_check(self.dialogs, app, window, snapshot)
        self.dialogs.click(window, native_cancel)
        app.finish()

    def encoding(self, app, code):
        identity = next(identity for identity, item in self.reference.items()
                        if item['label'] == '&More...\tF9')
        app.begin('menu', str(identity))
        title = {'en': 'Select Encoding', 'zh_CN': '选择编码', 'zh_TW': '選擇編碼'}[code]
        window = self.dialogs.wait(app.process, title)
        if sys.platform == 'win32':
            assert 'Unicode' in self.dialogs.texts(window), self.dialogs.texts(window)
        else:
            # wxGTK3's generic tree exposes its native control name through ATK;
            # its drawn rows are retained in the real screenshot for review.
            expected = {'en': 'Current encoding', 'zh_CN': '当前编码', 'zh_TW': '目前編碼'}[code]
            assert expected in self.dialogs.texts(window), self.dialogs.texts(window)
            capture_and_check(self.dialogs, app, window, self.logs / f'encoding-{code}.png')
        self.dialogs.click(window, TEXT[code]['cancel'] if sys.platform == 'win32' else 'Cancel')
        app.finish()

    def startup(self):
        for code in ('en', 'zh_CN', 'zh_TW'):
            app = self.start(PREFERENCE[code], preferred='zh_TW' if code == 'en' else 'en')
            try:
                self.displayed(app, code, PREFERENCE[code])
                if code == 'en':
                    self.reference = menus(app.state)
                self.goto(app, code)
                if code != 'en':
                    self.encoding(app, code)
                app.close()
            finally:
                app.stop()
            print(f'PASS: actual packaged {code}, relocated resources, menu IDs/accelerators, native accessible dialog and stock Cancel', flush=True)
        cases = [
            (None, 'zh_TW', 'zh_TW', 1024), (1024, 'zh_CN', 'zh_CN', 1024),
            (1024, 'en:zh_CN', 'en', 1024), (1024, 'zh_TW:zh_CN', 'zh_TW', 1024),
            (1024, 'fr:en:zh_CN', 'en', 1024), (1024, 'fr:zh_CN:en', 'zh_CN', 1024),
            (1024, 'zh_HK:en', 'zh_TW', 1024), (1024, 'zh-Hans:en', 'zh_CN', 1024),
            (1024, 'zh-Hant:en', 'zh_TW', 1024),
            (2057, 'zh_TW', 'en', 1033), (1041, 'zh_CN', 'en', None),
            (3076, 'en', 'zh_TW', 1028), (5124, 'en', 'zh_TW', 1028), (4100, 'en', 'zh_CN', 2052),
            (4, 'en', 'zh_CN', 2052), (6148, 'en', 'zh_CN', 2052),
            (67588, 'en', 'zh_CN', 2052), (-63484, 'en', 'zh_CN', 2052),
            ('2052suffix', 'en', 'zh_CN', 2052), (0, 'en', 'en', 1024), (9999, 'en', 'en', 1024),
            (-1, 'zh_TW', 'zh_TW', 1024), (0, 'zh_CN', 'zh_CN', 1024),
            ('invalid', 'en', 'en', 1024),
        ]
        # The other supported original primaries are valid configured choices
        # even though this release does not deliver their application catalogs.
        cases += [(value, 'zh_TW', 'en', None) for value in (1036, 1031, 1040, 1042, 1045, 1046, 1049, 1060)]
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
        cache = read_cmake_cache(self.args.cmake_dir.resolve())
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
                # &File is shared with wxstd. Omitting only the application
                # entry proves actual matching stock-MO lookup in the shown
                # menu, independently of GTK3's system-owned stock buttons.
                one.write_text('msgid "&File"\nmsgstr ""\n', encoding='utf-8')
                subprocess.run([str(gettext / ('msgattrib.exe' if sys.platform == 'win32' else 'msgattrib')),
                                '--set-fuzzy', f'--only-file={one}', '-o', partial, ROOT / 'po' / (code + '.po')], check=True)
                subprocess.run([cache['WXNOTEPAD_MSGFMT'], '--check', '--check-format', '-o', app_mo, partial], check=True)
                app = self.start(PREFERENCE[code])
                try:
                    self.displayed(app, code, PREFERENCE[code])
                    app.close()
                finally:
                    app.stop()
                print(f'PASS: {code} shown File caption resolves from matching shipped wxstd when app entry is absent', flush=True)
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

    def base64(self):
        self.prepare_reference()
        captions = {
            'en': ('Standard &Encode', 'URL &Safe Encode', 'Encode as HTML Embedded &Image', '&Decode', 'Decode as &Hex'),
            'zh_CN': ('标准编码(&E)', 'URL 安全编码(&S)', '编码为 HTML 嵌入图片(&I)', '解码(&D)', '解码为 Hex(&H)'),
            'zh_TW': ('標準編碼(&E)', 'URL 安全編碼(&S)', '編碼為 HTML 嵌入圖片(&I)', '解碼(&D)', '解碼為 Hex(&H)'),
        }
        identities = [next(identity for identity, item in self.reference.items() if item['label'] == caption)
                      for caption in captions['en']]
        undo = next(identity for identity, item in self.reference.items() if item['label'] == '&Undo\tCtrl+Z')
        redo = next(identity for identity, item in self.reference.items() if item['label'] == '&Redo\tCtrl+Y')
        for code in ('en', 'zh_CN', 'zh_TW'):
            app = self.start(PREFERENCE[code])
            try:
                current = menus(app.state)
                assert app.state['menu_top.6.label'] == {'en': '&Tools', 'zh_CN': '工具(&T)', 'zh_TW': '工具(&T)'}[code]
                group = next(identity for identity, item in current.items() if item['label'] == '&Base64')
                # Approved tools can precede Base64; retain its direct Tools
                # parent and the same observed group position in every language.
                group_path = self.reference[group]['path']
                assert group_path.rsplit('/', 1)[0] == '6', self.reference[group]
                assert current[group]['path'] == group_path, current[group]
                for index, identity in enumerate(identities):
                    assert current[identity]['label'] == captions[code][index]
                    assert current[identity]['path'] == f'{group_path}/{index}'
                    assert '\t' not in current[identity]['label'], 'Base64 has no original shortcuts'
                    before = b'foo' if index < 3 else b'Zm9v'
                    expected = (b'Zm9v', b'Zm9v', b'<img src="data:image/png;base64,Zm9v" />', b'foo', b'66 6F 6F')[index]
                    path = self.directory / f'base64-{code}-{index}.PNG'
                    path.write_bytes(before)
                    app.send('open', str(path))
                    self.dialogs.key(app, 'ctrl+a', expected_state={'selection_start': '0',
                                                                  'selection_end': str(len(before))})
                    app.choose(identity)
                    assert app.state['dirty'] == '1', (code, index, app.state['length'], app.state['selection_start'],
                                                       app.state['selection_end'])
                    app.send('save')
                    assert path.read_bytes() == expected
                    app.choose(undo)
                    app.send('save')
                    assert path.read_bytes() == before
                    app.choose(redo)
                    app.send('save')
                    assert path.read_bytes() == expected
            finally:
                app.stop()
        print('PASS: packaged English/Simplified/Traditional Base64 menu commands and saved Undo/Redo bytes', flush=True)

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
                readonly = next(identity for identity, item in self.reference.items()
                                if item['label'] == '&Read Only Mode\tF10')
                # The real shortcut establishes native input (and a Wayland
                # input serial) before the clipboard operations below.
                self.dialogs.key(app, 'F10')
                app.state = wait_state(app.folder, app.process, lambda state: state['read_only'] == '1')
                assert menus(app.state)[readonly]['checked'] == '1'
                self.dialogs.key(app, 'F10')
                app.state = wait_state(app.folder, app.process, lambda state: state['read_only'] == '0')
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

    def show_replace(self, app):
        if sys.platform == 'win32':
            app.choose(5039)  # wxID_REPLACE.
        else:
            if app.state['backend'] == 'GdkWaylandDisplay':
                app.state = wait_state(app.folder, app.process,
                                       lambda state: state['active'] == '1' and state['focus'] == '1', timeout=10)
            self.dialogs.key(app, 'ctrl+h')

    def dynamic(self):
        self.prepare_reference()
        for code in ('en', 'zh_CN', 'zh_TW'):
            title, query, replacement, find_all, replace_all, close, regex, invalid = DYNAMIC[code]
            for count in (1, 3):
                document = self.directory / f'dynamic-{code}-{count}.txt'
                document.write_bytes(b'\n'.join([b'cat'] * count))
                app = self.start(PREFERENCE[code], startup=document)
                try:
                    self.show_replace(app)  # Same menu command through the native accelerator.
                    window = self.dialogs.wait(app.process, title)
                    self.dialogs.set_input(window, query, 'cat')
                    self.dialogs.set_input(window, replacement, 'dog')
                    self.dialogs.click(window, find_all)
                    matches = (f'{count} match' + ('es' if count != 1 else '') if code == 'en' else
                               f'{count} 处匹配' if code == 'zh_CN' else f'{count} 個符合項目')
                    self.dialogs.wait_text(window, matches)
                    self.dialogs.click(window, replace_all)
                    replacements = (f'{count} replacement' + ('s' if count != 1 else '') if code == 'en' else
                                    f'{count} 处替换' if code == 'zh_CN' else f'取代 {count} 處')
                    self.dialogs.wait_text(window, replacements)
                    capture_and_check(self.dialogs, app, window, self.logs / f'replace-{code}-{count}.png')
                    self.dialogs.set_input(window, query, '[')
                    self.dialogs.click(window, regex)
                    self.dialogs.click(window, find_all)
                    self.dialogs.wait_text(window, invalid)
                    self.dialogs.click(window, close)
                    app.send('save')
                    assert document.read_bytes() == b'\n'.join([b'dog'] * count)
                    app.close()
                finally:
                    app.stop()
            print(f'PASS: {code} actual find/replace singular/plural counts, invalid-pattern feedback and exact operation bytes', flush=True)

    def regional(self):
        assert sys.platform != 'win32', 'Private glibc locale acceptance is registered on Linux'
        self.prepare_reference()
        locale_root = self.directory / 'regional-locale'
        locale_root.mkdir()
        subprocess.run(['localedef', '--no-archive', '-i', 'de_DE', '-f', 'UTF-8',
                        str(locale_root / 'de_DE.UTF-8')], check=True)
        regional = dict(os.environ, LOCPATH=str(locale_root), LC_ALL='de_DE.UTF-8')
        assert subprocess.check_output(['locale', 'decimal_point'], env=regional, text=True).strip() == ','
        date_command = next(identity for identity, item in self.reference.items()
                            if item['label'] == 'Time/Date (&Short Form)\tCtrl+F5')
        save_settings = next(identity for identity, item in self.reference.items()
                             if item['label'] == 'Save Settings &Now')
        for code in ('en', 'zh_CN', 'zh_TW'):
            settings(self.config, PREFERENCE[code], extra='TabWidth=7suffix\nZoomLevel=115suffix\n')
            text = self.config.read_text(encoding='utf-16').replace('UnrelatedLanguage=preserved',
                                                                   'DateTimeShort=%x\nUnrelatedLanguage=preserved')
            self.config.write_text(text, encoding='utf-16')
            app = self.launch(overrides={'LOCPATH': str(locale_root), 'LC_ALL': 'de_DE.UTF-8'})
            try:
                assert app.state['menu_top.0.label'] == TEXT[code]['file']
                assert app.state['tab_width'] == '7' and app.state['zoom'] == '115', app.state
                before = subprocess.check_output(['date', '+%x'], env=regional).strip()
                app.choose(date_command)
                after = subprocess.check_output(['date', '+%x'], env=regional).strip()
                destination = self.directory / f'regional-{code}.txt'
                app.send('save', str(destination))
                assert destination.read_bytes() in (before, after), destination.read_bytes()
                app.choose(save_settings)
                saved = read_settings(self.config)
                assert saved['Settings']['TabWidth'] == '7' and saved['Settings']['ZoomLevel'] == '115'
                assert saved['Settings2']['DateTimeShort'] == '%x'
                assert saved['Settings2']['UILanguage'] == str(PREFERENCE[code])
                app.close()
            finally:
                app.stop()
            print(f'PASS: {code} UI preserves private German comma-decimal regional date, original integer parsing and ASCII INI serialization', flush=True)

    def layout(self):
        self.prepare_reference()
        completion = next(identity for identity, item in self.reference.items()
                          if item['label'] == 'Auto &Completion Settings...')
        for code in ('en', 'zh_CN', 'zh_TW'):
            for scale in ((1, 1.5) if sys.platform != 'win32' else (1,)):
                settings(self.config, PREFERENCE[code])
                overrides = {'GDK_DPI_SCALE': str(scale)} if sys.platform != 'win32' else {}
                # Set the capture height before mapping so Wayland places the
                # status bar on screen; resize subsequently grows only the width.
                app = self.launch(arguments=('--window-position', '0,0,900,720,0'), overrides=overrides)
                try:
                    suffix = f'{code}-{scale}'
                    self.goto(app, code, self.logs / f'goto-{suffix}.png')
                    self.show_replace(app)
                    window = self.dialogs.wait(app.process, DYNAMIC[code][0])
                    self.dialogs.set_input(window, DYNAMIC[code][1], 'fixed sample 中文')
                    self.dialogs.set_input(window, DYNAMIC[code][2], 'replacement 取代')
                    capture_and_check(self.dialogs, app, window, self.logs / f'replace-{suffix}.png')
                    self.dialogs.click(window, DYNAMIC[code][5])
                    app.begin('menu', str(completion))
                    title = {'en': 'Auto Completion Settings', 'zh_CN': '自动完成设置', 'zh_TW': '自動完成設定'}[code]
                    window = self.dialogs.wait(app.process, title)
                    capture_and_check(self.dialogs, app, window, self.logs / f'completion-{suffix}.png')
                    self.dialogs.click(window, TEXT[code]['cancel'] if sys.platform == 'win32' else 'Cancel')
                    app.finish()
                    app.begin('menu', '5000')  # Real Open native file dialog.
                    title = {'en': 'Open file', 'zh_CN': '打开文件', 'zh_TW': '開啟檔案'}[code]
                    window = self.dialogs.wait(app.process, title)
                    try:
                        self.dialogs.navigate_folder(app, window, self.directory)
                    except BaseException:
                        capture_and_check(self.dialogs, app, window, self.logs / f'navigation-failure-{suffix}.png', system_dialog=True)
                        raise
                    # Approved original current/text/favorites policy uses the
                    # application's captions, including the extensionless (*) filter.
                    expected = {'en': 'All Files (*)', 'zh_CN': '所有文件 (*)', 'zh_TW': '所有檔案 (*)'}[code]
                    filters = self.dialogs.file_filters(window)
                    assert expected in filters, (expected, filters)
                    text_filter = {'en': 'Plain Text (*.txt)', 'zh_CN': '纯文本 (*.txt)', 'zh_TW': '純文字 (*.txt)'}[code]
                    assert text_filter in filters, (text_filter, filters)
                    (self.logs / f'filters-{suffix}.json').write_text(json.dumps(filters, ensure_ascii=False), encoding='utf-8')
                    capture_and_check(self.dialogs, app, window, self.logs / f'open-filters-{suffix}.png', system_dialog=True)
                    self.dialogs.cancel(window)
                    app.finish()
                    status_document = self.directory / f'status-{code}-{scale}.py'
                    status_document.write_bytes(b'\xff\xfe' + ('a' * 400 + '\r\nnext').encode('utf-16-le'))
                    app.send('open', str(status_document))
                    app.choose(menu_id(app.state, 'Python'))
                    go_to = next(identity for identity, item in self.reference.items()
                                 if item['label'] == '&Goto Line...\tCtrl+G')
                    app.begin('menu', str(go_to))
                    window = self.dialogs.wait(app.process, TEXT[code]['goto'])
                    self.dialogs.set_input(window, TEXT[code]['line'], '1')
                    self.dialogs.set_input(window, TEXT[code]['column'], '351')
                    okay = {'en': 'OK', 'zh_CN': '确定', 'zh_TW': '確認'}[code] if sys.platform == 'win32' else 'OK'
                    self.dialogs.click(window, okay)
                    app.finish()
                    expected_position = {'en': 'Ln 1, Col 351', 'zh_CN': '行 1, 列 351', 'zh_TW': '行 1，欄 351'}[code]
                    app.state = wait_state(app.folder, app.process,
                                           lambda state: state['position_status'] == expected_position)
                    assert app.state['caret_position'] == '350' and app.state['position_status'] == expected_position, app.state
                    for width in (900, 1100):
                        self.dialogs.resize(app, width, 720)
                        app.choose(menu_id(app.state, 'Python'))
                        frames = [frame for frame in self.dialogs.windows(app.process)
                                  if self.dialogs.title(frame) == app.state['title']]
                        assert len(frames) == 1
                        image = self.logs / f'status-{suffix}-{width}.png'
                        capture_and_check(self.dialogs, app, frames[0], image, system_dialog=True)
                        observed = {key: app.state[key] for key in ('dpi', 'document_status', 'position_status',
                                                                   'client_width', 'frame_width')}
                        image.with_suffix('.state.json').write_text(json.dumps(observed, ensure_ascii=False), encoding='utf-8')
                    app.close()
                finally:
                    app.stop()
                print(f'PASS: {code} scale={scale} shown GoTo/Replace/completion/native-filter control bounds, measured text and retained screenshots', flush=True)

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
        result = app.process.wait(timeout=10)
        assert result == 0, f'Restart parent exit={result}; process log={self.logs}'
        child = wait_child(self.observation, previous)
        try:
            assert child.process.pid != app.process.pid
            assert child.state['menu_top.0.label'] == TEXT['zh_CN']['file']
            assert read_settings(self.config)['Settings2']['UILanguage'] == '2052'
        except BaseException:
            child.stop()
            raise
        return child

    def errors(self):
        for code in ('en', 'zh_CN', 'zh_TW'):
            document = self.directory / f'error real 名字-{code}.txt'
            original = b'unchanged disk\n'
            document.write_bytes(original)
            app = self.start(PREFERENCE[code], extra='FileWatchingMode=0\n', startup=document)
            preserved = document.with_suffix('.preserved')
            lock = ExitStack()
            try:
                self.dirty(app)
                if sys.platform == 'win32':
                    lock.enter_context(document.open('rb'))  # Deny replacement, permit the ordinary write-open guard.
                else:
                    document.rename(preserved)
                    document.mkdir()
                app.begin('menu', '5003')
                title, prefix = {'en': ('Could not save file', 'Could not replace the destination: '),
                                 'zh_CN': ('无法保存文件', '无法替换目标文件: '),
                                 'zh_TW': ('無法儲存檔案', '無法取代目的檔案：')}[code]
                if sys.platform != 'win32':
                    prefix = {'en': 'Choose a file as the save destination.', 'zh_CN': '选择保存目标文件。',
                              'zh_TW': '選擇儲存目的檔案。'}[code]
                window = self.dialogs.wait(app.process, title, message=prefix)
                messages = [text for text in self.dialogs.texts(window) if text.startswith(prefix)]
                assert messages, self.dialogs.texts(window)
                if sys.platform == 'win32':
                    import ctypes
                    error_access_denied, error_sharing_violation = 5, 32
                    native_details = [ctypes.FormatError(error).strip()
                                      for error in (error_access_denied, error_sharing_violation)]
                    assert any(detail in text for detail in native_details for text in messages), messages
                else:
                    assert prefix in messages, messages
                (self.logs / f'error-{code}.json').write_text(json.dumps(messages, ensure_ascii=False), encoding='utf-8')
                capture_and_check(self.dialogs, app, window, self.logs / f'error-{code}.png')
                self.dialogs.answer(app.process, 'ok', title, window=window)
                app.finish()
                lock.close()
                assert app.state['dirty'] == '1'
                if sys.platform != 'win32':
                    assert preserved.read_bytes() == original
                    document.rmdir()
                    preserved.rename(document)
                else:
                    assert document.read_bytes() == original
                app.send('save')
                assert document.read_bytes() == b'X' + original
                app.close()
            finally:
                lock.close()
                if preserved.exists():
                    if document.is_dir():
                        document.rmdir()
                    preserved.rename(document)
                app.stop()
            detail = ' and exact native Windows sharing-violation detail' if sys.platform == 'win32' else ''
            print(f'PASS: {code} real failed Save reports localized application sentence{detail}; retained buffer/file saves successfully afterward', flush=True)

    def installed(self):
        assert sys.platform != 'win32'
        self.prepare_reference()
        parent = Path('/opt/wxnotepad-file-workflows-test')
        assert parent.is_dir() and os.access(parent, os.W_OK), 'Owned /opt fixture parent must be prepared by the GUI dependency setup'
        with tempfile.TemporaryDirectory(prefix='localization-', dir=parent) as name:
            prefix = Path(name)
            executable = prefix / 'bin' / self.executable.name
            shutil.copytree(self.executable.parent, executable.parent, ignore=shutil.ignore_patterns('Notepad4.ini'))
            (prefix / 'share').mkdir()
            (executable.parent / 'locale').rename(prefix / 'share/locale')
            assert not (executable.parent / 'locale').exists()
            for code in ('en', 'zh_CN', 'zh_TW'):
                config_home = self.directory / ('user-config-' + code)
                config = config_home / 'Notepad4/Notepad4.ini'
                config.parent.mkdir(parents=True)
                original = settings(config, PREFERENCE[code])
                process_environment = environment(executable, self.observation)
                process_environment['XDG_CONFIG_HOME'] = str(config_home)
                app = Process(executable, self.directory / ('installed-' + code), process_environment,
                              cwd=self.working, log_path=self.logs / f'installed-{code}.log')
                try:
                    self.displayed(app, code, PREFERENCE[code])
                    self.goto(app, code)
                    app.close()
                finally:
                    app.stop()
                assert config.read_bytes() == original
                assert not (executable.parent / 'Notepad4.ini').exists()
                print(f'PASS: installed {code} loads actual share/locale package from /opt/bin with separate XDG original-format settings and unrelated cwd', flush=True)

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
                    assert read_settings(self.config)['Settings2']['UILanguage'] == '2052'
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
            assert read_settings(self.config)['Settings2']['UILanguage'] == '2052'
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
            assert read_settings(self.config)['Settings2']['UILanguage'] == '2052'
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
    parser.add_argument('--scenario', choices=('startup', 'lifecycle', 'restart', 'native', 'dynamic', 'regional', 'layout', 'errors', 'installed', 'base64'), default='startup')
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    # Linux's native temporary filesystem exercises POSIX application IPC and
    # file operations. Logs and retained failure evidence stay in the report tree.
    temporary_parent = args.evidence if sys.platform == 'win32' else None
    with tempfile.TemporaryDirectory(prefix=args.scenario + '-', dir=temporary_parent) as name:
        fixture = Acceptance(args, Path(name))
        try:
            getattr(fixture, args.scenario)()
        except BaseException:
            failure = args.evidence / f'{args.scenario}-failure-{Path(name).name}'
            shutil.copytree(name, failure)
            raise
    print(f'PASS: packaged localization {args.scenario}', flush=True)


if __name__ == '__main__':
    main()
