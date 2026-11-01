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
                setup("aab", "(?<=aa)(b)", "$1!");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 2, 3, "lookbehind is initially found from document start");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "aab" && !editor->Send(Message::CanUndo),
                        "original replacement revalidation cannot retain lookbehind before selection start");
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
                Require(Text(editor) == "a X", "original Replace revalidates an arbitrary matching selection");
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
                Require(Text(editor) == "X a!!", "original Replace refreshes captures after an edit while dirty");
                setup("a a", "(a)", "X");
                Click(dialog, wxID_FORWARD);
                query->SetValue("(a)|b");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X a", "changed query revalidates the current matching selection");
                auto* matchCase = wxDynamicCast(wxWindow::FindWindowByName("Match case", frame), wxCheckBox);
                setup("a a", "(a)", "X");
                Click(dialog, wxID_FORWARD);
                Toggle(matchCase, true);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X a", "changed options revalidate the current matching selection");
                Toggle(matchCase, false);
                setup("a a", "(a)", "X");
                Click(dialog, wxID_FORWARD);
                Toggle(regex, false);
                Toggle(regex, true);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X a", "restored regex mode revalidates the current selection");
                std::puts("PASS: original replacement revalidation uses live query, options and selection");
                setup("中文中文", "(?=中)", "_");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "_中文中文", "original matching caret is replaced immediately");
                Selection(editor, 1, 1, "original Replace resumes at the insertion end");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "__中文中文", "original Replace can repeat at the same original character");
                Selection(editor, 2, 2, "replacement continuation does not skip an original character");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "___中文中文", "repeated empty replacements follow original target context");
                setup("中文", "a*", "_");
                editor->Send(Message::GotoPos, 6);
                Click(dialog, wxID_FORWARD);
                Click(dialog, wxID_REPLACE);
                Selection(editor, 7, 7, "original EOF continuation stays at the new document end");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "中文__", "original EOF empty match can be replaced repeatedly");
                setup("", "^", "X");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X", "original empty document caret is replaced immediately");
                Click(dialog, wxID_REPLACE);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X", "empty document start must be replaced only once without wrap");
                setup("中文", "(?=中)", "");
                Click(dialog, wxID_FORWARD);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "中文" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "empty expansion of empty match must leave history untouched");
                Require(frame->GetStatusBar()->GetStatusText(2) == "Match found",
                        "an empty no-op resumes at the same original match");
                std::puts("PASS: original empty replacement continuation, repeated EOF and empty no-op");
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
                Require(Text(editor) == "X a", "restored selection is revalidated through the original matcher");
                setup("中文", "(?=中)", "_");
                Click(dialog, wxID_FORWARD);
                editor->Send(Message::GotoPos, 3);
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "_中文", "restored caret is immediately revalidated as an empty match");
                Selection(editor, 1, 1, "replacement resumes after insertion at restored caret");
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
                Require(Text(editor) == "X", "New uses fresh original captures from the current empty document");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X", "continuation after insertion cannot match the preceding line start");
                std::puts("PASS: Save bytes, BOM/mixed EOL, savepoint Undo/Redo and live New document");
                setup("", "^", "X");
                Click(dialog, wxID_FORWARD);
                Require(frame->New(), "New must also succeed when the old document is already empty");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X", "empty New uses the current original matching caret immediately");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == "X", "repeated continuation after empty New follows original line context");
                std::puts("PASS: empty New reads fresh regex context without separate confirmation state");
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
