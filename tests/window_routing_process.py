"""Original window routing through settings menus and real packaged processes."""
import argparse
from pathlib import Path
import tempfile
from contextlib import ExitStack
import subprocess
import time
import sys
import json
import os

from localization_process import Acceptance, PREFERENCE, TEXT
from localization_package import menu_id, menus, read_settings, ROOT, Process, Child, wait_child, settings
from startup_options_process import pending_start, finish_start
from resource_process import wait_state


CAPTIONS = {
    'en': ('Reuse Window', 'Single File Instance', 'Window O&ptions'),
    'zh_CN': ('重用窗口(R)', '单文件实例模式(F)', '窗口选项(&P)'),
    'zh_TW': ('重用視窗(R)', '單個實體模式(F)', '視窗選項(&P)'),
}
SAVE_TITLES = {'en': 'Save changes?', 'zh_CN': '保存修改？', 'zh_TW': '儲存變更？'}
SAVE_FRAGMENTS = {'en': 'before opening another file?', 'zh_CN': '打开另一个文件前', 'zh_TW': '開啟另一個檔案前'}
ERROR_TITLES = {'en': 'Could not save file', 'zh_CN': '无法保存文件', 'zh_TW': '無法儲存檔案'}
ERROR_FRAGMENTS = {'en': 'Choose a file as the save destination.', 'zh_CN': '选择保存目标文件。',
                   'zh_TW': '選擇儲存目的檔案。'}


def answer_save(fixture, app, code, choice, window):
    if sys.platform == 'win32' or code == 'en':
        fixture.dialogs.answer(app.process, choice, window=window)
    else:
        captions = {'zh_CN': {'yes': '保存(S)', 'no': '放弃(D)', 'cancel': '取消(C)'},
                    'zh_TW': {'yes': '儲存(S)', 'no': '捨棄(D)', 'cancel': '取消(C)'}}
        fixture.dialogs.click(window, captions[code][choice])


def settings_menu(fixture):
    for code, language in PREFERENCE.items():
        app = fixture.start(language, code)
        try:
            reuse, single, window = CAPTIONS[code]
            reuse_id, single_id = menu_id(app.state, reuse), menu_id(app.state, single)
            entries = menus(app.state)
            window_id = next(identity for identity, item in entries.items() if item['label'] == window)
            assert entries[reuse_id]['path'].startswith(entries[window_id]['path'] + '/')
            assert entries[reuse_id]['checked'] == '0' and entries[single_id]['checked'] == '1'
            app.choose(reuse_id)
            app.choose(single_id)
            stored = read_settings(fixture.config)
            assert stored['Settings2']['ReuseWindow'] == '1'
            assert stored['Settings2']['SingleFileInstance'] == '0'
            assert stored['Settings']['SaveSettings'] == '0'
            app.close()
            app.stop()
            app = fixture.launch(code)
            assert menus(app.state)[reuse_id]['checked'] == '1'
            assert menus(app.state)[single_id]['checked'] == '0'
            app.choose(reuse_id)
            app.choose(single_id)
            stored = read_settings(fixture.config)
            assert 'ReuseWindow' not in stored['Settings2']
            assert 'SingleFileInstance' not in stored['Settings2']
            app.close()
        finally:
            app.stop()
    print('PASS: original localized menu positions, defaults, immediate Settings2 writes and restore', flush=True)
    first = fixture.start()
    second = None
    try:
        second = fixture.launch(arguments=('-n',))
        first.choose(menu_id(first.state, 'Reuse Window'))
        second.choose(menu_id(second.state, 'Single File Instance'))
        stored = read_settings(fixture.config)
        assert stored['Settings2']['ReuseWindow'] == '1'
        assert stored['Settings2']['SingleFileInstance'] == '0'
        first.choose(menu_id(first.state, 'Save Settings Now'))
        first.close()
        second.close()
        stored = read_settings(fixture.config)
        assert stored['Settings2']['ReuseWindow'] == '1'
        assert stored['Settings2']['SingleFileInstance'] == '0'
    finally:
        first.stop()
        if second:
            second.stop()
    print('PASS: independent windows immediately persist only the setting their command changed', flush=True)


def incoming(fixture, arguments, code='en', overrides=None):
    from localization_package import environment
    fixture.count += 1
    env = environment(fixture.executable, fixture.observation, code)
    env.update(overrides or {})
    return pending_start(fixture.executable, fixture.directory / f'incoming-{fixture.count}', env, arguments, fixture.working)


def same_file(fixture):
    target = fixture.working / 'same 中文.txt'
    target.write_bytes(b'original\r\nsecond\r\n')
    app = fixture.start(startup=target)
    requester = None
    try:
        fixture.dirty(app)
        before = dict(app.state)
        requester = incoming(fixture, ('-ro', '-g', '2,1', str(target)))
        try:
            assert requester.process.wait(timeout=5) == 0
        except subprocess.TimeoutExpired as error:
            raise AssertionError('SFI hit must hand off and exit instead of creating another editor') from error
        app.send('snapshot')
        for field in ('length', 'dirty', 'document_path', 'caret_position', 'read_only', 'can_undo'):
            assert app.state[field] == before[field], (field, before, app.state)
        assert target.read_bytes() == b'original\r\nsecond\r\n'
        app.choose(menu_id(app.state, 'Undo'))
        assert app.state['dirty'] == '0' and app.state['can_redo'] == '1'
        app.close()
    finally:
        app.stop()
        if requester:
            requester.stop()
    print('PASS: same-file dirty document retains identity/history/caret/read-only and exits requester', flush=True)


def forward(fixture):
    original = fixture.working / 'before.txt'
    target = fixture.working / 'forwarded 中文.txt'
    original.write_bytes(b'untouched original\r\n')
    target.write_bytes(b'first\r\ncaf\xe9\r\n')
    app = fixture.start(startup=original)
    requester = None
    try:
        requester = incoming(fixture, ('-r', '-e', 'windows-1252', '-s', 'cpp', '-ro', '-g', '2,1', target.name))
        assert requester.process.wait(timeout=5) == 0
        app.send('snapshot')
        assert Path(app.state['document_path']) == target
        assert app.state['encoding'] == 'CP1252' and app.state['length'] == '14'
        assert app.state['read_only'] == '1' and app.state['caret_position'] == '7'
        assert 'C++' in app.state['document_status'], app.state['document_status']
        saved = fixture.directory / 'forwarded saved.txt'
        app.send('save', str(saved))
        assert saved.read_bytes() == b'first\r\ncaf\xe9\r\n'
        assert original.read_bytes() == b'untouched original\r\n'
        assert target.read_bytes() == b'first\r\ncaf\xe9\r\n'
        app.close()
    finally:
        app.stop()
        if requester:
            requester.stop()
    print('PASS: real reuse delivers caller-relative path, source encoding, scheme, readonly and caret', flush=True)


def forward_current(fixture):
    code = fixture.args.language
    a, b = fixture.working / 'source-A.txt', fixture.working / 'source-B.txt'
    a.write_bytes(b'unchanged source\r\n')
    b.write_bytes(b'first\r\ncaf\xe9\r\n')
    app = fixture.start(PREFERENCE[code], code, startup=a)
    requester = None
    try:
        requester = incoming(fixture, ('-r', '-e', 'cp1252', '--startup-encoding', '3',
                                       '-s', 'cpp', '-ro', '-g', '2,1', str(b)), code)
        assert requester.process.wait(timeout=5) == 0
        app.send('snapshot')
        assert Path(app.state['document_path']) == b and app.state['encoding'] == 'UTF-8'
        assert app.state['title'].startswith('*') and app.state['dirty'] == '0'
        assert app.state['bom'] == '0' and app.state['read_only'] == '1'
        assert app.state['length'] == '14' and app.state['caret_position'] == '7'
        assert 'C++' in app.state['document_status']
        destination = fixture.directory / 'current-utf8.txt'
        app.send('save', str(destination))
        assert destination.read_bytes() == b'first\r\ncaf\xc3\xa9\r\n'
        assert b.read_bytes() == b'first\r\ncaf\xe9\r\n' and a.read_bytes() == b'unchanged source\r\n'
        app.close()
    finally:
        app.stop()
        if requester:
            requester.stop()
    print(f'PASS: {code} forwarded source/current identities remain distinct and save independent expected bytes', flush=True)


def native_alt(fixture, app, character):
    if sys.platform != 'win32':
        fixture.dialogs.key(app, 'alt+' + character.lower())
        return
    import ctypes
    user = fixture.dialogs.user
    handle = int(app.state['frame_handle'])
    fixture.dialogs.activate(handle)
    assert user.GetForegroundWindow() == handle
    from ctypes import wintypes
    class KeyboardInput(ctypes.Structure):
        _fields_ = [('vk', wintypes.WORD), ('scan', wintypes.WORD), ('flags', wintypes.DWORD),
                    ('time', wintypes.DWORD), ('extra', ctypes.c_size_t)]
    class InputData(ctypes.Union):
        native_input_union_bytes = 32  # Win64 INPUT includes a 32-byte MOUSEINPUT union member.
        _fields_ = [('keyboard', KeyboardInput), ('padding', ctypes.c_byte * native_input_union_bytes)]
    class Input(ctypes.Structure):
        _fields_ = [('kind', wintypes.DWORD), ('data', InputData)]
    user.SendInput.argtypes = [wintypes.UINT, ctypes.POINTER(Input), ctypes.c_int]
    user.SendInput.restype = wintypes.UINT
    packet = (Input * 4)()
    for entry, (key, flags) in zip(packet, ((0x12, 0), (ord(character), 0), (ord(character), 2), (0x12, 2))):
        entry.kind = 1  # INPUT_KEYBOARD.
        entry.data.keyboard = KeyboardInput(key, 0, flags, 0, 0)
    assert user.SendInput(4, packet, ctypes.sizeof(Input)) == 4, ctypes.get_last_error()


def force_new(fixture):
    code = fixture.args.language
    target = fixture.working / 'force-new.txt'
    text = 'café\r\nsecond\tvalue\r\n'
    target.write_bytes(text.encode('cp1252'))
    app = fixture.start(PREFERENCE[code], code, extra='DefaultEncoding=1252\nLoadASCIIasUTF8=0\n',
                        startup=target, arguments=('--startup-encoding', '1252', '--startup-caret', '2,10'))
    child = None
    try:
        app.choose(menu_id(app.state, 'UTF-8'))
        assert app.state['title'].startswith('*') and app.state['dirty'] == '0'
        configure_routing(fixture, 1, 0)
        before = dict(app.state)
        fields = ('length', 'dirty', 'encoding', 'bom', 'document_path', 'caret_position', 'read_only')
        for character in ('N', '0'):
            previous = set(path.name for path in fixture.observation.iterdir())
            (fixture.logs / f'alt-{character}-before.json').write_text(json.dumps(app.state), encoding='utf-8')
            native_alt(fixture, app, character)
            try:
                child = wait_child(fixture.observation, previous)
            except BaseException:
                from resource_process import observation_context
                app.send('snapshot')
                (fixture.logs / f'alt-{character}-failure.json').write_text(
                    json.dumps({'state': app.state, 'native': observation_context(app.folder, app.process),
                                'children': [path.name for path in fixture.observation.iterdir()]}, ensure_ascii=False),
                    encoding='utf-8')
                raise
            app.send('snapshot')
            assert child.process.pid != app.process.pid and app.process.poll() is None
            if character == 'N':
                assert child.state['document_path'] == '' and child.state['length'] == '0'
                child.close()
            else:
                for field in fields:
                    assert child.state[field] == before[field], (character, field, before[field], child.state[field])
                destination = fixture.directory / 'alt-zero-utf8.txt'
                child.send('save', str(destination))
                assert destination.read_bytes() == text.encode('utf-8')
                child.close()
            child.stop()
            child = None
            app.send('snapshot')
            for field in fields:
                assert app.state[field] == before[field]
        previous = set(path.name for path in fixture.observation.iterdir())
        restart = {'en': 'Restart', 'zh_CN': '重新启动(T)', 'zh_TW': '重新啟動(R)'}[code]
        app.begin('menu', str(menu_id(app.state, restart)))
        assert app.process.wait(timeout=10) == 0
        child = wait_child(fixture.observation, previous)
        assert child.process.pid != app.process.pid
        for field in fields:
            assert child.state[field] == before[field], ('Restart', field, before[field], child.state[field])
        destination = fixture.directory / 'restart-utf8.txt'
        child.send('save', str(destination))
        assert destination.read_bytes() == text.encode('utf-8')
        destination.write_bytes(b'watched replacement\r\n')
        child.state = wait_state(child.folder, child.process, lambda state: state['length'] == '21' and state['dirty'] == '0')
        assert target.read_bytes() == text.encode('cp1252')
        child.close()
    finally:
        app.stop()
        if child:
            child.stop()
    print(f'PASS: {code} Alt+N/Alt+0/Restart force distinct processes with reuse enabled; source/current/caret/watchers retained', flush=True)


def startup_id(fixture):
    import gi
    gi.require_version('Gdk', '3.0')
    from gi.repository import Gdk, Gio
    Gdk.init([])
    code = fixture.args.language
    target = fixture.working / 'desktop-startup.txt'
    target.write_bytes(b'unchanged desktop launch\r\n')
    settings(fixture.config, PREFERENCE[code])
    app = fixture.launch(code, startup=target, overrides={'WAYLAND_DEBUG': 'client'})
    requester = None
    try:
        before = dict(app.state)
        display = Gdk.Display.get_default()
        context = display.get_app_launch_context()
        info = Gio.AppInfo.create_from_commandline(str(fixture.executable), 'Owned wxnotepad desktop launcher',
                                                   Gio.AppInfoCreateFlags.NONE)
        # Use the actual desktop caller API, never fabricate an activation ID.
        identifier = context.get_startup_notify_id(info, [])
        environment_key = 'XDG_ACTIVATION_TOKEN' if app.state['backend'] == 'GdkWaylandDisplay' else 'DESKTOP_STARTUP_ID'
        requester = incoming(fixture, ('-ns', str(target)), code,
                             {environment_key: identifier} if identifier else {})
        assert requester.process.wait(timeout=5) == 0
        app.send('snapshot')
        for field in ('length', 'dirty', 'document_path', 'caret_position', 'encoding', 'read_only'):
            assert app.state[field] == before[field]
        record = {'backend': app.state['backend'], 'caller_api': 'GdkAppLaunchContext.get_startup_notify_id',
                  'token_issued': bool(identifier), 'delivered': True, 'active': app.state['active'],
                  'identifier': identifier}
        if identifier and app.state['backend'] == 'GdkX11Display':
            window = app.state['frame_x11_window']
            assert int(window) > 0
            property_value = subprocess.check_output(['xprop', '-id', window, '_NET_STARTUP_ID'], text=True)
            assert identifier in property_value, (identifier, property_value)
            record['native_frame_window'] = window
            record['native_startup_id'] = property_value
        elif identifier:
            # The native protocol trace must contain the real issued token.
            native = (fixture.logs / 'app-001/stderr.log').read_text(encoding='utf-8')
            assert identifier in native
            record['passed_to_native'] = True
        else:
            record['limitation'] = 'Actual compositor/GTK caller supplied no startup or activation token.'
        (fixture.logs / 'desktop-startup.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
        app.close()
    finally:
        app.stop()
        if requester:
            requester.stop()
    print(f'PASS: {code} actual desktop startup ID API, native forwarding when issued, and separate activation result', flush=True)


def configure_routing(fixture, reuse, single):
    text = fixture.config.read_text(encoding='utf-16')
    text = text.replace('UnrelatedLanguage=preserved',
                        f'UnrelatedLanguage=preserved\nReuseWindow={reuse}\nSingleFileInstance={single}')
    fixture.config.write_text(text, encoding='utf-16')


def matrix(fixture):
    reference = json.loads(Path(__file__).with_name('window_routing_reference.json').read_text(encoding='utf-8'))
    a, b = fixture.working / 'A.txt', fixture.working / 'B.txt'
    a.write_bytes(b'first\r\nsecond\r\n')
    b.write_bytes(b'other\r\nfile\r\n')
    code = fixture.args.language
    rows = []
    for index, row in enumerate(reference['rows']):
        app = fixture.start(PREFERENCE[code], code, startup=a, arguments=('-n',))
        requester = None
        try:
            configure_routing(fixture, row['reuse'], row['single'])
            prefix = ('/', '-', '--')[index % 3] if sys.platform == 'win32' else ('-', '--')[index % 2]
            arguments = [prefix + row['switch'][1:]] if row['switch'] else []
            if row['path'] != 'none':
                arguments.append(str(a if row['path'] == 'same' else b))
            requester = incoming(fixture, arguments, code)
            if row['new_window']:
                finish_start(requester)
                assert requester.process.pid != app.process.pid and app.process.poll() is None
                expected = b'' if row['path'] == 'none' else a.read_bytes() if row['path'] == 'same' else b.read_bytes()
                destination = fixture.directory / f'child-{index}.txt'
                requester.send('save', str(destination))
                assert destination.read_bytes() == expected, row
                requester.close()
            else:
                assert requester.process.wait(timeout=5) == 0, row
            expected = bytes.fromhex(row['parent_hex'])
            destination = fixture.directory / f'parent-{index}.txt'
            app.send('save', str(destination))
            assert destination.read_bytes() == expected, row
            assert a.read_bytes() == b'first\r\nsecond\r\n' and b.read_bytes() == b'other\r\nfile\r\n'
            rows.append(dict(row, parent_pid=app.process.pid, requester_pid=requester.process.pid,
                             requester_exited=requester.process.poll() is not None))
            (fixture.logs / 'routing-matrix.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
            app.close()
        finally:
            app.stop()
            if requester:
                requester.stop()
    (fixture.logs / 'routing-matrix.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    print(f'PASS: {code} all 60 independent original settings/override/path routes, including SFI miss', flush=True)


def capture_policy_failure(fixture, app, requester, choice, wait_started_ns, failure):
    """Freeze owned process/file observations before cleanup or any new command."""
    from app_observation import read_state
    snapshot = {'choice': choice, 'monotonic_ns': time.monotonic_ns(),
                'requester_wait_started_ns': wait_started_ns,
                'failure_type': type(failure).__name__, 'failure': str(failure)}
    for role, child in (('receiver', app), ('requester', requester)):
        record = {'pid': child.process.pid, 'exit': child.process.poll()}
        try:
            record['state'] = {key: value for key, value in read_state(child.folder).items()
                               if not key.startswith('menu.')}
        except (OSError, ValueError) as error:
            record['state_error'] = str(error)
        for name, path in (('command', child.folder / 'command'), ('stderr', Path(child.log.name))):
            try:
                record[name] = path.read_text(encoding='utf-8', errors='replace')
            except OSError as error:
                record[name + '_error'] = str(error)
        if sys.platform == 'linux':
            for name in ('stat', 'status', 'wchan'):
                try:
                    record['proc_' + name] = (Path('/proc') / str(child.process.pid) / name).read_text()
                except OSError as error:
                    record['proc_' + name + '_error'] = str(error)
            gcda_files = {}
            try:
                for descriptor in (Path('/proc') / str(child.process.pid) / 'fd').iterdir():
                    try:
                        target = os.readlink(descriptor)
                        if target.endswith('.gcda'):
                            gcda_files[descriptor.name] = {'path': target}
                            try:
                                info = Path('/proc') / str(child.process.pid) / 'fdinfo' / descriptor.name
                                gcda_files[descriptor.name]['fdinfo'] = info.read_text()
                            except OSError as error:
                                gcda_files[descriptor.name]['fdinfo_error'] = str(error)
                    except OSError:
                        pass
                record['gcda_files'] = gcda_files
            except OSError as error:
                record['gcda_files_error'] = str(error)
        snapshot[role] = record
    if sys.platform == 'linux':
        pids = {str(app.process.pid), str(requester.process.pid)}
        try:
            owned_locks = []
            for row in Path('/proc/locks').read_text().splitlines():
                fields = row.split()
                pid_field = 5 if fields[1] == '->' else 4
                if fields[pid_field] in pids:
                    owned_locks.append(row)
            snapshot['owned_proc_locks'] = owned_locks
        except OSError as error:
            snapshot['proc_locks_error'] = str(error)
    destination = fixture.logs / f'policy-{choice}-failure.json'
    destination.write_text(json.dumps(snapshot, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'Owned policy failure snapshot: {destination}', flush=True)


def policy(fixture):
    code = fixture.args.language
    a, b = fixture.working / 'policy-A.txt', fixture.working / 'policy-B.txt'
    for choice in ('yes', 'no', 'cancel', 'failed-save'):
        a.write_bytes(b'first\r\nsecond\r\n')
        b.write_bytes(b'other\r\nfile\r\n')
        app = fixture.start(PREFERENCE[code], code, extra='FileWatchingMode=0\n', startup=a)
        requester = None
        preserved = a.with_suffix('.preserved')
        with ExitStack() as locks:
            try:
                fixture.dirty(app)
                before = dict(app.state)
                if choice == 'failed-save':
                    if sys.platform == 'win32':
                        locks.enter_context(a.open('rb'))
                    else:
                        a.rename(preserved)
                        a.mkdir()
                        locks.callback(lambda: (a.rmdir(), preserved.rename(a)))
                requester = incoming(fixture, ('-r', '-s', 'json', '-ro', '-g', '2,1', str(b)), code)
                window = fixture.dialogs.wait(app.process, SAVE_TITLES[code], SAVE_FRAGMENTS[code])
                answer_save(fixture, app, code, 'yes' if choice == 'failed-save' else choice, window)
                if choice == 'failed-save':
                    error = fixture.dialogs.wait(app.process, ERROR_TITLES[code],
                                                 str(a) if sys.platform == 'win32' else ERROR_FRAGMENTS[code])
                    fixture.dialogs.answer(app.process, 'ok', window=error)
                wait_started_ns = time.monotonic_ns()
                try:
                    assert requester.process.wait(timeout=5) == 0
                except (subprocess.TimeoutExpired, AssertionError) as failure:
                    try:
                        capture_policy_failure(fixture, app, requester, choice, wait_started_ns, failure)
                    except OSError as error:
                        print(f'Could not retain owned policy failure snapshot: {error}', flush=True)
                    raise
                app.send('snapshot')
                if choice in ('cancel', 'failed-save'):
                    assert app.state['caret_position'] == '8'
                    for field in ('length', 'dirty', 'document_path', 'read_only', 'can_undo', 'encoding'):
                        assert app.state[field] == before[field], (choice, field, before[field], app.state[field])
                    assert 'JSON' not in app.state['document_status']
                    app.choose(menu_id(app.state, {'en': 'Undo', 'zh_CN': '撤销(U)', 'zh_TW': '復原(U)'}[code]))
                    assert app.state['dirty'] == '0' and app.state['can_redo'] == '1'
                else:
                    assert Path(app.state['document_path']) == b
                    assert app.state['read_only'] == '1' and 'JSON' in app.state['document_status']
                    assert app.state['caret_position'] == '7'
                locks.close()
                assert a.read_bytes() == (b'Xfirst\r\nsecond\r\n' if choice == 'yes' else b'first\r\nsecond\r\n')
                assert b.read_bytes() == b'other\r\nfile\r\n'
                app.close()
            finally:
                app.stop()
                if requester:
                    requester.stop()
        print(f'PASS: {code} forwarded {choice} is handled once, with original caret and disk/history protection', flush=True)


def policy_pending(fixture):
    code = fixture.args.language
    a, b = fixture.working / 'pending-A.txt', fixture.working / 'pending-B.txt'
    original = b'first\r\nsecond\r\n'
    a.write_bytes(original)
    b.write_bytes(b'other\r\nfile\r\n')
    app = fixture.start(PREFERENCE[code], code, extra='FileWatchingMode=0\n', startup=a)
    requester = None
    try:
        fixture.dirty(app)
        before = dict(app.state)
        requester = incoming(fixture, ('-r', '-s', 'json', '-ro', '-g', '2,1', str(b)), code)
        window = fixture.dialogs.wait(app.process, SAVE_TITLES[code], SAVE_FRAGMENTS[code])
        # A user's pending decision must outlive the transport's discovery timeout.
        try:
            early_exit = requester.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            early_exit = None
        answer_save(fixture, app, code, 'cancel', window)
        status = requester.process.wait(timeout=5)
        assert early_exit is None, f'requester exited with {early_exit} before the user answered Save'
        assert status == 0
        app.send('snapshot')
        assert app.state['caret_position'] == '8'
        for field in ('length', 'dirty', 'document_path', 'read_only', 'can_undo', 'encoding'):
            assert app.state[field] == before[field], (field, before[field], app.state[field])
        assert 'JSON' not in app.state['document_status']
        assert a.read_bytes() == original and b.read_bytes() == b'other\r\nfile\r\n'
        app.choose(menu_id(app.state, {'en': 'Undo', 'zh_CN': '撤销(U)', 'zh_TW': '復原(U)'}[code]))
        assert app.state['dirty'] == '0' and app.state['can_redo'] == '1'
        app.close()
    finally:
        app.stop()
        if requester:
            requester.stop()
    print(f'PASS: {code} requester awaits the real Save decision and acknowledges Cancel exactly once', flush=True)


def policy_disconnect(fixture):
    a, b = fixture.working / 'disconnect-A.txt', fixture.working / 'disconnect-B.txt'
    original = b'first\r\nsecond\r\n'
    for endpoint in ('requester', 'receiver'):
        a.write_bytes(original)
        b.write_bytes(b'other\r\nfile\r\n')
        app = fixture.start(extra='FileWatchingMode=0\n', startup=a)
        requester = None
        try:
            fixture.dirty(app)
            before = dict(app.state)
            requester = incoming(fixture, ('-r', str(b)))
            window = fixture.dialogs.wait(app.process, SAVE_TITLES['en'], SAVE_FRAGMENTS['en'])
            if endpoint == 'requester':
                requester.stop()
                answer_save(fixture, app, 'en', 'cancel', window)
                app.send('snapshot')
                assert app.process.poll() is None
                for field in ('length', 'dirty', 'document_path', 'read_only', 'can_undo', 'encoding'):
                    assert app.state[field] == before[field], (field, before[field], app.state[field])
                requester = incoming(fixture, ('-ns', str(a)))
                assert requester.process.wait(timeout=5) == 0
                app.choose(menu_id(app.state, 'Undo'))
                assert app.state['dirty'] == '0' and app.state['can_redo'] == '1'
                app.close()
            else:
                app.stop()
                assert requester.process.wait(timeout=5) == 2
                assert not (requester.folder / 'state').exists()
            assert a.read_bytes() == original and b.read_bytes() == b'other\r\nfile\r\n'
        finally:
            app.stop()
            if requester:
                requester.stop()
    print('PASS: pending modal sender disconnect preserves receiver; receiver disconnect exits requester without retry', flush=True)


def busy(fixture):
    code = fixture.args.language
    a, b = fixture.working / 'busy-A.txt', fixture.working / 'busy-B.txt'
    a.write_bytes(b'first\r\nsecond\r\n')
    b.write_bytes(b'other\r\nfile\r\n')
    for route in ('same', 'different', 'none'):
        for choice in ('yes', 'no'):
            app = fixture.start(PREFERENCE[code], code, startup=a, arguments=('-n',))
            requester = None
            try:
                goto = next(identity for identity, item in menus(app.state).items()
                            if item['label'].endswith('\tCtrl+G'))
                app.begin('menu', str(goto))
                modal = fixture.dialogs.wait(app.process, TEXT[code]['goto'])
                arguments = ('-ns', str(a)) if route == 'same' else ('-r', str(b)) if route == 'different' else ('-r',)
                requester = incoming(fixture, arguments, code)
                fragment = {'en': 'busy or has an active dialog box', 'zh_CN': '正忙或打开了对话框',
                            'zh_TW': '忙碌中或開啟了對話方塊'}[code]
                prompt = fixture.dialogs.wait(requester.process, 'wxnotepad', fragment)
                fixture.dialogs.answer(requester.process, choice, window=prompt)
                if choice == 'yes':
                    finish_start(requester)
                    assert requester.process.pid != app.process.pid
                    expected = '' if route == 'none' else str(a if route == 'same' else b)
                    assert requester.state['document_path'] == expected
                    requester.close()
                else:
                    assert requester.process.wait(timeout=5) == 0
                    assert not (requester.folder / 'state').exists()
                fixture.dialogs.click(modal, TEXT[code]['cancel'] if sys.platform == 'win32' else 'Cancel')
                app.finish()
                assert Path(app.state['document_path']) == a and app.state['length'] == '15'
                assert a.read_bytes() == b'first\r\nsecond\r\n' and b.read_bytes() == b'other\r\nfile\r\n'
                app.close()
            finally:
                app.stop()
                if requester:
                    requester.stop()
    print(f'PASS: {code} real modal same/different/empty routes ask Yes/New or No/exit and never deliver to busy frame', flush=True)


def aliases(fixture):
    a = fixture.working / 'Aliases 中文.txt'
    a.write_bytes(b'first\r\nsecond\r\n')
    hardlink = fixture.working / 'hardlink.txt'
    hardlink.hardlink_to(a)
    subdir = fixture.working / 'subdir'
    subdir.mkdir()
    cases = [str(a), a.name, str(subdir / '..' / a.name), str(hardlink)]
    if sys.platform == 'win32':
        cases.append(str(a).upper())
    for spelling in cases:
        app = fixture.start(startup=a, arguments=('-n',))
        requester = None
        try:
            fixture.dirty(app)
            before = dict(app.state)
            requester = incoming(fixture, ('-ns', '-ro', '-g', '2,1', spelling))
            assert requester.process.wait(timeout=5) == 0
            app.send('snapshot')
            for field in ('length', 'dirty', 'document_path', 'caret_position', 'can_undo', 'read_only'):
                assert app.state[field] == before[field], (spelling, field)
            app.choose(menu_id(app.state, 'Undo'))
            app.close()
        finally:
            app.stop()
            if requester:
                requester.stop()
    if sys.platform != 'win32':
        lower, upper = fixture.working / 'case.txt', fixture.working / 'CASE.txt'
        lower.write_bytes(b'lower')
        upper.write_bytes(b'UPPER')
        app = fixture.start(startup=lower)
        requester = None
        try:
            requester = incoming(fixture, ('-rs', str(upper)))
            finish_start(requester)
            assert requester.process.poll() is None and app.process.poll() is None
            assert Path(requester.state['document_path']) == upper
            assert Path(app.state['document_path']) == lower
            requester.close()
            app.close()
        finally:
            app.stop()
            if requester:
                requester.stop()
    print('PASS: native filesystem identity, relative/dot/hardlink aliases and platform case rules', flush=True)


def closing(fixture):
    code = fixture.args.language
    target = fixture.working / 'closing.txt'
    target.write_bytes(b'closing original\r\n')
    app = fixture.start(PREFERENCE[code], code, startup=target)
    requester = None
    try:
        endpoint = endpoint_for(app)
        app.begin('close')
        wait_for_close_withdrawal(app, endpoint)
        (fixture.logs / 'closing-transition.json').write_text(json.dumps({
            'receiver_pid': app.process.pid, 'endpoint': str(endpoint),
            'close_sequence': app.sequence, 'command_consumed': True,
            'endpoint_withdrawn': not endpoint.exists(), 'receiver_exit_at_barrier': app.process.poll()
        }, indent=2), encoding='utf-8')
        requester = incoming(fixture, ('-ns', str(target)), code)
        assert app.process.wait(timeout=10) == 0
        if endpoint:
            assert not endpoint.exists(), 'Orderly close must remove only its own listener'
        finish_start(requester)
        assert requester.process.pid != app.process.pid
        assert Path(requester.state['document_path']) == target
        assert target.read_bytes() == b'closing original\r\n'
        live_endpoint = endpoint_for(requester)
        requester.close()
        if live_endpoint:
            assert not live_endpoint.exists()
    finally:
        app.stop()
        if requester:
            requester.stop()
    print(f'PASS: {code} launch concurrent with target close creates one live independent receiver', flush=True)


def endpoint_for(app):
    if sys.platform == 'win32':
        return WindowsRoutingEndpoint(app.process.pid)
    candidates = list(Path(os.environ['XDG_RUNTIME_DIR']).glob(f'wxnotepad-*/{app.process.pid}.sock'))
    assert len(candidates) == 1, ('Process must own one private Unix listener', candidates)
    endpoint = candidates[0]
    assert endpoint.is_socket() and endpoint.parent.stat().st_uid == os.getuid()
    assert endpoint.parent.stat().st_mode & 0o077 == 0
    return endpoint


class WindowsRoutingEndpoint:
    """Read only this process's actual hidden native routing window."""
    window_class = 'wxnotepad.WindowRouting.1'

    def __init__(self, pid):
        import ctypes
        from ctypes import wintypes
        self.ctypes, self.types, self.pid = ctypes, wintypes, pid
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        self.user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        self.user.EnumWindows.argtypes = [callback, wintypes.LPARAM]
        windows = []

        @callback
        def visit(window, unused):
            if self.owns(window):
                windows.append(window)
            return True

        self.user.EnumWindows(visit, 0)
        assert len(windows) == 1, ('Process must own one native routing window', pid, windows)
        self.window = windows[0]

    def owns(self, window):
        owner = self.types.DWORD()
        self.user.GetWindowThreadProcessId(window, self.ctypes.byref(owner))
        name = self.ctypes.create_unicode_buffer(64)
        self.user.GetClassNameW(window, name, len(name))
        return owner.value == self.pid and name.value == self.window_class

    def exists(self):
        return self.owns(self.window)

    def __str__(self):
        return f'{self.window_class}:pid={self.pid}:hwnd={self.window}'


def wait_for_close_withdrawal(app, endpoint):
    from app_observation import read_state
    deadline = time.monotonic() + 15
    state = {}
    while time.monotonic() < deadline:
        try:
            state = read_state(app.folder)
        except OSError:
            pass
        consumed = not (app.folder / 'command').exists() and state.get('sequence') == str(app.sequence)
        # Publication and even its ACK precede frame.Close(). Require the real
        # listener to withdraw before asserting the closing route's new window.
        if consumed and state.get('result') == 'ok' and not endpoint.exists():
            return
        time.sleep(.01)
    raise AssertionError(('Close did not consume its command and withdraw its owned endpoint', str(endpoint), state))


def activation(fixture):
    code = fixture.args.language
    target = fixture.working / 'activation.txt'
    target.write_bytes(b'first\r\nsecond\r\n')
    app = fixture.start(PREFERENCE[code], code, startup=target)
    requester = None
    caller = None
    user = None
    try:
        if sys.platform == 'win32':
            import ctypes
            from ctypes import wintypes
            user = ctypes.WinDLL('user32', use_last_error=True)
            user.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
            user.CreateWindowExW.argtypes = [wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD,
                                             ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                             wintypes.HWND, wintypes.HMENU, wintypes.HINSTANCE, ctypes.c_void_p]
            user.CreateWindowExW.restype = wintypes.HWND
            user.DestroyWindow.argtypes = [wintypes.HWND]
            user.GetForegroundWindow.restype = wintypes.HWND
            user.ShowWindow(int(app.state['frame_handle']), 6)  # Native minimize, before the activation under test.
            caller = user.CreateWindowExW(0, 'STATIC', 'Owned routing launcher', 0x10cf0000,
                                           20, 20, 320, 120, None, None, None, None)
            assert caller
            # Establish an actual owned foreground launcher as the user's source
            # application. After launching, no test helper focuses the receiver.
            fixture.dialogs.activate(caller)
            assert user.GetForegroundWindow() == caller
            app.state = wait_state(app.folder, app.process, lambda state: state['shown'] == '0')
        elif app.state['backend'] == 'GdkX11Display':
            windows = subprocess.check_output(['xdotool', 'search', '--onlyvisible', '--pid',
                                                str(app.process.pid)], text=True).splitlines()
            assert len(windows) == 1
            subprocess.run(['xdotool', 'windowminimize', windows[0]], check=True)
            app.state = wait_state(app.folder, app.process, lambda state: state['shown'] == '0')
        else:
            # Minimize through the existing public Escape preference/gesture.
            # No key is sent to restore or activate the receiver after delivery.
            minimize = 'Minimize wxnotepad' if code == 'en' else '最小化 wxnotepad(M)'
            app.choose(menu_id(app.state, minimize))
            fixture.dialogs.key(app, 'Escape')
            from weston_scene import mapped_toplevel
            deadline = time.monotonic() + 15
            while True:
                scene = subprocess.check_output(['weston-debug', 'scene-graph'], text=True, timeout=2)
                if not mapped_toplevel(scene, app.process.pid):
                    break
                assert time.monotonic() < deadline, 'Escape must minimize the owned Wayland surface'
                time.sleep(.02)
            (fixture.logs / 'activation-minimized.scene.txt').write_text(scene, encoding='utf-8')
        # Wayland's compositor owns activation/minimization; delivery and actual
        # activation are recorded separately without a driver focus request.
        requester = incoming(fixture, ('-ns', str(target)), code)
        assert requester.process.wait(timeout=5) == 0
        if app.state['backend'] != 'GdkWaylandDisplay':
            app.state = wait_state(app.folder, app.process, lambda state: state['shown'] == '1')
        app.send('snapshot')
        record = {key: app.state[key] for key in ('backend', 'shown', 'active', 'focus', 'document_path')}
        record['delivered'] = True
        if app.state['backend'] == 'GdkWaylandDisplay':
            scene = subprocess.check_output(['weston-debug', 'scene-graph'], text=True, timeout=2)
            record['native_mapped'] = mapped_toplevel(scene, app.process.pid)
            (fixture.logs / 'activation-delivered.scene.txt').write_text(scene, encoding='utf-8')
        if sys.platform == 'win32':
            class GUIThreadInfo(ctypes.Structure):
                _fields_ = [('cbSize', wintypes.DWORD), ('flags', wintypes.DWORD),
                            ('active', wintypes.HWND), ('focus', wintypes.HWND), ('capture', wintypes.HWND),
                            ('menu', wintypes.HWND), ('move', wintypes.HWND), ('caret', wintypes.HWND),
                            ('caretRect', wintypes.RECT)]
            user.GetGUIThreadInfo.argtypes = [wintypes.DWORD, ctypes.POINTER(GUIThreadInfo)]
            info = GUIThreadInfo()
            info.cbSize = ctypes.sizeof(info)
            assert user.GetGUIThreadInfo(0, ctypes.byref(info))
            record.update(foreground=user.GetForegroundWindow(), native_focus=info.focus)
            assert record['foreground'] == int(app.state['frame_handle']), record
            assert info.focus == int(app.state['editor_handle']), record
        (fixture.logs / 'activation.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
        app.close()
    finally:
        app.stop()
        if requester:
            requester.stop()
        if caller:
            user.DestroyWindow(caller)
    print(f'PASS: {code} delivered and platform restore/activation observed independently', flush=True)


def sessions(fixture):
    code = fixture.args.language
    target = fixture.working / 'session.txt'
    target.write_bytes(b'private session original\r\n')
    app = fixture.start(PREFERENCE[code], code, startup=target)
    try:
        if sys.platform == 'win32':
            windows_session(fixture, app, target, code)
        else:
            records = []
            for backend in ('x11', 'wayland'):
                control = fixture.directory / ('peer-' + backend)
                control.mkdir()
                log = (fixture.logs / ('session-' + backend + '.log')).open('wb')
                peer = subprocess.Popen([sys.executable, str(ROOT / 'tools/xvfb_session.py'),
                                         'dbus-run-session', '--', sys.executable,
                                         str(ROOT / 'tools/clipboard_isolated.py'), backend, sys.executable,
                                         str(ROOT / 'tests/window_routing_session_peer.py'),
                                         str(fixture.executable), str(target), str(control), code],
                                        stdout=log, stderr=log)
                try:
                    deadline = time.monotonic() + 15
                    while not (control / 'ready.json').exists():
                        assert peer.poll() is None, (backend, peer.returncode)
                        assert time.monotonic() < deadline, 'Actual second desktop session did not become ready'
                        time.sleep(.02)
                    record = json.loads((control / 'ready.json').read_text(encoding='utf-8'))
                    assert record['pid'] != app.process.pid and Path(record['document_path']) == target
                    assert record['runtime'] != os.environ['XDG_RUNTIME_DIR']
                    if backend == 'x11':
                        assert record['display'] != os.environ['DISPLAY']
                    requester = incoming(fixture, ('-ns', str(target)), code)
                    try:
                        assert requester.process.wait(timeout=5) == 0
                    finally:
                        requester.stop()
                    records.append(record)
                finally:
                    (control / 'done').touch()
                    try:
                        assert peer.wait(timeout=10) == 0
                    finally:
                        if peer.poll() is None:
                            peer.terminate()
                            peer.wait(timeout=5)
                        log.close()
            (fixture.logs / 'isolated-sessions.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
        assert target.read_bytes() == b'private session original\r\n'
        app.close()
    finally:
        app.stop()
    print(f'PASS: {code} real desktop sessions each discover only their own process endpoints', flush=True)


def windows_session(fixture, app, target, code):
    import ctypes
    from ctypes import wintypes
    sys.path.insert(0, str(ROOT / 'tests/reference_capture'))
    from private_desktop import Process as DesktopProcess, kernel
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.CreateDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p,
                                    wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    user.CreateDesktopW.restype = wintypes.HANDLE
    user.CloseDesktop.argtypes = [wintypes.HANDLE]
    name = f'wxnotepad-routing-{os.getpid()}'
    desktop = user.CreateDesktopW(name, None, None, 0, 0x01ff, None)
    assert desktop
    owned = []

    def launch(arguments, folder):
        folder.mkdir()
        process = DesktopProcess([fixture.executable, '--observe', folder, *arguments],
                                 'WinSta0\\' + name, cwd=fixture.working)
        wrapped = Process.__new__(Process)
        wrapped.folder, wrapped.sequence = folder, 0
        wrapped.process = Child(process.pid)
        kernel.CloseHandle(process.handle)
        owned.append(wrapped)
        return wrapped

    try:
        peer = launch(('-rs', str(target)), fixture.directory / 'private-app')
        finish_start(peer)
        assert peer.process.poll() is None and app.process.poll() is None
        requester = launch(('-ns', str(target)), fixture.directory / 'private-requester')
        assert requester.process.wait(timeout=5) == 0
        requester = incoming(fixture, ('-ns', str(target)), code)
        owned.append(requester)
        assert requester.process.wait(timeout=5) == 0
        assert peer.process.poll() is None and app.process.poll() is None
        peer.close()
    finally:
        for process in owned:
            process.stop()
        assert user.CloseDesktop(desktop)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--cmake-dir', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--scenario', choices=('settings', 'same', 'forward', 'matrix', 'policy', 'policy-pending',
                                             'policy-disconnect', 'busy', 'aliases',
                                             'closing', 'sessions', 'activation', 'forward-current', 'force-new', 'startup-id'), default='settings')
    parser.add_argument('--language', choices=tuple(PREFERENCE), default='en')
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='window-routing-') as name:
        {'settings': settings_menu, 'same': same_file, 'forward': forward, 'matrix': matrix, 'policy': policy,
         'policy-pending': policy_pending, 'policy-disconnect': policy_disconnect,
         'busy': busy, 'aliases': aliases, 'closing': closing, 'sessions': sessions,
         'activation': activation, 'forward-current': forward_current, 'force-new': force_new,
         'startup-id': startup_id}[args.scenario](Acceptance(args, Path(name).resolve()))


if __name__ == '__main__':
    main()
