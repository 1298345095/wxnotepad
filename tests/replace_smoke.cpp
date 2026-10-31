#include "editor_control.h"
#include "editor_frame.h"
#include "native_dialog_helpers.h"
#include <wx/wx.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Command(EditorFrame* frame, int id) {
    if (!frame->GetMenuBar()->FindItem(id)) std::fprintf(stderr, "Missing menu command: %d\n", id);
    Require(frame->GetMenuBar()->FindItem(id), "native menu command must exist");
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(frame->GetHandle()), WM_COMMAND, MAKEWPARAM(id, 0), 0);
#elif defined(__WXGTK__)
    gtk_menu_item_activate(GTK_MENU_ITEM(frame->GetMenuBar()->FindItem(id)->GetMenuItem()));
#endif
    wxYield();
}
void Click(wxWindow* parent, int id) {
    auto* button = wxDynamicCast(parent->FindWindow(id), wxButton);
    Require(button && button->IsShown(), "Replace view must expose its native button");
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(parent->GetHandle()), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(static_cast<HWND>(button->GetHandle())), BN_CLICKED),
                 reinterpret_cast<LPARAM>(button->GetHandle()));
#elif defined(__WXGTK__)
    gtk_button_clicked(GTK_BUTTON(button->GetHandle()));
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
    Require(editor->Send(Scintilla::Message::GetSelectionStart) == start &&
                editor->Send(Scintilla::Message::GetSelectionEnd) == end,
            message);
}
class ReplaceApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one ONE one"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 0);
                Command(frame, wxID_REPLACE);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                Require(query && replacement && replacement->IsShownOnScreen(),
                        "Replace menu must open a modeless view with query and replacement text");
                auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                auto* wrap = wxDynamicCast(wxWindow::FindWindowByName("Wrap around", frame), wxCheckBox);
                Require(wrap && wrap->GetValue(), "Replace must share the default-enabled Wrap around option");
                wrap->SetValue(false);
                query->SetValue("one");
                replacement->SetValue("$1\\tail");
                Click(dialog, wxID_REPLACE);
                Selection(editor, 0, 3, "Replace at an arbitrary caret must first select the next match");
                Require(Text(editor) == "one ONE one" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "first Replace without a matching selection must not edit");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "$1\\tail ONE one", "replacement dollars and backslashes must be literal");
                Selection(editor, 8, 11, "successful Replace must select the next match after inserted text");
                Require(editor->Send(Message::GetModify), "replacement must mark the document modified");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == "one ONE one" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                        "one Undo must restore exact bytes and the original savepoint");
                Command(frame, wxID_REDO);
                Require(Text(editor) == "$1\\tail ONE one" && editor->Send(Message::GetModify),
                        "Redo must restore the single replacement");
                std::puts("PASS: native Replace selects first, replaces literally, advances and undoes once");
                dialog->Close();
                wxYield();
                wxKeyEvent replaceKey(wxEVT_CHAR_HOOK);
                replaceKey.m_keyCode = 'H';
                replaceKey.m_controlDown = true;
                replaceKey.SetEventObject(editor);
                editor->GetEventHandler()->ProcessEvent(replaceKey);
                wxYield();
                Require(dialog->IsShown() && replacement->IsShown() && replacement->GetValue() == "$1\\tail",
                        "Ctrl+H must reopen the retained modeless Replace view");
                Command(frame, wxID_FIND);
                Require(dialog->GetTitle() == "Find" && !replacement->IsShown() && query->GetValue() == "one",
                        "Find must share the current query and hide replacement controls");
                query->SetValue(wxString::FromUTF8("中文😀"));
                Command(frame, wxID_REPLACE);
                Require(dialog->GetTitle() == "Replace" && query->GetValue() == wxString::FromUTF8("中文😀") &&
                            replacement->GetValue() == "$1\\tail",
                        "switching views must share query and retain replacement text");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文😀 中文😀"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 10, 0);
                replacement->SetValue(wxString::FromUTF8("好"));
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "好 中文😀", "Replace must accept an exact reversed Unicode selection");
                Selection(editor, 4, 14, "Replace must select next Unicode match on byte boundaries");
                replacement->Clear();
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "好 ", "empty replacement must delete the selected match");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == "好 中文😀", "deletion must undo in one action");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == "中文😀 中文😀" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "two replacements must be exactly two undo actions and reach the savepoint");
                std::puts("PASS: Ctrl+H, shared view state, reversed Unicode selection and empty deletion");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one two one"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 0, 3);
                query->SetValue("two");
                replacement->SetValue("X");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "one two one" && !editor->Send(Message::CanUndo),
                        "query changes must reject a previously selected different match");
                Selection(editor, 4, 7, "query changes must immediately select the new query");
                editor->Send(Message::SetSel, 0, 7);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "one two one", "a selection containing a match must not be replaced");
                editor->Send(Message::SetSel, 0, 3);
                query->SetValue("one");
                editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("bad"));
                editor->Send(Message::SetSel, 0, 3);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "bad two one", "edits must invalidate a formerly matching range");
                Selection(editor, 8, 11, "Replace must find the live remaining match after an edit");
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 0, 3);
                query->Clear();
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "one two one" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                        "empty query must preserve text, selection, savepoint and redo history");
                Selection(editor, 0, 3, "empty query must preserve current selection");
                query->SetValue("missing");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "one two one" && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo),
                        "no-match Replace must preserve redo history");
                Selection(editor, 0, 3, "no-match Replace must preserve the selection");
                Require(frame->New(), "New must work with the clean Replace view open");
                query->SetValue("one");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor).empty() && !editor->Send(Message::CanUndo),
                        "Replace must revalidate against the new empty document");
                std::puts("PASS: query, selection, live edit and New changes revalidate without stale replacements");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one one"));
                editor->Send(Message::SetSel, 4, 7);
                replacement->SetValue("X");
                wrap->SetValue(true);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "one X", "wrapped replacement must edit only the confirmed match");
                Selection(editor, 0, 3, "Replace must wrap to the first match using the current search policy");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one one"));
                editor->Send(Message::SetSel, 4, 7);
                wrap->SetValue(false);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "one X", "non-wrapping Replace must still replace the confirmed match");
                Selection(editor, 5, 5, "exhausted non-wrapping Replace must retain the post-replacement caret");
                std::puts("PASS: replacement advancement uses the shared current wrap policy");
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
wxIMPLEMENT_APP(ReplaceApp);
