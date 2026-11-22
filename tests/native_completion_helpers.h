#pragma once
#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include <string>
#include <vector>
#ifdef __WXGTK__
#include <gdk/gdkkeysyms.h>
#endif
namespace NativeCompletion {
inline void Key(EditorControl* editor, int key) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYDOWN, key, 0);
#elif defined(__WXGTK__)
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->key.send_event = TRUE;
    event->key.keyval = key;
    gtk_widget_event(editor->GetConnectWidget(), event);
    gdk_event_free(event);
#endif
    wxYield();
}
inline std::vector<std::string> Candidates(EditorControl* editor) {
    using Scintilla::Message;
    std::vector<std::string> result;
    intptr_t previous = -1;
    while (editor->Send(Message::AutoCActive)) {
        const auto index = editor->Send(Message::AutoCGetCurrent);
        if (index == previous) break;
        previous = index;
        const auto length = editor->Send(Message::AutoCGetCurrentText);
        std::string word(static_cast<size_t>(length) + 1, '\0');
        editor->Send(Message::AutoCGetCurrentText, 0, reinterpret_cast<intptr_t>(word.data()));
        word.pop_back();
        result.push_back(word);
#ifdef __WXMSW__
        Key(editor, VK_DOWN);
#else
        Key(editor, GDK_KEY_Down);
#endif
    }
    return result;
}
inline void Wheel(EditorControl* editor) {
#ifdef __WXMSW__
    const auto point = editor->ClientToScreen(wxPoint(80, 80));
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_MOUSEWHEEL, MAKEWPARAM(0, 120),
                 MAKELPARAM(point.x, point.y));
#elif defined(__WXGTK__)
    auto* event = gdk_event_new(GDK_SCROLL);
    event->scroll.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->scroll.send_event = TRUE;
    event->scroll.direction = GDK_SCROLL_UP;
    event->scroll.x = 80;
    event->scroll.y = 80;
    gdk_event_set_device(
        event, gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(editor->GetConnectWidget()))));
    gtk_widget_event(editor->GetConnectWidget(), event);
    gdk_event_free(event);
#endif
    wxYield();
}
inline void DoubleClick(wxWindow* list, const wxPoint& point) {
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(list->GetHandle());
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
    SendMessageW(handle, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
    SendMessageW(handle, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(point.x, point.y));
#elif defined(__WXGTK__)
    auto* widget = list->GetConnectWidget();
    for (auto type : {GDK_BUTTON_PRESS, GDK_BUTTON_RELEASE, GDK_2BUTTON_PRESS}) {
        auto* event = gdk_event_new(type);
        event->button.window = static_cast<GdkWindow*>(g_object_ref(list->GTKGetDrawingWindow()));
        event->button.send_event = TRUE;
        event->button.button = 1;
        event->button.x = point.x;
        event->button.y = point.y;
        gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget))));
        gtk_widget_event(widget, event);
        gdk_event_free(event);
    }
#endif
    wxYield();
}
} // namespace NativeCompletion
