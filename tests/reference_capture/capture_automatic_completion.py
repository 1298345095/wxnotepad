"""Capture pinned original automatic completion behavior on an owned desktop.

Usage: python tests/reference_capture/capture_automatic_completion.py ORIGINAL_EXE OUTPUT_DIR
Creates a separate portable host; does not use the clipboard or desktop input.
"""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import sys, subprocess, json, time
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from capture_number_radix import find_editor_windows
original=Path(sys.argv[1]).resolve()
out=Path(sys.argv[2]).resolve()
out.mkdir(parents=True,exist_ok=True)
host=out/'original-portable'
host.mkdir(exist_ok=True)
import shutil
shutil.copy2(original, host/'Notepad4.exe')
(host/'Notepad4.ini').write_text('[Settings]\nSaveSettings=0\nAutoCompleteOption=7\nAutoCIgnoreCase=0\n',encoding='utf-8')
u=c.WinDLL('user32',use_last_error=True); k=c.WinDLL('kernel32',use_last_error=True)
u.SendMessageW.argtypes=[w.HWND,w.UINT,c.c_size_t,c.c_ssize_t];u.SendMessageW.restype=c.c_ssize_t
u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)];u.GetClassNameW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
cb=c.WINFUNCTYPE(w.BOOL,w.HWND,c.c_ssize_t);u.EnumWindows.argtypes=[cb,c.c_ssize_t];u.EnumChildWindows.argtypes=[w.HWND,cb,c.c_ssize_t]
k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];k.OpenProcess.restype=w.HANDLE
k.VirtualAllocEx.argtypes=[w.HANDLE,c.c_void_p,c.c_size_t,w.DWORD,w.DWORD];k.VirtualAllocEx.restype=c.c_void_p
for n in ['WriteProcessMemory','ReadProcessMemory']:getattr(k,n).argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.c_void_p]
k.VirtualFreeEx.argtypes=[w.HANDLE,c.c_void_p,c.c_size_t,w.DWORD];k.CloseHandle.argtypes=[w.HANDLE]

fixtures=[('.txt','alpha alpine a', [ord('l')], 15),('.txt','alpha alpine al', [ord('p')],15),('.cpp','pr',[ord('i'),('key',13)],15),('.txt','alpha alpine a',[ord('l'),('key',13)],14),('.txt','alpha alpine a',[ord('l'),('key',9)],13),('.txt','alpha alpine a',[ord('l'),ord(' ')],15),('.txt','alpha alpine al',[ord('p'),('key',8)],15),('.cpp','obj->field obj-', [ord('>')],15)]
for count in (20,70):
 text=' '.join([f'ab{i:02}' for i in range(count)]+[f'ac{i:02}' for i in range(80-count)])+' '
 fixtures.append(('.txt',text,[ord('a'),ord('b')],15))
 if count==20:fixtures.append(('.txt',text,[ord('a'),ord('b'),('key',8)],15))
fixtures.append(('.txt','alpha alpine a',[ord('l')],15,True))
fixtures.append(('.txt',chr(92)+'i',[ord('n'),('key',9)],13,False,True))
for before,candidate in [('ar','array[]'),('ar[]','array[]'),('ve','vector<>'),('ve<>','vector<>')]:
 fixtures.append(('.cpp',before,[('show',candidate),('key',13)],15))
results=[]
for fixture in fixtures:
 ext,text,events,fill=fixture[:4]
 (host/'Notepad4.ini').write_text(chr(10).join(('[Settings]', 'SaveSettings=0', 'AutoCompleteOption=7', f'AutoCFillUpMask={fill}', f'LaTeXInputMethod={int(len(fixture)>5)}', '')), encoding='utf-8')
 p=host/('fixture'+ext);p.write_text('\ufeff',encoding='utf-8')
 process=subprocess.Popen([str(host/'Notepad4.exe'),'/n',str(p)])
 frame,editor=find_editor_windows(u,process,cb);time.sleep(.3);handle=k.OpenProcess(0x38,False,process.pid);remote=k.VirtualAllocEx(handle,None,65536,0x3000,4)
 def send(m,a=0,b=0):return u.SendMessageW(editor,m,a,b)
 def read(n):
  data=c.create_string_buffer(n);assert k.ReadProcessMemory(handle,remote,data,n,None);return data.raw.split(b'\0')[0].decode('utf-8')
 try:
  data=text.encode()+b'\0';assert k.WriteProcessMemory(handle,remote,data,len(data),None)
  send(2101);send(2037,65001);send(2004);send(2001,len(data)-1,remote);send(2160,len(data)-1,len(data)-1);send(2014);send(2352,0,-1)
  if len(fixture)>4 and fixture[4]:send(2171,1)
  for event in events:
   if isinstance(event,tuple) and event[0]=='show':
    data=event[1].encode()+b'\0';assert k.WriteProcessMemory(handle,remote,data,len(data),None)
    send(2160,2,2);send(2100,2,remote)
   elif isinstance(event,tuple):u.SendMessageW(editor,0x100,event[1],0)
   else:u.SendMessageW(editor,0x102,event,0)
  active=send(2102);items=[]
  if active:
   previous=-1
   for _ in range(10000):
    index=send(2445)
    if index==previous:break
    previous=index;size=send(2610,0,remote);items.append(read(size+1));u.SendMessageW(editor,0x100,0x28,0)
  send(2182,65536,remote)
  result={'extension':ext,'before':text,'events':events,'fill':fill,'readOnly':len(fixture)>4 and fixture[4],'latex':len(fixture)>5,'after':read(65536),'active':active,'items':items,'caret':send(2008),'anchor':send(2009)}
  send(2101);send(2176);send(2182,65536,remote);result['undo']={'text':read(65536),'caret':send(2008),'anchor':send(2009)};results.append(result)
 finally:
  send(2171,0);send(2101);send(2014);k.VirtualFreeEx(handle,remote,0,0x8000);k.CloseHandle(handle);u.SendMessageW(frame,0x10,0,0);process.wait(timeout=5)
(out/'original-automatic.json').write_text(json.dumps(results,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('captured',len(results),'original automatic cases')
