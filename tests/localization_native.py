"""External native dialog/accessibility driver for owned packaged app processes."""
import ctypes
from ctypes import wintypes
import sys
import time


def plain(text):
    return text.replace('&', '').replace('_', '')


class Dialogs:
    def __init__(self):
        if sys.platform == 'win32':
            self.user = ctypes.WinDLL('user32', use_last_error=True)
            self.user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
            self.user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
            self.user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
            self.user.IsWindowVisible.argtypes = [wintypes.HWND]
            self.user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
            self.user.SendMessageW.restype = wintypes.LPARAM
            self.callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            self.user.EnumWindows.argtypes = [self.callback, wintypes.LPARAM]
            self.user.EnumChildWindows.argtypes = [wintypes.HWND, self.callback, wintypes.LPARAM]
            ctypes.OleDLL('ole32').CoInitialize(None)
        else:
            import gi
            gi.require_version('Atspi', '2.0')
            from gi.repository import Atspi
            self.atspi = Atspi
            Atspi.set_timeout(1000, 1000)

    def windows(self, process):
        if sys.platform != 'win32':
            result = []
            desktop = self.atspi.get_desktop(0)
            for index in range(desktop.get_child_count()):
                application = desktop.get_child_at_index(index)
                if application.get_process_id() == process.pid:
                    result.extend(application.get_child_at_index(i) for i in range(application.get_child_count()))
            return result
        result = []

        @self.callback
        def visit(window, unused):
            owner = wintypes.DWORD()
            self.user.GetWindowThreadProcessId(window, ctypes.byref(owner))
            if owner.value == process.pid and self.user.IsWindowVisible(window):
                result.append(window)
            return True

        self.user.EnumWindows(visit, 0)
        return result

    def title(self, window):
        if sys.platform != 'win32':
            return window.get_name()
        text = ctypes.create_unicode_buffer(4096)
        self.user.GetWindowTextW(window, text, len(text))
        return text.value

    def children(self, window):
        if sys.platform != 'win32':
            result = []
            for index in range(window.get_child_count()):
                child = window.get_child_at_index(index)
                result.append(child)
                result.extend(self.children(child))
            return result
        result = []

        @self.callback
        def visit(child, unused):
            if self.user.IsWindowVisible(child):
                result.append(child)
            return True

        self.user.EnumChildWindows(window, visit, 0)
        return result

    def texts(self, window):
        texts = [self.title(child) for child in self.children(window)]
        if sys.platform == 'win32':
            texts.extend(self.accessible_names(window, entries_only=False))
        return texts

    def wait(self, process, title):
        deadline = time.monotonic() + 10
        observed = []
        while time.monotonic() < deadline:
            assert process.poll() is None, process.returncode
            observed = self.windows(process)
            for window in observed:
                if self.title(window) == title:
                    return window
            time.sleep(.03)
        raise AssertionError(f'Native dialog {title!r} not shown: {[self.title(w) for w in observed]}')

    def click(self, window, caption):
        if sys.platform == 'win32':
            for child in self.children(window):
                if plain(self.title(child)) == caption:
                    self.user.SendMessageW(child, 0x00F5, 0, 0)  # BM_CLICK
                    return
        else:
            for child in self.children(window):
                if child.get_role() == self.atspi.Role.PUSH_BUTTON and plain(child.get_name()) == caption:
                    action = child.get_action_iface()
                    assert action and action.do_action(0), caption
                    return
        raise AssertionError(f'Native button {caption!r} not present: {self.texts(window)}')

    def cancel(self, window):
        if sys.platform == 'win32':
            self.user.SendMessageW(window, 0x0111, 2, 0)  # Native IDCANCEL.
        else:
            for caption in ('Cancel', '取消'):
                if any(plain(text) == caption for text in self.texts(window)):
                    self.click(window, caption)
                    return
            raise AssertionError(self.texts(window))

    def set_input(self, window, name, text):
        if sys.platform != 'win32':
            for child in self.children(window):
                if child.get_role() in (self.atspi.Role.TEXT, self.atspi.Role.ENTRY) and child.get_name() == name:
                    assert child.get_editable_text_iface().set_text_contents(text)
                    return
        else:
            entries = []
            for child in self.children(window):
                kind = ctypes.create_unicode_buffer(256)
                self.user.GetClassNameW(child, kind, len(kind))
                if kind.value.lower() == 'edit':
                    entries.append(child)
            names = self.accessible_names(window)
            self.user.SetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPCWSTR]
            for child, accessible_name in zip(entries, names):
                if accessible_name == name:
                    assert self.user.SetWindowTextW(child, text)
                    return
        raise AssertionError(f'Accessible input {name!r} absent')

    def key(self, app, key):
        if sys.platform == 'win32':
            self.user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
            assert key == 'F10', key
            handle = int(app.state['editor_handle'])
            assert self.user.PostMessageW(handle, 0x0100, 0x79, 1)
            assert self.user.PostMessageW(handle, 0x0101, 0x79, 0xc0000001)
        else:
            import os
            import subprocess
            from pathlib import Path
            if app.state['backend'] == 'GdkX11Display':
                windows = subprocess.check_output(['xdotool', 'search', '--onlyvisible', '--pid',
                                                   str(app.process.pid)], text=True).splitlines()
                assert windows
                subprocess.run(['xdotool', 'windowfocus', '--sync', windows[0]], check=True)
            focus = Path(os.environ['WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS'])
            pending = focus.with_suffix('.localization-pending')
            pending.write_text('key:' + key, encoding='utf-8')
            pending.replace(focus)

    def resize(self, app, width, height):
        if sys.platform == 'win32':
            self.user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int,
                                               ctypes.c_int, ctypes.c_int, wintypes.UINT]
            assert self.user.SetWindowPos(int(app.state['frame_handle']), None, 0, 0, width, height, 0x0002 | 0x0004)
        else:
            windows = [window for window in self.windows(app.process) if self.title(window) == app.state['title']]
            assert len(windows) == 1
            assert windows[0].get_component_iface().set_size(width, height)

    def answer(self, process, choice, title='wxnotepad'):
        window = self.wait(process, title)
        if sys.platform == 'win32':
            # Same native task-dialog buttons as FileCommandDriver, scoped to
            # the independently launched app's visible dialog.
            self.user.SendMessageW(window, 0x0400 + 102, {'yes': 6, 'no': 7, 'cancel': 2, 'ok': 1}[choice], 0)
        else:
            captions = {'yes': ('Yes', '是(Y)', '是'), 'no': ('No', '否(N)', '否'),
                        'cancel': ('Cancel', '取消'), 'ok': ('OK', '确定', '確定')}
            for child in self.children(window):
                if child.get_role() == self.atspi.Role.PUSH_BUTTON and plain(child.get_name()) in captions[choice]:
                    assert child.get_action_iface().do_action(0), choice
                    return window
            raise AssertionError((choice, self.texts(window)))
        return window

    def accessible_names(self, window, entries_only=True):
        if sys.platform != 'win32':
            return [child.get_name() for child in self.children(window)
                    if child.get_role() in (self.atspi.Role.TEXT, self.atspi.Role.ENTRY)]
        # Query the actual native MSAA client accessible object. Its public
        # name is separate from wxWindow's stable internal SetName selector.
        class GUID(ctypes.Structure):
            _fields_ = [('data1', wintypes.DWORD), ('data2', wintypes.WORD), ('data3', wintypes.WORD),
                        ('data4', ctypes.c_ubyte * 8)]

        class VariantData(ctypes.Union):
            _fields_ = [('integer', ctypes.c_long), ('storage', ctypes.c_void_p * 2)]

        class Variant(ctypes.Structure):
            _fields_ = [('type', ctypes.c_ushort), ('reserved', ctypes.c_ushort * 3), ('value', VariantData)]

        oleacc = ctypes.WinDLL('oleacc')
        oleaut = ctypes.OleDLL('oleaut32')
        oleacc.AccessibleObjectFromWindow.argtypes = [wintypes.HWND, wintypes.DWORD,
                                                     ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p)]
        oleaut.SysFreeString.argtypes = [ctypes.c_void_p]
        oleaut.SysFreeString.restype = None
        iid = GUID(0x618736e0, 0x3c3d, 0x11cf, (ctypes.c_ubyte * 8)(0x81, 0x0c, 0, 0xaa, 0, 0x38, 0x9b, 0x71))
        result = []

        def table_of(accessible):
            return ctypes.cast(accessible, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p))).contents

        def release(accessible):
            ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(table_of(accessible)[2])(accessible)

        def names(accessible, descend):
            table = table_of(accessible)
            get_name = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, Variant,
                                         ctypes.POINTER(ctypes.c_void_p))(table[10])
            get_count = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.POINTER(ctypes.c_long))(table[8])
            get_child = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, Variant,
                                          ctypes.POINTER(ctypes.c_void_p))(table[9])
            count = ctypes.c_long()
            if descend:
                get_count(accessible, ctypes.byref(count))
            for identity in range(count.value + 1):
                value = Variant()
                value.type = 3  # VT_I4; zero is CHILDID_SELF.
                value.value.integer = identity
                returned = ctypes.c_void_p()
                if get_name(accessible, value, ctypes.byref(returned)) == 0 and returned:
                    result.append(ctypes.wstring_at(returned))
                    oleaut.SysFreeString(returned)
                if identity == 0:
                    continue
                dispatch = ctypes.c_void_p()
                if get_child(accessible, value, ctypes.byref(dispatch)) != 0 or not dispatch:
                    continue
                query = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.POINTER(GUID),
                                           ctypes.POINTER(ctypes.c_void_p))(table_of(dispatch)[0])
                child_accessible = ctypes.c_void_p()
                try:
                    if query(dispatch, ctypes.byref(iid), ctypes.byref(child_accessible)) == 0:
                        try:
                            names(child_accessible, True)
                        finally:
                            release(child_accessible)
                finally:
                    release(dispatch)

        for child in ([window] + self.children(window)):
            name = ctypes.create_unicode_buffer(256)
            self.user.GetClassNameW(child, name, len(name))
            if entries_only and name.value.lower() != 'edit':
                continue
            accessible = ctypes.c_void_p()
            if oleacc.AccessibleObjectFromWindow(child, 0xfffffffc, ctypes.byref(iid), ctypes.byref(accessible)) != 0:
                continue
            try:
                names(accessible, not entries_only)
            finally:
                release(accessible)
        return result
