"""Capture pinned Notepad4 automatic tag closure on an isolated Windows desktop."""
import json
from pathlib import Path
import sys
from character_capture import Session, prepare_host

exe, output = map(Path, sys.argv[1:3])


host = prepare_host(exe, output)

rows=[]
for language, suffix, encoding, command, codec, bom, newline, eol in [
    (language,suffix,encoding,command,codec,bom,newline,eol)
    for language,suffix in [("Html","html"),("Xml","xml")]
    for encoding,command,codec,bom in [("utf8",40103,"utf-8",b""),("utf8bom",40104,"utf-8",bytes.fromhex("efbbbf")),("utf16le",40101,"utf-16-le",bytes.fromhex("fffe")),("gbk",None,"gbk",b"")]
    for eol,newline in enumerate([chr(13)+chr(10),chr(13),chr(10)])]:
    path=host/f"input.{suffix}"
    initial = ("# coding: GBK"+newline if encoding == "gbk" else "")+"中"+newline+"<div"
    path.write_bytes(bom + initial.encode(codec))
    session = Session(host / "Notepad4.exe", path)
    before=initial
    pos=len(before.encode())
    cases=[(before,pos,'>',False,pos,-1,-1,False)]
    if command is not None: session.command(command)
    session.send(2031,eol)
    for before,pos,ch,readonly,anchor,extraAnchor,extraCaret,overtype in cases:
        captured = session.capture_case(before, pos, ch, readonly, anchor, extraAnchor, extraCaret, overtype)
        saved=path.read_bytes().hex()
        rows.append(dict(encoding=encoding,bom=bool(bom),eol=eol,beforeBytes=(bom+before.encode(codec)).hex(),anchor=anchor,extraAnchor=extraAnchor,extraCaret=extraCaret,overtype=overtype,saved=saved,language=language,before=before,position=pos,character=ord(ch),readonly=readonly,**captured))
    session.close()
(output/'original.json').write_text(json.dumps(dict(revision='89b8b36e0ee2c23f73be922315f17261ccfbbe1a',cases=rows),indent=2)+'\n')
print(f'Captured {len(rows)} original encoded tag cases')
