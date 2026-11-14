"""Capture delivered completion behaviors on an owned private Windows desktop.

Usage: python capture_completion_behaviors.py ORIGINAL_EXE OUTPUT_DIRECTORY [GROUP]
Only public original commands/messages are used; no clipboard or desktop input.
GROUP is documents, strings, interior, embedded, scopes or fallbacks. Within a
capture input, A|B places the caret between A and B; the marker is not inserted.
"""
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil
import sys
import time
import original_host as h

# Pinned Scintilla.h and original Notepad4 resource.h identifiers.
SCI_GETSTYLEINDEXAT = 2038
SCI_REDO = 2011
SCI_AUTOCCANCEL = 2101
SCI_AUTOCACTIVE = 2102
SCI_AUTOCSELECT = 2108
SCI_UNDO = 2176
SCI_BRACEBADLIGHT = 2352
SCI_AUTOCGETCURRENT = 2445
SCI_AUTOCGETCURRENTTEXT = 2610
IDM_EDIT_COMPLETEWORD = 40373

# Win32 message, virtual-key and desktop access identifiers.
WM_KEYDOWN = 0x0100
VK_DOWN = 0x28
VK_RETURN = 0x0D
GENERIC_ALL = 0x10000000

original = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
host = out / 'original-portable'
host.mkdir(exist_ok=True)
executable = host / 'Notepad4.exe'
shutil.copy2(original, executable)

# Scheme, extension, document, original options, ignore case, LaTeX input, scope.
cases = [
    ('Plain Text', 'txt', 'bugprone-branch-clone bug', 12, False, False, 255),
    ('C/C++', 'cpp', 'alpha.beta.gamma al', 12, False, False, 255),
    ('C/C++', 'cpp', 'alpha->beta->gamma al', 12, False, False, 255),
    ('C/C++', 'cpp', 'alpha::beta::gamma al', 12, False, False, 255),
    ('JavaScript', 'js', 'alpha.$thing al', 12, False, False, 255),
    ('JavaScript', 'js', 'alpha.' + 'x' * 130 + ' al', 12, False, False, 255),
    ('Plain Text', 'txt', 'alpha.éclair alpha.élan al', 12, False, False, 255),
    ('Plain Text', 'txt', 'alpha.中word alpha.中help al', 12, False, False, 255),
    ('Plain Text', 'txt', 'al' + 'x' * 130 + ' alpha al', 12, False, False, 255),
    ('Plain Text', 'txt', 'AlphaLonger alphaLower alphal', 7, True, False, 255),
    ('Plain Text', 'txt', 'AlphaLonger alphaLower alphal', 7, False, False, 255),
    ('Plain Text', 'txt', 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOne abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNTwo abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMN', 12, False, False, 255),
    ('Plain Text', 'txt', '0xABCD 0xABCE 0xABC', 12, False, False, 255),
    ('C/C++', 'cpp', '  #  inc', 7, False, False, 255),
    ('C/C++', 'cpp', 'x #  inc', 7, False, False, 255),
    ('Go', 'go', 'if (ready) {} i', 12, False, False, 255),
    ('C/C++', 'cpp', '#define Thing (x)\nThi', 12, False, False, 255),
    ('Rust', 'rs', 'user!(); us', 12, False, False, 255),
    ('Plain Text', 'txt', 'alpha.alpine alpha.al', 12, False, False, 255),
    ('Plain Text', 'txt', 'other.alpine other.al', 12, False, False, 255),
    ('C/C++', 'cpp', 'alpha.beta.gamma alpha::', 12, False, False, 255),
]
groups = {
    'documents': cases,
    'fallbacks': [
        ('Plain Text', 'txt', 'alpha alpine unknown.alp', 12, False, False, 255),
        ('C/C++', 'cpp', 'alpha alpine unknown::alp', 12, False, False, 255),
        ('JavaScript', 'js', 'alpha alpine unknown.$alp', 12, False, False, 255),
        ('C/C++', 'cpp', 'alpha.beta.gamma unknown.alp', 12, False, False, 255),
        ('Plain Text', 'txt', 'bugprone-branch-clone b', 12, False, False, 255),
        ('Rust', 'rs', 'user!=other; us', 12, False, False, 255),
        ('Rust', 'rs', 'user !(); us', 12, False, False, 255),
    ],
    'strings': [
        ('C/C++', 'cpp', '"\\nAlpha \\nAlpine \\nAl', 12, False, False, 255),
        ('C/C++', 'cpp', '"%sAlpha %sAlpine %sAl', 12, False, False, 255),
        ('C/C++', 'cpp', 'R"(\\nAlpha \\nAlpine \\nAl', 12, False, False, 255),
        ('Python', 'py', '"\\nAlpha \\nAlpine \\nAl', 12, False, False, 255),
        ('Python', 'py', 'r"\\nAlpha \\nAlpine \\nAl', 12, False, False, 255),
        ('Lua', 'lua', '"%sAlpha %sAlpine %sAl', 12, False, False, 255),
        ('Bash', 'sh', "'\\nAlpha \\nAlpine \\nAl", 12, False, False, 255),
        ('Bash', 'sh', '"\\nAlpha \\nAlpine \\nAl', 12, False, False, 255),
        ('JSON', 'json', '{"key":"%sAlpha %sAlpine %sAl', 12, False, False, 255),
        ('JavaScript', 'js', '$alpha $alpine $al', 7, False, False, 255),
        ('Rust', 'rs', 'macro_rules! m { ($alpha:ident, $alpine:ident) => { $al', 7, False, False, 255),
        ('C/C++', 'cpp', '/// @par', 7, False, False, 255),
        ('C/C++', 'cpp', '/// \\par', 7, False, False, 255),
        ('C/C++', 'cpp', '/// <sum', 7, False, False, 255),
        ('JavaScript', 'js', '/** @par', 7, False, False, 255),
        ('TypeScript', 'ts', '/// <ref', 7, False, False, 255),
    ],
    'interior': [
        ('C/C++', 'cpp', '/// @par| ', 7, False, False, 255),
        ('C/C++', 'cpp', '/// \\par| ', 7, False, False, 255),
        ('C/C++', 'cpp', '/// <sum| ', 7, False, False, 255),
        ('JavaScript', 'js', '/** @par| */', 7, False, False, 255),
        ('TypeScript', 'ts', '/// <ref| ', 7, False, False, 255),
        ('Web Source Code', 'html', '<style>@med| </style>', 7, False, False, 255),
        ('Web Source Code', 'html', '<style>a::bef| {}</style>', 7, False, False, 255),
        ('Web Source Code', 'html', '<style>a:hov|{}</style>', 7, False, False, 255),
        ('Web Source Code', 'html', '<style>a{color:hov|}</style>', 7, False, False, 255),
        ('Web Source Code', 'html', '<script>/** @par| */</script>', 7, False, False, 255),
        ('Web Source Code', 'html', '<script>$alpha $alpine $al|;</script>', 7, False, False, 255),
        ('Plain Text', 'txt', '中hello 中help 中he|llo', 12, False, False, 255),
        ('C/C++', 'cpp', '"\\nAlpha" R"(\\nAl', 12, False, False, 255),
    ],
    'embedded': [
        ('Web Source Code', 'html', '<style>@med', 7, False, False, 255),
        ('Web Source Code', 'html', '<style>a::bef', 7, False, False, 255),
        ('Web Source Code', 'html', '<style>a{color:hov', 7, False, False, 255),
        ('Web Source Code', 'html', '<style>a{color:blu', 7, False, False, 255),
        ('Web Source Code', 'html', '<script>/** @par', 7, False, False, 255),
        ('Web Source Code', 'html', '<script>$alpha $alpine $al', 7, False, False, 255),
        ('ASP VBScript', 'asp', '<% Dim a\r\nLef', 7, False, False, 255),
        ('Web Source Code', 'html', '<script type="text/vbscript">Lef', 7, False, False, 255),
        ('Web Source Code', 'html', '<p>text</p><?php arr', 7, False, False, 255),
        ('Property List', 'plist', '<arr', 7, False, False, 255),
        ('CMake', 'cmake', 'mes', 7, False, False, 255),
        ('TOML', 'toml', 'alpha="one"\nalpine="two"\nal', 12, False, False, 255),
        ('YAML', 'yaml', 'alpha: one\nalpine: two\nal', 12, False, False, 255),
    ],
    'scopes': [
        ('C/C++', 'cpp', '// alpha\n"alpine"\nal', 12, False, False, 159),
        ('C/C++', 'cpp', '// alpha\n"alpine"\nal', 12, False, False, 175),
        ('C/C++', 'cpp', '"alpha al', 12, False, False, 248),
        ('Web Source Code', 'html', '<p>alpha</p><script>alpine; al', 12, False, False, 143),
        ('Plain Text', 'txt', 'alpha alpine al', 7, False, False, 251),
        ('Plain Text', 'txt', 'alpha alpine al', 2, False, False, 255),
    ],
}
cases = groups[sys.argv[3] if len(sys.argv) > 3 else 'documents']

user = h.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
name = 'wxnotepad-completion-behaviors'
desktop = user.CreateDesktopW(name, None, None, 0, GENERIC_ALL, None)
assert desktop


def state(app):
    value = app.state()
    value['text'] = app.text().decode('utf-8')
    return value


def candidates(app):
    words = []
    previous = -1
    while app.send(SCI_AUTOCACTIVE):
        current = app.send(SCI_AUTOCGETCURRENT)
        if current == previous:
            break
        previous = current
        size = app.send(SCI_AUTOCGETCURRENTTEXT, 0, app.memory)
        buffer = c.create_string_buffer(size + 1)
        assert h.kernel.ReadProcessMemory(app.handle, app.memory, buffer, size + 1, None)
        words.append(buffer.value.decode('utf-8'))
        user.SendMessageW(app.editor, WM_KEYDOWN, VK_DOWN, 0)
    return words


results = []
try:
    for scheme, extension, before, options, ignore, latex, scope in cases:
        caret = len(before.split('|', 1)[0].encode('utf-8'))
        before = before.replace('|', '', 1)
        (host / 'Notepad4.ini').write_text(
            f'[Settings]\nSaveSettings=0\nAutoCompleteOption={options}\nAutoCIgnoreCase={int(ignore)}\n'
            f'LaTeXInputMethod={int(latex)}\nAutoCompleteScope={scope}\n', encoding='utf-16')
        document = host / ('fixture.' + extension)
        document.write_bytes(b'\xef\xbb\xbf')
        app = h.Original(document, executable, desktop, name)
        try:
            data = before.encode('utf-8')
            app.reset(data, caret, caret)
            app.send(SCI_BRACEBADLIGHT, 0, -1)
            result = dict(scheme=scheme, before=before, initialCaret=caret, options=options, ignore=ignore, latex=latex, scope=scope)
            result['styles'] = [app.send(SCI_GETSTYLEINDEXAT, index) for index in range(len(data))]
            result['currentStyle'] = app.send(SCI_GETSTYLEINDEXAT, caret)
            app.command(IDM_EDIT_COMPLETEWORD)
            result['command'] = state(app)
            result['items'] = candidates(app)
            if result['items']:
                app.send(SCI_AUTOCSELECT, 0, app.put(result['items'][0].encode('utf-8') + b'\0'))
                user.SendMessageW(app.editor, WM_KEYDOWN, VK_RETURN, 0)
            result['confirmed'] = state(app)
            app.send(SCI_AUTOCCANCEL)
            app.send(SCI_UNDO)
            result['undo'] = state(app)
            app.send(SCI_REDO)
            result['redo'] = state(app)
            results.append(result)
            print(scheme, repr(before), '=>', result['command']['text'], result['items'], flush=True)
        finally:
            app.close()
finally:
    user.CloseDesktop(desktop)
(out / 'original-behaviors.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
