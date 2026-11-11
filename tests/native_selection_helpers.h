#pragma once
#include "native_search_helpers.h"
namespace NativeSelection {
inline void Mouse(EditorControl* editor, wxEventType type, wxPoint point, bool control = false, bool alt = false) {
#ifdef __WXMSW__
    BYTE previous[256];
    if (!GetKeyboardState(previous)) throw std::runtime_error("read owned native test thread keyboard state");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    modified[VK_MENU] = alt ? 0x80 : 0;
    if (!SetKeyboardState(modified)) throw std::runtime_error("set owned native test thread Alt state");
    const UINT message = type == wxEVT_LEFT_DOWN ? WM_LBUTTONDOWN : type == wxEVT_LEFT_UP ? WM_LBUTTONUP : WM_MOUSEMOVE;
    const WPARAM flags = (type == wxEVT_LEFT_UP ? 0 : MK_LBUTTON) | (control ? MK_CONTROL : 0);
    SendMessageW(static_cast<HWND>(editor->GetHandle()), message, flags, MAKELPARAM(point.x, point.y));
    SetKeyboardState(previous);
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    const auto nativeType = type == wxEVT_LEFT_DOWN ? GDK_BUTTON_PRESS
                            : type == wxEVT_LEFT_UP ? GDK_BUTTON_RELEASE
                                                    : GDK_MOTION_NOTIFY;
    auto* event = gdk_event_new(nativeType);
    const guint modifiers =
        (control ? GDK_CONTROL_MASK : 0) | (alt ? GDK_MOD1_MASK : 0) | (type == wxEVT_LEFT_DOWN ? 0 : GDK_BUTTON1_MASK);
    if (type == wxEVT_MOTION) {
        event->motion.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
        event->motion.send_event = TRUE;
        event->motion.time = GDK_CURRENT_TIME;
        event->motion.x = point.x;
        event->motion.y = point.y;
        event->motion.state = modifiers;
    } else {
        event->button.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
        event->button.send_event = TRUE;
        event->button.time = GDK_CURRENT_TIME;
        event->button.x = point.x;
        event->button.y = point.y;
        event->button.button = 1;
        event->button.state = modifiers;
    }
    auto* display = gtk_widget_get_display(widget);
    gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(display)));
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
}
} // namespace NativeSelection
