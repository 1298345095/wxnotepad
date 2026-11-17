#pragma once

#include "editor/editor_control.h"
#include "native_paint_helpers.h"
#include <wx/wx.h>
#include <stdexcept>

namespace NativeFold {
using Scintilla::Message;
inline void Paint(EditorControl* editor) {
    NativePaint::Complete(editor);
}
inline void MarginClick(EditorControl* editor, int line, const char* marginMessage) {
    using enum Scintilla::Message;
    const auto width = editor->Send(GetMarginWidthN, 2);
    if (width <= 0) throw std::runtime_error(marginMessage);
    const auto position = editor->Send(PositionFromLine, line);
    const auto x = static_cast<int>(editor->Send(GetMarginWidthN, 0) + editor->Send(GetMarginWidthN, 1) + width / 2);
    const int y = static_cast<int>(editor->Send(PointYFromPosition, 0, position) + editor->Send(TextHeight, line) / 2);
    if (y < 0 || y >= editor->GetClientSize().y)
        throw std::runtime_error("native click must target an on-screen document line");
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(editor->GetHandle());
    const LPARAM point = MAKELPARAM(x, y);
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(handle, WM_LBUTTONUP, 0, point);
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    for (const auto type : {GDK_BUTTON_PRESS, GDK_BUTTON_RELEASE}) {
        auto* event = gdk_event_new(type);
        event->button.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
        event->button.send_event = TRUE;
        event->button.time = GDK_CURRENT_TIME;
        event->button.x = x;
        event->button.y = y;
        event->button.button = 1;
        if (type == GDK_BUTTON_RELEASE) event->button.state = GDK_BUTTON1_MASK;
        auto* display = gtk_widget_get_display(widget);
        gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(display)));
        gtk_widget_event(widget, event);
        gdk_event_free(event);
    }
#endif
    Paint(editor);
}
} // namespace NativeFold
