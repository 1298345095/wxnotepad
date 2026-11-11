"""Freeze independent original lexer states on an owned Windows desktop."""
import ctypes as c
from ctypes import wintypes as w
import argparse
import original_host as host
import json
from pathlib import Path
import shutil
from private_desktop import Process

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exe', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
root = args.output.resolve()
root.mkdir(parents=True, exist_ok=True)
user = c.WinDLL('user32', use_last_error=True)
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, c.c_void_p, w.DWORD, w.DWORD, c.c_void_p]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
name = 'wxnotepad-original-194'
desktop = user.CreateDesktopW(name, None, None, 0, 0x10000000, None)
assert desktop
portable = root / 'original-portable'
portable.mkdir(exist_ok=True)
exe = portable / 'Notepad4.exe'
shutil.copy2(args.exe, exe)
(portable / 'Notepad4.ini').write_text('[Settings]\nSaveSettings=0\nWordWrap=0\nDetectIndentation=0\n', encoding='utf-16')
fixtures = [('case.toml',
  '# TOML config\n'
  '[package]\n'
  'name = "wx\\t界"\n'
  'time = 1979-05-27T07:32:00Z\n'
  'count = 0x2A\n'
  "raw = '''literal\n"
  "text'''\n"
  'text = """multi\n'
  'line\\ntext"""\n'
  '[package.build]\n'
  'enabled = true\n',
  'line\\ntext'),
 ('case.yaml',
  '%YAML 1.2\n'
  '---\n'
  'defaults: &defaults\n'
  '  enabled: true\n'
  '  quoted: "wx\\t界"\n'
  '  literal: |\n'
  '    line one\n'
  '    line two\n'
  '\n'
  'copy:\n'
  '  <<: *defaults\n'
  "  value: 'quote''text'\n"
  '...\n',
  '    line two'),
 ('CMakeLists.txt',
  'cmake_minimum_required(VERSION 3.25)\n'
  'project(sample)\n'
  '#[=[ bracket comment\n'
  'nested ]=] text\n'
  ']=]\n'
  'function(build target)\n'
  '  set(value [=[bracket\n'
  'argument]=])\n'
  '  if(DEFINED ENV{HOME})\n'
  '    message("${target}:$<CONFIG>\\n")\n'
  '  endif()\n'
  'endfunction()\n',
  'argument]=]')]
rows=[]
operations=[]
try:
    for filename,text,token in fixtures:
        raw=text.encode();path=portable/filename;path.write_bytes(raw)
        app=host.Original(path, exe, desktop, name)
        try:
            position=raw.index(token.encode())
            def snap(stage):
                app.send(4003,0,-1)
                data=app.text()
                return dict(stage=stage,text=data.hex(),styles=bytes(app.send(2038,i)&255 for i in range(len(data))).hex(),folds=[app.send(2223,i) for i in range(app.send(2154))])
            states=[snap('base')]
            app.send(2175);app.send(2645,position,1);states.append(snap('edit'))
            app.send(2176);states.append(snap('undo'))
            app.send(2011);states.append(snap('redo'))
            rows.append(dict(name=filename,position=position,lexer=app.send(4002),states=states))
            for command in range(7):
                caret=raw.index(b'\n')+1
                app.reset(raw,caret,caret)
                app.send(4003,0,-1)
                app.command([40485,40486,40487,40488,40489,40490,40252][command])
                operations.append(dict(name=filename,kind='navigation',before=raw.hex(),position=caret,
                    action=command,after=app.text().hex(),anchor=app.send(2009),caret=app.send(2008)))
            contexts={'case.toml': [(b"key = 'raw\\", 39), (b'key = "text\\', 34), (b'key = ', 40), (b'# comment', 34)], 'case.yaml': [(b"key: 'raw\\", 39), (b'key: "text\\', 34), (b'key: ', 40), (b'# comment', 34)], 'CMakeLists.txt': [(b'set(x [=[raw\\', 34), (b'message("text\\', 34), (b'$<CONFIG', 60), (b'# comment', 34)]}
            for before,character in contexts[filename]:
                app.reset(before,len(before),len(before))
                app.send(4003,0,-1)
                host.user.SendMessageW(app.editor,0x102,character,0)
                operations.append(dict(name=filename,kind='character',before=before.hex(),position=len(before),
                    action=character,after=app.text().hex(),anchor=app.send(2009),caret=app.send(2008)))
                history=[]
                while app.send(2174):
                    app.send(2176)
                    history.append(app.text().hex())
                operations[-1]['undo']=','.join(history)
                history=[]
                while app.send(2016):
                    app.send(2011)
                    history.append(app.text().hex())
                operations[-1]['redo']=','.join(history)


        finally:app.close()
finally:user.CloseDesktop(desktop)
(root/'original.json').write_text(json.dumps(rows,indent=2),encoding='utf-8', newline='\n')
lines=['# Frozen Notepad4 89b8b36 states; capture_config_languages.py on a private Windows desktop.', '# filename\tstate\tdeleted-byte-position\tutf8-hex\tstyle-bytes-hex\tfold-levels']
for row in rows:
    for state in row['states']:
        lines.append('\t'.join([row['name'],state['stage'],str(row['position']),state['text'],state['styles'],','.join(map(str,state['folds']))]))
(root/'config_languages_reference.tsv').write_text('\n'.join(lines)+'\n',encoding='utf-8', newline='\n')
print('PASS: independently captured 3 original languages, 12 incremental states',[(r['name'],r['lexer']) for r in rows])

lines=['# Frozen original public navigation and typed-character operations; capture_config_languages.py.']
for row in operations:
    lines.append('\t'.join(str(row.get(key,'')) for key in ['name','kind','before','position','action','after','anchor','caret','undo','redo']))
(root/'config_operations_reference.tsv').write_text('\n'.join(lines)+'\n',encoding='utf-8', newline='\n')
(root/'original-operations.json').write_text(json.dumps(operations,indent=2),encoding='utf-8', newline='\n')
print('PASS: captured',len(operations),'original navigation and character operations')
