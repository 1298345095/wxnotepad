"""Capture pinned Notepad4 automatic tag closure on an isolated Windows desktop."""
import ctypes as c
from ctypes import wintypes as w
import csv
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

exe, output = map(Path, sys.argv[1:3])


output.mkdir(parents=True, exist_ok=True)
host = output / "original-portable"
host.mkdir(exist_ok=True)
shutil.copy2(exe, host / "Notepad4.exe")
(host / "Notepad4.ini").write_text("[Notepad4]\n[Settings]\nSaveSettings=0\nLoadASCIIasUTF8=0\nLoadANSIasUTF8=0\nNoEncodingTags=0\nSkipUnicodeDetection=0\n", encoding="utf-8")
u, k = c.WinDLL("user32", use_last_error=True), c.WinDLL("kernel32", use_last_error=True)
u.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.EnumChildWindows.argtypes = [w.HWND, c.c_void_p, c.c_ssize_t]
u.SendMessageW.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
u.SendMessageW.restype = c.c_ssize_t
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
k.OpenProcess.restype = w.HANDLE
k.VirtualAllocEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD]
k.VirtualAllocEx.restype = c.c_void_p
k.WriteProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
k.VirtualFreeEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD]
k.CloseHandle.argtypes = [w.HANDLE]
callback = c.WINFUNCTYPE(w.BOOL, w.HWND, c.c_ssize_t)

rows=[]
for language, suffix, encoding, command, codec, bom, newline, eol in [
    (language,suffix,encoding,command,codec,bom,newline,eol)
    for language,suffix in [("Html","html"),("Xml","xml")]
    for encoding,command,codec,bom in [("utf8",40103,"utf-8",b""),("utf8bom",40104,"utf-8",bytes.fromhex("efbbbf")),("utf16le",40101,"utf-16-le",bytes.fromhex("fffe")),("gbk",None,"gbk",b"")]
    for eol,newline in enumerate([chr(13)+chr(10),chr(13),chr(10)])]:
    path=host/f"input.{suffix}"
    initial = ("# coding: GBK"+newline if encoding == "gbk" else "")+"中"+newline+"<div"
    path.write_bytes(bom + initial.encode(codec))
    process=subprocess.Popen([str(host/"Notepad4.exe"), "/ns", "/n", str(path)])
    windows=[]; editors=[]
    @callback
    def top(hwnd, _):
        pid=w.DWORD(); name=c.create_unicode_buffer(128)
        u.GetWindowThreadProcessId(hwnd,c.byref(pid)); u.GetClassNameW(hwnd,name,128)
        if pid.value==process.pid and name.value=="Notepad4": windows.append(hwnd)
        return True
    @callback
    def child(hwnd,_):
        name=c.create_unicode_buffer(128); u.GetClassNameW(hwnd,name,128)
        if name.value=="Scintilla": editors.append(hwnd)
        return True
    deadline=time.monotonic()+15
    while not windows and time.monotonic()<deadline:
        u.EnumWindows(top,0); time.sleep(.05)
    assert len(windows)==1,windows
    frame=windows[0];u.EnumChildWindows(frame,child,0);assert len(editors)==1
    editor=editors[0];handle=k.OpenProcess(0x38,False,process.pid)
    memory=k.VirtualAllocEx(handle,None,65536,0x3000,4);assert memory
    def send(message,wp=0,lp=0):return u.SendMessageW(editor,message,wp,lp)
    def state():
        text=bytes(send(2007,p)&255 for p in range(send(2006))).decode('utf-8')
        return [text,*[send(m) for m in (2009,2008,2159,2174,2016)],[[send(2579,i),send(2577,i)] for i in range(send(2570))]]
    before=initial
    pos=len(before.encode())
    cases=[(before,pos,'>',False,pos,-1,-1,False)]
    if command is not None: u.SendMessageW(frame,0x111,command,0)
    send(2031,eol)
    for before,pos,ch,readonly,anchor,extraAnchor,extraCaret,overtype in cases:
        send(2171,0);send(2422,0);send(2782,0)
        data=before.encode();assert k.WriteProcessMemory(handle,memory,data+b'\0',len(data)+1,None)
        send(2181,0,memory);send(2160,anchor,pos)
        if extraCaret>=0:send(2573,extraCaret,extraAnchor)
        send(2186,int(overtype))
        u.SendMessageW(frame,0x111,40004,0)
        send(2175);send(2014);send(2782,3)
        send(4003,0,-1);send(2171,int(readonly));u.SendMessageW(editor,0x102,ord(ch),0)
        after=state();history=[]
        while send(2174):send(2176);history.append(state())
        redo=[]
        while send(2016):send(2011);redo.append(state())
        u.SendMessageW(frame,0x111,40004,0)
        saved=path.read_bytes().hex()
        rows.append(dict(encoding=encoding,bom=bool(bom),eol=eol,beforeBytes=(bom+before.encode(codec)).hex(),anchor=anchor,extraAnchor=extraAnchor,extraCaret=extraCaret,overtype=overtype,saved=saved,language=language,before=before,position=pos,character=ord(ch),readonly=readonly,after=after,undo=history,redo=redo))
    send(2171,0);send(2175);u.SendMessageW(frame,0x10,0,0)
    try:process.wait(timeout=10)
    except subprocess.TimeoutExpired:process.terminate();raise
    k.VirtualFreeEx(handle,memory,0,0x8000);k.CloseHandle(handle)
(output/'original.json').write_text(json.dumps(dict(revision='89b8b36e0ee2c23f73be922315f17261ccfbbe1a',cases=rows),indent=2)+'\n')
print(f'Captured {len(rows)} original encoded tag cases')
