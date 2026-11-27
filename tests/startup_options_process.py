"""Exercise ordinary command-line opening through the real packaged application."""
import argparse
from contextlib import contextmanager
import ctypes
import json
import os
from pathlib import Path
import sys
import subprocess
import shutil
import tempfile

from localization_package import Process, bundle_application, environment, menu_id, settings
from localization_native import Dialogs
from resource_process import wait_state
from observed_shutdown_process import obstruct_publication


def summary(state):
    return {key: value for key, value in state.items() if not key.startswith("menu.")}


def start(*args, **kwargs):
    app = Process.__new__(Process)
    try:
        app.__init__(*args, **kwargs)
        return app
    except BaseException:
        app.stop()
        raise


def pending_start(executable, folder, env, arguments, cwd):
    """Keep startup observable while an actual native confirmation is pending."""
    app = Process.__new__(Process)
    app.folder = folder
    folder.mkdir(parents=True)
    app.sequence = 0
    app.log = (folder / 'stderr.log').open('wb')
    app.process = subprocess.Popen([str(executable), '--observe', str(folder), *arguments],
                                   cwd=cwd, env=env, stdout=app.log, stderr=app.log)
    return app


def finish_start(app):
    app.state = wait_state(app.folder, app.process, lambda state: state['sequence'] == '0' and
                           (state['backend'] != 'GdkWaylandDisplay' or state['active'] == '1'))
    assert app.state['shown'] == '1', summary(app.state)


def paths(executable, root):
    settings(executable.parent / 'Notepad4.ini', 1033, 'WordWrap=0\n')
    working = root / 'caller working 中文'
    working.mkdir()
    original = 'ordinary\t中文😀\r\nsecond\n'.encode('utf-8')
    for index, (filename, absolute, marker) in enumerate((
            ('relative.txt', False, ''), ('absolute.txt', True, ''),
            ('space 中文😀.txt', False, ''), ('-leading file.txt', False, '-'),
            ('--leading 中文😀.txt', False, '--'))):
        target = working / filename
        target.write_bytes(original)
        arguments = ([marker] if marker else []) + [str(target) if absolute else filename]
        app = start(executable, root / f'path-{index}', environment(executable, root / 'children'),
                      arguments=arguments, cwd=working)
        try:
            assert Path(app.state['document_path']) == target, summary(app.state)
            assert app.state['length'] == str(len(original)), summary(app.state)
            assert app.state['dirty'] == '0' and app.state['encoding'] == 'UTF-8', summary(app.state)
            app.send('save')
            assert target.read_bytes() == original
            app.close()
        finally:
            app.stop()
    print('PASS: packaged single-file opening, caller cwd, Unicode and option terminators', flush=True)


def encodings(executable, root):
    settings(executable.parent / 'Notepad4.ini', 1033, 'WordWrap=0\nLoadASCIIasUTF8=0\n')
    cases = (
        ('wInDoWs_1252', b'caf\xe9\r\n', 'café\r\n', 'CP1252'),
        ('ibm437', b'\x82\x9c\r\n', 'é£\r\n', 'CP437'),
        ('GB-2312', b'\xd6\xd0\xce\xc4\r\n', '中文\r\n', 'CP936'),
        ('csbig5', b'\xa4\xa4\xa4\xe5\r\n', '中文\r\n', 'CP950'),
        ('GB_18030', '中文😀\r\n'.encode('gb18030'), '中文😀\r\n', 'CP54936'),
        ('uTf_8', '中文😀\r\n'.encode('utf-8'), '中文😀\r\n', 'UTF-8'),
        ('utf-16le', '中文😀\r\n'.encode('utf-16-le'), '中文😀\r\n', 'UTF-16LE'),
        ('utf-16be', '中文😀\r\n'.encode('utf-16-be'), '中文😀\r\n', 'UTF-16BE'),
        # Independent original literals retained by issue #279's codec fixtures.
        ('sHiFt_JiS', b'\xb6\xfb\xfc\r\n', 'ｶ髙\r\n', 'CP932'),
        ('uHc', b'\xb0\xa1\x81\x41\r\n', '가갂\r\n', 'CP949'),
    )
    prefixes = ('/', '-', '--') if sys.platform == 'win32' else ('-', '--')
    for index, (alias, data, text, encoding) in enumerate(cases):
        target = root / f'encoding-{index}.txt'
        target.write_bytes(data)
        prefix = prefixes[index % len(prefixes)]
        app = start(executable, root / f'enc-{index}', environment(executable, root / 'children'),
                    arguments=(prefix + 'e', alias, prefix + 's', 'json', str(target)), cwd=root)
        try:
            assert app.state['encoding'] == encoding, (alias, summary(app.state))
            assert app.state['length'] == str(len(text.encode('utf-8'))), (alias, summary(app.state))
            assert 'JSON' in app.state['document_status'], summary(app.state)
            destination = root / f'saved-{index}.txt'
            app.send('save', str(destination))
            assert destination.read_bytes() == data, alias
            assert target.read_bytes() == data
            app.close()
        finally:
            app.stop()
    print('PASS: original source aliases, source bytes, manual scheme and platform option prefixes', flush=True)


def positions(executable, root, display=False):
    fixture = 'startup_display_reference.json' if display else 'startup_g_reference.json'
    reference = json.loads(Path(__file__).with_name(fixture).read_text(encoding='utf-8'))
    settings(executable.parent / 'Notepad4.ini', 1033, 'WordWrap=0\nTabWidth=4\n')
    target = root / 'positions 中文😀.txt'
    original = reference['text'].encode('utf-8')
    target.write_bytes(original)
    for index, expected in enumerate(reference['positions']):
        option = expected['option'] if sys.platform == 'win32' else '-' + expected['option'][1:]
        app = start(executable, root / f'position-{index}', environment(executable, root / 'children'),
                    arguments=(option, expected['location'], str(target)), cwd=root)
        try:
            for field, key in (('caret_position', 'caret'), ('selection_start', 'anchor'),
                               ('selection_end', 'caret'), ('dirty', 'modified'), ('read_only', 'readonly')):
                assert app.state[field] == str(expected[key]), (expected['option'], expected['location'], field, summary(app.state))
            for field in ('first_visible_line', 'x_offset'):
                assert app.state[field] == str(expected[field]), (expected['location'], field, summary(app.state))
            assert app.state['tab_width'] == '4', 'A jump tab-width override must not change the document settings'
            assert app.state['length'] == str(len(original)), summary(app.state)
            assert target.read_bytes() == original
            app.close()
        finally:
            app.stop()
    print('PASS: original independent startup positions and empty selections: ' + fixture, flush=True)


def readonly(executable, root):
    settings(executable.parent / 'Notepad4.ini', 1033, 'WordWrap=0\nTabWidth=4\n')
    target = root / 'foldable source 中文😀.txt'
    original = ('int f() {\n' + '// filler\n' * 99 + '\t中😀target\n}\n').encode('utf-8')
    target.write_bytes(original)
    mode = target.stat().st_mode
    prefixes = ('/', '-', '--') if sys.platform == 'win32' else ('-', '--')
    for index, prefix in enumerate(prefixes):
        app = start(executable, root / f'readonly-{index}', environment(executable, root / 'children'),
                    arguments=('--window-position', '100,100,640,480,0', prefix + 's', 'cpp',
                               prefix + 'g', '101,4', prefix + 'rO', str(target)), cwd=root)
        try:
            assert app.state['read_only'] == '1' and 'Read Only Mode' in app.state['title'], summary(app.state)
            assert app.state['caret_position'] == '1008', summary(app.state)
            assert app.state['selection_start'] == app.state['selection_end'] == '1008', summary(app.state)
            assert app.state['fold_headers'], 'Manually selected C++ produces the original foldable document'
            first = int(app.state['first_visible_line'])
            visible = int(app.state['caret_visible_line'])
            assert first > 0 and first <= visible < first + int(app.state['lines_on_screen']), summary(app.state)
            assert app.state['dirty'] == '0' and target.stat().st_mode == mode
            app.choose(menu_id(app.state, 'Read Only Mode'))
            assert app.state['read_only'] == '0' and target.stat().st_mode == mode, summary(app.state)
            assert target.read_bytes() == original
            app.close()
        finally:
            app.stop()
    print('PASS: readonly startup mode, unchanged permissions, manual folding scheme and visible distant caret', flush=True)


def help_message(executable, root):
    dialogs = Dialogs(root / 'dialogs')
    for code, preference, title, usage in (
            ('en', 1033, 'Command Line Help', 'Usage: wxnotepad [options] [file]'),
            ('zh_CN', 2052, '命令行帮助', '用法：wxnotepad [选项] [文件]'),
            ('zh_TW', 1028, '命令列說明', '用法：wxnotepad [選項] [檔案]')):
        settings(executable.parent / 'Notepad4.ini', preference)
        with (root / (code + '-help.log')).open('wb') as log:
            process = subprocess.Popen([str(executable), '/?' if sys.platform == 'win32' else '-?'],
                                       cwd=root, env=environment(executable, root / 'children', code),
                                       stdout=log, stderr=log)
            try:
                window = dialogs.wait(process, title, message=usage)
                text = '\n'.join(dialogs.texts(window))
                assert usage in text, (code, text)
                assert all(option in text for option in ('-e ', '-s ', '-g ', '-G ', '-ro', '-?')), text
                assert '--startup-' not in text and ('Windows' if sys.platform == 'win32' else 'Linux') in text
                dialogs.answer(process, 'ok', window=window)
                assert process.wait(timeout=10) == 0
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
    print('PASS: delivered-only command-line help and clean exit in all three interface languages', flush=True)


def missing_files(executable, root):
    dialogs = Dialogs(root / 'dialogs')
    for code, preference, title in (('en', 1033, 'Create file?'), ('zh_CN', 2052, '创建文件？'),
                                     ('zh_TW', 1028, '建立檔案？')):
        settings(executable.parent / 'Notepad4.ini', preference)
        for choice in ('yes', 'no', 'cancel'):
            target = root / f'{code}-{choice}-missing 中文😀.txt'
            app = pending_start(executable, root / (code + '-' + choice),
                                environment(executable, root / 'children', code),
                                ('-s', 'json', '-ro', str(target)), root)
            try:
                window = dialogs.wait(app.process, title, message=target.name)
                assert target.name in '\n'.join(dialogs.texts(window))
                dialogs.answer(app.process, choice, window=window)
                if choice == 'cancel':
                    assert app.process.wait(timeout=10) == 0
                    assert not (app.folder / 'state').exists()
                else:
                    finish_start(app)
                    assert app.state['length'] == '0' and app.state['dirty'] == '0', summary(app.state)
                    assert 'JSON' in app.state['document_status'], summary(app.state)
                    assert app.state['read_only'] == ('1' if choice == 'yes' else '0'), summary(app.state)
                    assert app.state['document_path'] == (str(target) if choice == 'yes' else ''), summary(app.state)
                    app.close()
                if choice == 'yes':
                    assert target.read_bytes() == b''
                else:
                    assert not target.exists()
            finally:
                app.stop()
    print('PASS: real missing-file Yes/No/Cancel, explicit creation only, blank fallback and cancelled process exit', flush=True)


@contextmanager
def unreadable(path):
    if sys.platform == 'win32':
        with obstruct_publication(path, 'exclusive-reader'):
            yield
    else:
        mode = path.stat().st_mode
        path.chmod(0)
        try:
            yield
        finally:
            path.chmod(mode)


def failures(executable, root):
    dialogs = Dialogs(root / 'dialogs')
    target = root / 'unreadable 中文😀.txt'
    original = b'caf\xe9\r\n'
    target.write_bytes(original)
    for code, preference, title, not_migrated, cannot_read in (
            ('en', 1033, 'Could not open file', 'not migrated', 'Could not open the file for reading.'),
            ('zh_CN', 2052, '无法打开文件', '尚未迁移', '无法打开文件以读取。'),
            ('zh_TW', 1028, '無法開啟檔案', '尚未移植', '無法開啟檔案進行讀取。')):
        settings(executable.parent / 'Notepad4.ini', preference)
        app = pending_start(executable, root / (code + '-unsupported'),
                            environment(executable, root / 'children', code), ('-e', 'utf-7', str(target)), root)
        try:
            window = dialogs.wait(app.process, title, message='65000')
            text = '\n'.join(dialogs.texts(window))
            assert '65000' in text and not_migrated in text, text
            dialogs.answer(app.process, 'ok', window=window)
            assert app.process.wait(timeout=10) != 0
            assert not (app.folder / 'state').exists()
        finally:
            app.stop()
        with unreadable(target):
            app = pending_start(executable, root / (code + '-unreadable'),
                                environment(executable, root / 'children', code), ('-e', 'cp1252', str(target)), root)
            try:
                window = dialogs.wait(app.process, title, message=cannot_read)
                assert cannot_read in '\n'.join(dialogs.texts(window))
                dialogs.answer(app.process, 'ok', window=window)
                finish_start(app)
                assert app.state['length'] == '0' and app.state['document_path'] == '', summary(app.state)
                assert app.state['dirty'] == '0' and app.state['caret_position'] == '0', summary(app.state)
                app.close()
            finally:
                app.stop()
        assert target.read_bytes() == original
    print('PASS: localized unavailable-codec and read failures preserve bytes, report failure and retain no partial load', flush=True)


def missing_parameters(executable, root):
    dialogs = Dialogs(root / 'dialogs')
    prefixes = ('/', '-', '--') if sys.platform == 'win32' else ('-', '--')
    for code, preference, title, message in (
            ('en', 1033, 'Command Line Error', 'Option "%s" requires a parameter.'),
            ('zh_CN', 2052, '命令行错误', '选项“%s”需要参数。'),
            ('zh_TW', 1028, '命令列錯誤', '選項「%s」需要參數。')):
        settings(executable.parent / 'Notepad4.ini', preference)
        for prefix in prefixes:
            for option in ('e', 's', 'g', 'G'):
                argument = prefix + option
                folder = root / f'{code}-{len(prefix)}-{ord(prefix[0])}-{option}'
                # Case-sensitive option names need distinct Windows log paths.
                folder = folder.with_name(folder.name + str(ord(option)))
                before = set(root.iterdir())
                app = pending_start(executable, folder,
                                    environment(executable, root / 'children', code), (argument,), root)
                try:
                    window = dialogs.wait(app.process, title, message=message % argument)
                    assert message % argument in '\n'.join(dialogs.texts(window))
                    dialogs.answer(app.process, 'ok', window=window)
                    assert app.process.wait(timeout=10) != 0
                    assert not (folder / 'state').exists()
                    assert not any(path.is_file() for path in set(root.iterdir()) - before)
                finally:
                    app.stop()
    # Preserve the adjacent original nonnumeric g/G pathname fallback using
    # owned relative filenames. No drive-root pathname is created or probed.
    settings(executable.parent / 'Notepad4.ini', 1033)
    for option in ('g', 'G'):
        target = root / f'-{option} nonnumeric-{ord(option)}.txt'
        data = b'original pathname fallback\r\n'
        target.write_bytes(data)
        app = start(executable, root / f'fallback-{ord(option)}', environment(executable, root / 'children'),
                    arguments=('-' + option, target.name[3:]), cwd=root)
        try:
            assert Path(app.state['document_path']) == target, summary(app.state)
            assert app.state['length'] == str(len(data)) and app.state['dirty'] == '0', summary(app.state)
            assert target.read_bytes() == data
            app.close()
        finally:
            app.stop()
    print('PASS: localized missing operands fail without pathname fallback; nonnumeric g/G retains original fallback', flush=True)


def native_ansi_page():
    if sys.platform != 'win32':
        return 65001  # The private native runtime uses C.UTF-8.
    native = ctypes.WinDLL('kernel32', use_last_error=True)
    return native.GetACP()


def identities(executable, root):
    settings(executable.parent / 'Notepad4.ini', 1033, 'LoadASCIIasUTF8=0\n')
    for index, (alias, source, current, encoding, expected) in enumerate((
            ('utf-8', b'caf\xc3\xa9\r\n', '1252', 'CP1252', b'caf\xe9\r\n'),
            ('cp1252', b'caf\xe9\r\n', '3', 'UTF-8', b'caf\xc3\xa9\r\n'))):
        target = root / f'source-current-{index}.txt'
        target.write_bytes(source)
        app = start(executable, root / f'identities-{index}', environment(executable, root / 'children'),
                    arguments=('-e', alias, '--startup-encoding', current, str(target)), cwd=root)
        try:
            assert app.state['encoding'] == encoding and app.state['length'] == '7', summary(app.state)
            assert app.state['title'].startswith('*'), 'The source and current encoding remain different format identities'
            assert target.read_bytes() == source
            destination = root / f'current-{index}.txt'
            app.send('save', str(destination))
            assert destination.read_bytes() == expected and target.read_bytes() == source
            app.close()
        finally:
            app.stop()
    for identity in ('ANSI', 'OEM'):
        target = root / (identity + '.txt')
        target.write_bytes(b'native ASCII\r\n')
        app = start(executable, root / identity, environment(executable, root / 'children'),
                    arguments=('-e', identity, str(target)), cwd=root)
        try:
            status = [text for key, text in app.state.items() if key.startswith('status.') and key.endswith('.text')]
            if identity == 'ANSI':
                # Original EditLoadFile promotes its ASCII/UTF7 analysis result
                # from ANSI to the explicit system codec before loading.
                page = native_ansi_page()
                expected = {65001: 'UTF-8', 936: 'CP936', 950: 'CP950', 1252: 'CP1252', 932: 'CP932', 949: 'CP949'}[page]
                assert app.state['encoding'] == expected, summary(app.state)
                assert not any(text.startswith('ANSI (') for text in status), status
            else:
                assert any(text.startswith(identity + ' (') for text in status), (identity, status)
            assert app.state['dirty'] == '0'
            app.send('save')
            assert target.read_bytes() == b'native ASCII\r\n'
            app.close()
        finally:
            app.stop()
    # Independent original native-details capture: CP936 non-ASCII input keeps
    # ANSI identity, including its valid GBK and invalid lead-byte cases.
    # The private C.UTF-8 runtime must also retain invalid native bytes on save.
    page = native_ansi_page()
    native_cases = (b'\xd6\xd0\xce\xc4\r\n', b'\x81') if page == 936 else ((b'\x81',) if sys.platform != 'win32' else ())
    for index, data in enumerate(native_cases):
        target = root / f'native-bytes-{index}.txt'
        target.write_bytes(data)
        app = start(executable, root / f'native-{index}', environment(executable, root / 'children'),
                    arguments=('-e', 'ANSI', str(target)), cwd=root)
        try:
            status = [text for key, text in app.state.items() if key.startswith('status.') and key.endswith('.text')]
            assert any(text.startswith('ANSI (') for text in status), status
            assert app.state['dirty'] == '0'
            app.send('save')
            assert target.read_bytes() == data
            app.close()
        finally:
            app.stop()
    print('PASS: independent source/current encodings, ANSI ASCII promotion and native ANSI/OEM byte identities', flush=True)


def installed(executable, root):
    parent = Path('/opt/wxnotepad-file-workflows-test')
    assert parent.is_dir() and os.access(parent, os.W_OK), 'Use the documented owned installed-mode test prefix'
    with tempfile.TemporaryDirectory(prefix='startup-', dir=parent) as name:
        prefix = Path(name)
        application = prefix / 'bin' / executable.name
        shutil.copytree(executable.parent, application.parent, ignore=shutil.ignore_patterns('Notepad4.ini'))
        (prefix / 'share').mkdir()
        (application.parent / 'locale').rename(prefix / 'share/locale')
        target = root / 'installed source 中文😀.txt'
        original = b'first\r\ncaf\xe9\r\n'
        target.write_bytes(original)
        for code, preference, file_menu in (('en', 1033, '&File'), ('zh_CN', 2052, '文件(&F)'),
                                             ('zh_TW', 1028, '檔案(&F)')):
            config_home = root / (code + '-config')
            config = config_home / 'Notepad4/Notepad4.ini'
            config.parent.mkdir(parents=True)
            config_bytes = settings(config, preference)
            env = environment(application, root / 'children', code)
            env['XDG_CONFIG_HOME'] = str(config_home)
            app = start(application, root / (code + '-installed'), env,
                        arguments=('-e', 'cp1252', '-s', 'json', '-g', '2,5', '-ro', str(target)), cwd=root)
            try:
                assert app.state['menu_top.0.label'] == file_menu, summary(app.state)
                assert app.state['encoding'] == 'CP1252' and app.state['length'] == '14', summary(app.state)
                assert app.state['caret_position'] == '12' and app.state['read_only'] == '1', summary(app.state)
                assert 'JSON' in app.state['document_status'], summary(app.state)
                assert Path(app.state['document_path']) == target
                app.close()
            finally:
                app.stop()
            assert config.read_bytes() == config_bytes and target.read_bytes() == original
            assert not (application.parent / 'Notepad4.ini').exists()
    print('PASS: installed startup options, XDG settings and delivered locale resources in all three languages', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--cmake-dir', type=Path, required=True)
    parser.add_argument('--scenario', choices=('paths', 'encodings', 'positions', 'display', 'readonly', 'help', 'missing', 'failures', 'missing-parameters', 'identities', 'installed'), default='paths')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='wxnotepad-launch-') as name:
        root = Path(name).resolve()
        executable = bundle_application(args.executable.resolve(), args.cmake_dir.resolve(), root / 'bundle')
        if args.scenario == 'display':
            positions(executable, root, display=True)
        else:
            {'paths': paths, 'encodings': encodings, 'positions': positions, 'readonly': readonly,
             'help': help_message, 'missing': missing_files, 'failures': failures,
             'missing-parameters': missing_parameters, 'identities': identities, 'installed': installed}[args.scenario](executable, root)


if __name__ == '__main__':
    main()
