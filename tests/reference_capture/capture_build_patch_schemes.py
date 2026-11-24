"""Capture pinned original INI/Make/Diff public states on an owned desktop."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
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
    '[Settings]\nSaveSettings=0\nWordWrap=0\nDetectIndentation=0\nWarnLineEndings=0\n', encoding='utf-16')
user = host.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
name = 'wxnotepad-original-build-patch-268'
desktop = user.CreateDesktopW(name, None, None, 0, 0x10000000, None)
assert desktop
fixtures = [
    ('case.ini', '; comment 界\r\n# alternate\n[section]\r\nkey = value\n@=default\r\n'
     '  spaced: setting\n; group one\n; group two\n[next]\nvalue="unfinished\n', b'[next]'),
    ('Makefile', '# build 界\r\ninclude common.mk\nifdef DEBUG\nSOURCES := one.c \\\r\n two.c\n'
     'all: $(SOURCES)\r\n\t$(CC) ${CFLAGS} $< -o $@\n'
     'define run\n\techo $(subst a,b,$(NAME))\nendef\nendif\n', b'\\\r\n'),
    ('case.ninja', '# ninja\nrule compile\n  command = cc $in -o $out\n'
     'build 界.o: compile 界.c\ndefault 界.o\n', b'rule compile'),
    ('case.diff', 'diff --git a/界.txt b/界.txt\r\nindex 123..456 100644\n'
     '--- a/界.txt\r\n+++ b/界.txt\n@@ -1,2 +1,2 @@\r\n context\n-old\r\n+new\n'
     '\\ No newline at end of file\n@@ malformed\n+incomplete\n', b'diff --git'),
    ('case.patch', '*** old.txt\n--- new.txt\r\n***************\n*** 1,3 ****\n'
     '  same\n- old\n! changed\n--- 1,3 ----\n  same\n+ new\n! revised 界\n'
     '*** broken\n? incomplete\n', b'*** 1,3'),
]
states = []
operations = []
completions = []
detections = []


def history(app, available, command):
    result = []
    while app.send(available):
        app.send(command)
        result.append(app.text().hex())
    return ','.join(result)


def candidates(app):
    result = []
    previous = -1
    while app.send(2102):
        current = app.send(2445)
        if current == previous:
            break
        previous = current
        length = app.send(2610, 0, app.memory)
        buffer = c.create_string_buffer(length + 1)
        assert host.kernel.ReadProcessMemory(app.handle, app.memory, buffer, length + 1, None)
        result.append(buffer.value.decode('utf-8'))
        user.SendMessageW(app.editor, 0x100, 0x28, 0)
    return result


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

            records = [snapshot('base')]
            app.send(2175)
            app.send(2645, position, 1)
            records.append(snapshot('edit'))
            app.send(2176)
            records.append(snapshot('undo'))
            app.send(2011)
            records.append(snapshot('redo'))
            states.append(dict(name=filename, position=position, lexer=app.send(4002), states=records))
            if filename not in ('case.ini', 'Makefile', 'case.diff'):
                continue
            contexts = ([(b'key = ', 40), (b'; comment', 34), (b'  key=value', 13)] if filename == 'case.ini'
                        else [(b'all:', 13), (b'\tcommand', 13), (b'if', 13), (b'define run', 13),
                              (b'for item', 13), (b'ifeq (a,b)', 13), (b'$(value', 40), (b'# comment', 34),
                              (b'$(fil', ord('t'))]
                        if filename == 'Makefile' else [(b'+line', 40), (b' context', 13), (b'--- file', 34)])
            if filename == 'Makefile':
                contexts += [(b'define run\n\tbody\nendef\n', 13, 10), (b'define run\n', 13, 10)]
            for context in contexts:
                before, action = context[:2]
                caret = context[2] if len(context) == 3 else len(before)
                app.send(2101)
                app.reset(before, caret, caret)
                app.send(4003, 0, -1)
                eol = app.send(2030)
                user.SendMessageW(app.editor, 0x100 if action == 13 else 0x102, action, 0)
                items = candidates(app)
                app.send(2101)
                row = dict(name=filename, kind='character', before=before.hex(), position=caret, action=action,
                           after=app.text().hex(), anchor=app.send(2009), caret=app.send(2008), items=items, eol=eol)
                row['undo'] = history(app, 2174, 2176)
                row['redo'] = history(app, 2016, 2011)
                operations.append(row)
            if filename != 'case.diff':
                before = b'\tvalue=one\r\n\tother=two\n'
                app.reset(before, 0, len(before))
                app.command(40354)  # IDM_EDIT_LINECOMMENT, pinned resource.h
                row = dict(name=filename, kind='comment', before=before.hex(), position=len(before), action=0,
                           after=app.text().hex(), anchor=app.send(2009), caret=app.send(2008), items=[], eol=app.send(2030))
                row['undo'] = history(app, 2174, 2176)
                row['redo'] = history(app, 2016, 2011)
                operations.append(row)
            prefixes = ([b'SHELL := bash\nSHE', b'!INCL', b'$(fi'] if filename == 'Makefile'
                        else [b'alpha=one\nalpine=two\nalp'] if filename == 'case.ini' else [b'+alpha\n+alpine\n+alp'])
            for before in prefixes:
                app.send(2101)
                app.reset(before, len(before), len(before))
                app.send(4003, 0, -1)
                app.command(40373)
                items = candidates(app)
                after = app.text()
                if items:
                    app.send(2108, 0, app.put(items[0].encode('utf-8') + b'\0'))
                    user.SendMessageW(app.editor, 0x100, 13, 0)
                row = dict(name=filename, before=before.hex(), after=after.hex(), confirmed=app.text().hex(), items=items,
                           anchor=app.send(2009), caret=app.send(2008))
                app.send(2101)
                row['undo'] = history(app, 2174, 2176)
                row['redo'] = history(app, 2016, 2011)
                completions.append(row)
        finally:
            app.close()
    section = '[section]\nkey=value\n'
    for filename, text in [
        ('guess.unknown', section), ('guess', section), ('.private-settings', section),
        ('guess.txt', section), ('guess.cfg', section), ('my.cnf', section), ('guess.toml', section),
        ('guess-hash.unknown', section + '# comment\n'),
        ('guess-cpp.unknown', section + '// comment\n'),
        ('.guess-cpp', section + '#define VALUE 1\n'),
        ('guess-json.unknown', '[section]\n{ "value": 1 }\n'),
        ('guess-json-values.unknown', section + '{ "value": 1 }\n'),
        ('guess-yaml.unknown', section + '---\n'), ('guess-indent.unknown', ' ' + section),
        ('guess-prefix.unknown', 'x' * 4000 + '\n' + section),
        ('guess-leading.unknown', '\n' + section),
        ('guess-xml.unknown', '<?xml version="1.0"?>\n' + section),
        ('guess-shebang.unknown', '#!/not-known\n' + section),
    ]:
        path = portable / filename
        raw = text.encode('utf-8')
        path.write_bytes(raw)
        app = host.Original(path, exe, desktop, name)
        try:
            detections.append(dict(name=filename, text=raw.hex(), lexer=app.send(4002)))
        finally:
            app.close()
finally:
    user.CloseDesktop(desktop)
for filename, data in [('original.json', states), ('original-operations.json', operations),
                       ('original-completions.json', completions), ('original-detections.json', detections)]:
    (root / filename).write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8', newline='\n')
lines = ['# Independent pinned Notepad4 89b8b36; capture_build_patch_schemes.py on an owned desktop.']
for row in states:
    for state in row['states']:
        lines.append('\t'.join([row['name'], state['stage'], str(row['position']), state['text'],
                                state['styles'], ','.join(map(str, state['folds']))]))
(root / 'build_patch_reference.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
for filename, data, fields in [
    ('build_patch_operations_reference.tsv', operations,
     ['name', 'kind', 'before', 'position', 'action', 'after', 'anchor', 'caret', 'undo', 'redo', 'items', 'eol']),
    ('build_patch_completions_reference.tsv', completions,
     ['name', 'before', 'after', 'confirmed', 'anchor', 'caret', 'undo', 'redo', 'items']),
]:
    lines = ['# Independent pinned original public operations; capture_build_patch_schemes.py.']
    for row in data:
        values = [(' '.join(row[field]) if field == 'items' else str(row[field])) for field in fields]
        lines.append('\t'.join(values))
    (root / filename).write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
lines = ['# Independent original INI content-detection outcomes; capture_build_patch_schemes.py.']
for row in detections:
    lines.append('\t'.join([row['name'], row['text'], str(row['lexer'])]))
(root / 'build_patch_detection_reference.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
(root / 'provenance.json').write_text(json.dumps(dict(
    revision='89b8b36e0ee2c23f73be922315f17261ccfbbe1a',
    executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
    capture_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()), indent=2) + '\n', encoding='utf-8')
print('PASS: captured', len(states) * 4, 'original style/fold states,', len(operations), 'editing operations and',
      len(completions), 'completion cases and', len(detections), 'INI detection cases')
