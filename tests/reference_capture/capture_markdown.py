"""Freeze pinned Markdown dialect states on an owned private Windows desktop.

Usage: python capture_markdown.py <pinned-Notepad4.exe> <external-evidence-dir>
"""
import ctypes as c
from ctypes import wintypes as w
import json
from pathlib import Path
import shutil
import sys

import original_host as host

executable, output = map(Path, sys.argv[1:3])
output.mkdir(parents=True, exist_ok=True)
portable = output / 'original-portable'
portable.mkdir(exist_ok=True)
exe = portable / 'Notepad4.exe'
shutil.copy2(executable, exe)
(portable / 'Notepad4.ini').write_text(
    '[Settings]\nSaveSettings=0\nWordWrap=0\nDetectIndentation=0\n', encoding='utf-16')
user = host.user
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.EnumDesktopWindows.argtypes = [w.HANDLE, host.CALLBACK, w.LPARAM]
user.CloseDesktop.argtypes = [w.HANDLE]
name = 'wxnotepad-original-markdown-195'
desktop = user.CreateDesktopW(name, None, None, 0, 0x10000000, None)
assert desktop
text = '''---
title: Metadata
---
# Heading one
text *em* **strong** ~~strike~~ ~sub~ ^super^ :smile: \\*escape
[text](https://example.com "title") <https://example.org> https://example.net
## Heading two
> quote
>> nested

- [x] task
- bullet
1. ordered

```python
code `inside`
```

    indented code

`inline code` $math$ $$display$$

| A | B |
|---|---|
| 1 | 2 |

Setext heading
==============
### Third
#### Fourth
##### Fifth
###### Sixth
---
Term
: definition

(@example) example
[@citation] H~2~O x^2^ {+added+} [-removed-]
\\(math\\) \\[display\\]

<!-- HTML comment -->
<div class="sample">html <em>embedded</em></div>

## Incomplete
**unfinished _emphasis
[unfinished](url
``` unclosed
tail
'''
raw = text.encode('utf-8')
path = portable / 'case.md'
path.write_bytes(raw)
position = raw.index(b'```\n')
rows = []
try:
    dialects = [('GitHub GFM', 40719), ('GitLab GLFM', 40720), ('Pandoc', 40721)]
    fixtures = [
        (text, b'```\n'),
        ('+++\ntitle = "Metadata"\n+++\n# Heading\nbody\n', b'+++\n#'),
        (';;;\n{"title": "Metadata"}\n;;;\n# Heading\nbody\n', b';;;\n#'),
        ('% Title\n% Author\n% Date\n\n# Heading\nbody\n', b'% Date'),
    ]
    for dialect, command, fixture, token in [(dialect, command, fixture, token)
                                            for dialect, command in dialects
                                            for fixture, token in fixtures]:
        raw = fixture.encode('utf-8')
        path.write_bytes(raw)
        position = raw.index(token)
        app = host.Original(path, exe, desktop, name)
        try:
            app.command(command)
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
            navigation_text = (b'# Native Markdown\nH~2~O x^2^ ~~strike~~ *emphasis* **strong**\n\n'
                               b'> filled quote\n\n---\n\n## Nested\nneedle\n')
            app.reset(navigation_text)
            app.send(4003, 0, -1)
            found = navigation_text.index(b'needle')
            app.send(2160, found, found + 6)
            app.command(40485)  # Search > Goto Block Start
            navigation = dict(text=navigation_text.hex(), anchor=app.send(2009), caret=app.send(2008))
            attributes = {str(style): {key: app.send(message, style) for key, message in
                          [('fore', 2481), ('back', 2482), ('bold', 2483), ('italic', 2484),
                           ('size', 2062), ('eolfill', 2487), ('underline', 2488), ('strike', 2283)]}
                          for style in sorted(set(bytes.fromhex(states[0]['styles'])))}
            rows.append(dict(name=dialect, position=position, lexer=app.send(4002),
                             states=states, attributes=attributes, navigation=navigation))
        finally:
            app.close()
finally:
    user.CloseDesktop(desktop)
(output / 'original.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
lines = ['# Frozen Notepad4 89b8b36 Markdown dialect states from capture_markdown.py.',
         '# dialect\tstate\tdeleted-byte-position\tutf8-hex\tstyle-bytes-hex\tfold-levels']
for row in rows:
    for state in row['states']:
        lines.append('\t'.join([row['name'], state['stage'], str(row['position']), state['text'],
                                state['styles'], ','.join(map(str, state['folds']))]))
(output / 'markdown_reference.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('PASS: independently captured 3 original Markdown dialects, 48 incremental states')
