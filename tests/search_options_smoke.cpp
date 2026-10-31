#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>
#include "native_dialog_helpers.h"
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Command(EditorFrame* frame, int id) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(frame->GetHandle()), WM_COMMAND, MAKEWPARAM(id, 0), 0);
#elif defined(__WXGTK__)
    auto* item = frame->GetMenuBar()->FindItem(id);
    Require(item, "native menu command must exist");
    gtk_menu_item_activate(GTK_MENU_ITEM(item->GetMenuItem()));
#endif
    wxYield();
}
void Toggle(wxCheckBox* option, bool checked) {
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(option->GetHandle());
    SendMessageW(handle, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(static_cast<HWND>(option->GetParent()->GetHandle()), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(handle), BN_CLICKED), reinterpret_cast<LPARAM>(handle));
#elif defined(__WXGTK__)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(option->GetHandle()), checked);
#endif
    wxYield();
    Require(option->GetValue() == checked, "native checkbox must show its new value");
}
void Click(wxWindow* dialog, int id) {
    auto* button = wxDynamicCast(dialog->FindWindow(id), wxButton);
    Require(button, "dialog must expose the expected native button");
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(button->GetHandle());
    SendMessageW(static_cast<HWND>(dialog->GetHandle()), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(handle), BN_CLICKED),
                 reinterpret_cast<LPARAM>(handle));
#elif defined(__WXGTK__)
    gtk_button_clicked(GTK_BUTTON(button->GetHandle()));
#endif
    wxYield();
}
void NativeF3(wxWindow* window) {
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
std::string Text(EditorControl* editor) {
    const auto length = static_cast<size_t>(editor->Send(Scintilla::Message::GetLength));
    std::string text(length + 1, '\0');
    editor->Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
void Selection(EditorControl* editor, intptr_t start, intptr_t end, const char* message) {
    const auto actualStart = editor->Send(Scintilla::Message::GetSelectionStart);
    const auto actualEnd = editor->Send(Scintilla::Message::GetSelectionEnd);
    if (actualStart != start || actualEnd != end)
        std::fprintf(stderr, "Range: %lld..%lld; expected %lld..%lld; flags: %lld\n",
                     static_cast<long long>(actualStart), static_cast<long long>(actualEnd),
                     static_cast<long long>(start), static_cast<long long>(end),
                     static_cast<long long>(editor->Send(Scintilla::Message::GetSearchFlags)));
    Require(actualStart == start && actualEnd == end, message);
}
class SearchOptionsApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* matchCase = wxDynamicCast(wxWindow::FindWindowByName("Match case", frame), wxCheckBox);
                Require(query && matchCase, "Find must expose the native Match case checkbox");
                Require(!matchCase->GetValue(), "Match case must initially be off");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("ONE One one"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue("one");
                Command(frame, wxID_FORWARD);
                Selection(editor, 0, 3, "default search must ignore case");
                Toggle(matchCase, true);
                Command(frame, wxID_FORWARD);
                Selection(editor, 8, 11, "Match case must immediately skip the differently cased next match");
                std::puts("PASS: native Match case event immediately controls Find Next");
                auto* wholeWord = wxDynamicCast(wxWindow::FindWindowByName("Whole word", frame), wxCheckBox);
                Require(wholeWord, "Find must expose the native Whole word checkbox");
                Require(!wholeWord->GetValue(), "Whole word must initially be off independently of Match case");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one_one one1 1one one one-one"));
                editor->Send(Message::GotoPos, 0);
                Command(frame, wxID_FORWARD);
                Selection(editor, 0, 3, "Whole word off must allow a substring in an underscore word");
                Toggle(wholeWord, true);
                Command(frame, wxID_FORWARD);
                Selection(editor, 18, 21, "Whole word must immediately reject underscore and digit word fragments");
                std::puts("PASS: native Whole word event controls original-core word boundaries");
                auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                Require(dialog, "query and options must share the retained modeless dialog");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 22, 25, "native dialog Find Next must apply both options at a hyphen boundary");
                NativeF3(query);
                Selection(editor, 26, 29, "native dialog F3 must apply both options");
                Toggle(matchCase, false);
                Require(wholeWord->GetValue(), "changing Match case must retain Whole word independently");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("éCOLE école école2 _école école."));
                editor->Send(Message::GotoPos, 0);
                query->SetValue(wxString::FromUTF8("ÉCOLE"));
                Command(frame, wxID_FORWARD);
                Selection(editor, 0, 6, "accented Unicode case folding must match an exact six-byte word");
                Command(frame, wxID_FORWARD);
                Selection(editor, 7, 13, "accented lowercase word must match without Match case");
                Command(frame, wxID_FORWARD);
                Selection(editor, 30, 36, "whole accented words must exclude adjacent digits and underscores");
                Toggle(matchCase, true);
                query->SetValue(wxString::FromUTF8("école"));
                editor->Send(Message::GotoPos, 0);
                wxCommandEvent enter(wxEVT_TEXT_ENTER, query->GetId());
                enter.SetEventObject(query);
                query->GetEventHandler()->ProcessEvent(enter);
                Selection(editor, 7, 13, "Enter must use current case and word options for accented text");
                std::puts("PASS: accented literal ranges, independent options and native dialog/F3/Enter paths");
                editor->Send(Message::SetText, 0,
                             reinterpret_cast<intptr_t>("中文中文 A中文B 中文_ 中文1 中文。 中文"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue(wxString::FromUTF8("中文"));
                Command(frame, wxID_FORWARD);
                Selection(editor, 14, 20,
                          "CJK word class must reject a repeated CJK fragment but end at Latin letters");
                Command(frame, wxID_FORWARD);
                Selection(editor, 22, 28, "CJK and ASCII underscore classes form an original-core word boundary");
                Command(frame, wxID_FORWARD);
                Selection(editor, 30, 36, "CJK and ASCII digits form an original-core word boundary");
                Command(frame, wxID_FORWARD);
                Selection(editor, 38, 44, "Chinese punctuation must delimit a CJK word");
                Command(frame, wxID_FORWARD);
                Selection(editor, 48, 54, "standalone Chinese word must retain complete UTF-8 characters");
                std::puts("PASS: explicit original-core CJK class boundaries");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("Straße STRASSE Straße2"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue("STRASSE");
                Toggle(matchCase, false);
                Command(frame, wxID_FORWARD);
                Selection(editor, 0, 7, "whole-word Unicode folding must retain the actual Straße byte length");
                Toggle(matchCase, true);
                Command(frame, wxID_FORWARD);
                Selection(editor, 8, 15, "changing Match case must immediately reject Unicode folded candidates");
                Toggle(wholeWord, false);
                Require(matchCase->GetValue(), "changing Whole word must retain Match case independently");
                query->SetValue(wxString::FromUTF8("Straße"));
                Command(frame, wxID_FORWARD);
                Selection(editor, 16, 23, "disabling Whole word must immediately allow a digit-suffixed substring");
                Toggle(wholeWord, true);
                editor->Send(Message::SetSel, 23, 16);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 24);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 23, 16);
                Click(dialog, wxID_FORWARD);
                Require(Text(editor) == "Straße STRASSE Straße2" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                        "option changes and no-match searches must preserve text, save point and redo history");
                Require(editor->Send(Message::GetAnchor) == 23 && editor->Send(Message::GetCurrentPos) == 16,
                        "option-sensitive no match must retain reversed selection");
                Click(dialog, wxID_CANCEL);
                Command(frame, wxID_FIND);
                Require(matchCase->GetValue() && wholeWord->GetValue() &&
                            query->GetValue() == wxString::FromUTF8("Straße"),
                        "reopening Find must retain independent session options and query");
                Require(frame->New(), "clean New must succeed with search options active");
                Require(matchCase->GetValue() && wholeWord->GetValue(), "New must retain session search options");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("Straße2 Straße"));
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_CANCEL);
                wxKeyEvent f3(wxEVT_CHAR_HOOK);
                f3.m_keyCode = WXK_F3;
                f3.SetEventObject(editor);
                frame->GetEventHandler()->ProcessEvent(f3);
                wxYield();
                Selection(editor, 9, 16, "F3 with Find closed must use the retained case and whole-word options");
                std::puts("PASS: Unicode folding, immediate toggles, session retention and unchanged editing history");
                Command(frame, wxID_REPLACE);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                Require(replacement && replacement->IsShownOnScreen() && matchCase->IsShownOnScreen() &&
                            wholeWord->IsShownOnScreen(),
                        "Replace must share the query and visible session option controls");
                replacement->SetValue("X");
                query->SetValue("one");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("ONE one one_one one"));
                editor->Send(Message::SetSel, 0, 3);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "ONE one one_one one",
                        "Replace must reject a selection invalid under Match case");
                Selection(editor, 4, 7, "Replace must find the next match using the current case and word options");
                editor->Send(Message::SetSel, 8, 11);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "ONE one one_one one",
                        "Replace must reject a selection invalid under Whole word");
                Selection(editor, 16, 19, "Replace must find the next whole word after an invalid substring selection");
                Toggle(wholeWord, false);
                editor->Send(Message::SetSel, 8, 11);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "ONE one X_one one",
                        "Replace must immediately apply a changed Whole word option");
                std::puts("PASS: Replace shares query/options and revalidates selection after option changes");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
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
wxIMPLEMENT_APP(SearchOptionsApp);
