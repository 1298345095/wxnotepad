#include "native_key_helpers.h"
#include "native_fold_helpers.h"
#include "block_navigation_fixtures.h"
#include "file_helpers.h"
#include <cstdio>
#include <stdexcept>
#include <span>
#include <memory>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
constexpr const char* names[] = {"Goto Block Start",
                                 "Goto Block End",
                                 "Goto Previous Block",
                                 "Goto Next Block",
                                 "Goto Previous Sibling Block",
                                 "Goto Next Sibling Block",
                                 "Select Lines in Current Block"};
void Position(EditorControl* editor, const BlockReference::Fixture& row) {
    if (editor->Send(Message::GetAnchor) != row.expectedAnchor ||
        editor->Send(Message::GetCurrentPos) != row.expectedCaret ||
        editor->Send(Message::GetSelectionMode) != row.selectionMode ||
        editor->Send(Message::GetSelections) != row.selections ||
        editor->Send(Message::GetRectangularSelectionAnchorVirtualSpace) != row.anchorVirtual ||
        editor->Send(Message::GetRectangularSelectionCaretVirtualSpace) != row.caretVirtual) {
        std::fprintf(stderr,
                     "case doc=%d command=%d mode=%d setup=%d input=%ld/%ld actual=%lld/%lld mode=%lld selections=%lld "
                     "expected=%ld/%ld mode=%d selections=%d\n",
                     row.document, row.command, row.mode, row.setup, row.anchor, row.caret,
                     static_cast<long long>(editor->Send(Message::GetAnchor)),
                     static_cast<long long>(editor->Send(Message::GetCurrentPos)),
                     static_cast<long long>(editor->Send(Message::GetSelectionMode)),
                     static_cast<long long>(editor->Send(Message::GetSelections)), row.expectedAnchor,
                     row.expectedCaret, row.selectionMode, row.selections);
        throw std::runtime_error("block command must reproduce independently captured original selection");
    }
}
void Visible(EditorControl* editor) {
    const auto position = editor->Send(Message::GetCurrentPos);
    const auto x = editor->Send(Message::PointXFromPosition, 0, position);
    const auto y = editor->Send(Message::PointYFromPosition, 0, position);
    Require(x >= 0 && x < editor->GetClientSize().x && y >= 0 && y < editor->GetClientSize().y,
            "block navigation reveals its actual target on both axes");
}
void ReferenceCase(EditorFrame* frame, const int* commands, const int* modeIds, const BlockReference::Document& source,
                   const BlockReference::Fixture& row, FileEncoding encoding, bool bom) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::FoldAll, static_cast<uintptr_t>(Scintilla::FoldAction::Expand));
    editor->Send(Message::SetSelectionMode, 0);
    NativeSearch::Command(frame, modeIds[row.mode]);
    editor->Send(Message::SetSel, row.anchor, row.caret);
    if (row.setup == 1) {
        editor->Send(Message::FoldAll, static_cast<uintptr_t>(Scintilla::FoldAction::Contract));
        editor->Send(Message::SetSel, row.anchor, row.caret);
    } else if (row.setup == 2)
        editor->Send(Message::SetReadOnly, 1);
    else if (row.setup == 3) {
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, row.anchor);
        editor->Send(Message::SetRectangularSelectionCaret, row.caret);
        editor->Send(Message::SetRectangularSelectionAnchorVirtualSpace, 3);
        editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, 2);
    } else if (row.setup == 4)
        editor->Send(Message::AddSelection, 0, 0);
    const auto beforeCaret = editor->Send(Message::GetCurrentPos);
    const auto undo = editor->Send(Message::CanUndo);
    const auto redo = editor->Send(Message::CanRedo);
    NativeSearch::Command(frame, commands[row.command]);
    Position(editor, row);
    Require(NativeSearch::Text(editor) == source.text && !editor->Send(Message::GetModify) &&
                editor->Send(Message::CanUndo) == undo && editor->Send(Message::CanRedo) == redo &&
                frame->DocumentEncoding() == encoding && frame->DocumentHasBom() == bom,
            "block commands preserve bytes, encoding, save point and undo/redo availability");
    for (intptr_t line = 0; line < editor->Send(Message::GetLineCount); ++line) {
        if (editor->Send(Message::GetLineVisible, line) != row.visible[line] - '0') {
            std::fprintf(stderr, "visibility doc=%d command=%d setup=%d line=%lld\n", row.document, row.command,
                         row.setup, static_cast<long long>(line));
            throw std::runtime_error("block command must reproduce original folded-line visibility");
        }
        Require(editor->Send(Message::GetFoldExpanded, line) == row.expanded[line] - '0',
                "block commands reproduce original expanded headers and retain unrelated folds");
    }
    if (row.command < 6 && beforeCaret != row.expectedCaret) Visible(editor);
}
void ReferenceCases(EditorFrame* frame, const int* commands) {
    auto* editor = frame->Editor();
    const int modeIds[] = {frame->GetMenuBar()->FindMenuItem("Settings", "None"),
                           frame->GetMenuBar()->FindMenuItem("Settings", "Visual Studio"),
                           frame->GetMenuBar()->FindMenuItem("Settings", "Normal"),
                           frame->GetMenuBar()->FindMenuItem("Settings", "Old Visual Studio")};
    const auto encoding = frame->DocumentEncoding();
    const bool bom = frame->DocumentHasBom();
    for (size_t document = 0; document < std::size(BlockReference::documents); ++document) {
        const auto& source = BlockReference::documents[document];
        editor->Send(Message::SetReadOnly, 0);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", source.language));
        editor->Send(Message::SetSelectionMode, 0);
        editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(source.text));
        editor->Send(Message::SetTabWidth, 4);
        editor->Send(Message::EmptyUndoBuffer);
        editor->Send(Message::ClearAll);
        editor->Send(Message::Undo);
        editor->Send(Message::SetSavePoint);
        // Undo invalidates styling from the start of the restored text. Prepare
        // the captured fold tree after the destructive history setup.
        editor->Send(Message::Colourise, 0, -1);
        NativeFold::Paint(editor);
        // All five original documents have a captured fold header at line zero.
        // Release lexers may also style the EOF sentinel (Length()+1).
        Require(editor->Send(Message::GetEndStyled) >= editor->Send(Message::GetLength) &&
                    (editor->Send(Message::GetFoldLevel, 0) & 0x2000) != 0,
                "original fixture's styling and first-line fold header are ready after history setup");
        for (const auto& row : BlockReference::fixtures) {
            if (row.document != static_cast<int>(document)) continue;
            ReferenceCase(frame, commands, modeIds, source, row, encoding, bom);
        }
        editor->Send(Message::SetReadOnly, 0);
        editor->Send(Message::Redo);
        Require(NativeSearch::Text(editor).empty(), "pending original edit still redoes after block commands");
        editor->Send(Message::Undo);
        Require(NativeSearch::Text(editor) == source.text && !editor->Send(Message::GetModify),
                "undo still restores the source document and its save point");
    }
    editor->Send(Message::ClearAll);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    for (const int command : std::span(commands, std::size(names)))
        Require(!frame->GetMenuBar()->IsEnabled(command), "original block menus disable on an empty document");
    std::puts("PASS: original block navigation/selection runtime fixtures, folding, visibility and preserved document "
              "history");
}
void LoadKeyboardFixture(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetReadOnly, 0);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    editor->Send(Message::SetSelectionMode, 0);
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(BlockReference::documents[0].text));
    editor->Send(Message::SetTabWidth, 4);
    editor->Send(Message::Colourise, 0, -1);
    editor->SetLineSelectionMode(LineSelectionMode::VisualStudio);
    editor->Send(Message::SetSel, 27, 27);
}
void ExcludedShortcut(EditorFrame* frame, int key, bool control, bool shift) {
    auto* editor = frame->Editor();
    const bool valid = key == '[' ? !control && !shift : key == ']' ? !control : !shift;
    if (valid) return;
    LoadKeyboardFixture(frame);
    NativeKeys::Shortcut(frame, key, control, true, shift);
    Require(editor->Send(Message::GetAnchor) == 27 && editor->Send(Message::GetCurrentPos) == 27 &&
                editor->Send(Message::GetSelectionMode) == 0 &&
                NativeSearch::Text(editor) == BlockReference::documents[0].text,
            "additional native Ctrl/Shift modifiers must follow original no-command routing");
}
void KeyboardCases(EditorFrame* frame, const int* commands) {
    auto* editor = frame->Editor();
    const int keys[] = {'[', ']', ',', '.', ',', '.', ']'};
    for (size_t index = 0; index < std::size(keys); ++index) {
        const bool control = index == 4 || index == 5;
        const bool shift = index == 6;
        const auto accelerator =
            std::unique_ptr<wxAcceleratorEntry>(frame->GetMenuBar()->FindItem(commands[index])->GetAccel());
        Require(accelerator && accelerator->GetKeyCode() == keys[index] &&
                    accelerator->GetFlags() ==
                        (wxACCEL_ALT | (control ? wxACCEL_CTRL : 0) | (shift ? wxACCEL_SHIFT : 0)),
                "block menu accelerators reproduce original punctuation and exact modifiers");
        LoadKeyboardFixture(frame);
        NativeKeys::Shortcut(frame, keys[index], control, true, shift);
        const auto row = std::find_if(std::begin(BlockReference::fixtures), std::end(BlockReference::fixtures),
                                      [index](const auto& row) {
                                          return row.document == 0 && row.command == static_cast<int>(index) &&
                                                 row.mode == 1 && row.setup == 0 && row.anchor == 27 && row.caret == 27;
                                      });
        Require(row != std::end(BlockReference::fixtures), "native shortcut has an independent original runtime case");
        Position(editor, *row);
        Require(NativeSearch::Text(editor) == BlockReference::documents[0].text,
                "native punctuation navigation preserves document bytes");
    }
    for (const int key : {'[', ']', ',', '.'}) {
        for (const bool control : {false, true}) {
            for (const bool shift : {false, true}) {
                ExcludedShortcut(frame, key, control, shift);
            }
        }
    }
    LoadKeyboardFixture(frame);
    editor->Send(Message::SetSel, 2, 27);
    NativeKeys::Shortcut(frame, ',', true, false, true);
    Require(editor->Send(Message::GetAnchor) == 27 && editor->Send(Message::GetCurrentPos) == 2,
            "Ctrl+Shift+Comma retains the original selection-boundary shortcut");
    NativeKeys::Shortcut(frame, '.', true, false, true);
    Require(editor->Send(Message::GetAnchor) == 2 && editor->Send(Message::GetCurrentPos) == 27,
            "Ctrl+Shift+Period changes the caret endpoint while retaining selected text");
    std::puts("PASS: owned native OEM/punctuation shortcuts, exact added modifiers and selection-boundary routing");
}
void FilePolicyCases(EditorFrame* frame, const int* commands) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("block-policy");
    std::filesystem::create_directory(directory);
    const auto path = directory / "source.cpp";
    const std::string text = BlockReference::documents[3].text;
    std::string bytes("\xff\xfe", 2);
    for (const char character : text) {
        bytes.push_back(character);
        bytes.push_back('\0');
    }
    TestFiles::WriteBytes(path, bytes);
    editor->Send(Message::SetSavePoint);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Automatic"));
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                frame->DocumentEncoding() == FileEncoding::Utf16Le && frame->DocumentHasBom(),
            "block policy fixture opens an independently encoded UTF-16LE document");
    editor->SetLineSelectionMode(LineSelectionMode::VisualStudio);
    editor->Send(Message::Colourise, 0, -1);
    NativeFold::Paint(editor);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSelectionMode, 0);
    editor->Send(Message::SetSel, 6, 6);
    NativeSearch::Command(frame, commands[6]);
    // Original no-parent captures select the whole document with the caret at zero.
    Require(editor->Send(Message::GetAnchor) == 16 && editor->Send(Message::GetCurrentPos) == 0 &&
                editor->Send(Message::GetSelectionMode) == 0,
            "automatic C++ syntax uses original Select All fallback when the document has no fold parent");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Plain Text"));
    editor->Send(Message::Colourise, 0, -1);
    NativeFold::Paint(editor);
    editor->Send(Message::SetSelectionMode, 0);
    editor->Send(Message::SetSel, 6, 6);
    NativeSearch::Command(frame, commands[6]);
    const auto row =
        std::find_if(std::begin(BlockReference::fixtures), std::end(BlockReference::fixtures), [](const auto& row) {
            return row.document == 3 && row.command == 6 && row.mode == 1 && row.setup == 0 && row.anchor == 6 &&
                   row.caret == 6;
        });
    Require(row != std::end(BlockReference::fixtures),
            "Plain Text policy has an independently captured original block");
    Position(editor, *row);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Automatic"));
    editor->Send(Message::Colourise, 0, -1);
    NativeFold::Paint(editor);
    editor->Send(Message::SetSelectionMode, 0);
    editor->Send(Message::SetSel, 6, 6);
    NativeSearch::Command(frame, commands[0]);
    Require(editor->Send(Message::GetAnchor) == 6 && editor->Send(Message::GetCurrentPos) == 6,
            "returning to Automatic uses the current C++ fold tree without stale Plain Text parents");
    Require(NativeSearch::Text(editor) == text && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                frame->DocumentEncoding() == FileEncoding::Utf16Le && frame->DocumentHasBom() &&
                frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes,
            "block commands and syntax policy changes preserve UTF-16 BOM, disk bytes, save point and pending history");
    editor->Send(Message::Redo);
    Require(NativeSearch::Text(editor) == text + "!", "pending edit still redoes after navigation and saving");
    editor->Send(Message::Undo);
    Require(NativeSearch::Text(editor) == text && !editor->Send(Message::GetModify),
            "undo returns to the original UTF-16 document save point");
    std::puts("PASS: Automatic/Plain Text policy transitions and independent UTF-16 disk bytes/history");
}
class BlockApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "owned native window must be presented");
                int commands[std::size(names)];
                for (size_t index = 0; index < std::size(names); ++index) {
                    commands[index] = frame->GetMenuBar()->FindMenuItem("Search", names[index]);
                    Require(commands[index] != wxNOT_FOUND, "Search exposes the original navigation commands");
                }
                ReferenceCases(frame, commands);
                KeyboardCases(frame, commands);
                FilePolicyCases(frame, commands);
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(BlockApp);
