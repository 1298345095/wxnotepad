"""Launch owned native processes with an explicit Win32 STARTUPINFO desktop."""
import ctypes as c
from ctypes import wintypes as w
import subprocess

kernel = c.WinDLL('kernel32', use_last_error=True)
class Startup(c.Structure):
    _fields_ = [('cb',w.DWORD),('lpReserved',w.LPWSTR),('lpDesktop',w.LPWSTR),('lpTitle',w.LPWSTR),('dwX',w.DWORD),('dwY',w.DWORD),('dwXSize',w.DWORD),('dwYSize',w.DWORD),('dwXCountChars',w.DWORD),('dwYCountChars',w.DWORD),('dwFillAttribute',w.DWORD),('dwFlags',w.DWORD),('wShowWindow',w.WORD),('cbReserved2',w.WORD),('lpReserved2',c.POINTER(w.BYTE)),('hStdInput',w.HANDLE),('hStdOutput',w.HANDLE),('hStdError',w.HANDLE)]
class ProcessInfo(c.Structure):
    _fields_=[('hProcess',w.HANDLE),('hThread',w.HANDLE),('dwProcessId',w.DWORD),('dwThreadId',w.DWORD)]
kernel.CreateProcessW.argtypes=[w.LPCWSTR,w.LPWSTR,c.c_void_p,c.c_void_p,w.BOOL,w.DWORD,c.c_void_p,w.LPCWSTR,c.POINTER(Startup),c.POINTER(ProcessInfo)]
kernel.WaitForSingleObject.argtypes=[w.HANDLE,w.DWORD]
kernel.GetExitCodeProcess.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]
kernel.CloseHandle.argtypes=[w.HANDLE]
class Process:
    def __init__(self,args,desktop,cwd=None):
        startup=Startup();startup.cb=c.sizeof(startup);startup.lpDesktop=desktop
        info=ProcessInfo();command=c.create_unicode_buffer(subprocess.list2cmdline([str(a) for a in args]))
        if not kernel.CreateProcessW(None,command,None,None,False,0,None,str(cwd) if cwd else None,c.byref(startup),c.byref(info)):raise c.WinError(c.get_last_error())
        kernel.CloseHandle(info.hThread);self.handle=info.hProcess;self.pid=info.dwProcessId
    def wait(self,timeout=120):
        result=kernel.WaitForSingleObject(self.handle,int(timeout*1000))
        if result!=0:raise TimeoutError('owned native process did not complete')
        code=w.DWORD();assert kernel.GetExitCodeProcess(self.handle,c.byref(code));kernel.CloseHandle(self.handle);return code.value
