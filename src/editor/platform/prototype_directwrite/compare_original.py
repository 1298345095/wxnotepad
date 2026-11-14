"""Compare owned original/wx HWNDs; default mode validates geometry without timing.

The measured phase is the same 12 warm long-line resizes, driven externally for
both applications. --measure requires a fresh user-ready performance window.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / 'tools'))
from resources import memory

MiB = 1024 * 1024
REMOTE_BYTES = 4 * MiB
WM_CLOSE, WM_NULL, WM_APP = 0x10, 0, 0x8000
CHECK_GEOMETRY = WM_APP + 0x35
SWP_NOMOVE, SWP_NOZORDER, SWP_NOACTIVATE = 0x2, 0x4, 0x10
SWP_NOMOVE_NOZORDER_NOACTIVATE = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
RDW_INVALIDATE, RDW_UPDATENOW = 0x1, 0x100
RDW_INVALIDATE_UPDATENOW = RDW_INVALIDATE | RDW_UPDATENOW
PROCESS_QUERY_INFORMATION, PROCESS_VM_READ, PROCESS_VM_WRITE, PROCESS_VM_OPERATION = 0x400, 0x10, 0x20, 0x8
PROCESS_ACCESS = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION
MEM_COMMIT, MEM_RESERVE, MEM_RELEASE, PAGE_READWRITE = 0x1000, 0x2000, 0x8000, 4
SMTO_BLOCK, SMTO_ABORTIFHUNG, SMTO_ERRORONEXIT = 0x1, 0x2, 0x20
TECHNOLOGY_DIRECTWRITE, WRAP_NONE, WRAP_WORD = 1, 0, 1
STYLE_DEFAULT, STYLE_LINENUMBER = 32, 33
CALL_TIMEOUT_MS = 10000
INITIAL_VIEWPORT = (992, 598)
RESIZE_VIEWPORTS = [(872 if index % 2 == 0 else 1172, 598) for index in range(12)]

user = c.WinDLL('user32', use_last_error=True)
kernel = c.WinDLL('kernel32', use_last_error=True)
callback_type = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)


def bind(library, name, arguments, result=w.BOOL):
    function = getattr(library, name)
    function.argtypes, function.restype = arguments, result
    return function


bind(user, 'EnumWindows', [callback_type, w.LPARAM])
bind(user, 'EnumChildWindows', [w.HWND, callback_type, w.LPARAM])
bind(user, 'GetWindowThreadProcessId', [w.HWND, c.POINTER(w.DWORD)], w.DWORD)
bind(user, 'GetClassNameW', [w.HWND, w.LPWSTR, c.c_int], c.c_int)
bind(user, 'GetWindowTextW', [w.HWND, w.LPWSTR, c.c_int], c.c_int)
bind(user, 'GetPropW', [w.HWND, w.LPCWSTR], w.HANDLE)
bind(user, 'GetClientRect', [w.HWND, c.POINTER(w.RECT)])
bind(user, 'GetWindowRect', [w.HWND, c.POINTER(w.RECT)])
bind(user, 'GetDpiForWindow', [w.HWND], w.UINT)
bind(user, 'SetProcessDpiAwarenessContext', [w.HANDLE])
bind(user, 'SetWindowPos', [w.HWND, w.HWND, c.c_int, c.c_int, c.c_int, c.c_int, w.UINT])
bind(user, 'RedrawWindow', [w.HWND, c.c_void_p, w.HANDLE, w.UINT])
bind(user, 'PostMessageW', [w.HWND, w.UINT, w.WPARAM, w.LPARAM])
bind(user, 'SendMessageTimeoutW', [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT,
                                 w.UINT, c.POINTER(c.c_size_t)], c.c_size_t)
bind(kernel, 'OpenProcess', [w.DWORD, w.BOOL, w.DWORD], w.HANDLE)
bind(kernel, 'CloseHandle', [w.HANDLE])
bind(kernel, 'VirtualAllocEx', [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD], c.c_void_p)
bind(kernel, 'VirtualFreeEx', [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD])
bind(kernel, 'WriteProcessMemory', [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p])
bind(kernel, 'ReadProcessMemory', [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p])
bind(kernel, 'GetProcessTimes', [w.HANDLE, *([c.POINTER(w.FILETIME)] * 4)])


def require(value, message):
    if not value:
        raise RuntimeError(message)


def dimensions(hwnd, client=True):
    rect = w.RECT()
    require((user.GetClientRect if client else user.GetWindowRect)(hwnd, c.byref(rect)), 'read native rectangle')
    return rect.right - rect.left, rect.bottom - rect.top


class OwnedApp:
    def __init__(self, executable, fixture, folder, original, messages):
        self.original, self.messages = original, messages
        self.handle = self.memory = None
        self.editor = self.frame = None
        self.stream = (folder / ('original-native.log' if original else 'wx-native.log')).open('w', encoding='utf-8')
        args = [str(executable), '/n', '/ns', '/e', 'utf-8', str(fixture)] if original else [str(executable), str(fixture)]
        self.process = subprocess.Popen(args, stdout=self.stream, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 15
            while not self.editor and time.monotonic() < deadline:
                require(self.process.poll() is None, 'owned app exited during startup')

                @callback_type
                def top(hwnd, unused):
                    pid = w.DWORD()
                    user.GetWindowThreadProcessId(hwnd, c.byref(pid))
                    if pid.value == self.process.pid:
                        @callback_type
                        def child(control, unused):
                            name = c.create_unicode_buffer(128)
                            user.GetClassNameW(control, name, len(name))
                            if (original and name.value == 'Scintilla') or (not original and user.GetPropW(control, 'wxnotepad.comparison')):
                                self.frame, self.editor = hwnd, control
                            return True
                        user.EnumChildWindows(hwnd, child, 0)
                    return True
                user.EnumWindows(top, 0)
                if not self.editor:
                    time.sleep(0.02)
            require(self.editor, 'owned editor appeared')
            self.handle = kernel.OpenProcess(PROCESS_ACCESS, False, self.process.pid)
            require(self.handle, 'open owned process')
            self.memory = kernel.VirtualAllocEx(self.handle, None, REMOTE_BYTES,
                                               MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
            require(self.memory, 'allocate owned scratch buffer')
        except BaseException:
            self.close()
            raise

    def raw(self, message, first=0, second=0):
        result = c.c_size_t()
        require(user.SendMessageTimeoutW(self.editor, message, first, second,
                                        SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
                                        CALL_TIMEOUT_MS, c.byref(result)), f'native message {message} completed')
        return c.c_ssize_t(result.value).value

    def send(self, name, first=0, second=0):
        return self.raw(self.messages[name], first, second)

    def put(self, data, offset=0):
        require(offset + len(data) <= REMOTE_BYTES, 'scratch buffer bounds')
        require(kernel.WriteProcessMemory(self.handle, self.memory + offset, data, len(data), None), 'write owned scratch')
        return self.memory + offset

    def read(self, size, offset=0):
        require(offset + size <= REMOTE_BYTES, 'scratch read bounds')
        data = c.create_string_buffer(size)
        require(kernel.ReadProcessMemory(self.handle, self.memory + offset, data, size, None), 'read owned scratch')
        return data.raw

    def string(self, name, first=0):
        self.put(bytes(512))
        size = self.send(name, first, self.memory)
        require(0 <= size < 512, 'string result fits scratch')
        return self.read(size + 1).split(b'\0')[0].decode('utf-8')

    def styled(self):
        size = self.send('GetLength')
        self.put(struct.pack('qqQ', 0, size, self.memory + 64))
        require(self.send('GetStyledTextFull', 0, self.memory) == size, 'read every styled byte')
        # Pinned Notepad4 returns styles+NUL then text+NUL, rather than upstream's interleaving.
        raw = self.read(size * 2 + 2, 64)
        result = bytearray(size * 2)
        result[::2], result[1::2] = raw[size + 1:-1], raw[:size]
        return bytes(result)

    def cpu(self):
        created, exited, system, application = (w.FILETIME() for unused in range(4))
        require(kernel.GetProcessTimes(self.handle, c.byref(created), c.byref(exited), c.byref(system),
                                       c.byref(application)), 'read process CPU')
        return sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in (system, application)) * 100

    def paint(self):
        for unused in range(2):
            require(user.RedrawWindow(self.editor, None, None, RDW_INVALIDATE_UPDATENOW), 'synchronous native paint')
            self.raw(WM_NULL)

    def resize(self, target, setup=False):
        # Initial wx nonclient/statusbar allocation can settle during the first paint.
        # Timed resizes always perform exactly one native frame resize.
        for unused in range(3 if setup else 1):
            current = dimensions(self.editor)
            outer = dimensions(self.frame, False)
            require(user.SetWindowPos(self.frame, None, 0, 0, outer[0] + target[0] - current[0],
                                      outer[1] + target[1] - current[1], SWP_NOMOVE_NOZORDER_NOACTIVATE), 'resize owned frame')
            self.paint()
            if dimensions(self.editor) == target:
                return
        raise RuntimeError(f'exact native editor client viewport: expected={target}, actual={dimensions(self.editor)}, before={current}')

    def geometry(self):
        if not self.original:
            require(self.raw(CHECK_GEOMETRY) == 1, 'wx internal wrap width agrees with viewport')
        return dict(client=dimensions(self.editor), dpi=user.GetDpiForWindow(self.editor),
                    margins=[self.send('GetMarginWidthN', index) for index in range(self.send('GetMargins'))],
                    left=self.send('GetMarginLeft'), right=self.send('GetMarginRight'),
                    line_height=self.send('TextHeight', 0), wrap=self.send('WrapCount', 0),
                    first_visible=self.send('GetFirstVisibleLine'), screen_lines=self.send('LinesOnScreen'),
                    anchor=self.send('GetAnchor'), caret=self.send('GetCurrentPos'))

    def close(self):
        try:
            if self.memory:
                kernel.VirtualFreeEx(self.handle, self.memory, 0, MEM_RELEASE)
            if self.handle:
                kernel.CloseHandle(self.handle)
            if self.frame and self.process.poll() is None:
                user.PostMessageW(self.frame, WM_CLOSE, 0, 0)
                self.process.wait(timeout=10)
        finally:
            if self.process.poll() is None:
                self.process.terminate()
                self.process.wait(timeout=5)
            self.stream.close()


STYLE_FIELDS = ('Fore', 'Back', 'Italic', 'Weight', 'Stretch', 'CharacterSet', 'EOLFilled',
                'Underline', 'Case', 'Visible', 'Changeable', 'HotSpot', 'Strike', 'SizeFractional')
GLOBAL_FIELDS = ('FontQuality', 'Zoom', 'LayoutCache', 'PositionCache', 'PhasesDraw', 'WrapVisualFlags',
                 'WrapVisualFlagsLocation', 'WrapStartIndent', 'WrapIndentMode', 'EdgeMode',
                 'ViewWS', 'ViewEOL', 'TabWidth', 'Indent', 'IndentationGuides', 'ExtraAscent', 'ExtraDescent')


def scene(app, style_ids):
    styles = []
    for identifier in style_ids:
        values = {key: app.send('StyleGet' + key, identifier) for key in STYLE_FIELDS}
        values['Font'] = app.string('StyleGetFont', identifier)
        styles.append(dict(id=identifier, values=values))
    return dict(styles=styles, globals={key: app.send('Get' + key) for key in GLOBAL_FIELDS},
                locale=app.string('GetFontLocale'),
                margins=[dict(width=app.send('GetMarginWidthN', index), type=app.send('GetMarginTypeN', index))
                         for index in range(app.send('GetMargins'))],
                left=app.send('GetMarginLeft'), right=app.send('GetMarginRight'))


def apply_scene(app, template):
    for key, value in template['globals'].items():
        app.send('Set' + key, value)
    app.send('SetFontLocale', 0, app.put(template['locale'].encode() + b'\0'))
    for style in template['styles']:
        for key, value in style['values'].items():
            app.send('StyleSet' + key, style['id'], app.put(value.encode() + b'\0') if key == 'Font' else value)
    # Realize fonts and let the original app finish its queued line-number update
    # before applying the controlled margin widths. This is setup, never timed.
    app.paint()
    time.sleep(0.05)
    app.send('SetMargins', len(template['margins']))
    for index, margin in enumerate(template['margins']):
        app.send('SetMarginTypeN', index, margin['type'])
        app.send('SetMarginWidthN', index, margin['width'])
    app.send('SetMarginLeft', 0, template['left'])
    app.send('SetMarginRight', 0, template['right'])


def fixture_bytes():
    # Same complete UTF-8 long-line fixture as benchmark_probe.cpp::Fixture(true).
    parts, size, index = [b'name = ['], len(b'name = ['), 0
    while size < MiB:
        part = f'"item{index} 中文😀 text words ", '.encode('utf-8')
        parts.append(part)
        size += len(part)
        index += 1
    return b''.join(parts) + b'0]\n'


def check(app, expected):
    styled = app.styled()
    require(styled[::2] == expected, 'complete original fixture bytes preserved')
    require(app.send('GetModify') == 0, 'save point preserved')
    require(app.send('GetTechnology') == TECHNOLOGY_DIRECTWRITE and app.send('GetBufferedDraw') == 0,
            'original DirectWrite buffer contract')
    return hashlib.sha256(styled).hexdigest()


def run_case(executable, fixture, folder, original, messages, template, measured):
    folder.mkdir()
    app = OwnedApp(executable, fixture, folder, original, messages)
    try:
        expected = fixture.read_bytes()
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            title = c.create_unicode_buffer(512)
            user.GetWindowTextW(app.frame, title, len(title))
            if fixture.name in title.value and app.send('GetLength') == len(expected):
                break
            time.sleep(0.02)
        else:
            raise RuntimeError('complete file load and final application title')
        app.send('SetTechnology', TECHNOLOGY_DIRECTWRITE)
        app.send('SetWrapMode', WRAP_NONE)
        app.send('Colourise', 0, -1)
        styled = app.styled()
        style_ids = sorted(set(styled[1::2]) | {STYLE_DEFAULT, STYLE_LINENUMBER})
        before = scene(app, style_ids)
        if template is None:
            template = before
        else:
            apply_scene(app, template)
        actual = scene(app, style_ids)
        (folder / 'scene-before.json').write_text(json.dumps(before, indent=2) + '\n', encoding='utf-8')
        (folder / 'scene-after.json').write_text(json.dumps(actual, indent=2) + '\n', encoding='utf-8')
        require(actual == template, 'matching complete font/style/cache/wrap settings')
        app.send('SetCurrentPos', len(expected) - 1)
        app.send('SetAnchor', len(expected) - 1)
        app.send('SetWrapMode', WRAP_WORD)
        app.paint()
        app.resize(INITIAL_VIEWPORT, setup=True)
        app.send('GotoPos', len(expected) - 1)
        app.paint()
        geometries = [app.geometry()]
        initial_hash = check(app, expected)
        resources = []
        if measured:
            time.sleep(1)  # outside every timed resize, as in the original probe
            resources.append(dict(stage='wrapped', **memory(app.process.pid)))
        intervals = []
        for target in RESIZE_VIEWPORTS:
            if measured:
                cpu, wall = app.cpu(), time.perf_counter_ns()
            app.resize(target)
            if measured:
                intervals.append(dict(wall_ns=time.perf_counter_ns() - wall, cpu_ns=app.cpu() - cpu))
            geometries.append(app.geometry())
        if measured:
            time.sleep(1)
            resources.append(dict(stage='after-resizes', **memory(app.process.pid)))
        require(check(app, expected) == initial_hash, 'all text/style bytes preserved after resizing')
        return template, dict(variant='original' if original else 'wx', before=before, scene=template,
                              geometries=geometries, styled_sha256=initial_hash, intervals=intervals, memory=resources,
                              wall_ns=sum(item['wall_ns'] for item in intervals),
                              cpu_ns=sum(item['cpu_ns'] for item in intervals))
    finally:
        app.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--core-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--measure', action='store_true')
    parser.add_argument('--scene', type=Path)
    args = parser.parse_args()
    require(user.SetProcessDpiAwarenessContext(w.HANDLE(-4)), 'controller uses physical per-monitor coordinates')
    args.output.mkdir()
    messages = dict((name, int(value)) for name, value in re.findall(r'^\s*(\w+) = (\d+),',
                    (args.core_root / 'scintilla/include/ScintillaMessages.h').read_text(), re.MULTILINE))
    original_folder = args.output / 'original-portable'
    original_folder.mkdir()
    original = original_folder / 'Notepad4.exe'
    shutil.copy2(args.original, original)
    (original_folder / 'Notepad4.ini').write_text('[Settings]\nSaveSettings=0\nWordWrap=0\n', encoding='utf-8')
    fixture = args.output / 'fixture.toml'
    fixture.write_bytes(fixture_bytes())
    require(fixture.stat().st_size == 1048597, 'unchanged benchmark fixture length')
    template = json.loads(args.scene.read_text(encoding='utf-8')) if args.scene else None
    require(not args.measure or template is not None, 'use the validated smoke scene for measurement')
    report = dict(measured=args.measure, complete=False, scratch_bytes_per_process=REMOTE_BYTES,
                  fixture_sha256=hashlib.sha256(fixture.read_bytes()).hexdigest(),
                  binaries={key: dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                            for key, path in [('original', original), ('wx', args.candidate)]}, rows=[])
    variants = dict(wx=(args.candidate, False), original=(original, True))
    for repetition in range(3 if args.measure else 1):
        for variant in (('original', 'wx') if repetition % 2 else ('wx', 'original')):
            executable, native = variants[variant]
            template, row = run_case(executable, fixture, args.output / f'{repetition + 1}-{variant}',
                                     native, messages, template, args.measure)
            report['rows'].append(row)
            (args.output / 'scene.json').write_text(json.dumps(template, indent=2) + '\n', encoding='utf-8')
            (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            reference = report['rows'][0]
            require(row['geometries'] == reference['geometries'], 'every viewport/metric/wrap/scroll observation matches')
            require(row['styled_sha256'] == reference['styled_sha256'], 'entire styled document matches both hosts')
            print(f'PASS: {repetition + 1} {variant}; ' + ('measured warm resizes' if args.measure else 'geometry only; no timing'), flush=True)
    report['complete'] = True
    (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
