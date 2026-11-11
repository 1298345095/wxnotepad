"""Observe the original application's owned editor through its public messages."""
import ctypes as c
from ctypes import wintypes as w
from private_desktop import Process
import time

user = c.WinDLL("user32", use_last_error=True)
kernel = c.WinDLL("kernel32", use_last_error=True)
user.SendMessageW.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
user.SendMessageW.restype = c.c_ssize_t
user.PostMessageW.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
kernel.OpenProcess.restype = w.HANDLE
kernel.VirtualAllocEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD]
kernel.VirtualAllocEx.restype = c.c_void_p
kernel.WriteProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
kernel.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
kernel.VirtualFreeEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD]
kernel.CloseHandle.argtypes = [w.HANDLE]
CALLBACK = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
user.EnumDesktopWindows.argtypes = [w.HANDLE, CALLBACK, w.LPARAM]


class Original:
    def __init__(self, path, executable, desktop, desktop_name):
        arguments = [str(executable), "/n", "/ns", "/e", "utf-8"]
        if path:
            arguments.append(str(path))
        self.process = Process(arguments, desktop_name)
        self.hwnd = self.editor = None
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline and not self.editor:
            def window(hwnd, _):
                pid = w.DWORD()
                user.GetWindowThreadProcessId(hwnd, c.byref(pid))
                if pid.value == self.process.pid:
                    def child(control, _):
                        name = c.create_unicode_buffer(100)
                        user.GetClassNameW(control, name, 100)
                        if name.value == "Scintilla":
                            self.editor, self.hwnd = control, hwnd
                        return True
                    user.EnumChildWindows(hwnd, CALLBACK(child), 0)
                return True
            user.EnumDesktopWindows(desktop, CALLBACK(window), 0)
            time.sleep(.03)
        assert self.editor, "owned original editor appeared"
        self.handle = kernel.OpenProcess(0x38, False, self.process.pid)
        assert self.handle
        self.memory = kernel.VirtualAllocEx(self.handle, None, 1048576, 0x3000, 4)
        assert self.memory

    def send(self, message, first=0, second=0):
        return user.SendMessageW(self.editor, message, first, second)

    def command(self, identifier):
        user.SendMessageW(self.hwnd, 0x111, identifier, 0)

    def put(self, data):
        assert len(data) < 1048576
        buffer = c.create_string_buffer(data)
        assert kernel.WriteProcessMemory(self.handle, self.memory, buffer, len(data), None)
        return self.memory

    def text(self):
        length = self.send(2006)
        self.send(2182, length + 1, self.memory)
        buffer = c.create_string_buffer(length + 1)
        assert kernel.ReadProcessMemory(self.handle, self.memory, buffer, length + 1, None)
        return buffer.raw[:length]

    def reset(self, text=b"AoldZ", anchor=1, caret=4):
        self.send(2171, 0)
        self.send(2422, 0)
        self.send(2181, 0, self.put(b"\0"))
        self.send(2001, len(text), self.put(text))
        self.send(2160, anchor, caret)
        self.send(2175)
        self.send(2014)

    def state(self):
        return {"hex": self.text().hex(), "anchor": self.send(2009), "caret": self.send(2008),
                "modified": self.send(2159), "undo": self.send(2174), "redo": self.send(2016),
                "selections": self.send(2570), "main": self.send(2575), "selectionMode": self.send(2423),
                "codepage": self.send(2137), "eol": self.send(2030), "readonly": self.send(2140)}

    def close(self):
        self.send(2171, 0)
        self.send(2014)
        kernel.VirtualFreeEx(self.handle, self.memory, 0, 0x8000)
        kernel.CloseHandle(self.handle)
        user.PostMessageW(self.hwnd, 0x10, 0, 0)
        self.process.wait(timeout=10)
