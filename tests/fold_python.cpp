#include "editor/editor_control.h"
#include <array>
#include "app/editor_frame.h"
#include "file_helpers.h"
#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include "native_key_helpers.h"
#include <ScintillaTypes.h>
#include <wx/wx.h>
#include <cstdio>
#include <print>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
using NativeFold::Paint;
void MarginClick(EditorControl* editor, int line) {
    NativeFold::MarginClick(editor, line, "Python must expose a DPI-scaled fold margin");
}
bool Header(EditorControl* editor, int line) {
    return (static_cast<uintptr_t>(editor->Send(Message::GetFoldLevel, line)) &
            static_cast<uintptr_t>(std::to_underlying(Scintilla::FoldLevel::HeaderFlag))) != 0;
}
int Level(EditorControl* editor, size_t line) {
    return static_cast<int>(static_cast<uintptr_t>(editor->Send(Message::GetFoldLevel, line)) & 0xfffU);
}
void Reachable(EditorControl* editor) {
    for (intptr_t line = 0; line < editor->Send(Message::GetLineCount); ++line)
        Require(editor->Send(Message::GetLineVisible, static_cast<uintptr_t>(line)),
                "every line must remain reachable after removing or resetting a fold");
}
void Replace(EditorControl* editor, size_t start, size_t length, const char* text) {
    using enum Scintilla::Message;
    editor->Send(SetTargetStart, start);
    editor->Send(SetTargetEnd, start + length);
    editor->Send(ReplaceTarget, std::char_traits<char>::length(text), reinterpret_cast<intptr_t>(text));
    Paint(editor);
}
void PythonNativeFolds(EditorFrame* frame, const std::string& source) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Python"));
    Paint(editor);
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    editor->Send(GotoPos, 0);
    // Literal levels follow the pinned FoldPyDoc: blank lines adopt
    // the next nonempty indentation; consecutive comments form a fold.
    const std::array<int, 13> levels = {{0, 4, 5, 4, 4, 8, 12, 8, 8, 8, 4, 0, 0}};
    for (size_t line = 0; line < std::size(levels); ++line) {
        Require(Level(editor, line) == 0x400 + levels[line],
                "Python suite, blank and comment levels must match the pinned lexer");
        Require(Header(editor, static_cast<int>(line)) == (line == 0 || line == 1 || line == 4 || line == 5),
                "only pinned class, method, nested suite and comment group headers may fold");
    }
    MarginClick(editor, 4);
    Require(!editor->Send(GetFoldExpanded, 4) && !editor->Send(GetLineVisible, 5) && editor->Send(GetLineVisible, 10),
            "native method header click must collapse only its lexer-defined suite");
    Require(NativeSearch::Text(editor) == source && !editor->Send(GetModify) && !editor->Send(CanUndo),
            "folding must preserve bytes, save point and history");
    std::puts("PASS: native Python method fold uses original lexer without editing");
    MarginClick(editor, 0);
    MarginClick(editor, 0);
    Require(editor->Send(GetLineVisible, 4) && !editor->Send(GetLineVisible, 5) && !editor->Send(GetFoldExpanded, 4),
            "reopening a class must retain a collapsed method");
    MarginClick(editor, 10);
    Require(!editor->Send(GetFoldExpanded, 4) && editor->Send(GetFoldExpanded, 0),
            "non-header native click must leave enclosing Python folds unchanged");
    MarginClick(editor, 4);
    MarginClick(editor, 1);
    Require(!editor->Send(GetLineVisible, 2) && editor->Send(GetLineVisible, 3),
            "native comment-group toggle must follow the pinned comment range");
    MarginClick(editor, 1);
    const auto caret = editor->Send(PositionFromLine, 6) + 12;
    editor->Send(SetSel, caret + 2, caret);
    MarginClick(editor, 4);
    for (const auto endpoint : {GetCurrentPos, GetAnchor})
        Require(editor->Send(GetLineVisible, editor->Send(LineFromPosition, editor->Send(endpoint))),
                "native contraction must leave caret and anchor visible");
    MarginClick(editor, 4);
    std::puts("PASS: Python nested state, non-header clicks, comment folds and caret visibility");
}
void PythonFoldEdits(EditorFrame* frame, const std::string& source) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    MarginClick(editor, 5);
    Replace(editor, source.find("            call()"), 4, "");
    Require(!Header(editor, 5) && editor->Send(GetLineVisible, 6),
            "dedenting the only suite body must remove its collapsed header and reveal the body");
    NativeSearch::Command(frame, wxID_UNDO);
    Paint(editor);
    Require(Header(editor, 5) && NativeSearch::Text(editor) == source && !editor->Send(GetModify) &&
                editor->Send(CanRedo),
            "Undo of dedent must restore the original fold and save point");
    MarginClick(editor, 5);
    MarginClick(editor, 5);
    Require(editor->Send(CanRedo) && !editor->Send(GetModify), "fold toggles must preserve redo and save point");
    NativeSearch::Command(frame, wxID_REDO);
    Paint(editor);
    Require(!Header(editor, 5) && editor->Send(GetLineVisible, 6),
            "Redo of dedent must recalculate the removed header with reachable text");
    NativeSearch::Command(frame, wxID_UNDO);
    Paint(editor);
    Replace(editor, source.find("        tail()"), 0, "    ");
    Require(Level(editor, 7) == 0x40c && Level(editor, 8) == 0x40c && editor->Send(GetLastChild, 5, -1) == 9,
            "indenting tail must extend the suite and move intervening blank/comment levels");
    NativeSearch::Command(frame, wxID_UNDO);
    Paint(editor);
    Require(editor->Send(GetLastChild, 5, -1) == 6 && NativeSearch::Text(editor) == source,
            "Undo of indent must restore the pinned suite boundary");
    NativeSearch::Command(frame, wxID_REDO);
    Paint(editor);
    Require(editor->Send(GetLastChild, 5, -1) == 9, "Redo of indent must restore the extended suite boundary");
    NativeSearch::Command(frame, wxID_UNDO);
    Paint(editor);
    MarginClick(editor, 0);
    Replace(editor, 0, source.find("after()"), "");
    Reachable(editor);
    Require(!Header(editor, 0), "removing a collapsed class must clear its header");
    NativeSearch::Command(frame, wxID_UNDO);
    Paint(editor);
    Reachable(editor);
    Require(Header(editor, 0) && NativeSearch::Text(editor) == source && !editor->Send(GetModify),
            "Undo of class removal must restore text, headers and save point");
    NativeSearch::Command(frame, wxID_REDO);
    Paint(editor);
    Reachable(editor);
    NativeSearch::Command(frame, wxID_UNDO);
    Paint(editor);
    std::puts("PASS: indent, dedent, header removal and Undo/Redo preserve pinned fold structure");
}
void PythonDocumentTransitions(EditorFrame* frame, const std::string& source) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-python-fold");
    std::filesystem::create_directory(directory);
    const auto path = directory / "fold.py";
    const auto filename = TestFiles::Filename(path);
    Require(frame->SaveAs(filename).status == SaveStatus::Saved, "Python fixture must save");
    MarginClick(editor, 0);
    Require(frame->Save().status == SaveStatus::Saved && !editor->Send(GetFoldExpanded, 0),
            "ordinary Save must retain Python contraction state");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Python"));
    Require(!editor->Send(GetFoldExpanded, 0), "repeated Python choice must retain folds");
    Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled &&
                frame->Open(filename + ".missing").status == OpenStatus::Failed && !editor->Send(GetFoldExpanded, 0),
            "cancelled and failed Open must retain Python folds");
    std::ifstream saved(path, std::ios::binary);
    Require(std::string(std::istreambuf_iterator<char>(saved), {}) == source,
            "fold operations and Save must preserve independently read disk bytes");
    saved.close();
    for (const auto* language : {"Plain Text", "C/C++", "JSON"}) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", language));
        Paint(editor);
        Reachable(editor);
        if (std::string(language) == "Plain Text")
            Require(editor->Send(GetMarginWidthN, 2) > 0, "Plain Text must retain the original fold margin");
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Python"));
        Paint(editor);
        Reachable(editor);
        Require(Header(editor, 4) && editor->Send(GetFoldExpanded, 4) && NativeSearch::Text(editor) == source &&
                    !editor->Send(GetModify) && editor->Send(CanRedo),
                "returning to Python must start expanded and retain bytes, save point and redo");
        MarginClick(editor, 0);
    }
    Require(frame->Open(filename).status == OpenStatus::Opened, "same-language Open must succeed");
    Paint(editor);
    Reachable(editor);
    Require(Header(editor, 4) && editor->Send(GetFoldExpanded, 4),
            "successful Python Open must reset contraction state");
    MarginClick(editor, 0);
    NativeSearch::Command(frame, wxID_NEW);
    Paint(editor);
    Require(editor->Send(GetLength) == 0 && editor->Send(GetMarginWidthN, 2) > 0,
            "New must clear Python text and retain the original Plain Text folding margin");
    std::filesystem::remove_all(directory);
    std::puts("PASS: Python language/document transitions and saved bytes retain view/history invariants");
}
class FoldApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native Python fold view is presented before observing paint and margins");
                const std::string source = "class Sample:\n"
                                           "    # first\n"
                                           "    # second\n"
                                           "\n"
                                           "    def method(self):\n"
                                           "        if ready:\n"
                                           "            call()\n"
                                           "\n"
                                           "        # tail note\n"
                                           "        tail()\n"
                                           "    value = 1\n"
                                           "after()\n";
                PythonNativeFolds(frame, source);
                PythonFoldEdits(frame, source);
                PythonDocumentTransitions(frame, source);
                frame->Destroy();
                ExitMainLoop();
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
                result_ = 1;
                frame->Destroy();
                ExitMainLoop();
            }
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(FoldApp);
