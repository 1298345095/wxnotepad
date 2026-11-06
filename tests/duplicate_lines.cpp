#include "app/editor_frame.h"
#include "file_helpers.h"
#include "native_search_helpers.h"
#include "settings/settings_store.h"
#include <wx/app.h>
#include <wx/menu.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Reset(EditorControl* editor, const char* text, intptr_t anchor, intptr_t caret,
           Scintilla::EndOfLine mode = Scintilla::EndOfLine::Lf) {
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(mode));
    editor->Send(Message::SetSel, anchor, caret);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
int CommandId(EditorFrame* frame, bool merge) {
    const int id =
        frame->GetMenuBar()->FindMenuItem("Edit", merge ? "Merge Duplicate Lines" : "Remove Duplicate Lines");
    Require(id != wxNOT_FOUND, "native Edit menu exposes both original duplicate-line commands");
    return id;
}
void Command(EditorFrame* frame, bool merge) {
    NativeSearch::Command(frame, CommandId(frame, merge));
}
void NonadjacentGroups(EditorFrame* frame) {
    for (const bool merge : {true, false}) {
        Reset(frame->Editor(), "A\nB\nA\nC", 0, 7);
        Command(frame, merge);
        Require(TestEditor::Text(frame) == (merge ? "A\nB\nC" : "B\nC"),
                "Merge keeps first occurrence; Remove eliminates every repeated-group member in original order");
    }
    std::puts("PASS: native Merge/Remove nonadjacent duplicate groups retain original order");
}
void Range(EditorControl* editor, intptr_t anchor, intptr_t caret) {
    if (editor->Send(Message::GetAnchor) != anchor || editor->Send(Message::GetCurrentPos) != caret)
        std::fprintf(stderr, "anchor/caret %lld/%lld; expected %lld/%lld\n",
                     static_cast<long long>(editor->Send(Message::GetAnchor)),
                     static_cast<long long>(editor->Send(Message::GetCurrentPos)), static_cast<long long>(anchor),
                     static_cast<long long>(caret));
    Require(editor->Send(Message::GetAnchor) == anchor && editor->Send(Message::GetCurrentPos) == caret,
            "original selection direction and replacement extent");
}
void History(EditorControl* editor, const std::string& before, const std::string& after) {
    Require(editor->Send(Message::CanUndo) && editor->Send(Message::GetModify),
            "ordinary replacement records history even for identical bytes");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == before && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
            "one Undo restores original bytes and save point");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == after && !editor->Send(Message::CanRedo) && editor->Send(Message::GetModify),
            "one Redo restores complete replacement");
}
void StreamFixtures(EditorFrame* frame) {
    struct Fixture {
        bool merge;
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        const char* after;
        intptr_t resultAnchor;
        intptr_t resultCaret;
        Scintilla::EndOfLine mode = Scintilla::EndOfLine::Lf;
    };
    // Literal results from EditSortLines(DontSort | MergeDuplicate/RemoveDuplicate), 89b8b36.
    const Fixture fixtures[] = {
        {true, "A\nB\nA\nC\nA", 0, 9, "A\nB\nC", 0, 5},
        {false, "A\nB\nA\nC\nA", 9, 0, "B\nC", 3, 0},
        {true, "PRE\nA\nB\nA\nTAIL", 5, 10, "PRE\nA\nB\nTAIL", 4, 8},
        {false, "PRE\nA\nB\nA\nTAIL", 10, 5, "PRE\nB\nTAIL", 6, 4},
        {true, "PRE\nA\nB\nA\nTAIL", 5, 9, "PRE\nA\nB\nTAIL", 4, 8},
        {true, "a\nA\na\nB\nB", 0, 9, "a\nA\nB", 0, 5},
        {false, "a\nA\na\nB\nB", 0, 9, "A", 0, 1},
        {true, "\nA\n\nB\n", 0, 6, "\nA\nB\n", 0, 5},
        {false, "\nA\n\nB\n", 0, 6, "A\nB\n", 0, 4},
        {true, "A\nA\n", 0, 4, "A\n", 0, 2},
        {false, "A\nA\n", 0, 4, "", 0, 0},
        {true, "A\nA", 0, 3, "A", 0, 1},
        {false, "A\nA", 0, 3, "", 0, 0},
        {true, "A\r\nB\nA\rC", 0, 8, "A\r\nB\r\nC", 0, 7, Scintilla::EndOfLine::CrLf},
        {false, "A\r\nB\nA\rC", 8, 0, "B\r\nC", 4, 0, Scintilla::EndOfLine::CrLf},
        {true, "a\nb\na\n", 0, 6, "a\rb\r", 0, 4, Scintilla::EndOfLine::Cr},
        {false, "a\nb\na\n", 0, 6, "b\r", 0, 2, Scintilla::EndOfLine::Cr},
        {true, "中\n😀\n中\n尾", 0, 16, "中\n😀\n尾", 0, 12},
        {false, "中\n😀\n中\n尾", 16, 0, "😀\n尾", 8, 0},
        {true, "é\né\né\nX", 0, 12, "é\né\nX", 0, 8},
        {true, "A\nB", 1, 3, "A\nB", 0, 3},
        {false, "A\nB", 3, 1, "A\nB", 3, 0},
    };
    auto* editor = frame->Editor();
    for (const auto& fixture : fixtures) {
        Reset(editor, fixture.before, fixture.anchor, fixture.caret, fixture.mode);
        Command(frame, fixture.merge);
        if (TestEditor::Text(editor) != fixture.after)
            std::fprintf(stderr, "merge=%d before=<%s> actual=<%s> expected=<%s>\n", fixture.merge, fixture.before,
                         TestEditor::Text(editor).c_str(), fixture.after);
        Require(TestEditor::Text(editor) == fixture.after, "literal original stream duplicate-line result");
        Range(editor, fixture.resultAnchor, fixture.resultCaret);
        Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(fixture.mode), "current EOL mode survives");
        History(editor, fixture.before, fixture.after);
    }
    std::puts("PASS: case-sensitive nonadjacent groups, empty/Unicode lines, partial/reversed endpoints, current EOL "
              "construction, final termination and identical replacement history");
}
void AvailabilityAndNoChange(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool merge : {true, false}) {
        const int id = CommandId(frame, merge);
        Require(frame->GetMenuBar()->FindItem(id)->GetItemLabel().Find('\t') == wxNOT_FOUND,
                "original duplicate-line commands have no accelerator");
        for (const auto caret : {0, 1, 2}) {
            Reset(editor, "A\nA", 0, caret);
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            Require(frame->GetMenuBar()->IsEnabled(id) == (caret == 2),
                    "original menu counts endpoint logical lines before end-at-start exclusion");
            if (merge)
                editor->MergeDuplicateLines();
            else
                editor->RemoveDuplicateLines();
            Require(TestEditor::Text(editor) == "A\nA" && !editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::GetModify),
                    "caret and fewer than two effective lines return without replacement");
            Range(editor, 0, caret);
        }
        Reset(editor, "A\nB\nA\nC", 1, 7);
        editor->Send(Message::SetReadOnly, 1);
        const auto revision = editor->DocumentRevision();
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(id),
                "original duplicate-line menu remains enabled for read-only selection");
        Command(frame, merge);
        Require(TestEditor::Text(editor) == "A\nB\nA\nC" && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify) && editor->DocumentRevision() == revision,
                "read-only core refuses byte changes, history and revision");
        Range(editor, 0, merge ? 5 : 3);
        editor->Send(Message::SetReadOnly, 0);

        Reset(editor, "A\nB", 0, 0);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, 0, 2);
        Command(frame, merge);
        Require(editor->Send(Message::CanRedo) && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                "effective single-line early return preserves pending Redo and save point");
        editor->Send(Message::SetSel, 0, 3);
        Command(frame, merge);
        Require(!editor->Send(Message::CanRedo), "identical multiline replacement discards pending Redo");
        History(editor, "A\nB", "A\nB");
    }
    std::puts("PASS: original menu count, no accelerators, read-only selection repair, early no-op and pending Redo");
}
void Rectangle(EditorControl* editor, intptr_t anchor, intptr_t caret, intptr_t anchorVirtual = 0,
               intptr_t caretVirtual = 0) {
    editor->Send(Message::SetRectangularSelectionAnchor, anchor);
    editor->Send(Message::SetRectangularSelectionCaret, caret);
    editor->Send(Message::SetRectangularSelectionAnchorVirtualSpace, anchorVirtual);
    editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, caretVirtual);
}
void Rectangles(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->SetIndentationWidths(4, 4);
    for (const bool reverse : {false, true}) {
        for (const bool merge : {true, false}) {
            Reset(editor, "A\nlong\nA\nlong\nTAIL", 0, 0);
            Rectangle(editor, reverse ? 12 : 1, reverse ? 1 : 12);
            Command(frame, merge);
            const char* after = merge ? "A  \nlong\nTAIL" : "TAIL";
            Require(TestEditor::Text(editor) == after,
                    "rectangle reuses padding then compares complete lines, retaining original order");
            if (merge) {
                Require(editor->Send(Message::SelectionIsRectangle) &&
                            editor->Send(Message::GetRectangularSelectionAnchor) == (reverse ? 7 : 1) &&
                            editor->Send(Message::GetRectangularSelectionCaret) == (reverse ? 1 : 7),
                        "original rectangle endpoint columns and vertical direction restored over result lines");
            } else {
                Require(!editor->Send(Message::SelectionIsRectangle),
                        "empty replacement collapses rectangle to stream");
                Range(editor, 0, 0);
            }
            editor->Send(Message::Undo);
            Require(TestEditor::Text(editor) == "A\nlong\nA\nlong\nTAIL" && !editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::GetModify),
                    "one Undo combines rectangle padding and duplicate replacement");
            Require(editor->Send(Message::SelectionIsRectangle) &&
                        editor->Send(Message::GetRectangularSelectionAnchor) == (reverse ? 12 : 1) &&
                        editor->Send(Message::GetRectangularSelectionCaret) == (reverse ? 1 : 12),
                    "core Undo restores original rectangular selection");
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == after, "Redo restores padding plus duplicate replacement");
        }
    }
    Reset(editor, "AB\n\nA\nAB", 0, 0);
    Rectangle(editor, 0, 8, 0, 2);
    Command(frame, true);
    Require(TestEditor::Text(editor) == "AB\n\nA ",
            "rectangle padding skips truly empty lines before duplicate comparison");
    Require(editor->Send(Message::GetRectangularSelectionAnchorVirtualSpace) == 0 &&
                editor->Send(Message::GetRectangularSelectionCaretVirtualSpace) == 0,
            "original FindColumn restoration clears virtual columns");
    History(editor, "AB\n\nA\nAB", "AB\n\nA ");
    Reset(editor, "A\nA", 0, 0, Scintilla::EndOfLine::CrLf);
    Rectangle(editor, 0, 3);
    Command(frame, true);
    Require(TestEditor::Text(editor) == "A" && editor->Send(Message::SelectionIsRectangle) &&
                editor->Send(Message::GetRectangularSelectionAnchor) == 0 &&
                editor->Send(Message::GetRectangularSelectionCaret) == 1,
            "unterminated result retains original EOL subtraction and single-line rectangle extent");
    History(editor, "A\nA", "A");
    std::puts("PASS: forward/reverse rectangle padding, full-line equality, skip-empty, virtual columns, original "
              "unterminated endpoint restoration, empty result and grouped history");
}
void SavedBytes(EditorFrame* frame) {
    const auto directory = TestFiles::TemporaryDirectory("duplicate-lines");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string merged;
        std::string removed;
    };
    const Fixture fixtures[] = {
        {"utf8-bom", FileEncoding::Utf8, "\xef\xbb\xbf中\r\nB\n中\rC", "\xef\xbb\xbf中\r\nB\r\nC",
         "\xef\xbb\xbf"
         "B\r\nC"},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\r\nB\n\xd6\xd0\rC", "\xd6\xd0\r\nB\r\nC", "B\r\nC"},
        {"utf16-bom", FileEncoding::Utf16Le,
         std::string("\xff\xfe\x2d\x4e\x0d\0\x0a\0\x42\0\x0a\0\x2d\x4e\x0d\0\x43\0", 18),
         std::string("\xff\xfe\x2d\x4e\x0d\0\x0a\0\x42\0\x0d\0\x0a\0\x43\0", 16),
         std::string("\xff\xfe\x42\0\x0d\0\x0a\0\x43\0", 10)},
    };
    for (const auto& fixture : fixtures) {
        for (const bool merge : {true, false}) {
            const auto path = directory / (std::string(fixture.name) + (merge ? "-merge.txt" : "-remove.txt"));
            {
                std::ofstream file(path, std::ios::binary);
                file.write(fixture.before.data(), fixture.before.size());
            }
            frame->Editor()->Send(Message::SetSavePoint);
            Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                    "saved-byte fixture opens through application");
            auto* editor = frame->Editor();
            editor->Send(Message::SetSel, 0, editor->Send(Message::GetLength));
            Command(frame, merge);
            const auto& after = merge ? fixture.merged : fixture.removed;
            Require(frame->DocumentEncoding() == fixture.encoding && frame->Save().status == SaveStatus::Saved &&
                        TestFiles::Bytes(path) == after,
                    "Save preserves encoding/BOM with original current-mode EOL reconstruction");
            editor->Send(Message::Undo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                    "one Undo and Save restores independent original disk bytes");
            editor->Send(Message::Redo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == after,
                    "one Redo and Save restores independent transformed disk bytes");
            std::printf("PASS: %s %s independent saved bytes, encoding/BOM/EOL and Undo/Redo\n", fixture.name,
                        merge ? "merge" : "remove");
        }
    }
    std::filesystem::remove_all(directory);
}
class DuplicateLinesApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        settings_->values.warnLineEndings = false;
        settings_->values.fixLineEndings = false;
        settings_->values.fixTrailingBlanks = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                NonadjacentGroups(frame);
                StreamFixtures(frame);
                AvailabilityAndNoChange(frame);
                Rectangles(frame);
                SavedBytes(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(DuplicateLinesApp);
