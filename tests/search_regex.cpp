#include "editor_control.h"
#include "editor_frame.h"
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
class RegexApp final : public wxApp {
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
                auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", frame), wxCheckBox);
                Require(query && regex, "Find must expose the native Regular expression option");
                Require(!regex->GetValue(), "Regular expression must initially be off");
                auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                Toggle(regex, true);
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文12 中文34"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue(wxString::FromUTF8("(中文)([0-9]+)"));
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 8, "Boost capture groups must select the first Chinese match");
                Command(frame, wxID_FORWARD);
                Selection(editor, 9, 17, "Boost capture groups must select the next Chinese match");
                std::puts("PASS: native regex option and frozen Chinese capture ranges");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中\n文 中\n文"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue(wxString::FromUTF8("中\\n文"));
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 7, "explicit cross-line regex must use the frozen forward range");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 8, 15, "Next must find the second cross-line regex");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 0, 7, "Previous must navigate to the same cross-line match as Next");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 8, 15, "Previous must wrap to the final cross-line match");
                Require(frame->GetStatusBar()->GetStatusText() == "Search wrapped", "Previous must report wrapping");
                std::puts("PASS: cross-line Previous/Next and backward wrap");
                query->SetValue("[");
                editor->Send(Message::SetSel, 15, 8);
                Click(dialog, wxID_FORWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "Invalid regular expression",
                        "invalid regex must be distinct from an exhausted search");
                Require(editor->Send(Message::GetAnchor) == 15 && editor->Send(Message::GetCurrentPos) == 8,
                        "invalid regex must preserve a reversed selection");
                Click(dialog, wxID_BACKWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "Invalid regular expression",
                        "Previous must also report an invalid expression");
                query->SetValue(wxString::FromUTF8("(?<=中)\\n文"));
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 3, 7, "correcting an invalid expression must immediately recover with lookbehind");
                std::puts("PASS: distinct invalid expression, preserved selection and correction/retry");
                auto* wrap = wxDynamicCast(wxWindow::FindWindowByName("Wrap around", frame), wxCheckBox);
                Require(wrap, "Find must expose Wrap around");
                Toggle(wrap, false);
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文ab"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue("a*");
                const std::pair<intptr_t, intptr_t> zeroRanges[] = {{0, 0}, {3, 3}, {6, 7}, {7, 7}, {8, 8}};
                for (const auto& [start, end] : zeroRanges) {
                    Click(dialog, wxID_FORWARD);
                    Selection(editor, start, end, "repeated zero-length Next must follow frozen UTF-8 ranges");
                }
                Click(dialog, wxID_FORWARD);
                Selection(editor, 8, 8, "exhausted zero-length Next must preserve the end caret");
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "end caret must exhaust without wrap");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 7, 7, "direction change after an empty match must advance to the previous match");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 6, 7, "Previous must retain the consuming match adjacent to an empty match");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 3, 3, "Previous must progress over a complete Chinese character");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 0, 0, "Previous must include the zero-length match at document start");
                Click(dialog, wxID_BACKWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "start caret must exhaust without wrap");
                Toggle(wrap, true);
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 8, 8, "empty-match backward wrap must select the final boundary once");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 0, "empty-match forward wrap must select the first boundary once");
                std::puts("PASS: frozen empty and consuming ranges, direction changes, UTF-8 and bounded wrap");
                Toggle(wrap, false);
                // Explicit expectations from prototype issue4-search.json,
                // baseline[case]["cxx11-requested"], frozen at 2f44bb9.
                struct Case {
                    const char* name;
                    const char* text;
                    const char* pattern;
                    std::vector<std::pair<intptr_t, intptr_t>> ranges;
                };
                const Case cases[] = {
                    {"lookbehind", "中文 英文", "(?<=中)文", {{3, 6}}},
                    {"unicode-word", "中文 abc_12", "\\w+", {{0, 6}, {7, 13}}},
                    {"unicode-dot", "中😀文", ".", {{0, 3}, {3, 7}, {7, 10}}},
                    {"pattern-backreference", "中中 中文 文文", "(中)\\1", {{0, 6}}},
                    {"alternation", "中文 测试 其他", "中文|测试", {{0, 6}, {7, 13}}},
                    {"cross-line", "中\n文", "中\n文", {{0, 7}}},
                    {"zero-lookahead", "中文中文", "(?=中)", {{0, 0}, {6, 6}}},
                    {"line-end-CRLF", "中文\r\n测试\r\n", "$", {{6, 6}, {14, 14}, {16, 16}}},
                    {"empty-lines", "a\n\nb\n", "^$", {{2, 2}, {5, 5}}},
                    {"empty-document", "", "^", {{0, 0}}},
                };
                for (const auto& example : cases) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(example.text));
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
                    editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                    editor->Send(Message::Undo);
                    editor->Send(Message::GotoPos, 0);
                    query->SetValue(wxString::FromUTF8(example.pattern));
                    for (const auto& [start, end] : example.ranges) {
                        Click(dialog, wxID_FORWARD);
                        Selection(editor, start, end, example.name);
                    }
                    Click(dialog, wxID_FORWARD);
                    Require(frame->GetStatusBar()->GetStatusText() == "No match found", "frozen corpus must exhaust");
                    // A fresh query makes the end caret an initial search position.
                    query->Clear();
                    query->SetValue(wxString::FromUTF8(example.pattern));
                    editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
                    for (auto range = example.ranges.rbegin(); range != example.ranges.rend(); ++range) {
                        Click(dialog, wxID_BACKWARD);
                        Selection(editor, range->first, range->second, example.name);
                    }
                    Click(dialog, wxID_BACKWARD);
                    Require(frame->GetStatusBar()->GetStatusText() == "No match found", "backward corpus must exhaust");
                    Require(Text(editor) == example.text && !editor->Send(Message::GetModify) &&
                                !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                            "regex navigation must preserve exact text, savepoint and redo history");
                    std::printf("PASS: frozen %s ranges in both directions and unchanged editing history\n",
                                example.name);
                }
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("a.b aXb Axb cat scatter"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue("a.b");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 3, "regex punctuation must initially match any character");
                NativeF3(query);
                Selection(editor, 4, 7, "native F3 must use regex mode");
                auto* matchCase = wxDynamicCast(wxWindow::FindWindowByName("Match case", frame), wxCheckBox);
                auto* wholeWord = wxDynamicCast(wxWindow::FindWindowByName("Whole word", frame), wxCheckBox);
                Require(matchCase && wholeWord && !wholeWord->IsEnabled(),
                        "regex must disable inapplicable Whole word");
                Toggle(matchCase, true);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 4, 7, "Match case must reject Axb in regex mode immediately");
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "case-sensitive regex must exhaust");
                Toggle(regex, false);
                Require(wholeWord->IsEnabled() && matchCase->GetValue(),
                        "literal mode must restore independent options");
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 3, "turning regex off must immediately interpret a.b literally");
                Click(dialog, wxID_FORWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "literal punctuation must reject aXb");
                Toggle(wholeWord, true);
                Toggle(regex, true);
                Require(wholeWord->GetValue() && !wholeWord->IsEnabled(),
                        "regex must retain the inactive Whole word value");
                query->SetValue("cat");
                editor->Send(Message::GotoPos, 15);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 17, 20, "pinned Boost must keep matching inside scatter when Whole word is retained");
                Toggle(regex, false);
                Require(wholeWord->GetValue() && wholeWord->IsEnabled(), "literal mode must restore Whole word");
                editor->Send(Message::GotoPos, 15);
                Click(dialog, wxID_FORWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "restored literal Whole word must reject scatter");
                Toggle(regex, true);
                Command(frame, wxID_REPLACE);
                Require(dialog->FindWindow(wxID_REPLACE)->IsEnabled() &&
                            dialog->FindWindow(wxID_REPLACE_ALL)->IsEnabled(),
                        "both regex replacement commands must be available");
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                Require(replacement, "Replace must retain the shared regex options and query");
                replacement->SetValue("X");
                Click(dialog, wxID_REPLACE_ALL);
                Require(Text(editor) == "a.b aXb Axb X sXter",
                        "regex Replace All must use the shared mode and inactive Whole word policy");
                Toggle(regex, false);
                Require(dialog->FindWindow(wxID_REPLACE)->IsEnabled() &&
                            dialog->FindWindow(wxID_REPLACE_ALL)->IsEnabled(),
                        "literal mode must restore both replacement actions");
                std::puts("PASS: native F3, immediate mode/case changes, retained Whole word and replacement UI");
                Toggle(regex, true);
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("aab"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue("aa|(?<=aa)b");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 2, "alternation must first consume aa");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 2, 3, "a later lookbehind match must retain context before the previous match end");
                query->SetValue("(?<=aa)b");
                editor->Send(Message::GotoPos, 2);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 2, 3, "lookbehind must see context before an arbitrary caret");
                std::puts("PASS: sequential and caret lookbehind retain full document context");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("ab"));
                editor->Send(Message::GotoPos, 1);
                query->SetValue("a(?=b)");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 0, 1, "Previous must retain lookahead context after the caret");
                editor->Send(Message::GotoPos, 1);
                Toggle(wrap, true);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 1, "forward wrap must retain lookahead beyond the wrap boundary");
                Require(frame->GetStatusBar()->GetStatusText() == "Search wrapped",
                        "context-sensitive Next must report wrap");
                Toggle(wrap, false);
                editor->Send(Message::GotoPos, 1);
                query->SetValue("a\\z");
                Click(dialog, wxID_BACKWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "caret bound must not become physical EOF");
                std::puts("PASS: Previous and wrap keep lookahead and physical document-end context");
                query->SetValue("\\A.");
                editor->Send(Message::GotoPos, 2);
                Click(dialog, wxID_FORWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "No match found",
                        "buffer-start anchor must not match at a caret");
                Toggle(wrap, true);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 1, "buffer-start anchor must wrap to the actual document start");
                Toggle(wrap, false);
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中\n文\n"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue("^");
                for (const intptr_t boundary : {0, 4, 8}) {
                    Click(dialog, wxID_FORWARD);
                    Selection(editor, boundary, boundary, "line-start navigation must include the final empty line");
                }
                Click(dialog, wxID_FORWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "No match found", "final line-start must exhaust");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文中文"));
                editor->Send(Message::GotoPos, 0);
                query->SetValue(wxString::FromUTF8("(?=中)"));
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 0, "initial lookahead must include the caret");
                editor->Send(Message::GotoPos, 12);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 0, "an edit while dirty must invalidate previous empty-match progress");
                editor->Send(Message::Undo);
                editor->Send(Message::GotoPos, 0);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 0, "undo must also invalidate previous empty-match progress");
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 12);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 12, 0);
                query->SetValue("(");
                Click(dialog, wxID_FORWARD);
                Click(dialog, wxID_BACKWARD);
                Require(Text(editor) == "中文中文" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                            editor->Send(Message::GetAnchor) == 12 && editor->Send(Message::GetCurrentPos) == 0,
                        "invalid patterns must preserve exact text, reversed selection, savepoint and redo");
                query->SetValue(wxString::FromUTF8("文"));
                Toggle(wrap, true);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 3, 6, "a corrected pattern must recover after an invalid expression");
                Click(dialog, wxID_CANCEL);
                Command(frame, wxID_FIND);
                Require(regex->GetValue() && wholeWord->GetValue() && matchCase->GetValue() &&
                            query->GetValue() == wxString::FromUTF8("文"),
                        "reopening Find must retain regex, query and independent session options");
                Require(frame->New(), "New must replace the clean document while regex Find remains open");
                query->SetValue("^");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 0, "New must search the current empty document");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 0, 0, "single empty match must finish a wrapped pass");
                Require(frame->GetStatusBar()->GetStatusText() == "Search wrapped",
                        "single empty match must report wrap");
                std::puts(
                    "PASS: document anchors, final newline, edit/undo invalidation, invalid-pattern history and New");
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
wxIMPLEMENT_APP(RegexApp);
