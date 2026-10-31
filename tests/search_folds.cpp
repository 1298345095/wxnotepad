#include "editor_control.h"
#include "editor_frame.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using Scintilla::Message;
using namespace NativeSearch;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class FoldSearchApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                const std::string fixture = "void outer() {\n  if (true) {\n    needle();\n  }\n}\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(fixture.c_str()));
                auto* cpp = frame->GetMenuBar()->FindItem(frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                Require(cpp, "C++ language menu must exist");
                Command(frame, cpp->GetId());
                editor->Send(Message::Colourise, 0, -1);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::FoldAll, static_cast<uintptr_t>(Scintilla::FoldAction::Contract));
                Require(!editor->Send(Message::GetLineVisible, 2), "fixture match must begin hidden");
                editor->Send(Message::GotoPos, 0);
                Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                Require(query, "native Find must expose the query");
                query->SetValue("needle");
                NativeF3(query);
                Selection(editor, 33, 39, "Find must retain the hidden match byte range");
                Require(editor->Send(Message::GetLineVisible, 2), "Find must reveal the hidden matched line");
                Require(Text(editor) == fixture && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "revealing a match must preserve bytes, savepoint and history");
                std::puts("PASS: native Find reveals a folded match without editing");
                const std::string nested = "void outer() {\n  if (true) {\n    needle();\n  }\n  if (true) {\n"
                                           "    middle();\n  }\n  tail();\n  if (true) {\n    spare();\n  }\n}\n"
                                           "void other() {\n  needle();\n}\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(nested.c_str()));
                editor->Send(Message::ClearDocumentStyle);
                editor->Send(Message::Colourise, 0, -1);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 0);
                editor->Send(Message::FoldAll, static_cast<uintptr_t>(Scintilla::FoldAction::Contract) |
                                                   static_cast<uintptr_t>(Scintilla::FoldAction::ContractEveryLevel));
                auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", frame), wxCheckBox);
                Require(regex, "native Find must expose regex mode");
                Toggle(regex, true);
                query->SetValue("if \\(true\\) \\{[\\s\\S]*?tail\\(\\);");
                auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
                Click(dialog, wxID_FORWARD);
                Selection(editor, 17, 88, "multiline regex must retain its complete byte range");
                for (int line = 1; line <= 7; ++line)
                    Require(editor->Send(Message::GetLineVisible, line),
                            "every line in a multiline match must be visible");
                Require(!editor->Send(Message::GetFoldExpanded, 8) && !editor->Send(Message::GetLineVisible, 9) &&
                            !editor->Send(Message::GetFoldExpanded, 12) && !editor->Send(Message::GetLineVisible, 13),
                        "unrelated nested and top-level folds must stay collapsed");
                Require(Text(editor) == nested && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "revealing a multiline match must preserve bytes, savepoint and history");
                std::puts("PASS: complete multiline range visible, unrelated folds preserved");
                const auto collapse = [&] {
                    editor->Send(Message::GotoPos, 0);
                    editor->Send(Message::FoldAll,
                                 static_cast<uintptr_t>(Scintilla::FoldAction::Contract) |
                                     static_cast<uintptr_t>(Scintilla::FoldAction::ContractEveryLevel));
                };
                const auto load = [&](const std::string& bytes) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(bytes.c_str()));
                    editor->Send(Message::ClearDocumentStyle);
                    editor->Send(Message::Colourise, 0, -1);
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    collapse();
                };
                collapse();
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 17, 88, "Previous must wrap to the same complete multiline regex");
                Require(frame->GetStatusBar()->GetStatusText() == "Search wrapped", "Previous must report wrap");
                for (int line = 1; line <= 7; ++line)
                    Require(editor->Send(Message::GetLineVisible, line),
                            "Previous must reveal the complete multiline match");
                for (const bool regularExpression : {false, true}) {
                    Toggle(regex, regularExpression);
                    query->SetValue(regularExpression ? "(needle)" : "needle");
                    collapse();
                    Click(dialog, wxID_FORWARD);
                    Selection(editor, 33, 39, "Next must find the first hidden literal or regex match");
                    Require(editor->Send(Message::GetLineVisible, 2) && !editor->Send(Message::GetLineVisible, 5) &&
                                !editor->Send(Message::GetLineVisible, 9) && !editor->Send(Message::GetLineVisible, 13),
                            "Next must open only ancestors of its current match");
                    Click(dialog, wxID_FORWARD);
                    Selection(editor, 139, 145, "Next must find the second hidden match");
                    Require(editor->Send(Message::GetLineVisible, 13), "second match must be revealed");
                    Click(dialog, wxID_FORWARD);
                    Selection(editor, 33, 39, "Next must wrap across folded document text");
                    Require(frame->GetStatusBar()->GetStatusText() == "Search wrapped", "Next must report wrap");
                    collapse();
                    Click(dialog, wxID_BACKWARD);
                    Selection(editor, 139, 145, "Previous must wrap to the final hidden match");
                    Require(editor->Send(Message::GetLineVisible, 13) && !editor->Send(Message::GetLineVisible, 2),
                            "backward wrap must retain an unrelated collapsed outer block");
                    Click(dialog, wxID_BACKWARD);
                    Selection(editor, 33, 39, "Previous must find the first hidden match");
                }
                std::puts("PASS: literal/regex Next, Previous and wrap preserve unrelated nested folds");
                auto* wrap = wxDynamicCast(wxWindow::FindWindowByName("Wrap around", frame), wxCheckBox);
                Require(wrap, "native Find must expose wrap");
                query->SetValue("(?=needle)");
                Toggle(wrap, false);
                collapse();
                Click(dialog, wxID_FORWARD);
                Selection(editor, 33, 33, "zero-width hidden match must retain its byte position");
                Require(editor->Send(Message::GetLineVisible, 2), "zero-width match line must be visible");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 139, 139, "zero-width Next must progress after unfolding");
                Click(dialog, wxID_FORWARD);
                Require(frame->GetStatusBar()->GetStatusText() == "No match found", "zero-width search must exhaust");
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 33, 33, "zero-width Previous must retain revision/progress bookkeeping");
                Toggle(wrap, true);
                Click(dialog, wxID_BACKWARD);
                Selection(editor, 139, 139, "zero-width Previous must wrap to the final match");
                Require(Text(editor) == nested && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "all search directions and zero-width progress must preserve editing history");
                std::puts("PASS: hidden zero-width matches preserve progress, direction changes and wrap");
                std::string distant;
                for (int line = 0; line < 80; ++line)
                    distant += "// pad\n";
                distant += nested;
                load(distant);
                Toggle(regex, false);
                query->SetValue("needle");
                Click(dialog, wxID_FORWARD);
                Selection(editor, 593, 599, "distant folded match must retain its range");
                const auto caretY = editor->Send(Message::PointYFromPosition, 0, 599);
                Require(caretY >= 0 && caretY < editor->GetClientSize().y && editor->Send(Message::GetLineVisible, 82),
                        "Find must scroll the distant revealed selection into the viewport");
                std::puts("PASS: distant folded match is scrolled into the native viewport");
                Command(frame, wxID_REPLACE);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                auto* result = wxDynamicCast(wxWindow::FindWindowByName("Replacement result", frame), wxStaticText);
                Require(replacement && result, "native Replace must expose replacement text and count");
                load(nested);
                editor->Send(Message::GotoPos, 151);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                Command(frame, wxID_UNDO);
                collapse();
                editor->Send(Message::SetSel, 151, 0);
                query->SetValue("needle");
                replacement->SetValue("needle");
                Click(dialog, wxID_REPLACE_ALL);
                Require(result->GetLabel() == "2 replacements" && Text(editor) == nested &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo) && editor->Send(Message::GetAnchor) == 151 &&
                            editor->Send(Message::GetCurrentPos) == 0 && !editor->Send(Message::GetFoldExpanded, 0) &&
                            !editor->Send(Message::GetFoldExpanded, 12),
                        "literal no-op Replace All must retain folds, selection, savepoint and redo");
                Toggle(regex, true);
                query->SetValue("(needle)");
                replacement->SetValue("$1");
                Click(dialog, wxID_REPLACE_ALL);
                Require(result->GetLabel() == "2 replacements" && Text(editor) == nested &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo) && !editor->Send(Message::GetFoldExpanded, 0) &&
                            !editor->Send(Message::GetFoldExpanded, 12),
                        "capture no-op Replace All must retain collapsed folds and editing history");
                std::puts("PASS: literal/capture no-op replacement preserves collapsed folds and redo");
                load(nested);
                query->SetValue("(needle)");
                replacement->SetValue("$1X");
                Click(dialog, wxID_FORWARD);
                Click(dialog, wxID_REPLACE);
                const std::string one = "void outer() {\n  if (true) {\n    needleX();\n  }\n  if (true) {\n"
                                        "    middle();\n  }\n  tail();\n  if (true) {\n    spare();\n  }\n}\n"
                                        "void other() {\n  needle();\n}\n";
                Require(Text(editor) == one, "single replacement must expand the hidden match capture");
                Selection(editor, 140, 146, "single replacement must reveal and select the next hidden match");
                Require(editor->Send(Message::GetLineVisible, 13), "next replacement match must be visible");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == nested && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "single replacement Undo must restore the original savepoint");
                Command(frame, wxID_REDO);
                Require(Text(editor) == one, "single replacement Redo must restore capture bytes");
                std::puts("PASS: single capture replacement and following hidden match retain Undo/Redo");
                load(nested);
                Toggle(regex, false);
                query->SetValue("needle");
                replacement->SetValue("needleX");
                Click(dialog, wxID_REPLACE);
                Selection(editor, 33, 39, "unselected literal Replace must first find and reveal a hidden match");
                Click(dialog, wxID_REPLACE);
                Require(Text(editor) == one && editor->Send(Message::GetLineVisible, 13),
                        "literal single Replace must edit hidden content and reveal the following match");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == nested && !editor->Send(Message::GetModify),
                        "literal single replacement must preserve Undo/savepoint behavior");
                Toggle(regex, true);
                load(nested);
                query->SetValue("(needle)");
                replacement->SetValue("$1\\n    extra");
                Click(dialog, wxID_FORWARD);
                Click(dialog, wxID_REPLACE);
                const std::string singleStructural =
                    "void outer() {\n  if (true) {\n    needle\n    extra();\n  }\n  if (true) {\n"
                    "    middle();\n  }\n  tail();\n  if (true) {\n    spare();\n  }\n}\n"
                    "void other() {\n  needle();\n}\n";
                Require(Text(editor) == singleStructural && editor->Send(Message::GetLineCount) == 17 &&
                            editor->Send(Message::GetLineVisible,
                                         editor->Send(Message::LineFromPosition, editor->Send(Message::GetCurrentPos))),
                        "single structural replacement must retain a visible valid next-match caret");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == nested && !editor->Send(Message::GetModify),
                        "single structural replacement Undo must restore the original bytes/savepoint");
                Command(frame, wxID_REDO);
                Require(Text(editor) == singleStructural,
                        "single structural replacement Redo must restore inserted lines");
                std::puts("PASS: literal and structural single replacement preserve visibility and history");
                const auto replaceAll = [&](const char* find, const char* with, const std::string& expected,
                                            const char* count) {
                    load(nested);
                    query->SetValue(find);
                    replacement->SetValue(with);
                    Click(dialog, wxID_REPLACE_ALL);
                    Require(Text(editor) == expected && result->GetLabel() == count,
                            "Replace All must include hidden matches and report the exact count");
                    Command(frame, wxID_UNDO);
                    Require(Text(editor) == nested && !editor->Send(Message::GetModify) &&
                                !editor->Send(Message::CanUndo),
                            "one Undo must restore all replaced bytes and savepoint");
                    Command(frame, wxID_REDO);
                    Require(Text(editor) == expected, "one Redo must restore the complete replacement");
                };
                const std::string both = "void outer() {\n  if (true) {\n    needleX();\n  }\n  if (true) {\n"
                                         "    middle();\n  }\n  tail();\n  if (true) {\n    spare();\n  }\n}\n"
                                         "void other() {\n  needleX();\n}\n";
                replaceAll("(needle)", "$1X", both, "2 replacements");
                const std::string zero = "void outer() {\n  if (true) {\n    Xneedle();\n  }\n  if (true) {\n"
                                         "    middle();\n  }\n  tail();\n  if (true) {\n    spare();\n  }\n}\n"
                                         "void other() {\n  Xneedle();\n}\n";
                replaceAll("(?=needle)", "X", zero, "2 replacements");
                std::puts("PASS: hidden capture and zero-width Replace All preserve counts and grouped Undo/Redo");
                const std::string inserted =
                    "void outer() {\n  if (true) {\n    needle\n    extra();\n  }\n  if (true) {\n"
                    "    middle();\n  }\n  tail();\n  if (true) {\n    spare();\n  }\n}\n"
                    "void other() {\n  needle\n    extra();\n}\n";
                replaceAll("(needle)", "$1\\n    extra", inserted, "2 replacements");
                editor->Send(Message::Colourise, 0, -1);
                Require(editor->Send(Message::GetLineCount) == 18 &&
                            editor->Send(Message::GetLineVisible,
                                         editor->Send(Message::LineFromPosition, editor->Send(Message::GetCurrentPos))),
                        "inserted lines must retain valid folding and a visible caret");
                const std::string joined = "void outer() {  if (true) {    needle();  }  if (true) {    middle();  }"
                                           "  tail();  if (true) {    spare();  }}void other() {  needle();}";
                replaceAll("\\n", "", joined, "15 replacements");
                editor->Send(Message::Colourise, 0, -1);
                Require(editor->Send(Message::GetLineCount) == 1 && editor->Send(Message::GetLineVisible, 0) &&
                            editor->Send(Message::LineFromPosition, editor->Send(Message::GetCurrentPos)) == 0,
                        "joined folded lines must leave reachable text and a valid caret");
                std::puts("PASS: line insertion/deletion replacements preserve grouping, counts and valid caret");
                const std::string structural = "void outer() \n  if (true) \n    needle();\n  \n  if (true) \n"
                                               "    middle();\n  \n  tail();\n  if (true) \n    spare();\n  \n\n"
                                               "void other() \n  needle();\n\n";
                replaceAll("\\{|\\}", "", structural, "10 replacements");
                editor->Send(Message::Colourise, 0, -1);
                wxYield();
                for (intptr_t line = 0; line < editor->Send(Message::GetLineCount); ++line)
                    Require(editor->Send(Message::GetLineVisible, line),
                            "removed fold headers must leave every line reachable");
                Require(editor->Send(Message::GetLineVisible,
                                     editor->Send(Message::LineFromPosition, editor->Send(Message::GetCurrentPos))),
                        "structural replacement must leave a visible valid caret");
                const auto path = std::filesystem::temp_directory_path() /
                                  ("wxnotepad-fold-search-" + std::to_string(wxGetProcessId()) + ".cpp");
#ifdef __WXMSW__
                const wxString filename(path.wstring());
#else
                const auto filename = wxString::FromUTF8(path.string());
#endif
                Require(frame->SaveAs(filename).status == SaveStatus::Saved, "structural replacement must save");
                std::ifstream saved(path, std::ios::binary);
                const std::string disk(std::istreambuf_iterator<char>(saved), {});
                Require(disk == structural && !editor->Send(Message::GetModify),
                        "Save must write independently observed exact replacement bytes");
                saved.close();
                std::filesystem::remove(path);
                std::puts("PASS: structural replacement leaves reachable text, valid caret and exact saved bytes");
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
wxIMPLEMENT_APP(FoldSearchApp);
