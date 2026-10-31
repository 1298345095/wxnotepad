#pragma once

#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "native_dialog_helpers.h"
#include <wx/wx.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>

// Native operations target only widgets owned by each independent search test.
// Test cases and expected document bytes remain in their own GUI processes.
namespace NativeSearch {
#ifdef __WXMSW__
inline bool WindowsShortcut(wxWindow* window, int key, std::initializer_list<std::pair<int, BYTE>> modifiers) {
    BYTE previous[256];
    if (!GetKeyboardState(previous)) throw std::runtime_error("must read this test thread's keyboard state");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    for (const auto& [modifier, state] : modifiers)
        modified[modifier] = state;
    if (!SetKeyboardState(modified)) throw std::runtime_error("must set this test thread's shortcut modifiers");
    MSG message{};
    message.hwnd = static_cast<HWND>(window->GetHandle());
    message.message = WM_KEYDOWN;
    message.wParam = key;
    const bool handled = wxGetTopLevelParent(window)->MSWTranslateMessage(&message);
    SetKeyboardState(previous);
    return handled;
}
#endif
inline void Command(EditorFrame* frame, int id) {
    // Opening a native menu refreshes command enablement before dispatch. Do
    // the same when sending a command directly to this test's owned frame.
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    auto* item = frame->GetMenuBar()->FindItem(id);
    if (!item) throw std::runtime_error("native menu command must exist");
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(frame->GetHandle()), WM_COMMAND, MAKEWPARAM(id, 0), 0);
#elif defined(__WXGTK__)
    gtk_menu_item_activate(GTK_MENU_ITEM(item->GetMenuItem()));
#else
    wxCommandEvent command(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(command);
#endif
    wxYield();
}
inline void Toggle(wxCheckBox* option, bool checked) {
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(option->GetHandle());
    SendMessageW(handle, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(static_cast<HWND>(option->GetParent()->GetHandle()), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(handle), BN_CLICKED), reinterpret_cast<LPARAM>(handle));
#elif defined(__WXGTK__)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(option->GetHandle()), checked);
#endif
    wxYield();
}
inline void Click(wxWindow* dialog, int id) {
    auto* button = wxDynamicCast(dialog->FindWindow(id), wxButton);
    if (!button) throw std::runtime_error("native dialog button must exist");
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(button->GetHandle());
    // Notify the owned dialog directly: BM_CLICK requires activating an inactive
    // dialog, which would steal desktop focus.
    SendMessageW(static_cast<HWND>(dialog->GetHandle()), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(handle), BN_CLICKED),
                 reinterpret_cast<LPARAM>(handle));
#elif defined(__WXGTK__)
    gtk_button_clicked(GTK_BUTTON(button->GetHandle()));
#endif
    wxYield();
}
inline void Selection(EditorControl* editor, intptr_t start, intptr_t end, const char* message) {
    const auto actualStart = editor->Send(Scintilla::Message::GetSelectionStart);
    const auto actualEnd = editor->Send(Scintilla::Message::GetSelectionEnd);
    if (actualStart != start || actualEnd != end)
        std::fprintf(stderr, "Range: %lld..%lld; expected %lld..%lld\n", static_cast<long long>(actualStart),
                     static_cast<long long>(actualEnd), static_cast<long long>(start), static_cast<long long>(end));
    if (actualStart != start || actualEnd != end) throw std::runtime_error(message);
}
inline std::string Text(EditorControl* editor) {
    std::string text(static_cast<size_t>(editor->Send(Scintilla::Message::GetLength)) + 1, '\0');
    editor->Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
inline void NativeF3(wxWindow* window) {
#ifdef __WXMSW__
    PostMessageW(static_cast<HWND>(window->GetHandle()), WM_KEYDOWN, VK_F3, 0);
    PostMessageW(static_cast<HWND>(window->GetHandle()), WM_KEYUP, VK_F3, 0);
#elif defined(__WXGTK__)
    auto* widget = GTK_WIDGET(window->GetHandle());
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_F3;
    event->key.state = static_cast<GdkModifierType>(0);
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
} // namespace NativeSearch
