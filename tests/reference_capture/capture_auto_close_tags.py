"""Capture pinned Notepad4 automatic tag closure on an isolated Windows desktop."""
import json
from pathlib import Path
import sys
from character_capture import Session, prepare_host

exe, output = map(Path, sys.argv[1:3])


host = prepare_host(exe, output)

rows=[]
for language, suffix in [("PlainText", "txt"), ("Cpp", "cpp"), ("Html", "html"), ("Xml", "xml"), ("JavaScript", "js"), ("Jsx", "jsx"), ("TypeScript", "ts"), ("Tsx", "tsx")]:
    path=host/f"input.{suffix}"
    path.write_text("", encoding="utf-8")
    session = Session(host / "Notepad4.exe", path)
    markup = ['<div', '<p', '<br', '<BR', '<area', '<basefont', '<command', '<frame', '<keygen', '<isindex', '<wbr', '<x:custom-name_1.ext', '<3tag', '<é', '<div class="x"', '<div data="a>b"', '<div/', '<div / ', '</div', '<', '< ', '<!DOCTYPE html', '<?xml', '<!--hello', '<a <b', '<a >b', '<a\nname', '<a'+ 'x'*508, '<a'+ 'x'*509, '<a'+ 'x'*510, '#include <iostream', 'std::vector<int', 'a < b', 'obj-', '<script> a < b', '<script>a<b', '<script>a <Widget', '<style>a<b', '  #include <iostream', '	#include <x', '<script>const x="<div', '<style> a < b', '<style>p::before{content:"<div', 'const x = <Component', 'const f = <T', '// <div', '"<div']
    cases=[(text,len(text.encode()),'>',False,len(text.encode()),-1,-1,False) for text in markup]
    cases += [("<div",4,'>',True,4,-1,-1,False), ("<divxx",6,'>',False,4,-1,-1,False), ("<div\n<span",4,'>',False,4,10,10,False)]
    for before,pos,ch,readonly,anchor,extraAnchor,extraCaret,overtype in cases:
        captured = session.capture_case(before, pos, ch, readonly, anchor, extraAnchor, extraCaret, overtype)
        saved=path.read_bytes().decode('utf-8')
        rows.append(dict(anchor=anchor,extraAnchor=extraAnchor,extraCaret=extraCaret,overtype=overtype,saved=saved,language=language,before=before,position=pos,character=ord(ch),readonly=readonly,**captured))
    session.close()
(output/'original.json').write_text(json.dumps(dict(revision='89b8b36e0ee2c23f73be922315f17261ccfbbe1a',cases=rows),indent=2)+'\n')
lines=['// Captured from original Notepad4 89b8b36; see capture_auto_close_tags.py.', '#pragma once', '#include <array>', 'namespace AutoCloseTagsReference {', 'struct Selection { intptr_t anchor, caret; };', 'struct State { const char* text; intptr_t anchor, caret; bool modified, canUndo, canRedo; std::initializer_list<Selection> selections; };', 'struct Case { SyntaxLanguage language; const char* before; intptr_t position; int character; bool readOnly; intptr_t anchor, extraAnchor, extraCaret; bool overtype; const char* saved; State after; std::initializer_list<State> undo, redo; };', f'inline const std::array<Case, {len(rows)}> cases = {{{{']
def lit(x):return json.dumps(x,ensure_ascii=True)
def bool_lit(x):return 'true' if x else 'false'
def state_lit(s):return '{'+lit(s[0])+', '+', '.join(str(x) for x in s[1:3])+', '+', '.join(bool_lit(x) for x in s[3:6])+', {'+', '.join('{'+str(a)+', '+str(b)+'}' for a,b in s[6])+'}}'
for r in rows:
    lines.append('{SyntaxLanguage::'+r['language']+', '+lit(r['before'])+', '+str(r['position'])+', '+str(r['character'])+', '+bool_lit(r['readonly'])+', '+', '.join(str(r[k]) for k in ['anchor','extraAnchor','extraCaret'])+', '+bool_lit(r['overtype'])+', '+lit(r['saved'])+', '+state_lit(r['after'])+', {'+', '.join(map(state_lit,r['undo']))+'}, {'+', '.join(map(state_lit,r['redo']))+'}},')
lines+=['}};','} // namespace AutoCloseTagsReference']
Path(__file__).resolve().parents[1].joinpath('auto_close_tags_reference.h').write_text('\n'.join(lines)+'\n')
print(f'Captured {len(rows)} original cases')
