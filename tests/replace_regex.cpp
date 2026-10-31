#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include <wx/wx.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>
#include "native_search_helpers.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <fstream>
#include <wx/statusbr.h>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
using NativeSearch::Click;
using NativeSearch::Command;
using NativeSearch::NativeF3;
using NativeSearch::Selection;
using NativeSearch::Text;
using NativeSearch::Toggle;
class RegexReplaceApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                Command(frame, wxID_REPLACE);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", frame), wxCheckBox);
                Require(query && replacement && regex, "native Replace menu must expose regex replacement controls");
                auto* dialog = query->GetParent();
                Toggle(regex, true);
                Require(dialog->FindWindow(wxID_REPLACE)->IsEnabled(), "single regex Replace must be enabled");
                Require(dialog->FindWindow(wxID_REPLACE_ALL)->IsEnabled(), "regex Replace All must be enabled");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文12 中文34"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 0);
                query->SetValue(wxString::FromUTF8("(中文)([0-9]+)"));
                replacement->SetValue("$2:$1:$$:$&:$0:\\0");
                NativeF3(query);
                Selection(editor, 0, 8, "native F3 must confirm the first regex match");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "12:中文:$:中文12:中文12:中文12 中文34",
                        "original dollar and backslash captures must expand");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == "中文12 中文34" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "one Undo must restore original bytes and savepoint");
                Command(frame, wxID_REDO);
                Require(Text(editor) == "12:中文:$:中文12:中文12:中文12 中文34" && editor->Send(Message::GetModify),
                        "Redo must restore the single expansion");
                std::puts("PASS: native regex Replace, original numeric capture expansion and one-step Undo/Redo");
                auto* wrap = wxDynamicCast(wxWindow::FindWindowByName("Wrap around", frame), wxCheckBox);
                Toggle(wrap, false);
                const auto setup = [&](const char* before, const char* pattern, const char* with) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(before));
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    editor->Send(Message::GotoPos, 0);
                    query->SetValue(wxString::FromUTF8(pattern));
                    replacement->SetValue(wxString::FromUTF8(with));
                };
                const auto example = [&](const char* before, const char* pattern, const char* with,
                                         const char* expected) {
                    setup(before, pattern, with);
                    Click(dialog, wxID_FORWARD);
                    Click(dialog, wxID_REPLACE);
                    if (Text(editor) != expected)
                        std::fprintf(stderr, "Expansion actual: %s; expected: %s\n", Text(editor).c_str(), expected);
                    Require(Text(editor) == expected, "replacement must follow the explicit original-format example");
                    Command(frame, wxID_UNDO);
                    Require(Text(editor) == before && !editor->Send(Message::GetModify) &&
                                !editor->Send(Message::CanUndo),
                            "each expansion must be one undo action");
                    Command(frame, wxID_REDO);
                    Require(Text(editor) == expected && editor->Send(Message::GetModify),
                            "Redo must restore exact expansion bytes");
                };
                example("中文12", "(中文)([0-9]+)", "\\2-\\1", "12-中文");
                example("a", "(a)", "$$|\\\\|$10|${name}|$`|$'|\\u|\\x|$|\\", "$|\\|a0|${name}|$`|$'|\\u|\\x|$|\\");
                example("b", "(a)?(b)", "$1|\\1|$2|\\2", "||b|b");
                example("a", "(a)", "\\1\\n\\t", "a\n\t");
                example("中文后", "(?<=中)文(?=后)", "X", "中X后");
                example("aab", "(?<=aa)b", "X", "aaX");
                example("中\n文", "(中)\\n(文)", "$2:$1", "文:中");
                example("a", "(a)", "", "");
                std::puts("PASS: explicit original dollar/backslash literals, optional groups, Unicode and contextual "
                          "captures");
                setup("a", "(a)", "$&");
                editor->Send(Message::GotoPos, 1);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                Command(frame, wxID_UNDO);
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_FORWARD);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "a" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo),
                        "byte-identical regex expansion must preserve savepoint and redo history");
                std::puts("PASS: byte-identical expansion is an editing no-op");
                setup("a a", "(a)", "X");
                Click(dialog, wxID_FORWARD);
                editor->Send(Message::SetSel, 2, 3);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "a a",
                        "moving to another matching selection must invalidate regex confirmation");
                setup("a a!", "(a)", "X");
                editor->Send(Message::GotoPos, 4);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_FORWARD);
                editor->Send(Message::SetTargetStart, 0);
                editor->Send(Message::SetTargetEnd, 1);
                editor->Send(Message::ReplaceTarget, 1, reinterpret_cast<intptr_t>("A"));
                editor->Send(Message::SetSel, 0, 1);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "A a!!",
                        "an edit while dirty must invalidate still-matching selected captures");
                setup("a a", "(a)", "X");
                Click(dialog, wxID_FORWARD);
                query->SetValue("(a)|b");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "a a",
                        "query changes must require a fresh match even if selection still matches");
                auto* matchCase = wxDynamicCast(wxWindow::FindWindowByName("Match case", frame), wxCheckBox);
                setup("a a", "(a)", "X");
                Click(dialog, wxID_FORWARD);
                Toggle(matchCase, true);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "a a", "option changes must invalidate prior confirmation");
                Toggle(matchCase, false);
                setup("a a", "(a)", "X");
                Click(dialog, wxID_FORWARD);
                Toggle(regex, false);
                Toggle(regex, true);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "a a", "mode changes must invalidate prior confirmation");
                std::puts(
                    "PASS: moved selection, dirty edit, changed query/options and mode cannot reuse confirmation");
                setup("中文中文", "(?=中)", "_");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "中文中文",
                        "an arbitrary caret must first confirm an empty match without inserting");
                Selection(editor, 0, 0, "first Replace must select the empty lookahead");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "_中文中文", "confirmed empty lookahead must insert once");
                Selection(editor, 7, 7, "Replace must advance past a whole Chinese character to the next lookahead");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "_中文_中文", "second Replace must use the next original lookahead");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "_中文_中文", "exhausted empty matches must not keep reinserting");
                setup("中文", "a*", "_");
                Click(dialog, wxID_FORWARD);
                Click(dialog, wxID_REPLACE);
                Selection(editor, 4, 4, "zero-length continuation must preserve UTF-8 boundaries");
                Click(dialog, wxID_REPLACE);
                Selection(editor, 8, 8, "zero-length continuation must include EOF once");
                Click(dialog, wxID_REPLACE);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "_中_文_", "empty EOF match must be replaced once and then exhausted");
                setup("", "^", "X");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor).empty(), "empty document caret requires confirmation");
                Click(dialog, wxID_REPLACE);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X", "empty document start must be replaced only once without wrap");
                setup("中文", "(?=中)", "");
                Click(dialog, wxID_FORWARD);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "中文" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "empty expansion of empty match must leave history untouched");
                Require(frame->GetStatusBar()->GetStatusText(2) == "No match found",
                        "an empty no-op must still advance");
                std::puts("PASS: arbitrary caret confirmation, UTF-8 empty-match progress, EOF and empty no-op");
                setup("a1 a2", "a([0-9])", "$1");
                editor->Send(Message::GotoPos, 3);
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 0, 2, "Previous must select the earlier match before replacement");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "1 a2", "revalidation must restore captures overwritten by Previous traversal");
                setup("a a", "(a)", "X");
                Click(dialog, wxID_FORWARD);
                editor->Send(Message::GotoPos, 2);
                editor->Send(Message::SetSel, 0, 1);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "a a",
                        "moving away and restoring the selected range must invalidate confirmation");
                setup("中文", "(?=中)", "_");
                Click(dialog, wxID_FORWARD);
                editor->Send(Message::GotoPos, 3);
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "中文",
                        "restoring an arbitrary caret must not restore an empty-match confirmation");
                Selection(editor, 0, 0, "restored arbitrary caret must search the current position afresh");
                setup("a", "(a)", "X");
                editor->Send(Message::GotoPos, 1);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                Command(frame, wxID_UNDO);
                editor->Send(Message::SetSel, 1, 0);
                query->SetValue("[");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "a" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo),
                        "invalid regex Replace must preserve text, savepoint and redo history");
                Require(editor->Send(Message::GetAnchor) == 1 && editor->Send(Message::GetCurrentPos) == 0 &&
                            frame->GetStatusBar()->GetStatusText(2) == "Invalid regular expression",
                        "invalid regex Replace must preserve selection direction and report its error");
                query->SetValue("(a)");
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "a", "corrected query must first confirm a match");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X", "corrected query must recover and replace normally");
                std::puts("PASS: Previous captures, restored selections/carets and invalid-pattern history/retry");
                const auto path = std::filesystem::temp_directory_path() /
                                  ("wxnotepad-regex-" + std::to_string(wxGetProcessId()) + ".txt");
                {
                    std::ofstream file(path, std::ios::binary);
                    file << "\xef\xbb\xbf"
                            "中文12\r\nlast\n";
                }
                editor->Send(Message::SetSavePoint);
#ifdef __WXMSW__
                const wxString filename(path.wstring());
#else
                const wxString filename = wxString::FromUTF8(path.string());
#endif
                Require(frame->Open(filename).status == OpenStatus::Opened,
                        "UTF-8 BOM fixture must open through the application");
                query->SetValue(wxString::FromUTF8("(中文)([0-9]+)"));
                replacement->SetValue("$2:$1");
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_FORWARD);
                Click(dialog, wxID_REPLACE);
                Command(frame, wxID_SAVE);
                const std::string bytes = [&] {
                    std::ifstream file(path, std::ios::binary);
                    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                }();
                Require(bytes == "\xef\xbb\xbf"
                                 "12:中文\r\nlast\n" &&
                            frame->DocumentHasUtf8Bom() && !editor->Send(Message::GetModify),
                        "Save must preserve BOM and untouched mixed EOL with independently checked capture bytes");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == "中文12\r\nlast\n" && editor->Send(Message::GetModify),
                        "Undo after Save must move away from the new savepoint");
                Command(frame, wxID_REDO);
                Require(Text(editor) == "12:中文\r\nlast\n" && !editor->Send(Message::GetModify),
                        "Redo must restore the saved expansion and savepoint");
                std::filesystem::remove(path);
                Require(frame->New(), "New must use the live empty document");
                query->SetValue("^");
                replacement->SetValue("X");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor).empty(), "New must invalidate captures from the prior document");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X", "New must permit a fresh confirmed empty match");
                std::puts("PASS: Save bytes, BOM/mixed EOL, savepoint Undo/Redo and live New document");
                setup("", "^", "X");
                Click(dialog, wxID_FORWARD);
                Require(frame->New(), "New must also succeed when the old document is already empty");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor).empty(),
                        "New must invalidate an empty-match confirmation even when no bytes change");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X", "fresh confirmation after empty New must permit replacement");
                std::puts("PASS: empty New invalidates document identity without requiring a text change");
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
wxIMPLEMENT_APP(RegexReplaceApp);
