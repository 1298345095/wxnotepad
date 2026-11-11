"""Capture pinned original completion dialog normalization on an owned desktop.

Usage: python tests/reference_capture/capture_word_completion.py ORIGINAL_EXE OUTPUT_DIR
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

u.IsWindowVisible.argtypes=[w.HWND]
u.GetDlgItem.argtypes=[w.HWND,c.c_int];u.GetDlgItem.restype=w.HWND
u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
u.SetWindowTextW.argtypes=[w.HWND,w.LPCWSTR]
u.PostMessageW.argtypes=u.SendMessageW.argtypes
results=[]
for value in ['0','-1','invalid','12suffix','4294967295','2147483648','9999999999999']:
 process=subprocess.Popen([str(host/'Notepad4.exe'),'/n'])
 frame,editor=find_editor_windows(u,process,cb)
 def dialog():
  u.PostMessageW(frame,0x111,40450,0)
  time.sleep(.2)
  for _ in range(100):
   found=[]
   @cb
   def visit(hwnd,_):
    pid=w.DWORD();u.GetWindowThreadProcessId(hwnd,c.byref(pid));name=c.create_unicode_buffer(128);u.GetClassNameW(hwnd,name,128)
    if pid.value==process.pid and name.value=='#32770' and u.IsWindowVisible(hwnd):found.append(hwnd)
    return True
   u.EnumWindows(visit,0)
   if found:return found[0]
   time.sleep(.02)
  raise RuntimeError('no owned settings dialog')
 try:
  d=dialog()
  for id in [151,152,153,157]:
   data=c.create_unicode_buffer(value);u.SendMessageW(u.GetDlgItem(d,id),12,0,c.addressof(data))
  u.SendMessageW(d,0x111,1,0)
  d=dialog();row={'input':value}
  for id in [151,152,153,157]:
   text=c.create_unicode_buffer(128);u.SendMessageW(u.GetDlgItem(d,id),13,128,c.addressof(text));row[str(id)]=text.value
  results.append(row);u.SendMessageW(d,0x111,2,0)
 finally:u.SendMessageW(frame,0x10,0,0);process.wait(timeout=5)
(out/'original-dialog.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
print('captured original dialog numeric normalization')
