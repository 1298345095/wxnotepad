#include "diagnostics/print.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <cstdio>
#include <print>
#include <stdexcept>
#include <utility>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Case(EditorControl* editor, const char* before, intptr_t anchor, intptr_t caret, const char* after,
          intptr_t expectedCaret) {
    using enum Scintilla::Message;
    // Match EditNewText's history reset when replacing the fixture document.
    editor->Send(SetUndoSelectionHistory, 0);
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(before));
    editor->Send(SetSel, anchor, caret);
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    editor->Send(SetUndoSelectionHistory, 3);
    editor->Send(NewLine);
    if (NativeSearch::Text(editor) != after)
        std::println(stderr, "Before='{}' actual='{}' expected='{}'", before, NativeSearch::Text(editor).c_str(),
                     after);
    Require(NativeSearch::Text(editor) == after, "Enter must produce exact original bytes");
    NativeSearch::Selection(editor, expectedCaret, expectedCaret, "Enter caret placement");
    editor->Send(Undo);
    Require(NativeSearch::Text(editor) == before && !editor->Send(CanUndo) && !editor->Send(GetModify),
            "one Undo restores bytes and save point");
    Require(editor->Send(GetAnchor) == anchor && editor->Send(GetCurrentPos) == caret,
            "Undo restores original selection direction");
    editor->Send(Redo);
    Require(NativeSearch::Text(editor) == after && !editor->Send(CanRedo), "one Redo restores edit");
    NativeSearch::Selection(editor, expectedCaret, expectedCaret, "Redo restores Enter caret");
}
class AutoindentApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                using enum Scintilla::Message;
                auto* editor = frame->Editor();
                editor->Send(SetText, 0, reinterpret_cast<intptr_t>(" \tword"));
                editor->Send(SetSel, 6, 6);
                editor->Send(EmptyUndoBuffer);
                editor->Send(SetSavePoint);
                editor->Send(NewLine);
                Require(NativeSearch::Text(editor) == " \tword\r\n \t",
                        "Enter copies literal leading whitespace by default");
                NativeSearch::Selection(editor, 10, 10, "caret follows copied indentation");
                editor->Send(Undo);
                Require(NativeSearch::Text(editor) == " \tword" && !editor->Send(CanUndo),
                        "Enter and indentation form one undo step");
                std::puts("PASS: default leading whitespace and newline history");
                for (const auto* language : {"Plain Text", "C/C++", "Python"}) {
                    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", language));
                    editor->Send(SetEOLMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::EndOfLine::Lf)));
                    Case(editor, " \tword", 6, 6, " \tword\n \t", 9);
                    Case(editor, "  {}", 3, 3, "  {\n  \t\n  }", 7);
                    Case(editor, "[]", 1, 1, "[\n\t\n]", 3);
                    Case(editor, "()", 1, 1, "(\n\t\n)", 3);
                    Case(editor, "{]", 1, 1, "{\n\t\n]", 3);
                    Case(editor, "  {", 3, 3, "  {\n  \t", 7);
                    Case(editor, "  if x:", 7, 7, "  if x:\n  \t", 11);
                    Case(editor, "  /**", 5, 5, "  /**\n   *", 10);
                    Case(editor, "  /*!", 5, 5, "  /*!\n   *", 10);
                    Case(editor, "  /*", 4, 4, "  /*\n  ", 7);
                    Case(editor, "  *", 3, 3, "  *\n  ", 6);
                    Case(editor, "  // comment", 12, 12, "  // comment\n  ", 15);
                    Case(editor, "  { ", 4, 4, "  { \n  ", 7);
                    Case(editor, "{ }", 1, 1, "{\n\t }", 3);
                    Case(editor, "  {DELETE}", 9, 3, "  {\n  \t\n  }", 7);
                    Case(editor, " \t", 2, 2, " \t\n \t", 5);
                    Case(editor, "word", 0, 0, "\nword", 1);
                    Case(editor, "", 0, 0, "\n", 1);
                }
                std::puts("PASS: delivered schemes preserve delimiter, colon, comment, whitespace and selection rules");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "JSON"));
                Case(editor, "{}", 1, 1, "{\n  \n}", 4);
                editor->Send(SetEOLMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::EndOfLine::CrLf)));
                Case(editor, "[]", 1, 1, "[\r\n  \r\n]", 5);
                editor->Send(SetEOLMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::EndOfLine::Cr)));
                Case(editor, "()", 1, 1, "(\r  \r)", 4);
                editor->Send(SetTabWidth, 7);
                editor->Send(SetIndent, 3);
                Case(editor, "{", 1, 1, "{\r       ", 9);
                editor->Send(SetTabIndents, 0);
                Case(editor, "{", 1, 1, "{\r   ", 5);
                editor->Send(SetUseTabs, 1);
                Case(editor, "{", 1, 1, "{\r   ", 5);
                editor->Send(SetIndent, 0);
                Case(editor, "{", 1, 1, "{\r", 2);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Auto Indent"));
                Require(!editor->AutoIndentEnabled(), "Auto Indent menu disables the original option");
                Case(editor, " \t{}", 3, 3, " \t{\r}", 4);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Auto Indent"));
                Require(editor->AutoIndentEnabled(), "Auto Indent menu enables the original option");
                std::puts("PASS: JSON defaults, CRLF/LF/CR, independent widths and enabled/disabled options");
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                result_ = 1;
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(AutoindentApp);
