#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include "native_fold_helpers.h"
#include "file_helpers.h"
#include <span>
#include "selection_commands_reference.h"
#include "selection_rectangle_reference.h"
#include <array>
#include <cstdio>
#include <print>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using Scintilla::Message;
constexpr std::array<const char*, 5> names = {{"Select All", "Select Word", "Select Lines (Expand Selection)",
                                               "Select to Document Start", "Select to Document End"}};
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Position(EditorControl* editor, const SelectionReference::Fixture& row,
              std::span<const SelectionReference::Range> rectangleRanges = {}) {
    using enum Scintilla::Message;
    if (editor->Send(GetAnchor) != row.expectedAnchor || editor->Send(GetCurrentPos) != row.expectedCaret ||
        editor->Send(GetSelectionMode) != row.selectionMode || editor->Send(GetSelections) != row.selections ||
        editor->Send(GetMainSelection) != row.main ||
        editor->Send(GetRectangularSelectionAnchorVirtualSpace) != row.anchorVirtual ||
        editor->Send(GetRectangularSelectionCaretVirtualSpace) != row.caretVirtual) {
        std::println(
            stderr,
            "case document={:d} command={:d} mode={:d} setup={:d} repeat={:d} input={:d}/{:d} actual={:d}/{:d} "
            "mode={:d} count={:d} expected={:d}/{:d} mode={:d} count={:d}",
            row.document, row.command, row.mode, row.setup, row.repetition, row.anchor, row.caret,
            static_cast<long long>(editor->Send(GetAnchor)), static_cast<long long>(editor->Send(GetCurrentPos)),
            static_cast<long long>(editor->Send(GetSelectionMode)), static_cast<long long>(editor->Send(GetSelections)),
            row.expectedAnchor, row.expectedCaret, row.selectionMode, row.selections);
        throw std::runtime_error("selection command reproduces independently captured original endpoints/mode");
    }
    for (intptr_t index = 0; index < row.selections; ++index) {
        // Retained rectangle ranges depend on native font measurements; the
        // original edge commands retain every prepared native range exactly.
        const auto& range = rectangleRanges.empty() ? row.ranges[static_cast<size_t>(index)]
                                                    : rectangleRanges[static_cast<size_t>(index)];
        if (editor->Send(GetSelectionNAnchor, index) != range.anchor ||
            editor->Send(GetSelectionNCaret, index) != range.caret ||
            editor->Send(GetSelectionNAnchorVirtualSpace, index) != range.anchorVirtual ||
            editor->Send(GetSelectionNCaretVirtualSpace, index) != range.caretVirtual) {
            std::print(stderr,
                       "range doc={:d} command={:d} mode={:d} setup={:d} input={:d}/{:d} N={:d} actual={:d}/{:d} "
                       "{:d}/{:d} expected={:d}/{:d} {:d}/{:d}",
                       row.document, row.command, row.mode, row.setup, row.anchor, row.caret,
                       static_cast<long long>(index), static_cast<long long>(editor->Send(GetSelectionNAnchor, index)),
                       static_cast<long long>(editor->Send(GetSelectionNCaret, index)),
                       static_cast<long long>(editor->Send(GetSelectionNAnchorVirtualSpace, index)),
                       static_cast<long long>(editor->Send(GetSelectionNCaretVirtualSpace, index)), range.anchor,
                       range.caret, range.anchorVirtual, range.caretVirtual);
            throw std::runtime_error(
                "every original multiple/rectangular selection range retains exact endpoints and virtual space");
        }
    }
}
void Prepare(EditorFrame* frame, const SelectionReference::Fixture& row, const int* modes) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, SelectionReference::documents[row.document], row.anchor, row.caret);
    editor->Send(SetWrapMode, row.setup == 5);
    editor->Send(SetTabWidth, 4);
    NativeSearch::Command(frame, modes[row.mode]);
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Undo);
    editor->Send(SetSavePoint);
    editor->Send(Colourise, 0, -1);
    NativeFold::Paint(editor);
    if (row.setup == 5 && row.document == 1)
        Require(editor->Send(WrapCount, 3) > 1, "long-line fixture has real wrapped continuation rows");
    editor->Send(SetSel, row.anchor, row.caret);
    if (row.setup == 1) {
        editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Rectangle)));
        editor->Send(SetRectangularSelectionAnchor, row.anchor);
        editor->Send(SetRectangularSelectionCaret, row.caret);
        editor->Send(SetRectangularSelectionAnchorVirtualSpace, 3);
        editor->Send(SetRectangularSelectionCaretVirtualSpace, 2);
        if (row.command == 2 && (row.mode & std::to_underlying(LineSelectionMode::VisualStudio))) {
            const auto input = std::find_if(SelectionReference::rectangleInputs.begin(),
                                            SelectionReference::rectangleInputs.end(), [&row](const auto& input) {
                                                return input.document == row.document && input.mode == row.mode &&
                                                       input.anchor == row.anchor && input.caret == row.caret;
                                            });
            Require(input != SelectionReference::rectangleInputs.end(),
                    "rectangle promotion has independent original input ranges");
            Require(editor->Send(GetSelections) == input->selections && editor->Send(GetMainSelection) == input->main,
                    "rectangle promotion starts with the original range count and main index");
            // The original promotion consumes the main byte range. Reproduce its
            // captured input exactly despite native font-derived rectangle geometry.
            for (size_t index = 0; index < static_cast<size_t>(input->selections); ++index) {
                const auto& range = input->ranges[index];
                editor->Send(SetSelectionNAnchor, index, range.anchor);
                editor->Send(SetSelectionNCaret, index, range.caret);
                editor->Send(SetSelectionNAnchorVirtualSpace, index, range.anchorVirtual);
                editor->Send(SetSelectionNCaretVirtualSpace, index, range.caretVirtual);
                Require(editor->Send(GetSelectionNAnchor, index) == range.anchor &&
                            editor->Send(GetSelectionNCaret, index) == range.caret &&
                            editor->Send(GetSelectionNAnchorVirtualSpace, index) == range.anchorVirtual &&
                            editor->Send(GetSelectionNCaretVirtualSpace, index) == range.caretVirtual,
                        "rectangle promotion starts with every exact original byte range and virtual space");
            }
            Require(editor->Send(GetAnchor) == row.anchor && editor->Send(GetCurrentPos) == row.caret,
                    "original rectangle inputs retain outer endpoints and direction");
        }
    } else if (row.setup == 2)
        editor->Send(AddSelection, 0, 0);
    else if (row.setup == 3)
        editor->Send(SetReadOnly, 1);
    else if (row.setup == 4) {
        constexpr intptr_t FoldHeader = 0x2400;
        constexpr intptr_t FoldChild = 0x401;
        constexpr intptr_t FoldBase = 0x400;
        editor->Send(SetFoldLevel, 0, FoldHeader);
        editor->Send(SetFoldLevel, 1, FoldChild);
        editor->Send(SetFoldLevel, 2, FoldBase);
        editor->Send(SetFoldExpanded, 0, 0);
        editor->Send(HideLines, 1, 1);
    }
}
void ReferenceCases(EditorFrame* frame, const int* commands) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const std::array<int, 4> modes = {{frame->GetMenuBar()->FindMenuItem("Settings", "None"),
                                       frame->GetMenuBar()->FindMenuItem("Settings", "Visual Studio"),
                                       frame->GetMenuBar()->FindMenuItem("Settings", "Normal"),
                                       frame->GetMenuBar()->FindMenuItem("Settings", "Old Visual Studio")}};
    const auto encoding = frame->DocumentEncoding();
    const auto bom = frame->DocumentHasBom();
    for (const auto& row : SelectionReference::fixtures) {
        if (row.repetition == 0) Prepare(frame, row, modes.data());
        const auto undo = editor->Send(CanUndo);
        const auto redo = editor->Send(CanRedo);
        std::array<SelectionReference::Range, 2> rectangleRanges{};
        const bool retainedRectangle = row.selectionMode == std::to_underlying(Scintilla::SelectionMode::Rectangle);
        if (retainedRectangle) {
            for (size_t index = 0; index < static_cast<size_t>(row.selections); ++index)
                rectangleRanges[index] = {static_cast<long>(editor->Send(GetSelectionNAnchor, index)),
                                          static_cast<long>(editor->Send(GetSelectionNCaret, index)),
                                          static_cast<long>(editor->Send(GetSelectionNAnchorVirtualSpace, index)),
                                          static_cast<long>(editor->Send(GetSelectionNCaretVirtualSpace, index))};
        }
        if (row.document == 0 && row.command == 3 && row.setup == 1 && row.anchor == 4 && row.caret == 13) {
            constexpr auto DefaultStyle = std::to_underlying(Scintilla::StylesCommon::Default);
            char font[128]{};
            editor->Send(StyleGetFont, DefaultStyle, reinterpret_cast<intptr_t>(font));
            std::println(
                "Native rectangle geometry: font={} size={:d} prepared range={:d}/{:d}; original range={:d}/{:d}", font,
                static_cast<long long>(editor->Send(StyleGetSize, DefaultStyle)), rectangleRanges[0].anchor,
                rectangleRanges[0].caret, row.ranges[0].anchor, row.ranges[0].caret);
        }
        NativeSearch::Command(frame, commands[row.command]);
        Position(editor, row,
                 retainedRectangle ? std::span(rectangleRanges.data(), static_cast<size_t>(row.selections))
                                   : std::span<SelectionReference::Range>{});
        if (NativeSearch::Text(editor) != SelectionReference::documents[row.document] || editor->Send(GetModify) ||
            editor->Send(CanUndo) != undo || editor->Send(CanRedo) != redo || frame->DocumentEncoding() != encoding ||
            frame->DocumentHasBom() != bom) {
            std::println(stderr,
                         "state document={:d} setup={:d} input={:d}/{:d} modify={:d} undo={:d} redo={:d} text={}",
                         row.document, row.setup, row.anchor, row.caret,
                         static_cast<long long>(editor->Send(GetModify)), static_cast<long long>(editor->Send(CanUndo)),
                         static_cast<long long>(editor->Send(CanRedo)), NativeSearch::Text(editor).c_str());
            throw std::runtime_error(
                "selection commands preserve bytes, encoding, save point and history availability");
        }
        for (intptr_t line = 0; line < editor->Send(GetLineCount); ++line)
            Require(editor->Send(GetLineVisible, line) == row.visible[line] - '0',
                    "selection commands preserve original folded-line visibility");
        if (row.repetition == 1 || row.command != 2) {
            editor->Send(SetReadOnly, 0);
            editor->Send(Redo);
            Require(NativeSearch::Text(editor) == std::string(SelectionReference::documents[row.document]) + "!",
                    "pending edit still redoes after selection commands");
            editor->Send(Undo);
            Require(!editor->Send(GetModify), "undo restores the original document save point");
        }
    }
    std::puts("PASS: original selection endpoint/mode/virtual-space, wrap/fold and history reference samples");
}
void KeyboardCases(EditorFrame* frame, const int* commands) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const std::array<int, 4> modes = {{frame->GetMenuBar()->FindMenuItem("Settings", "None"),
                                       frame->GetMenuBar()->FindMenuItem("Settings", "Visual Studio"),
                                       frame->GetMenuBar()->FindMenuItem("Settings", "Normal"),
                                       frame->GetMenuBar()->FindMenuItem("Settings", "Old Visual Studio")}};
    for (int index = 0; index < 5; ++index) {
        const auto accelerator =
            std::unique_ptr<wxAcceleratorEntry>(frame->GetMenuBar()->FindItem(commands[index])->GetAccel());
        if (index >= 3)
            Require(!accelerator, "document-edge commands retain the original absence of shortcuts");
        else
            Require(accelerator && accelerator->GetKeyCode() == (index == 0 ? 'A' : WXK_SPACE) &&
                        accelerator->GetFlags() == (wxACCEL_CTRL | (index == 1   ? wxACCEL_ALT
                                                                    : index == 2 ? wxACCEL_SHIFT
                                                                                 : 0)),
                    "selection menu labels retain original keys and exact modifiers");
    }
    for (int command = 0; command < 3; ++command) {
        for (int mode = 0; mode < (command == 2 ? 4 : 1); ++mode) {
            const auto row = std::find_if(SelectionReference::fixtures.begin(), SelectionReference::fixtures.end(),
                                          [command, mode](const auto& row) {
                                              return row.document == 0 && row.command == command && row.mode == mode &&
                                                     row.setup == 0 && row.anchor == 4 && row.caret == 4 &&
                                                     row.repetition == 0;
                                          });
            Require(row != SelectionReference::fixtures.end(), "native key has an original runtime reference sample");
            Prepare(frame, *row, modes.data());
            NativeKeys::Shortcut(frame, command == 0 ? 'A' : WXK_SPACE, true, command == 1, command == 2);
            Position(editor, *row);
            Require(NativeSearch::Text(editor) == SelectionReference::documents[0] && !editor->Send(GetModify) &&
                        !editor->Send(CanUndo) && editor->Send(CanRedo),
                    "native selection shortcuts preserve bytes, modified state and pending history");
        }
    }
    TestEditor::ResetStream(editor, SelectionReference::documents[0], 4, 4);
    NativeKeys::Shortcut(frame, WXK_SPACE, true, true, true);
    TestEditor::RequireSelection(editor, 4, 4, "Ctrl+Alt+Shift+Space must not alias either original selection gesture");
    Require(NativeSearch::Text(editor) == SelectionReference::documents[0] && !editor->Send(GetModify) &&
                !editor->Send(CanUndo),
            "extra Space modifiers must not insert text or create history");
    std::puts("PASS: native Ctrl+A/Ctrl+Alt+Space/Ctrl+Shift+Space, line modes and extra modifiers");
}
void ViewportCases(EditorFrame* frame, const int* commands) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    std::string text;
    for (int line = 0; line < 100; ++line) {
        text += std::string(220, 'x');
        text.push_back('\n');
    }
    editor->Send(SetWrapMode, 0);
    for (const int command : {0, 3, 4}) {
        TestEditor::ResetStream(editor, text.c_str(), 1000, 1100);
        NativeFold::Paint(editor);
        editor->Send(SetFirstVisibleLine, 30);
        editor->Send(SetXOffset, 10);
        NativeFold::Paint(editor);
        const auto first = editor->Send(GetFirstVisibleLine);
        const auto horizontal = editor->Send(GetXOffset);
        Require(first == 30 && horizontal > 0, "viewport fixture starts away from document origin on both axes");
        NativeSearch::Command(frame, commands[command]);
        if (editor->Send(GetFirstVisibleLine) != first || editor->Send(GetXOffset) != horizontal) {
            std::println(stderr, "viewport command={:d} before={:d}/{:d} after={:d}/{:d}", command,
                         static_cast<long long>(first), static_cast<long long>(horizontal),
                         static_cast<long long>(editor->Send(GetFirstVisibleLine)),
                         static_cast<long long>(editor->Send(GetXOffset)));
            throw std::runtime_error(
                "Select All and document boundary setters preserve the original viewport without caret exposure");
        }
    }
    std::puts("PASS: original Select All/document-edge viewport policy");
}

void FilePolicyCases(EditorFrame* frame, const int* commands) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("selection-commands");
    std::filesystem::create_directory(directory);
    const auto path = directory / "source.txt";
    const std::string text = "top\r\n  body\r\nlast";
    std::string bytes("\x{ff}\x{fe}", 2);
    for (const char character : text) {
        bytes.push_back(character);
        bytes.push_back('\0');
    }
    TestFiles::WriteBytes(path, bytes);
    editor->Send(SetReadOnly, 0);
    editor->Send(SetSavePoint);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                frame->DocumentEncoding() == FileEncoding::Utf16Le && frame->DocumentHasBom(),
            "selection policy fixture opens independent UTF-16LE/BOM bytes");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "None"));
    editor->Send(BeginUndoAction);
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(EndUndoAction);
    editor->Send(BeginUndoAction);
    editor->Send(InsertText, 0, reinterpret_cast<intptr_t>("#"));
    editor->Send(EndUndoAction);
    editor->Send(Undo);
    Require(editor->Send(GetModify) && editor->Send(CanUndo) && editor->Send(CanRedo),
            "dirty selection fixture has real Undo and Redo edits");
    for (const int command : std::span(commands, names.size())) {
        editor->Send(SetSel, 10, 3);
        NativeSearch::Command(frame, command);
        Require(NativeSearch::Text(editor) == text + "!" && editor->Send(GetModify) && editor->Send(CanUndo) &&
                    editor->Send(CanRedo) && frame->DocumentEncoding() == FileEncoding::Utf16Le &&
                    frame->DocumentHasBom(),
                "all selection commands preserve a dirty UTF-16 document and both history directions");
    }
    editor->Send(Redo);
    Require(NativeSearch::Text(editor) == "#" + text + "!",
            "pending Redo remains usable after every selection command");
    editor->Send(Undo);
    editor->Send(Undo);
    Require(NativeSearch::Text(editor) == text && !editor->Send(GetModify),
            "Undo returns to the original UTF-16 save point");
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes,
            "selection-only operations retain byte-exact UTF-16LE/BOM disk output");
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    std::puts("PASS: dirty state, both history directions and independent UTF-16LE/BOM disk bytes");
}

class SelectionCommandsApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "owned native window must be presented");
                std::array<int, std::size(names)> commands{};
                for (size_t index = 0; index < names.size(); ++index) {
                    commands[index] = frame->GetMenuBar()->FindMenuItem(index == 0 ? "Edit" : "Search", names[index]);
                    Require(commands[index] != wxNOT_FOUND, "native menus expose every original selection command");
                }
                ReferenceCases(frame, commands.data());
                KeyboardCases(frame, commands.data());
                ViewportCases(frame, commands.data());
                FilePolicyCases(frame, commands.data());
                TestEditor::ResetStream(frame->Editor(), "");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                for (const int command : commands)
                    Require(!frame->GetMenuBar()->IsEnabled(command),
                            "original selection menus disable on empty documents");
                result_ = 0;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
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
wxIMPLEMENT_APP(SelectionCommandsApp);
