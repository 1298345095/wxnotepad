"""Observe pinned original selection commands launching an owned executable on a private desktop."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import shutil
import time

from original_host import Original, user, kernel

PINNED_ORIGINAL_SHA256 = 'c4ae363348fe833b39c9d9b77f1f0b85565f140f7c5e52c51a0b91cf896227d9'
KEYS = ('GoogleSearchUrl', 'BingSearchUrl', 'WikiSearchUrl', 'CustomAction1', 'CustomAction2')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--recorder', type=Path, required=True)
    parser.add_argument('--recorder-script', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--only-literals', action='store_true')
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    portable = output / 'original-portable'
    portable.mkdir()
    executable = portable / 'Notepad4.exe'
    shutil.copy2(args.original, executable)
    assert hashlib.sha256(executable.read_bytes()).hexdigest() == PINNED_ORIGINAL_SHA256
    recorder = args.recorder.resolve()
    script = ''
    if args.recorder_script:
        script = f' "{args.recorder_script.resolve()}"'
    else:
        program_folder = output / 'program folder 中文'
        program_folder.mkdir()
        recorder = program_folder / 'record program 中文.exe'
        shutil.copy2(args.recorder, recorder)
        for dependency in args.recorder.parent.glob('*.dll'):
            shutil.copy2(dependency, program_folder / dependency.name)
    record = output / 'received.txt'
    os.environ['WXNOTEPAD_RUN_RECORD'] = str(record)
    os.environ['WX265_VALUE'] = 'expanded 中文 value'
    template = f'"{recorder}"{script} "%s" "%%WX265_VALUE%%" literal=%%%%'
    ini = ('[Settings]\nSaveSettings=0\nWarnLineEndings=0\nSelectOption=15\n[Settings2]\nUILanguage=1033\n'
           + ''.join(f'{key}={template}\n' for key in KEYS))
    (portable / 'Notepad4.ini').write_text(ini, encoding='utf-16', newline='\r\n')
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    user.GetMenu.argtypes = [w.HWND]
    user.GetMenu.restype = w.HMENU
    user.GetMenuState.argtypes = [w.HMENU, w.UINT, w.UINT]
    name = 'selection-actions-reference-' + str(os.getpid())
    desktop = user.CreateDesktopW(name, None, None, 0, 0x1ff, None)
    assert desktop, c.WinError(c.get_last_error())
    observations = []

    def capture(label, app, selected, action=3, shape='stream', readonly=False, launches=True):
        record.unlink(missing_ok=True)
        app.reset(selected, 0, len(selected))
        if shape == 'rectangle':
            app.send(2422, 1)
            app.send(2590, 0)
            app.send(2588, 7)
        elif shape == 'multiple':
            app.send(2563, 1)
            app.send(2572, 2, 0)
            app.send(2573, 8, 6)
        elif shape == 'caret':
            app.send(2160, 1, 1)
        if readonly:
            app.command(40024)  # Original Read Only Mode; updates both application and control state.
        user.SendMessageW(app.hwnd, 0x116, user.GetMenu(app.hwnd), 0)
        enabled = [not bool(user.GetMenuState(user.GetMenu(app.hwnd), 40562 + index, 0) & 3) for index in range(5)]
        before = app.state()
        app.command(40562 + action)
        deadline = time.monotonic() + (5 if launches else .25)
        while not record.exists() and time.monotonic() < deadline:
            time.sleep(.01)
        after = app.state()
        assert before == after, (label, 'original selection action preserves document state', before, after)
        assert record.exists() == launches, (label, 'original expected launch state', launches)
        observed = record.read_text(encoding='utf-8').splitlines() if launches else None
        observations.append(dict(name=label, input_hex=selected.hex(), action=action, shape=shape,
                                 readonly=readonly, enabled=enabled, before=before, after=after, cwd_argv=observed))
        print(label, json.dumps(observed, ensure_ascii=False), flush=True)
        if readonly:
            app.command(40024)

    try:
        original = Original(None, executable, desktop, name)
        try:
            if args.only_literals:
                capture('literal-dollar', original, b"$WX265_VALUE O'Reilly")
                capture('selected-environment', original, b'%WX265_VALUE%')
                kernel.WritePrivateProfileStringW.argtypes = [w.LPCWSTR, w.LPCWSTR, w.LPCWSTR, w.LPCWSTR]
                assert kernel.WritePrivateProfileStringW('Settings2', 'CustomAction1', template.replace('"%s"', '%s'),
                                                         str(portable / 'Notepad4.ini'))
                capture('unquoted-apostrophe', original, b"O'Reilly")
            else:
                for action, key in enumerate(KEYS):
                    capture(key, original, ' one two 中 '.encode(), action=action)
                capture('reserved', original, ' a:b/c?d=e&f#tail '.encode())
                capture('multiline', original, ' first\r\nsecond line 中 '.encode())
                capture('rectangle', original, b'abXX\r\ncdYY', shape='rectangle')
                capture('multiple', original, b'abXX\r\ncdYY', shape='multiple')
                capture('readonly', original, b'one two', readonly=True)
                capture('empty', original, b'', launches=False)
                capture('caret', original, b'word', shape='caret', launches=False)
                capture('trim-empty', original, b' \a\b\f\n\r\t\v', launches=False)
        finally:
            original.close()
        if not args.only_literals:
            documents = output / 'documents'
            documents.mkdir()
            source = documents / 'named.txt'
            source.write_bytes(b'independent disk bytes\r\n')
            original = Original(source, executable, desktop, name)
            try:
                deadline = time.monotonic() + 5
                while original.text() != source.read_bytes() and time.monotonic() < deadline:
                    time.sleep(.01)
                assert original.text() == source.read_bytes(), 'original named document finished loading before capture'
                capture('named-directory', original, b'one two')
            finally:
                original.close()
    finally:
        user.CloseDesktop(desktop)
        (output / 'observations.json').write_text(json.dumps({
            'revision': '89b8b36e0ee2c23f73be922315f17261ccfbbe1a',
            'original_sha256': PINNED_ORIGINAL_SHA256,
            'recorder_sha256': hashlib.sha256(recorder.read_bytes()).hexdigest(),
            'template': template, 'observations': observations}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
