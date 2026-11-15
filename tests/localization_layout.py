"""Inspect and capture the actual visible native dialog layout externally."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import struct
import sys
import zlib

TEXT_ROUNDING_PIXELS = 2


def screenshot_windows(user, window, destination):
    gdi = ctypes.WinDLL('gdi32', use_last_error=True)
    user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.GetWindowDC.argtypes = [wintypes.HWND]
    user.GetWindowDC.restype = wintypes.HDC
    user.ReleaseDC.argtypes = [wintypes.HWND, wintypes.HDC]
    user.PrintWindow.argtypes = [wintypes.HWND, wintypes.HDC, wintypes.UINT]
    gdi.CreateCompatibleDC.argtypes = [wintypes.HDC]
    gdi.CreateCompatibleDC.restype = wintypes.HDC
    gdi.CreateCompatibleBitmap.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int]
    gdi.CreateCompatibleBitmap.restype = wintypes.HBITMAP
    gdi.SelectObject.argtypes = [wintypes.HDC, wintypes.HANDLE]
    gdi.SelectObject.restype = wintypes.HANDLE
    gdi.DeleteObject.argtypes = [wintypes.HANDLE]
    gdi.DeleteDC.argtypes = [wintypes.HDC]
    gdi.GetDIBits.argtypes = [wintypes.HDC, wintypes.HBITMAP, wintypes.UINT, wintypes.UINT,
                              ctypes.c_void_p, ctypes.c_void_p, wintypes.UINT]
    rect = wintypes.RECT()
    assert user.GetWindowRect(window, ctypes.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    dc = user.GetWindowDC(window)
    memory = gdi.CreateCompatibleDC(dc)
    bitmap = gdi.CreateCompatibleBitmap(dc, width, height)
    previous = gdi.SelectObject(memory, bitmap)
    try:
        assert user.PrintWindow(window, memory, 2), 'Capture the real shown native dialog'
        gdi.SelectObject(memory, previous)
        header = struct.pack('<IiiHHIIiiII', 40, width, -height, 1, 32, 0, width * height * 4, 0, 0, 0, 0)
        pixels = ctypes.create_string_buffer(width * height * 4)
        assert gdi.GetDIBits(memory, bitmap, 0, height, pixels, ctypes.create_string_buffer(header), 0) == height
        raw = pixels.raw
        rows = []
        for row in range(height):
            bgr = raw[row * width * 4:(row + 1) * width * 4]
            rgb = bytearray(width * 3)
            rgb[0::3], rgb[1::3], rgb[2::3] = bgr[2::4], bgr[1::4], bgr[0::4]
            rows.append(b'\0' + rgb)

        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

        destination.write_bytes(b'\x89PNG\r\n\x1a\n' +
                                chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                                chunk(b'IDAT', zlib.compress(b''.join(rows))) + chunk(b'IEND', b''))
    finally:
        gdi.DeleteObject(bitmap)
        gdi.DeleteDC(memory)
        user.ReleaseDC(window, dc)


def capture_and_check(dialogs, app, window, destination, system_dialog=False):
    """Check visible controls and measured text, preserving a reviewable image."""
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    records = []
    if sys.platform != 'win32':
        import gi
        gi.require_version('Gdk', '3.0')
        gi.require_version('Pango', '1.0')
        gi.require_version('PangoCairo', '1.0')
        from gi.repository import Gdk, Pango, PangoCairo
        # Both backends run on a private Xvfb display; for Wayland this captures
        # the nested compositor's actual presented view, not another desktop.
        Gdk.set_allowed_backends('x11')
        if not hasattr(capture_and_check, '_display'):
            capture_and_check._display = Gdk.Display.open(os.environ['DISPLAY'])
        display = capture_and_check._display
        assert display
        root = display.get_default_screen().get_root_window()
        pixbuf = Gdk.pixbuf_get_from_window(root, 0, 0, root.get_width(), root.get_height())
        assert pixbuf and pixbuf.savev(str(destination), 'png', [], [])
        extent = window.get_component_iface().get_extents(dialogs.atspi.CoordType.WINDOW)
        context = PangoCairo.FontMap.get_default().create_context()
        PangoCairo.context_set_resolution(context, float(app.state['dpi']))
        for child in ([] if system_dialog else dialogs.children(window)):
            if not child.get_state_set().contains(dialogs.atspi.StateType.SHOWING):
                continue
            component = child.get_component_iface()
            if not component:
                continue
            rect = component.get_extents(dialogs.atspi.CoordType.WINDOW)
            if rect.width <= 0 or rect.height <= 0:
                continue
            assert rect.x >= 0 and rect.y >= 0 and rect.x + rect.width <= extent.width + TEXT_ROUNDING_PIXELS and \
                rect.y + rect.height <= extent.height + TEXT_ROUNDING_PIXELS, (child.get_name(), rect, extent)
            record = {'role': child.get_role_name(), 'name': child.get_name(),
                      'bounds': [rect.x, rect.y, rect.width, rect.height]}
            if child.get_role() == dialogs.atspi.Role.LABEL:
                text_interface = child.get_text_iface()
                text = dialogs.atspi.Text.get_text(child, 0, -1) if text_interface else child.get_name()
                if text:
                    attributes = dialogs.atspi.Text.get_default_attributes(child) if text_interface else {}
                    font = Pango.FontDescription()
                    font.set_family(attributes.get('family-name', 'Sans'))
                    font.set_size(round(float(attributes.get('size', '10')) * Pango.SCALE))
                    layout = Pango.Layout.new(context)
                    layout.set_font_description(font)
                    layout.set_text(text, -1)
                    layout.set_width(rect.width * Pango.SCALE)
                    layout.set_wrap(Pango.WrapMode.WORD_CHAR)
                    width, height = layout.get_pixel_size()
                    record['text_extent'] = [width, height]
                    assert height <= rect.height + TEXT_ROUNDING_PIXELS, (text, record, attributes)
            records.append(record)
    else:
        user = dialogs.user
        screenshot_windows(user, window, destination)
        user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        user.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
        client = wintypes.RECT()
        origin = wintypes.POINT()
        assert user.GetClientRect(window, ctypes.byref(client)) and user.ClientToScreen(window, ctypes.byref(origin))
        gdi = ctypes.WinDLL('gdi32', use_last_error=True)
        user.GetDC.argtypes = [wintypes.HWND]
        user.GetDC.restype = wintypes.HDC
        gdi.SelectObject.argtypes = [wintypes.HDC, wintypes.HANDLE]
        gdi.SelectObject.restype = wintypes.HANDLE
        class LogFont(ctypes.Structure):
            _fields_ = [('height', wintypes.LONG), ('width', wintypes.LONG), ('escapement', wintypes.LONG),
                        ('orientation', wintypes.LONG), ('weight', wintypes.LONG), ('italic', ctypes.c_ubyte),
                        ('underline', ctypes.c_ubyte), ('strikeout', ctypes.c_ubyte), ('charset', ctypes.c_ubyte),
                        ('out_precision', ctypes.c_ubyte), ('clip_precision', ctypes.c_ubyte),
                        ('quality', ctypes.c_ubyte), ('pitch', ctypes.c_ubyte), ('face', ctypes.c_wchar * 32)]
        gdi.GetObjectW.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p]
        gdi.CreateFontIndirectW.argtypes = [ctypes.POINTER(LogFont)]
        gdi.CreateFontIndirectW.restype = wintypes.HANDLE
        gdi.DeleteObject.argtypes = [wintypes.HANDLE]
        user.DrawTextW.argtypes = [wintypes.HDC, wintypes.LPCWSTR, ctypes.c_int,
                                   ctypes.POINTER(wintypes.RECT), wintypes.UINT]
        user.GetWindowLongPtrW.argtypes = [wintypes.HWND, ctypes.c_int]
        user.GetWindowLongPtrW.restype = wintypes.LPARAM
        for child in ([] if system_dialog else dialogs.children(window)):
            rect = wintypes.RECT()
            assert user.GetWindowRect(child, ctypes.byref(rect))
            width, height = rect.right - rect.left, rect.bottom - rect.top
            x, y = rect.left - origin.x, rect.top - origin.y
            assert x >= 0 and y >= 0 and x + width <= client.right and y + height <= client.bottom, (dialogs.title(child), rect, client)
            kind = ctypes.create_unicode_buffer(256)
            user.GetClassNameW(child, kind, len(kind))
            text = dialogs.title(child)
            record = {'class': kind.value, 'name': text, 'bounds': [x, y, width, height]}
            if text and kind.value in ('Static', 'Button'):
                dc = user.GetDC(child)
                font = user.SendMessageW(child, 0x0031, 0, 0)  # WM_GETFONT.
                description = LogFont()
                assert font and gdi.GetObjectW(font, ctypes.sizeof(description), ctypes.byref(description))
                local_font = gdi.CreateFontIndirectW(ctypes.byref(description))
                assert local_font
                previous = gdi.SelectObject(dc, local_font)
                assert previous
                record['font'] = {'face': description.face, 'height': description.height, 'weight': description.weight}
                measured = wintypes.RECT(0, 0, width, 0)
                try:
                    visible = text.replace('&', '')
                    static_type = user.GetWindowLongPtrW(child, -16) & 0x1F  # GWL_STYLE / SS_TYPEMASK.
                    single_line = '\n' not in visible and (kind.value == 'Button' or static_type in (0x0B, 0x0C))
                    mode = 0x0020 if single_line else 0x0010  # DT_SINGLELINE or DT_WORDBREAK.
                    assert user.DrawTextW(dc, visible, -1, ctypes.byref(measured), 0x0400 | 0x0800 | mode)
                    record['text_extent'] = [measured.right, measured.bottom]
                    assert measured.bottom <= height + TEXT_ROUNDING_PIXELS, record
                    if single_line:
                        assert measured.right <= width + TEXT_ROUNDING_PIXELS, record
                finally:
                    if previous:
                        gdi.SelectObject(dc, previous)
                    gdi.DeleteObject(local_font)
                    user.ReleaseDC(child, dc)
            records.append(record)
    destination.with_suffix('.json').write_text(json.dumps({'dpi': app.state['dpi'], 'controls': records},
                                                          ensure_ascii=False, indent=2), encoding='utf-8')
    return records
