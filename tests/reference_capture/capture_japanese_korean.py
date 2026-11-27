"""Capture CP932/CP949 Open and Save bytes from an owned original application."""
import ctypes as c
from ctypes import wintypes as w
from hashlib import sha256
import json
import os
from pathlib import Path
import shutil
import sys

from original_host import Original, user


def main():
    source, output = (Path(item).resolve() for item in sys.argv[1:3])
    portable = output / 'original-portable'
    portable.mkdir(parents=True)
    executable = portable / 'Notepad4.exe'
    shutil.copy2(source, executable)
    (portable / 'Notepad4.ini').write_text(
        '[Settings]\nSaveSettings=0\nFileWatchingMode=0\nAutoSaveOption=0\nWarnLineEndings=0\n'
        'FixLineEndings=0\nFixTrailingBlanks=0\nLoadASCIIasUTF8=0\nLoadANSIasUTF8=0\n'
        '[Suppressed Messages]\nMsgConv3=1\n', encoding='utf-16')
    user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
    user.CreateDesktopW.restype = w.HANDLE
    user.CloseDesktop.argtypes = [w.HANDLE]
    desktop_name = 'japanese-korean-reference-' + str(os.getpid())
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x1ff, None)
    assert desktop, 'owned original desktop was created'
    rows = []
    # NUL separators keep each independently supplied one/two-byte input distinct.
    all_bytes = b''.join(bytes([value]) + b'\0' for value in range(256))
    all_bytes += b''.join(value.to_bytes(2, 'big') + b'\0' for value in range(65536))
    try:
        for page in (932, 949):
            path = output / f'cp{page}.input.bin'
            path.write_bytes(all_bytes)
            original = Original(path, executable, desktop, desktop_name, f'cp{page}')
            try:
                before = original.state()
                assert before['codepage'] == 65001, 'explicit non-ANSI source uses original Unicode editor buffer'
                text = original.text()
                (output / f'cp{page}.utf8').write_bytes(text)
                original.command(40004)  # IDM_FILE_SAVE
                saved = path.read_bytes()
                (output / f'cp{page}.saved.bin').write_bytes(saved)
                path.write_bytes(all_bytes)
                rows.append({'page': page, 'before': before, 'after': original.state(),
                             'input_sha256': sha256(all_bytes).hexdigest(), 'utf8_sha256': sha256(text).hexdigest(),
                             'saved_sha256': sha256(saved).hexdigest()})
                sample = ('ｶ髙' if page == 932 else '가갂') + '¥😀\r\n'
                original.reset(sample.encode())
                original.command(40004)
                rows[-1]['loss_text'] = sample
                rows[-1]['loss_saved'] = path.read_bytes().hex()
                path.write_bytes(all_bytes)
                print(f'Captured original CP{page}: input={len(all_bytes)} Unicode={len(text)} Save={len(saved)}', flush=True)
            finally:
                original.close()
    finally:
        user.CloseDesktop(desktop)
        (output / 'original.json').write_text(json.dumps({
            'revision': '89b8b36e0ee2c23f73be922315f17261ccfbbe1a',
            'executable_sha256': sha256(executable.read_bytes()).hexdigest(), 'cases': rows},
            ensure_ascii=True, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
