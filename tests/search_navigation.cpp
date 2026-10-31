#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/checkbox.h>
#include <wx/statusbr.h>
#include <wx/button.h>
#include "native_search_helpers.h"
#include <cstdio>
#include <stdexcept>

namespace {
using NativeSearch::Click;
using NativeSearch::Command;
using NativeSearch::Selection;
using NativeSearch::Text;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Wrap(wxCheckBox* checkbox, bool enabled) {
    NativeSearch::Toggle(checkbox, enabled);
}
void ShiftF3(wxWindow* window) {
#ifdef __WXMSW__
    BYTE previous[256];
    Require(GetKeyboardState(previous), "must read this test thread's keyboard state");
    BYTE shifted[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(shifted));
    shifted[VK_SHIFT] = 0x80;
    Require(SetKeyboardState(shifted), "must set this test thread's Shift modifier");
    MSG message{};
    message.hwnd = static_cast<HWND>(window->GetHandle());
    message.message = WM_KEYDOWN;
    message.wParam = VK_F3;
    const bool handled = wxGetTopLevelParent(window)->MSWTranslateMessage(&message);
    SetKeyboardState(previous);
    Require(handled, "the native owned window must translate Shift+F3");
#elif defined(__WXGTK__)
    auto* widget = GTK_WIDGET(window->GetHandle());
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_F3;
    event->key.state = GDK_SHIFT_MASK;
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
class NavigationApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("oneone one"));
                editor->Send(Message::GotoPos, 10);
                Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                Require(query, "Find must expose the query");
                query->SetValue("one");
                Command(frame, wxID_BACKWARD);
                Selection(editor, 7, 10, "Previous from document end must select the last match");
                Command(frame, wxID_BACKWARD);
                Selection(editor, 3, 6, "Previous must advance before the selected match");
                Command(frame, wxID_BACKWARD);
                Selection(editor, 0, 3, "Previous must include an adjacent match at the start");
                std::puts("PASS: native previous menu, adjacent matches and document start");
                auto* wrap = wxDynamicCast(wxWindow::FindWindowByName("Wrap around", frame), wxCheckBox);
                Require(wrap && wrap->GetValue(), "Wrap around must be enabled by default");
                Command(frame, wxID_BACKWARD);
                Selection(editor, 7, 10, "Previous at the start must wrap to the final match");
                Require(frame->GetStatusBar()->GetStatusText() == "Search wrapped",
                        "a wrapped search must report crossing the boundary");
                Command(frame, wxID_FORWARD);
                Selection(editor, 0, 3, "Next at the end must wrap to the first match");
                Require(frame->GetStatusBar()->GetStatusText() == "Search wrapped", "Next must report wrapping");
                Command(frame, wxID_FORWARD);
                Selection(editor, 3, 6, "direction changes must not skip an adjacent match");
                Require(frame->GetStatusBar()->GetStatusText() == "Match found",
                        "a normal match must clear a stale wrapped result");
                std::puts("PASS: default wrap in both directions and direction changes");
                auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 0, 3, "native dialog Previous must select the adjacent previous match");
                ShiftF3(query);
                Selection(editor, 7, 10, "native Shift+F3 in Find must wrap backward");
                Click(dialog, wxID_CANCEL);
                ShiftF3(frame);
                Selection(editor, 3, 6, "native Shift+F3 with Find hidden must retain the query");
                Command(frame, wxID_FIND);
                Require(wrap->GetValue(), "Wrap must remain enabled after reopening Find");
                Wrap(wrap, false);
                editor->Send(Message::SetSel, 3, 0);
                Click(dialog, wxID_BACKWARD);
                Require(editor->Send(Message::GetAnchor) == 3 && editor->Send(Message::GetCurrentPos) == 0,
                        "disabled wrap at document start must preserve a reversed selection");
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "disabled wrap must report no match at the boundary");
                editor->Send(Message::SetSel, 10, 7);
                Click(dialog, wxID_FORWARD);
                Require(editor->Send(Message::GetAnchor) == 10 && editor->Send(Message::GetCurrentPos) == 7,
                        "disabled forward wrap at document end must preserve a reversed selection");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 3, 6, "Previous must start before the smaller endpoint of reversed selections");
                editor->Send(Message::GotoPos, 3);
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 0, 3, "Previous from a caret must include a match ending at the caret");
                std::puts("PASS: native dialog Previous, Shift+F3 and disabled wrap boundaries");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文😀中文😀"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 20);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                query->SetValue(wxString::FromUTF8("中文😀"));
                Wrap(wrap, true);
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 10, 20, "Previous must select complete multibyte characters");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 0, 10, "Previous must include adjacent multibyte matches");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 10, 20, "direction change must retain multibyte byte boundaries");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 10, "forward wrap must preserve multibyte byte boundaries");
                Require(Text(editor) == "中文😀中文😀" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                        "successful navigation must preserve text, save point and redo history");
                query->SetValue("missing");
                editor->Send(Message::SetSel, 10, 0);
                Click(dialog, wxID_BACKWARD);
                Click(dialog, wxID_FORWARD);
                Require(editor->Send(Message::GetAnchor) == 10 && editor->Send(Message::GetCurrentPos) == 0 &&
                            frame->GetStatusBar()->GetStatusText() == "No match found",
                        "no match in either direction must preserve selection direction and report failure");
                query->Clear();
                Click(dialog, wxID_BACKWARD);
                Require(editor->Send(Message::GetAnchor) == 10 && editor->Send(Message::GetCurrentPos) == 0,
                        "empty Previous must not execute a search");
                editor->Send(Message::Redo);
                Require(Text(editor) == "中文😀中文😀!" && editor->Send(Message::GetModify),
                        "navigation must leave the original redo action available");
                editor->Send(Message::Undo);
                Require(!editor->Send(Message::GetModify), "undo must still reach the original save point");
                std::puts("PASS: multibyte navigation, empty/no match and exact editing history");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one"));
                wxYield();
                editor->Send(Message::SetSel, 3, 0);
                query->SetValue("one");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 0, 3, "a single match must remain selected after backward wrapping");
                Require(frame->GetStatusBar()->GetStatusText() == "Search wrapped", "single match must report wrap");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 3, "a single match must remain selected after forward wrapping");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(""));
                wxYield();
                Click(dialog, wxID_BACKWARD);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 0, "empty document navigation must terminate at the caret");
                Require(frame->GetStatusBar()->GetStatusText() == "No match found", "empty document has no match");
                std::puts("PASS: single match and empty document terminate with defined selections");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnExit() override { return result_; }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(NavigationApp);
