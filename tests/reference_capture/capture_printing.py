import argparse
import base64
import ctypes as c
import hashlib
import json
import shutil
import subprocess
import time
from ctypes import wintypes as w
from pathlib import Path

WM_COMMAND = 0x111
PAGE_SETUP = 40013
PRINT = 40014
SAVE_SETTINGS = 40444


class Native:
    def __init__(self, process):
        self.process = process
        self.user = c.WinDLL("user32", use_last_error=True)
        self.callback = c.WINFUNCTYPE(w.BOOL, w.HWND, c.c_ssize_t)
        self.user.EnumWindows.argtypes = [self.callback, c.c_ssize_t]
        self.user.EnumChildWindows.argtypes = [w.HWND, self.callback, c.c_ssize_t]
        self.user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
        self.user.GetWindow.argtypes = [w.HWND, w.UINT]
        self.user.GetWindow.restype = w.HWND
        self.user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, c.c_int]
        self.user.IsWindowVisible.argtypes = [w.HWND]
        self.user.IsWindowEnabled.argtypes = [w.HWND]
        self.user.GetDlgCtrlID.argtypes = [w.HWND]
        self.user.GetDlgItem.argtypes = [w.HWND, c.c_int]
        self.user.GetDlgItem.restype = w.HWND
        self.user.GetMenu.argtypes = [w.HWND]
        self.user.GetMenu.restype = w.HMENU
        self.user.GetSubMenu.argtypes = [w.HMENU, c.c_int]
        self.user.GetSubMenu.restype = w.HMENU
        self.user.GetMenuItemCount.argtypes = [w.HMENU]
        self.user.GetMenuStringW.argtypes = [w.HMENU, w.UINT, w.LPWSTR, c.c_int, w.UINT]
        self.user.GetMenuItemID.argtypes = [w.HMENU, c.c_int]
        self.send = self.user.SendMessageW
        self.send.argtypes = [w.HWND, w.UINT, c.c_size_t, c.c_ssize_t]
        self.send.restype = c.c_ssize_t
        self.post = self.user.PostMessageW
        self.post.argtypes = self.send.argtypes
        self.item = self.user.GetDlgItem

    def windows(self, parent=None, related=False, handles_only=False):
        result = []

        @self.callback
        def visit(window, _):
            pid = w.DWORD()
            self.user.GetWindowThreadProcessId(window, c.byref(pid))
            owner = self.user.GetWindow(window, 4) if related else None
            while owner and pid.value != self.process.pid:
                self.user.GetWindowThreadProcessId(owner, c.byref(pid))
                owner = self.user.GetWindow(owner, 4)
            if handles_only or pid.value == self.process.pid:
                result.append(window)
            return True

        if parent:
            self.user.EnumChildWindows(parent, visit, 0)
        else:
            self.user.EnumWindows(visit, 0)
        return result

    def class_name(self, window):
        text = c.create_unicode_buffer(128)
        self.user.GetClassNameW(window, text, len(text))
        return text.value

    def text(self, window):
        text = c.create_unicode_buffer(1024)
        self.send(window, 0xD, len(text), c.addressof(text))
        return text.value

    def set_text(self, window, value):
        text = c.create_unicode_buffer(str(value))
        self.send(window, 0xC, 0, c.addressof(text))

    def controls(self, dialog):
        return [
            dict(
                id=self.user.GetDlgCtrlID(child),
                cls=self.class_name(child),
                text=self.text(child),
                enabled=bool(self.user.IsWindowEnabled(child)),
                checked=self.send(child, 0xF0, 0, 0) if self.class_name(child) == "Button" else None,
            )
            for child in self.windows(dialog)
            if self.user.IsWindowVisible(child)
        ]

    def wait(self, predicate, description):
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            if result := predicate():
                return result
            if self.process.poll() is not None:
                break
            time.sleep(0.04)
        raise RuntimeError(f"Timed out waiting for {description}")

    def dialog(self, exclude=()):
        return self.wait(
            lambda: next(
                (
                    window
                    for window in self.windows()
                    if window not in exclude
                    and self.class_name(window) == "#32770"
                    and self.user.IsWindowVisible(window)
                ),
                None,
            ),
            "native dialog",
        )

    def open(self, frame, command):
        self.post(frame, WM_COMMAND, command, 0)
        try:
            return self.dialog()
        except RuntimeError:
            for window in self.windows(related=True):
                title = c.create_unicode_buffer(256)
                get_title = self.user.GetWindowTextW
                get_title.argtypes = [w.HWND, w.LPWSTR, c.c_int]
                get_title(window, title, len(title))
                pid = w.DWORD()
                self.user.GetWindowThreadProcessId(window, c.byref(pid))
                print(
                    "Owner diagnostic:",
                    pid.value,
                    self.process.pid,
                    self.class_name(window),
                    title.value,
                    bool(self.user.IsWindowEnabled(window)),
                    flush=True,
                )
            raise

    def respond(self, dialog, command):
        self.post(dialog, WM_COMMAND, command, self.item(dialog, command) or 0)

    def closed(self, dialog):
        self.wait(lambda: not self.user.IsWindowVisible(dialog), "dialog to close")

    def settings(self, dialog):
        return {
            **{
                key: self.send(self.item(dialog, control), 0x147, 0, 0)
                for key, control in [("header", 32), ("footer", 33), ("color", 34)]
            },
            **{
                key: self.text(self.item(dialog, control))
                for key, control in [("zoom", 30), ("left", 0x483), ("right", 0x484), ("top", 0x485), ("bottom", 0x486)]
            },
        }

    def menu(self, frame):
        menu = self.user.GetSubMenu(self.user.GetMenu(frame), 0)
        labels = []
        for index in range(self.user.GetMenuItemCount(menu)):
            text = c.create_unicode_buffer(256)
            self.user.GetMenuStringW(menu, index, text, len(text), 0x400)
            labels.append([self.user.GetMenuItemID(menu, index), text.value])
        return labels

    def automate_print(self, frame, action, output, trigger=None):
        self.wait(lambda: self.user.IsWindowEnabled(frame), "original frame ready for Print")
        window_handles = set(self.windows(handles_only=True))
        existing = ",".join(str(handle) for handle in sorted(window_handles))
        if trigger is None:
            self.post(frame, WM_COMMAND, PRINT, 0)
        else:
            trigger()
        proxy = self.wait(
            lambda: (
                not self.user.IsWindowEnabled(frame)
                and next(
                    (
                        window
                        for window in self.windows(related=True)
                        if window not in window_handles and self.class_name(window) == "ProxyModalWindow"
                    ),
                    None,
                )
            ),
            "new system Print dialog proxy blocking the original window",
        )
        print_pid = w.DWORD()
        self.user.GetWindowThreadProcessId(proxy, c.byref(print_pid))
        print("Print proxy association:", proxy, print_pid.value, "original process:", self.process.pid, flush=True)
        helper = Path(__file__).with_name("print_dialog_uia.ps1")
        command = (
            f"& {{\n{helper.read_text(encoding='utf-8')}\n}} -ProxyProcess {print_pid.value} "
            f"-Action {action} -Excluded '{existing}'"
        )
        encoded = base64.b64encode(command.encode("utf-16le")).decode("ascii")
        result = subprocess.run(
            ["powershell.exe", "-NoProfile", "-EncodedCommand", encoded],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=15,
        )
        (output / f"uia-{action}.stdout.json").write_bytes(result.stdout)
        (output / f"uia-{action}.stderr.log").write_bytes(result.stderr)
        if result.returncode:
            print(result.stderr.decode("utf-8", errors="replace"), flush=True)
            raise RuntimeError(f"Print UI Automation failed for {action}: {result.returncode}")
        return json.loads(result.stdout)


def add_unsaved_text(native, editor, text):
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.VirtualAllocEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD]
    kernel.VirtualAllocEx.restype = c.c_void_p
    kernel.WriteProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.c_void_p]
    kernel.VirtualFreeEx.argtypes = [w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    data = text.encode("utf-8")
    process = kernel.OpenProcess(0x28, False, native.process.pid)
    remote = None
    try:
        remote = kernel.VirtualAllocEx(process, None, len(data), 0x3000, 4)
        if not remote or not kernel.WriteProcessMemory(process, remote, data, len(data), None):
            raise c.WinError(c.get_last_error())
        native.send(editor, 2001, len(data), remote)
        return len(data)
    finally:
        if remote:
            kernel.VirtualFreeEx(process, remote, 0, 0x8000)
        if process:
            kernel.CloseHandle(process)


def print_pdf(native, dialog, pdf, rows):
    names = native.item(dialog, 0x471)
    if "Print to PDF" not in native.text(names):
        find = c.create_unicode_buffer("Microsoft Print to PDF")
        index = native.send(names, 0x158, c.c_size_t(-1).value, c.addressof(find))
        if index < 0:
            raise RuntimeError("Microsoft Print to PDF missing from original native printer dialog")
        native.send(names, 0x14E, index, 0)
        native.send(dialog, WM_COMMAND, 0x10000 | 0x471, names)
    native.respond(dialog, 1)
    save_pdf(native, pdf, rows, (dialog,))


def save_pdf(native, pdf, rows, exclude=()):
    save = native.dialog(exclude)
    rows["output_controls"] = native.controls(save)
    fields = [
        child
        for child in native.windows(save)
        if native.class_name(child) == "Edit"
        and native.user.IsWindowVisible(child)
        and native.user.IsWindowEnabled(child)
    ]
    if len(fields) != 1:
        raise RuntimeError(f"Expected one PDF filename field, got {rows['output_controls']}")
    native.set_text(fields[0], pdf)
    native.respond(save, 1)
    native.closed(save)
    native.wait(lambda: pdf.exists() and pdf.stat().st_size > 0, "original PDF output")


def capture(native, frame, editor, output, ini, rows, uia, range_only):
    rows["menu"] = native.menu(frame)
    setup = native.open(frame, PAGE_SETUP)
    rows["setup_controls"] = native.controls(setup)
    rows["defaults"] = native.settings(setup)
    native.set_text(native.item(setup, 30), "150%")
    native.send(native.item(setup, 32), 0x14E, 2, 0)
    native.respond(setup, 2)
    native.closed(setup)
    setup = native.open(frame, PAGE_SETUP)
    rows["after_cancel"] = native.settings(setup)
    assert rows["after_cancel"] == rows["defaults"], rows["after_cancel"]
    native.set_text(native.item(setup, 30), "150%")
    native.respond(setup, 1)
    native.closed(setup)
    setup = native.open(frame, PAGE_SETUP)
    rows["valid_zoom"] = native.settings(setup)
    assert rows["valid_zoom"]["zoom"] == "150%", rows["valid_zoom"]
    native.set_text(native.item(setup, 30), "invalid")
    native.respond(setup, 1)
    native.closed(setup)
    setup = native.open(frame, PAGE_SETUP)
    rows["invalid_zoom"] = native.settings(setup)
    assert rows["invalid_zoom"]["zoom"] == "100%", rows["invalid_zoom"]
    native.set_text(native.item(setup, 30), "125%")
    native.send(native.item(setup, 32), 0x14E, 2, 0)
    native.send(native.item(setup, 34), 0x14E, 3, 0)
    native.respond(setup, 1)
    native.closed(setup)
    confirmation = native.open(frame, SAVE_SETTINGS)
    rows["save_confirmation"] = native.text(confirmation)
    native.post(confirmation, 0x10, 0, 0)
    native.closed(confirmation)
    rows["saved"] = [line for line in ini.read_text(encoding="utf-16").splitlines() if line.startswith("Print")]

    prefix = "OutsideLine\r\nOutsideLeading "
    body = "".join(f"SelectedLine{index:03d} Unicode café\r\n" for index in range(120))
    suffix = "SelectedEnd OutsideTrailing\r\nOutsideLastLine\r\n"
    text = prefix + body + suffix
    end = add_unsaved_text(native, editor, text)
    assert end == native.send(editor, 2006, 0, 0), "original untitled editor accepted the unsaved fixture"
    native.send(editor, 2160, 0, 0)
    if range_only:
        rows["page_range"] = native.automate_print(frame, "PrintRange", output)
        try:
            save_pdf(native, output / "original-range-page-2.pdf", rows)
        finally:
            rows["range_return"] = {
                "frame_enabled": bool(native.user.IsWindowEnabled(frame)),
                "original_owned_windows": [
                    [handle, native.class_name(handle), native.text(handle)] for handle in native.windows(related=True)
                ],
            }
        return
    if uia:
        rows["print_no_selection"] = native.automate_print(frame, "Cancel", output)
        native.wait(lambda: native.user.IsWindowEnabled(frame), "original frame unblocked after Print Cancel")
    else:
        dialog = native.open(frame, PRINT)
        rows["print_no_selection"] = native.controls(dialog)
        native.respond(dialog, 2)
        native.closed(dialog)
    native.send(editor, 2160, len(prefix), len((prefix + body + "SelectedEnd").encode("utf-8")))

    def state():
        return [native.send(editor, command, 0, 0) for command in [2006, 2009, 2008, 2159, 2174, 2016]]

    rows["before"] = state()
    if uia:
        rows["print_selection"] = native.automate_print(frame, "Print", output)
        save_pdf(native, output / "original-selection.pdf", rows)
    else:
        dialog = native.open(frame, PRINT)
        rows["print_selection"] = native.controls(dialog)
        print_pdf(native, dialog, output / "original-selection.pdf", rows)
    rows["after"] = state()
    assert rows["before"] == rows["after"], (rows["before"], rows["after"])

    if uia:
        rows["selection_all_choice"] = native.automate_print(frame, "PrintAll", output)
        save_pdf(native, output / "original-selection-all.pdf", rows)
        rows["page_choices"] = native.automate_print(frame, "InspectPages", output)
        native.wait(
            lambda: native.user.IsWindowEnabled(frame), "original frame unblocked after inspecting page choices"
        )
        native.send(editor, 2160, 0, 0)
        rows["whole_document"] = native.automate_print(frame, "PrintWhole", output)
        save_pdf(native, output / "original-whole.pdf", rows)
        rows["page_range"] = native.automate_print(frame, "PrintRange", output)
        save_pdf(native, output / "original-range-page-2.pdf", rows)
    else:
        dialog = native.open(frame, PRINT)
        all_pages = next(
            item["id"]
            for item in rows["print_selection"]
            if item["cls"] == "Button"
            and item["text"].replace("&", "").split("(")[0].strip()
            in ("All", "All Pages", "全部", "所有页", "所有页面", "全部頁面")
        )
        native.send(native.item(dialog, all_pages), 0xF5, 0, 0)
        rows["selection_all_choice"] = native.controls(dialog)
        print_pdf(native, dialog, output / "original-selection-all.pdf", rows)


def capture_wrapped(native, frame, editor, output, rows):
    setup = native.open(frame, PAGE_SETUP)
    native.set_text(native.item(setup, 30), "125%")
    native.respond(setup, 1)
    native.closed(setup)
    text = "".join(f"PrintWrap{index:04d} " for index in range(1000, 2000))
    assert add_unsaved_text(native, editor, text) == native.send(editor, 2006, 0, 0)
    native.send(editor, 2051, 0, 0x2040DC)
    native.send(editor, 2052, 0, 0xFFE080)
    native.send(editor, 2160, 0, 0)
    rows["wrapped_document"] = native.automate_print(frame, "PrintWhole", output)
    save_pdf(native, output / "original-styled-wrap.pdf", rows)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--uia", action="store_true")
    exclusive = parser.add_mutually_exclusive_group()
    exclusive.add_argument("--wrapped-only", action="store_true", help="one native long-line job; requires --uia")
    exclusive.add_argument("--range-only", action="store_true", help="one native custom range job; requires --uia")
    args = parser.parse_args()
    if (args.range_only or args.wrapped_only) and not args.uia:
        parser.error("--range-only and --wrapped-only require --uia")
    output = args.output.resolve()
    output.mkdir(parents=True)
    binary = output / "Notepad4.exe"
    shutil.copyfile(args.executable, binary)
    ini = output / "Notepad4.ini"
    ini.write_text("[Settings]\nSaveSettings=0\nWordWrap=1\n", encoding="utf-16")
    process = subprocess.Popen([str(binary), "/n"], cwd=output)
    native = Native(process)
    rows = {
        "revision": "89b8b36e0ee2c23f73be922315f17261ccfbbe1a",
        "executable_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
    }
    frame = editor = None
    try:
        frame = native.wait(
            lambda: next((window for window in native.windows() if native.user.GetMenu(window)), None), "Notepad4 frame"
        )
        editor = native.wait(
            lambda: next(
                (window for window in native.windows(frame) if native.class_name(window) == "Scintilla"), None
            ),
            "Scintilla",
        )
        if args.wrapped_only:
            capture_wrapped(native, frame, editor, output, rows)
        else:
            capture(native, frame, editor, output, ini, rows, args.uia, args.range_only)
        print("Captured original Page Setup, Print and PDF output")
    finally:
        (output / "original.json").write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if process.poll() is None:
            for window in native.windows():
                if native.class_name(window) == "#32770" and native.user.IsWindowVisible(window):
                    native.post(window, 0x10, 0, 0)
            if editor:
                native.post(editor, 2014, 0, 0)
            if frame:
                native.post(frame, 0x10, 0, 0)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == "__main__":
    main()
