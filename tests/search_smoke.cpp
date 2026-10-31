#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statusbr.h>
#include <wx/checkbox.h>
#include "native_search_helpers.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using NativeSearch::Click;
using NativeSearch::Command;
using NativeSearch::NativeF3;
using NativeSearch::Selection;
using NativeSearch::Text;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool Focused(wxWindow* window) {
    wxStopWatch clock;
    while (wxWindow::FindFocus() != window && clock.Time() < 2000) {
        wxMilliSleep(10);
        wxYield();
    }
    return wxWindow::FindFocus() == window;
}
void Key(wxWindow* window, int key, bool control = false) {
    wxKeyEvent event(wxEVT_CHAR_HOOK);
    event.m_keyCode = key;
    event.m_controlDown = control;
    event.SetEventObject(window);
    window->GetEventHandler()->ProcessEvent(event);
    wxYield();
}
wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
class SearchApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one ONE one"));
                editor->Send(Message::GotoPos, 1);
                Require(frame->GetMenuBar()->FindItem(wxID_FIND), "Find menu command must exist");
                Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                Require(query && query->IsShownOnScreen(), "Find command must open a modeless query field");
                auto* wrap = wxDynamicCast(wxWindow::FindWindowByName("Wrap around", frame), wxCheckBox);
                Require(wrap, "Find must expose the Wrap around option");
                wrap->SetValue(false);
                query->SetValue("one");
                Command(frame, wxID_FORWARD);
                Selection(editor, 4, 7, "Find Next must select the next case-insensitive literal from the caret");
                std::puts("PASS: Find dialog and forward literal search from caret");
                NativeF3(query);
                Require(editor->Send(Message::GetSelectionStart) == 8 && editor->Send(Message::GetSelectionEnd) == 11,
                        "F3 in the modeless dialog must advance from the current selection end");
                auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                dialog->Close();
                wxYield();
                Require(!dialog->IsShown() && Focused(editor), "closing Find must restore editor focus");
                Key(editor, 'F', true);
                Require(dialog->IsShown() && query->GetValue() == "one" && Focused(query),
                        "Ctrl+F must reuse the session dialog and focus its retained query");
                std::puts("PASS: dialog F3, close focus and Ctrl+F session lifecycle");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("A中文😀 [a+b].*\nStraße STRASSE"));
                editor->Send(Message::GotoPos, 1);
                query->SetValue(wxString::FromUTF8("中文"));
                Click(dialog, wxID_FORWARD);
                Selection(editor, 1, 7, "native Find Next must select whole Chinese characters");
                query->SetValue(wxString::FromUTF8("😀"));
                wxCommandEvent enter(wxEVT_TEXT_ENTER, query->GetId());
                enter.SetEventObject(query);
                query->GetEventHandler()->ProcessEvent(enter);
                Selection(editor, 7, 11, "Enter must select a whole supplementary UTF-8 character");
                query->SetValue("[a+b].*");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 12, 19, "regex punctuation must be searched literally");
                query->SetValue("STRASSE");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 20, 27, "Unicode case folding must use the actual matched byte length");
                editor->Send(Message::SetSel, 35, 28);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 28, 35,
                          "forward search after a reversed selection must start at its end without wrapping");
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "exhausted search must report no match");
                std::puts("PASS: native dialog button, Enter, Unicode ranges, case folding and literal punctuation");
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 35);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 35, 28);
                wxYield();
                query->SetValue("missing");
                Click(dialog, wxID_FORWARD);
                Require(Text(editor) == "A中文😀 [a+b].*\nStraße STRASSE" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                        "no match must preserve text, save point and redo history");
                Require(editor->Send(Message::GetAnchor) == 35 && editor->Send(Message::GetCurrentPos) == 28,
                        "no match must preserve selection direction");
                query->Clear();
                Click(dialog, wxID_FORWARD);
                Selection(editor, 28, 35, "empty query must preserve selection");
                editor->Send(Message::Redo);
                Require(Text(editor) == "A中文😀 [a+b].*\nStraße STRASSE!",
                        "search must leave the real redo action intact");
                editor->Send(Message::Undo);
                Require(!editor->Send(Message::GetModify), "undo after search must reach the original save point");
                std::puts("PASS: exhausted and empty search preserve selection, document and editing history");
                Require(frame->New(), "clean New must succeed while Find is open");
                Require(dialog->IsShown(), "New must leave the modeless dialog available");
                query->SetValue("one");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 0, "New must search the new empty document");
                editor->Send(Message::AddText, 7, reinterpret_cast<intptr_t>("new ONE"));
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 4, 7, "edits while Find is open must be searchable immediately");
                editor->Send(Message::Undo);
                const auto path = std::filesystem::temp_directory_path() /
                                  ("wxnotepad-find-" + std::to_string(wxGetProcessId()) + ".txt");
                {
                    std::ofstream(path, std::ios::binary) << "opened one\n";
                }
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened,
                        "Open while Find is open must succeed");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 7, 10, "Find must search the opened document using the session query");
                std::filesystem::remove(path);
                std::puts("PASS: live edits, New and Open use current document state");
                std::string distant(100, '\n');
                distant += std::string(300, 'x') + "target";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(distant.c_str()));
                editor->Send(Message::GotoPos, 0);
                query->SetValue("target");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 400, 406, "distant match must have its explicit byte range selected");
                const auto x = editor->Send(Message::PointXFromPosition, 0, 406);
                const auto y = editor->Send(Message::PointYFromPosition, 0, 406);
                Require(x >= 0 && x < editor->GetClientSize().x && y >= 0 && y < editor->GetClientSize().y,
                        "Find Next must reveal an off-screen match on both axes");
                Click(dialog, wxID_CANCEL);
                Require(!dialog->IsShown() && Focused(editor), "native Close button must return editor focus");
                Key(editor, WXK_F3);
                Selection(editor, 400, 406, "F3 with the dialog closed must use the retained query without wrapping");
                std::puts("PASS: distant match reveal and F3 after native dialog close");
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
wxIMPLEMENT_APP(SearchApp);
