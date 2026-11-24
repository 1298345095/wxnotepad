"""Open the current disk document through the packaged native Tools command."""
import argparse
from contextlib import ExitStack
import ctypes
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time

from localization_process import Acceptance, PREFERENCE, TEXT
from localization_package import menu_id, menus, wait_child
from localization_native import plain
from resource_process import wait_state


CAPTIONS = {
    'en': 'Open &in New Window\tAlt+0',
    'zh_CN': '在新窗口中打开(&I)\tAlt+0',
    'zh_TW': '在新視窗中開啟(&I)\tAlt+0',
}
SCENARIOS = ('basic', 'policy', 'encodings', 'failures', 'history', 'installed')
PARENT_FIELDS = ('length', 'dirty', 'document_path', 'encoding', 'bom', 'read_only',
                 'caret_position', 'selection_start', 'selection_end')


def record(fixture, name, app):
    (fixture.logs / (name + '.json')).write_text(json.dumps(app.state, ensure_ascii=False, indent=2), encoding='utf-8')


def opened(fixture, app, caption):
    previous = set(path.name for path in fixture.observation.iterdir())
    app.choose(menu_id(app.state, caption))
    child = wait_child(fixture.observation, previous)
    assert app.process.poll() is None and child.process.pid != app.process.pid
    return child


def native_alt_zero(fixture, app):
    if sys.platform != 'win32':
        fixture.dialogs.key(app, 'alt+0')
        return
    user = fixture.dialogs.user
    window = int(app.state['frame_handle'])
    fixture.dialogs.activate(window)
    assert user.GetForegroundWindow() == window, 'Native Alt+0 must target only the owned parent'
    vk_alt, vk_zero, key_up = 0x12, ord('0'), 2
    user.keybd_event(vk_alt, 0, 0, 0)
    try:
        user.keybd_event(vk_zero, 0, 0, 0)
        user.keybd_event(vk_zero, 0, key_up, 0)
    finally:
        user.keybd_event(vk_alt, 0, key_up, 0)


def basic(fixture):
    document = fixture.directory / 'current saved 中文.json'
    original = b'\xef\xbb\xbf{"a":1}\r\nsecond\tvalue\r\n'
    document.write_bytes(original)
    wrap = None
    for code in ('en', 'zh_CN', 'zh_TW'):
        app = fixture.start(PREFERENCE[code], startup=document,
                            arguments=('--read-only', '--startup-caret', '2,10',
                                       '--window-position', '100,100,640,480,0'))
        child = None
        try:
            entries = menus(app.state)
            identity = next((identity for identity, item in entries.items() if item['label'] == CAPTIONS[code]), None)
            assert identity is not None, ('Missing original Tools command', code, CAPTIONS[code])
            restart = menu_id(app.state, {'en': 'Restart', 'zh_CN': '重新启动(T)', 'zh_TW': '重新啟動(R)'}[code])
            parent_path, position = entries[restart]['path'].rsplit('/', 1)
            assert entries[identity]['path'] == parent_path + '/' + str(int(position) + 1)
            if code == 'en':
                wrap = menu_id(app.state, 'Word Wrap')
            startup_wrap = app.state['wrap_mode']
            app.choose(wrap)
            assert app.state['wrap_mode'] != startup_wrap
            before = dict(app.state)
            child = opened(fixture, app, CAPTIONS[code].split('\t')[0].replace('&', ''))
            for field in (*PARENT_FIELDS, 'frame_width', 'frame_height', 'maximized'):
                assert child.state[field] == before[field], (field, before[field], child.state[field])
            assert child.state['menu_top.0.label'] == TEXT[code]['file']
            assert child.state['wrap_mode'] == startup_wrap, 'Child settings come from startup INI, before unsaved parent changes'
            assert 'JSON' in child.state['document_status']
            assert child.state['read_only'] == '1' and child.state['encoding'] == 'UTF-8' and child.state['bom'] == '1'
            assert document.read_bytes() == original
            if sys.platform == 'win32':
                assert int(child.state['frame_x']) == int(before['frame_x']) + 10
                assert int(child.state['frame_y']) == int(before['frame_y']) + 10
            for field in PARENT_FIELDS:
                assert app.state[field] == before[field], (field, before[field], app.state[field])
            assert app.state['wrap_mode'] == before['wrap_mode']
            record(fixture, 'child-' + code, child)
            relative = fixture.working / 'relative-current-file.txt'
            relative.write_bytes(b'original startup cwd')
            child.send('open', relative.name)
            assert child.state['length'] == '20'
            child.close()
            child.stop()
            previous = set(path.name for path in fixture.observation.iterdir())
            native_alt_zero(fixture, app)
            child = wait_child(fixture.observation, previous)
            assert child.process.pid != app.process.pid and app.process.poll() is None
            for field in PARENT_FIELDS:
                assert child.state[field] == before[field], (field, before[field], child.state[field])
            assert child.state['wrap_mode'] == startup_wrap
            child.close()
            app.close()
        finally:
            app.stop()
            if child:
                child.stop()
        print(f'PASS: {code} current-file menu and Alt+0 launch independent native children with relaunch state and cwd', flush=True)


def choose_save(fixture, app, window, destination):
    if sys.platform == 'win32':
        for control in fixture.dialogs.children(window):
            kind = ctypes.create_unicode_buffer(100)
            fixture.dialogs.user.GetClassNameW(control, kind, len(kind))
            if kind.value == 'Edit' and fixture.dialogs.user.IsWindowVisible(control):
                value = ctypes.create_unicode_buffer(str(destination))
                fixture.dialogs.user.SendMessageW(control, 0xb1, 0, -1)
                fixture.dialogs.user.SendMessageW(control, 0xc2, 1, ctypes.cast(value, ctypes.c_void_p).value)
                break
        else:
            raise AssertionError('Owned Save As chooser has no filename edit')
        fixture.dialogs.user.PostMessageW(window, 0x111, 1, 0)
    else:
        # GTK exposes the filename label through LABELLED_BY, while the entry's
        # own accessible name is empty. Hidden folder-creation fields also have
        # a Name label, so only the displayed editable entry is the save target.
        atspi = fixture.dialogs.atspi
        filename = []
        for entry in fixture.dialogs.children(window):
            if entry.get_role() not in (atspi.Role.TEXT, atspi.Role.ENTRY):
                continue
            state = entry.get_state_set()
            if not state.contains(atspi.StateType.SHOWING) or not state.contains(atspi.StateType.EDITABLE):
                continue
            labels = [relation.get_target(index).get_name()
                      for relation in entry.get_relation_set()
                      if relation.get_relation_type() == atspi.RelationType.LABELLED_BY
                      for index in range(relation.get_n_targets())]
            if any(plain(label).rstrip(':') == 'Name' for label in labels):
                filename.append(entry)
        assert len(filename) == 1, ('Native Save filename entry', len(filename))
        assert filename[0].get_editable_text_iface().set_text_contents(str(destination))
        # Editing the filename briefly disables Save during GTK's validation.
        # An AT-SPI action on that disabled button succeeds without saving.
        deadline = time.monotonic() + 10

        def ready_save():
            window.clear_cache()
            return [child for child in fixture.dialogs.children(window)
                    if child.get_role() == atspi.Role.PUSH_BUTTON and plain(child.get_name()) == 'Save'
                    and child.get_state_set().contains(atspi.StateType.SHOWING)
                    and child.get_state_set().contains(atspi.StateType.SENSITIVE)]

        while time.monotonic() < deadline:
            buttons = fixture.dialogs.chooser_lookup(ready_save, deadline, 'Save')
            if buttons:
                assert len(buttons) == 1
                assert buttons[0].get_action_iface().do_action(0)
                return
            time.sleep(.03)
        raise AssertionError('Native Save button did not become sensitive')


def policy(fixture):
    document = fixture.directory / 'policy disk 中文.txt'
    for name, unnamed, modified, enabled, choice in (
            ('named-clean', False, False, True, None),
            ('named-disabled', False, True, False, None),
            ('named-discard', False, True, True, 'no'),
            ('named-save', False, True, True, 'yes'),
            ('named-cancel', False, True, True, 'cancel'),
            ('untitled-clean', True, False, True, None),
            ('untitled-disabled', True, True, False, None),
            ('untitled-discard', True, True, True, 'no'),
            ('untitled-cancel', True, True, True, 'cancel'),
            ('untitled-save-cancel', True, True, True, 'yes'),
            ('untitled-save', True, True, True, 'yes')):
        original = b'disk original\r\n'
        document.write_bytes(original)
        saved_as = fixture.directory / (name + ' saved 中文.txt')
        app = fixture.start(extra=f'SaveBeforeRunningTools={int(enabled)}\nFileWatchingMode=0\n',
                            startup=None if unnamed else document)
        child = None
        try:
            if modified:
                fixture.dirty(app)
            before = dict(app.state)
            previous = set(path.name for path in fixture.observation.iterdir())
            app.begin('menu', str(menu_id(app.state, 'Open in New Window')))
            if choice:
                fixture.dialogs.answer(app.process, choice, 'Save changes?')
            if unnamed and choice == 'yes':
                window = fixture.dialogs.wait(app.process, 'Save file')
                if name == 'untitled-save-cancel':
                    fixture.dialogs.cancel(window)
                else:
                    choose_save(fixture, app, window, saved_as)
            app.finish()
            cancelled = choice == 'cancel' or name == 'untitled-save-cancel'
            chosen_save = choice == 'yes' and not cancelled
            assert app.process.poll() is None
            if cancelled:
                assert set(path.name for path in fixture.observation.iterdir()) == previous
            else:
                child = wait_child(fixture.observation, previous)
                assert child.process.pid != app.process.pid
                expected = b'X' if name == 'untitled-save' else b'' if unnamed else (
                    b'X' + original if chosen_save else original)
                assert child.state['length'] == str(len(expected))
                expected_path = str(saved_as) if name == 'untitled-save' else '' if unnamed else str(document)
                assert child.state['document_path'] == expected_path, (
                    name, 'document_path', expected_path, child.state['document_path'])
                child.send('save', str(fixture.directory / (name + '-child.txt')))
                assert (fixture.directory / (name + '-child.txt')).read_bytes() == expected
                child.close()
            assert document.read_bytes() == (b'X' + original if chosen_save and not unnamed else original)
            if not chosen_save:
                for field in PARENT_FIELDS:
                    assert app.state[field] == before[field], (name, field, before[field], app.state[field])
            else:
                assert app.state['dirty'] == '0' and app.state['length'] == before['length']
            record(fixture, name + '-parent', app)
            if modified:
                # Exercise the original history through real commands after the launch decision.
                app.choose(menu_id(app.state, 'Undo'))
                assert app.state['length'] == ('0' if unnamed else str(len(original)))
                assert app.state['dirty'] == ('1' if chosen_save else '0')
                app.choose(menu_id(app.state, 'Redo'))
                assert app.state['length'] == before['length']
                assert app.state['dirty'] == ('0' if chosen_save else '1')
                destination = fixture.directory / (name + '-parent.txt')
                app.send('save', str(destination))
                assert destination.read_bytes() == (b'X' if unnamed else b'X' + original)
            app.close()
        finally:
            app.stop()
            if child:
                child.stop()
        print(f'PASS: {name} disk bytes, parent buffer, save point and functional Undo/Redo', flush=True)


def encodings(fixture):
    text = 'café\r\nsecond\tvalue\r\n'
    for name, source, setting, selected, expected, encoding, bom in (
            ('cp1252-source', text.encode('cp1252'), 1252, None, text.encode('cp1252'), 'CP1252', '0'),
            ('utf8', text.encode(), 3, None, text.encode(), 'UTF-8', '0'),
            ('utf8-bom', b'\xef\xbb\xbf' + text.encode(), 4, None, b'\xef\xbb\xbf' + text.encode(), 'UTF-8', '1'),
            ('utf16le-bom', b'\xff\xfe' + text.encode('utf-16-le'), 1, None,
             b'\xff\xfe' + text.encode('utf-16-le'), 'UTF-16LE', '1'),
            ('utf16be-bom', b'\xfe\xff' + text.encode('utf-16-be'), 2, None,
             b'\xfe\xff' + text.encode('utf-16-be'), 'UTF-16BE', '1'),
            ('utf16le-no-bom', text.encode('utf-16-le'), 6, None,
             b'\xff\xfe' + text.encode('utf-16-le'), 'UTF-16LE', '1'),
            ('utf16be-no-bom', text.encode('utf-16-be'), 7, None,
             b'\xfe\xff' + text.encode('utf-16-be'), 'UTF-16BE', '1'),
            ('dirty-current-utf8', text.encode('cp1252'), 1252, 'UTF-8', text.encode(), 'UTF-8', '0'),
            ('dirty-current-utf8-bom', text.encode('cp1252'), 1252, 'UTF-8 BOM',
             b'\xef\xbb\xbf' + text.encode(), 'UTF-8', '1')):
        document = fixture.directory / (name + ' 中文.txt')
        document.write_bytes(source)
        app = fixture.start(extra='DefaultEncoding=1252\nLoadASCIIasUTF8=0\nFileWatchingMode=0\n', startup=document,
                            arguments=('--startup-encoding', str(setting), '--startup-caret', '2,10'))
        child = None
        try:
            app.choose(menu_id(app.state, 'C Shell'))
            if selected:
                app.choose(menu_id(app.state, selected))
                assert app.state['title'].startswith('*')
            before = dict(app.state)
            child = opened(fixture, app, 'Open in New Window')
            assert child.state['encoding'] == encoding and child.state['bom'] == bom, (name, child.state)
            assert child.state['caret_position'] == before['caret_position']
            assert menus(child.state)[menu_id(child.state, 'C Shell')]['checked'] == '1'
            assert child.state['length'] == str(len(text.encode()))
            if selected or name in ('utf16le-no-bom', 'utf16be-no-bom'):
                assert child.state['title'].startswith('*'), 'Original current encoding switches apply after disk load'
            assert document.read_bytes() == source, 'Child startup must not save either document'
            for field in PARENT_FIELDS:
                assert app.state[field] == before[field], (name, field, before[field], app.state[field])
            destination = fixture.directory / (name + '-child.txt')
            child.send('save', str(destination))
            assert destination.read_bytes() == expected, (name, expected, destination.read_bytes())
            record(fixture, name + '-child', child)
            child.close()
        finally:
            app.stop()
            if child:
                child.stop()
        print(f'PASS: {name} actual disk decoding/current format, explicit C Shell dialect, visual caret and child bytes', flush=True)


def failures(fixture):
    for failure in ('save', 'launch'):
        document = fixture.directory / (failure + ' failure 中文.txt')
        original = b'failure disk original\r\n'
        document.write_bytes(original)
        app = fixture.start(extra=f'SaveBeforeRunningTools={int(failure == "save")}\nFileWatchingMode=0\n',
                            startup=document)
        moved = fixture.executable.with_suffix('.running')
        mode = fixture.executable.stat().st_mode
        preserved = document.with_suffix('.preserved')
        with ExitStack() as cleanup:
            cleanup.callback(app.stop)
            try:
                fixture.dirty(app)
                before = dict(app.state)
                previous = set(path.name for path in fixture.observation.iterdir())
                if failure == 'save':
                    if sys.platform == 'win32':
                        cleanup.enter_context(document.open('rb'))
                    else:
                        document.rename(preserved)
                        document.mkdir()
                elif sys.platform == 'win32':
                    fixture.executable.rename(moved)
                else:
                    fixture.executable.chmod(0)
                app.begin('menu', str(menu_id(app.state, 'Open in New Window')))
                if failure == 'save':
                    fixture.dialogs.answer(app.process, 'yes', 'Save changes?')
                    title = 'Could not save file'
                    message = 'Choose a file as the save destination.' if sys.platform != 'win32' else None
                else:
                    title = 'Open in New Window failed'
                    message = 'Could not open the document in a new window.'
                dialog = fixture.dialogs.wait(app.process, title, message=message)
                fixture.dialogs.answer(app.process, 'ok', title, window=dialog)
                app.finish()
                assert app.process.poll() is None
                assert set(path.name for path in fixture.observation.iterdir()) == previous
                for field in PARENT_FIELDS:
                    assert app.state[field] == before[field], (failure, field, before[field], app.state[field])
                assert (preserved if preserved.exists() else document).read_bytes() == original
                record(fixture, failure + '-failure-parent', app)
                app.choose(menu_id(app.state, 'Undo'))
                assert app.state['dirty'] == '0' and app.state['length'] == str(len(original))
                app.choose(menu_id(app.state, 'Redo'))
                assert app.state['dirty'] == '1' and app.state['length'] == str(len(original) + 1)
            finally:
                if moved.exists():
                    moved.rename(fixture.executable)
                elif failure == 'launch':
                    fixture.executable.chmod(mode)
                if preserved.exists():
                    document.rmdir()
                    preserved.rename(document)
            destination = fixture.directory / (failure + '-recovered.txt')
            app.send('save', str(destination))
            assert destination.read_bytes() == b'X' + original
            app.close()
        print(f'PASS: native {failure} failure prevents child, reports error, preserves dirty parent and usable Undo/Redo/save', flush=True)


def history(fixture):
    document = fixture.directory / 'folded parent 中文.json'
    original = b'{\n  "a": {\n    "b": 1\n  }\n}\n'
    document.write_bytes(original)
    app = fixture.start(startup=document, extra='FileWatchingMode=0\n', arguments=('--startup-caret', '6,1'))
    child = None
    try:
        fixture.dirty(app)
        app.choose(menu_id(app.state, 'File Name and Extension'))
        app.choose(menu_id(app.state, 'Undo'))
        fixture.dialogs.key(app, 'ctrl+a', expected_state={'selection_start': '0', 'selection_end': str(len(original) + 1)})
        app.choose(menu_id(app.state, 'All Levels'))
        before = dict(app.state)
        assert before['can_undo'] == '1' and before['can_redo'] == '1'
        assert '0:0;' in before['fold_headers'] and '1:0;' in before['fold_headers']
        child = opened(fixture, app, 'Open in New Window')
        for field in (*PARENT_FIELDS, 'can_undo', 'can_redo', 'fold_headers'):
            assert app.state[field] == before[field], (field, before[field], app.state[field])
        assert child.state['dirty'] == '0' and child.state['length'] == str(len(original))
        destination = fixture.directory / 'folded-child.txt'
        child.send('save', str(destination))
        assert destination.read_bytes() == original
        child.close()
        record(fixture, 'folded-parent', app)
        app.choose(menu_id(app.state, 'Redo'))
        assert app.state['length'] == str(len(original) + 1 + len(document.name.encode()))
        app.choose(menu_id(app.state, 'Undo'))
        assert app.state['length'] == str(len(original) + 1) and app.state['can_redo'] == '1'
        app.choose(menu_id(app.state, 'Undo'))
        assert app.state['length'] == str(len(original)) and app.state['dirty'] == '0'
        app.choose(menu_id(app.state, 'Redo'))
        app.send('save', str(fixture.directory / 'folded-parent.txt'))
        assert (fixture.directory / 'folded-parent.txt').read_bytes() == original + b'X'
        assert document.read_bytes() == original
        app.close()
    finally:
        app.stop()
        if child:
            child.stop()
    print('PASS: folded selected dirty parent retains its exact save point, pending Undo and Redo across independent launch', flush=True)


class CurrentFileAcceptance(Acceptance):
    def start(self, language=1033, preferred='en', extra='', save=False, startup=None, arguments=()):
        if self.args.rendering_technology is not None:
            extra += f'RenderingTechnology={self.args.rendering_technology}\n'
        app = super().start(language, preferred, extra, save, startup, arguments)
        if self.args.rendering_technology is not None:
            assert menus(app.state)[menu_id(app.state, 'Direct2D')]['checked'] == str(self.args.rendering_technology)
        return app

    def launch(self, preferred='en', startup=None, arguments=(), overrides=None):
        environment = dict(getattr(self, 'startup_environment', {}))
        environment.update(overrides or {})
        return super().launch(preferred, startup, arguments, environment)


def installed(fixture):
    assert sys.platform != 'win32'
    parent = Path('/opt/wxnotepad-file-workflows-test')
    assert parent.is_dir() and os.access(parent, os.W_OK), 'The existing owned installation-test prefix must be writable'
    with tempfile.TemporaryDirectory(prefix='current-file-', dir=parent) as directory:
        prefix = Path(directory)
        executable = prefix / 'bin' / fixture.executable.name
        shutil.copytree(fixture.executable.parent, executable.parent, ignore=shutil.ignore_patterns('Notepad4.ini'))
        (prefix / 'share').mkdir()
        (executable.parent / 'locale').rename(prefix / 'share/locale')
        fixture.executable = executable
        config_home = fixture.directory / 'native-config 中文'
        fixture.config = config_home / 'Notepad4/Notepad4.ini'
        fixture.config.parent.mkdir(parents=True)
        fixture.startup_environment = {'XDG_CONFIG_HOME': str(config_home)}
        basic(fixture)
        assert not (executable.parent / 'Notepad4.ini').exists()
    print('PASS: installed current-file menu/Alt+0 children discover XDG settings and share/locale in all three languages', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--cmake-dir', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--scenario', choices=SCENARIOS, default='basic')
    parser.add_argument('--rendering-technology', type=int, choices=(0, 1))
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='current-file-window-') as directory:
        # Match native Document Open and Save As expansion of TEMP's 8.3 aliases.
        root = Path(directory).resolve(strict=True)
        {'basic': basic, 'policy': policy, 'encodings': encodings, 'failures': failures,
         'history': history, 'installed': installed}[args.scenario](CurrentFileAcceptance(args, root))


if __name__ == '__main__':
    main()
