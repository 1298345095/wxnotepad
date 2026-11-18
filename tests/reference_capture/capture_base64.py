"""Freeze original Base64 command bytes/history on an owned private desktop.

Usage: python capture_base64.py PINNED_NOTEPAD4_EXE EXTERNAL_EVIDENCE_DIRECTORY
No clipboard or foreground input is used.
"""
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
import threading
import time
from pathlib import Path
import shutil
import sys
from original_host import Original, user, CALLBACK

exe, output = map(Path, sys.argv[1:3])
output.mkdir(parents=True, exist_ok=True)
host = output / 'original-portable'
host.mkdir(exist_ok=True)
shutil.copy2(exe, host / 'Notepad4.exe')
(host / 'Notepad4.ini').write_text('[Notepad4]\n[Settings]\nSaveSettings=0\nLoadASCIIasUTF8=0\nLoadANSIasUTF8=0\n', encoding='utf-8')
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
name = 'wxnotepad-base64-reference-' + str(os.getpid())
desktop = user.CreateDesktopW(name, None, None, 0, 0x1ff, None)
assert desktop
rows = []
states = []


def checkpoint():
    (output / 'partial.json').write_text(json.dumps(dict(
        revision='89b8b36e0ee2c23f73be922315f17261ccfbbe1a', cases=rows, states=states), indent=2) + '\n')


def invoke(original, command, rectangle=False):
    if not rectangle:
        original.command(command)
        return 0
    worker = threading.Thread(target=original.command, args=(command,))
    worker.start()
    dialogs = []
    deadline = time.monotonic() + 5
    while worker.is_alive() and time.monotonic() < deadline:
        def window(hwnd, _):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            name = c.create_unicode_buffer(100)
            user.GetClassNameW(hwnd, name, 100)
            if pid.value == original.process.pid and name.value == '#32770' and hwnd not in dialogs:
                dialogs.append(hwnd)
                user.PostMessageW(hwnd, 0x111, 1, 0)  # Original MsgBoxWarn uses MessageBoxEx (IDOK).
            return True
        user.EnumDesktopWindows(desktop, CALLBACK(window), 0)
        time.sleep(.01)
    worker.join(timeout=1)
    assert not worker.is_alive(), 'owned rectangular-selection notification completed'
    return len(dialogs)
try:
    for suffix in [None, '.PNG', '', '.jpeg', '.tar.SVG', '.PNG space', '.svg.extra']:
        path = host / ('image' + suffix) if suffix is not None else None
        if path:
            path.write_bytes(b'')
        original = Original(path, host / 'Notepad4.exe', desktop, name)
        try:
            samples = [b'', b'f', b'fo', b'foo', b'foob', b'fooba', b'foobar', b'\x00\xff\xfe',
                       '中文😀'.encode(), b'AA==', b'AB', b'B', b'Z', b'Zg', b'Zm8', b'Zm9v',
                       b'Zm9vYg', b'Zm9vYmE', b'Zm9vYmFy', b'+/8=', b'-_8=', b'+_8=', b'Z g==',
                       b' Zg==', b'\tZg==', b'Zm9v\r\nYmFy', b'Zm9v!YmFy', b'Zm9v=YmFy',
                       b'=', b'!', b'\xff', b'A\x00A', b'AAECAwQFBgcICQoLDA0ODw==',
                       b'AAECAwQFBgcICQoLDA0ODxA=',
                       b'AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=']
            if suffix is not None:
                samples = [b'foo']
            for command in range(5):
                if suffix is not None and command != 2:
                    continue
                for sample in samples:
                    for eol in ([0, 1, 2] if command == 4 else [0]):
                        for reverse in [False, True]:
                            before = b'<' + sample + b'>'
                            anchor, caret = (len(before) - 1, 1) if reverse else (1, len(before) - 1)
                            original.reset(before, anchor, caret)
                            assert original.text() == before and original.send(2009) == anchor and original.send(2008) == caret
                            original.send(2031, eol)
                            original.command(40494 + command)
                            after = original.state()
                            original.send(2176)
                            undo = original.state()
                            original.send(2011)
                            redo = original.state()
                            rows.append(dict(command=command, suffix=suffix, eol=eol, input=before.hex(),
                                             anchor=anchor, caret=caret, after=after, undo=undo, redo=redo))
        finally:
            original.close()
        checkpoint()
    for encoding, content in [('utf-8', '中文é'), ('utf-16le', '中文é'), ('utf-16be', '中文é'),
                              ('gbk', '中文é'), ('gb18030', '中文é'), ('big5', '中文'),
                              ('windows-1252', 'é'), ('cp437', 'é'), ('ansi', '中文')]:
        path = host / ('encoding-' + encoding + '.txt')
        path.write_bytes(b'')
        original = Original(path, host / 'Notepad4.exe', desktop, name, encoding)
        try:
            native = encoding == 'ansi'
            if native:
                original.command(40100)  # Actual File/Encoding/ANSI entry on an empty document.
            codepage = original.send(2137)
            assert codepage == (c.windll.kernel32.GetACP() if native else 65001), (encoding, codepage)
            for command in range(5):
                decoded = b'5Lit5paHw6k=' if content == '中文é' else b'5Lit5paH' if content == '中文' else b'w6k='
                before = content.encode('mbcs' if native else 'utf-8') if command < 3 else b'1tDOxA==' if native else decoded
                original.reset(b'', 0, 0)
                original.send(2001, len(before), original.put(before))
                original.send(2160, 0, len(before))
                original.command(40004)
                before_saved = path.read_bytes().hex()
                original.send(2175)
                original.send(2014)
                assert original.text() == before
                original.command(40494 + command)
                after = original.state()
                original.send(2176)
                undo = original.state()
                original.send(2011)
                redo = original.state()
                original.command(40004)
                after_saved = path.read_bytes().hex()
                states.append(dict(command=command, encoding=encoding, mode='encoding', eol=0, input=before.hex(),
                                   anchor=0, caret=len(before), readonly=0, notices=0, after=after, undo=undo, redo=redo,
                                   before_saved=before_saved, after_saved=after_saved))
        finally:
            original.close()
        checkpoint()
    original = Original(None, host / 'Notepad4.exe', desktop, name)
    try:
        for command in range(5):
            for eol in [0, 1, 2]:
                for mode in ['empty', 'multiple', 'rectangle', 'virtual', 'readonly', 'pending', 'readonly_pending']:
                    before = b'YQ==\nYg==' if command >= 3 else b'a\nb'
                    anchor, caret = (0, 0) if mode == 'empty' else (len(before) - 1, 0)
                    original.reset(before, anchor, caret)
                    original.send(2031, eol)
                    if mode == 'multiple':
                        original.send(2563, 1)
                        original.send(2573, len(before), len(before) - 1)
                    if mode in ['rectangle', 'virtual']:
                        original.send(2422, 1)
                        original.send(2590, 0)
                        original.send(2588, len(before))
                        if mode == 'virtual':
                            original.send(2591, 5)
                            original.send(2589, 7)
                    if mode in ['pending', 'readonly_pending']:
                        original.send(2282, 1, original.put(b'!'))
                        original.send(2176)
                        assert original.send(2016)
                        original.send(2160, anchor, caret)
                    readonly = int(mode in ['readonly', 'readonly_pending'])
                    original.send(2171, readonly)
                    assert original.text() == before
                    notices = invoke(original, 40494 + command, mode in ['rectangle', 'virtual'])
                    after = original.state()
                    original.send(2171, 0)
                    original.send(2176)
                    undo = original.state()
                    original.send(2011)
                    redo = original.state()
                    states.append(dict(encoding='utf-8', mode=mode, eol=eol, input=before.hex(),
                                       anchor=anchor, caret=caret, readonly=readonly, notices=notices,
                                       after=after, undo=undo, redo=redo))
                    states[-1]['command'] = command
                    checkpoint()
    finally:
        original.close()
finally:
    user.CloseDesktop(desktop)
record = dict(revision='89b8b36e0ee2c23f73be922315f17261ccfbbe1a',
              executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), cases=rows, states=states)
(output / 'original.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
target = Path(__file__).resolve().parents[1] / 'base64_reference.tsv'
fields = ['hex', 'anchor', 'caret', 'modified', 'undo', 'redo']
with target.open('w', encoding='utf-8', newline='') as stream:
    stream.write('# Notepad4 89b8b36, owned private Windows desktop; capture_base64.py\n')
    for row in rows:
        values = [row['command'], '-' if row['suffix'] is None else row['suffix'] or '.',
                  row['eol'], row['input'], row['anchor'], row['caret']]
        for phase in ['after', 'undo', 'redo']:
            values.extend(row[phase][field] for field in fields)
        stream.write('\t'.join(map(str, values)) + '\n')
target = Path(__file__).resolve().parents[1] / 'base64_state_reference.tsv'
with target.open('w', encoding='utf-8', newline='') as stream:
    stream.write('# Notepad4 89b8b36; application encoding choices and selection states\n')
    for row in states:
        native = row['encoding'] == 'ansi'
        def view(data):
            return data.decode('mbcs').encode('utf-8') if native else data
        before = bytes.fromhex(row['input'])
        anchor = len(view(before[:row['anchor']]))
        caret = len(view(before[:row['caret']]))
        values = [row['command'], row['encoding'], row['mode'], row['eol'], view(before).hex(),
                  anchor, caret, row['readonly'], row['notices']]
        for phase in ['after', 'undo', 'redo']:
            state = dict(row[phase])
            raw = bytes.fromhex(state['hex'])
            state['anchor'] = len(view(raw[:state['anchor']]))
            state['caret'] = len(view(raw[:state['caret']]))
            state['hex'] = view(raw).hex()
            values.extend(state[field] for field in fields)
        values.extend([row.get('before_saved', '-'), row.get('after_saved', '-')])
        stream.write('\t'.join(map(str, values)) + '\n')
print(f'Captured {len(rows)} original Base64 cases')
print(f'Captured {len(states)} original Base64 encoding/selection states')
