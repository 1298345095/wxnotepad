"""Capture independent pinned CSS dialect/SQL states in an owned original copy."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil

import original_host as host

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exe', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
root = args.output.resolve()
portable = root / 'original-portable'
portable.mkdir(parents=True, exist_ok=True)
exe = portable / 'Notepad4.exe'
shutil.copy2(args.exe, exe)
(portable / 'Notepad4.ini').write_text(
    '[Settings]\nSaveSettings=0\nWordWrap=0\nDetectIndentation=0\n', encoding='utf-16')
user = host.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
name = 'wxnotepad-original-css-sql-256'
desktop = user.CreateDesktopW(name, None, None, 0, 0x10000000, None)
assert desktop
fixtures = [
    ('case.css', ':root {\n  --theme: #123456;\n}\n'
     'div.card:hover::before, #main[data-x="界"] {\n'
     '  /* comment */ color: var(--theme); width: calc(100% - 2px);\n'
     '  content: "escaped\\22 text";\n}\n@media screen {\n a { color: red; }\n}\n'
     '.unfinished { content: "open\n', b'comment */'),
    ('case.scss', '$tone: #123456;\n@mixin theme($x) {\n  color: $x;\n}\n'
     '.card {\n // comment\n &:hover { @include theme($tone); }\n'
     ' #{$name}-icon { margin: 2px; }\n}\n$x: "unfinished\n', b'#{$name}'),
    ('case.less', '@tone: #123456;\n.mixin(@x) { color: @x; }\n'
     '.card {\n // comment\n &:hover { .mixin(@tone); }\n'
     ' .@{name} { width: (2 + 3px); }\n}\n@x: "unfinished\n', b'@{name}'),
    ('case.hss', '$tone: #123456;\n.card {\n // comment\n color: $tone;\n'
     ' &:hover { background: rgb(1, 2, 3); }\n}\n@media screen {\n a { color: red; }\n}\n'
     '.unfinished { content: "open\n', b'comment'),
    ('case.sql', '-- comment\nCREATE FUNCTION sample(x INTEGER) RETURNS INTEGER\nBEGIN\n'
     ' SELECT COUNT(*), `quoted`, "double", [bracket], @value, 0xCAFE, B\'101\',\n'
     "  q'[quoted text]', 'escape\\ntext' FROM table_name;\n"
     ' /* block comment */\n IF x > 1 THEN\n  RETURN 2;\n END IF;\nEND;\n'
     "SELECT 'unfinished\n", b'block comment */'),
]
rows = []
completions = []
typing = []


def completion_items(app):
    items = []
    previous = -1
    while app.send(2102):
        current = app.send(2445)
        if current == previous:
            break
        previous = current
        size = app.send(2610, 0, app.memory)
        buffer = c.create_string_buffer(size + 1)
        assert host.kernel.ReadProcessMemory(app.handle, app.memory, buffer, size + 1, None)
        items.append(buffer.value.decode('utf-8'))
        user.SendMessageW(app.editor, 0x100, 0x28, 0)
    return items


try:
    for filename, text, token in fixtures:
        raw = text.encode('utf-8')
        path = portable / filename
        path.write_bytes(raw)
        app = host.Original(path, exe, desktop, name)
        try:
            position = raw.index(token)

            def snapshot(stage):
                app.send(4003, 0, -1)
                data = app.text()
                return dict(stage=stage, text=data.hex(),
                            styles=bytes(app.send(2038, i) & 255 for i in range(len(data))).hex(),
                            folds=[app.send(2223, i) for i in range(app.send(2154))])

            states = [snapshot('base')]
            app.send(2175)
            app.send(2645, position, 1)
            states.append(snapshot('edit'))
            app.send(2176)
            states.append(snapshot('undo'))
            app.send(2011)
            states.append(snapshot('redo'))
            rows.append(dict(name=filename, position=position, lexer=app.send(4002), states=states))
            for before in ([b'a:hov', b'a::bef', b'@med', b'a { col'] if filename.endswith('css')
                           else [b'a { col'] if filename.endswith(('.less', '.hss')) else [b'SEL', b'COU']):
                app.send(2101)
                app.reset(before, len(before), len(before))
                app.send(4003, 0, -1)
                app.command(40373)
                items = completion_items(app)
                after = app.text()
                if items:
                    app.send(2108, 0, app.put(items[0].encode('utf-8') + b'\0'))
                    user.SendMessageW(app.editor, 0x100, 0x0D, 0)
                completions.append(dict(name=filename, before=before.hex(), after=after.hex(), items=items,
                                        confirmed=app.text().hex()))
                app.send(2101)
            contexts = [(b'SE', 'L'), (b"SELECT Q", "'"), (b'SELECT ', '(')] if filename.endswith('.sql') else [
                (b'a { co', 'l'), (b'a { ', '('), (b'a { content: ', '"')]
            for before, character in contexts:
                app.reset(before, len(before), len(before))
                app.send(4003, 0, -1)
                user.SendMessageW(app.editor, 0x102, ord(character), 0)
                after = app.text()
                items = completion_items(app)
                if items:
                    app.send(2108, 0, app.put(items[0].encode('utf-8') + b'\0'))
                    user.SendMessageW(app.editor, 0x100, 0x0D, 0)
                confirmed = app.text()
                app.send(2101)
                app.send(2176)
                undo = app.text()
                app.send(2011)
                typing.append(dict(name=filename, before=before.hex(), character=ord(character), after=after.hex(),
                                   confirmed=confirmed.hex(), undo=undo.hex(), redo=app.text().hex(), items=items))
        finally:
            app.close()
finally:
    user.CloseDesktop(desktop)
(root / 'original.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8', newline='\n')
(root / 'original-completions.json').write_text(json.dumps(completions, indent=2) + '\n', encoding='utf-8', newline='\n')
(root / 'original-typing.json').write_text(json.dumps(typing, indent=2) + '\n', encoding='utf-8', newline='\n')
lines = ['# Frozen pinned Notepad4 89b8b36; capture_css_sql.py on an owned Windows desktop.',
         '# filename\tstate\tdeleted-byte-position\tutf8-hex\tstyle-bytes-hex\tfold-levels']
for row in rows:
    for state in row['states']:
        lines.append('\t'.join([row['name'], state['stage'], str(row['position']), state['text'],
                                state['styles'], ','.join(map(str, state['folds']))]))
(root / 'css_sql_reference.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
lines = ['# Independent original manual completion lists and native Enter bytes; capture_css_sql.py.']
for row in completions:
    lines.append('\t'.join([row['name'], row['before'], row['after'], row['confirmed'], ' '.join(row['items'])]).rstrip('\t'))
(root / 'css_sql_completion_reference.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
lines = ['# Independent original automatic completion and paired-character native typing; capture_css_sql.py.']
for row in typing:
    lines.append('\t'.join([row['name'], row['before'], str(row['character']), row['after'], row['confirmed'],
                            row['undo'], row['redo'], ' '.join(row['items'])]).rstrip('\t'))
(root / 'css_sql_typing_reference.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
print('PASS: independent original CSS/SCSS/Less/HSS/SQL, 20 incremental style/fold states')
