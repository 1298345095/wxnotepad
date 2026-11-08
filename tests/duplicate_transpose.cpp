#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/app.h>
#include <memory>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int CommandId(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    Require(id != wxNOT_FOUND, "native Edit menu exposes original duplication and transposition commands");
    return id;
}
void Menu(EditorFrame* frame, const char* label) {
    NativeSearch::Command(frame, CommandId(frame, label));
}
void FirstSlice(EditorFrame* frame) {
    TestEditor::ResetStream(frame->Editor(), "AB\nC", 0, 1, Scintilla::EndOfLine::Lf);
    Menu(frame, "Duplicate");
    Require(TestEditor::Text(frame) == "AAB\nC", "Duplicate Selection inserts selected bytes at selection end");
    TestEditor::RequireSelection(frame->Editor(), 0, 1, "Duplicate Selection retains original selection");
}
void LineCommands(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "AB\nC", 0, 1, Scintilla::EndOfLine::Lf);
    Menu(frame, "Duplicate Line");
    Require(TestEditor::Text(frame) == "AB\nAB\nC", "Duplicate Line copies the complete caret line");
    TestEditor::RequireSelection(editor, 0, 1, "Duplicate Line retains selection before insertion");
    TestEditor::ResetStream(editor, "AB\nC", 1, 4);
    Menu(frame, "Transpose");
    Require(TestEditor::Text(frame) == "C\nAB", "Transpose swaps caret line content with preceding logical line");
    TestEditor::RequireSelection(editor, 2, 2, "Transpose collapses selection at current line start");
}
void History(EditorControl* editor, const std::string& before, const std::string& after) {
    Require(editor->Send(Message::CanUndo) && editor->Send(Message::GetModify), "command records original history");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == before && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
            "single Undo restores complete operation and save point");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == after && !editor->Send(Message::CanRedo) && editor->Send(Message::GetModify),
            "single Redo restores complete operation");
}
void StreamFixtures(EditorFrame* frame) {
    struct Fixture {
        const char* command;
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        const char* after;
        intptr_t resultAnchor;
        intptr_t resultCaret;
        Scintilla::EndOfLine mode = Scintilla::EndOfLine::Lf;
    };
    // Literal outcomes from Editor::Duplicate/LineTranspose in pinned 89b8b36.
    const Fixture fixtures[] = {
        {"Duplicate", "", 0, 0, "\n", 0, 0},
        {"Duplicate", "AB", 1, 1, "AB\nAB", 1, 1},
        {"Duplicate", "AB\nC", 1, 0, "AAB\nC", 1, 0},
        {"Duplicate", "A\nB", 0, 2, "A\nA\nB", 0, 2},
        {"Duplicate", "A\nB", 2, 0, "A\nA\nB", 2, 0},
        {"Duplicate", "A\nB", 2, 2, "A\nB\nB", 2, 2},
        {"Duplicate", "A\n", 2, 2, "A\n\n", 2, 2},
        {"Duplicate", "中😀\n尾", 0, 7, "中😀中😀\n尾", 0, 7},
        {"Duplicate", "A\rB", 0, 2, "A\rA\rB", 0, 2},
        {"Duplicate Line", "", 0, 0, "\r\n", 0, 0, Scintilla::EndOfLine::CrLf},
        {"Duplicate Line", "AB\nCD", 1, 4, "AB\nCD\nCD", 1, 4},
        {"Duplicate Line", "AB\nCD", 4, 1, "AB\nAB\nCD", 7, 1},
        {"Duplicate Line", "A\r\nB", 0, 0, "A\nA\r\nB", 0, 0},
        {"Duplicate Line", "A\nB", 2, 2, "A\nB\r\nB", 2, 2, Scintilla::EndOfLine::CrLf},
        {"Duplicate Line", "A\rB", 2, 2, "A\rB\rB", 2, 2, Scintilla::EndOfLine::Cr},
        {"Duplicate Line", "中😀\n尾", 8, 8, "中😀\n尾\n尾", 8, 8},
        {"Transpose", "AB\nC", 0, 4, "C\nAB", 2, 2},
        {"Transpose", "A\r\nBB\nC\rD", 6, 6, "A\r\nC\nBB\rD", 5, 5},
        {"Transpose", "A\rB", 2, 2, "B\rA", 2, 2, Scintilla::EndOfLine::Cr},
        {"Transpose", "A\r\n", 3, 3, "\r\nA", 2, 2, Scintilla::EndOfLine::CrLf},
        {"Transpose", "中😀\r\n尾", 9, 12, "尾\r\n中😀", 5, 5, Scintilla::EndOfLine::CrLf},
        {"Transpose", "same\nsame", 6, 6, "same\nsame", 5, 5},
    };
    auto* editor = frame->Editor();
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(editor, fixture.before, fixture.anchor, fixture.caret, fixture.mode);
        Menu(frame, fixture.command);
        if (TestEditor::Text(frame) != fixture.after)
            std::fprintf(stderr, "%s before=<%s> actual=<%s> expected=<%s>\n", fixture.command, fixture.before,
                         TestEditor::Text(frame).c_str(), fixture.after);
        Require(TestEditor::Text(frame) == fixture.after, "literal original stream result");
        TestEditor::RequireSelection(editor, fixture.resultAnchor, fixture.resultCaret,
                                     "original stream selection endpoints");
        Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(fixture.mode),
                "EOL insertion mode survives");
        History(editor, fixture.before, fixture.after);
    }
    std::puts("PASS: stream/caret/empty/reversed/Unicode/EOL/unterminated fixtures and single-action Undo/Redo");
}
void AvailabilityAndNoChange(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const std::pair<const char*, const char*> commands[] = {
        {"Duplicate", "Alt+D"}, {"Duplicate Line", "Ctrl+D"}, {"Transpose", "Alt+S"}};
    for (const auto& [label, shortcut] : commands) {
        const int id = CommandId(frame, label);
        Require(frame->GetMenuBar()->FindItem(id)->GetItemLabel().AfterFirst('\t') == shortcut,
                "native menu advertises exact original modifier");
        for (const char* text : {"", "AB\nCD"}) {
            TestEditor::ResetStream(editor, text);
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            Require(frame->GetMenuBar()->IsEnabled(id), "original commands stay enabled for empty document/caret");
        }
        TestEditor::ResetStream(editor, "AB\nCD", 1, 4);
        editor->Send(Message::SetReadOnly, 1);
        const auto revision = editor->DocumentRevision();
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(id), "original menus remain enabled on read-only documents");
        Menu(frame, label);
        Require(TestEditor::Text(editor) == "AB\nCD" && editor->DocumentRevision() == revision &&
                    !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                "read-only core rejects text edits and history");
        const bool transpose = std::string(label) == "Transpose";
        TestEditor::RequireSelection(editor, 1, transpose ? 1 : 4,
                                     "read-only transpose still follows original caret repair");
    }
    for (const char* text : {"", "AB\nCD", "\n\n"}) {
        TestEditor::ResetStream(editor, text);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        const bool emptyLines = std::string(text) == "\n\n";
        editor->Send(Message::SetSel, emptyLines ? 1 : 0, emptyLines ? 1 : 0);
        Menu(frame, "Transpose");
        Require(TestEditor::Text(editor) == text && editor->Send(Message::CanRedo) && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "first-line and empty-line transpose preserve pending Redo and save point");
    }
    TestEditor::ResetStream(editor, "A\nB", 0, 0);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    Menu(frame, "Duplicate Line");
    Require(!editor->Send(Message::CanRedo), "text-changing duplication replaces pending Redo");
    TestEditor::ResetStream(editor, "AB\nCD", 4, 1);
    Menu(frame, "Transpose");
    Require(TestEditor::Text(editor) == "AB\nCD" && !editor->Send(Message::CanUndo),
            "reversed selection with caret on first logical line does not transpose its final line");
    TestEditor::RequireSelection(editor, 4, 1, "first-line transpose preserves reversed selection");
    std::puts("PASS: original availability, read-only caret repair, unchanged history and pending Redo");
}
void Rectangle(EditorControl* editor, intptr_t anchor, intptr_t caret) {
    editor->Send(Message::SetRectangularSelectionAnchor, anchor);
    editor->Send(Message::SetRectangularSelectionCaret, caret);
}
void RectangleDuplication(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool reverse : {false, true}) {
        TestEditor::ResetStream(editor, "ABC\nDEF", 0, 0, Scintilla::EndOfLine::Lf);
        Rectangle(editor, reverse ? 7 : 1, reverse ? 1 : 7);
        Menu(frame, "Duplicate");
        Require(TestEditor::Text(editor) == "ABCBC\nDEFEF" && editor->Send(Message::SelectionIsRectangle),
                "rectangle duplicates selected columns on each physical line");
        Require(editor->Send(Message::GetRectangularSelectionAnchor) == (reverse ? 9 : 1) &&
                    editor->Send(Message::GetRectangularSelectionCaret) == (reverse ? 1 : 9),
                "rectangle restores original column span and vertical direction");
        History(editor, "ABC\nDEF", "ABCBC\nDEFEF");
        TestEditor::ResetStream(editor, "ABC\nDEF", 0, 0, Scintilla::EndOfLine::Lf);
        Rectangle(editor, reverse ? 7 : 1, reverse ? 1 : 7);
        Menu(frame, "Duplicate Line");
        Require(TestEditor::Text(editor) == "ABC\nABC\nDEF\nDEF" && editor->Send(Message::SelectionIsRectangle),
                "rectangle line duplication copies each caret's complete logical line");
        Require(editor->Send(Message::GetRectangularSelectionAnchor) == (reverse ? 15 : 1) &&
                    editor->Send(Message::GetRectangularSelectionCaret) == (reverse ? 1 : 15),
                "line duplication extends original rectangle over inserted lines");
        History(editor, "ABC\nDEF", "ABC\nABC\nDEF\nDEF");
    }
    TestEditor::ResetStream(editor, "ABC\nDEF", 0, 0);
    Rectangle(editor, 1, 7);
    Menu(frame, "Transpose");
    Require(TestEditor::Text(editor) == "DEF\nABC" && !editor->Send(Message::SelectionIsRectangle),
            "transpose uses main rectangle caret line and collapses shape");
    TestEditor::RequireSelection(editor, 4, 4, "transpose rectangle ends at logical line start");
    History(editor, "ABC\nDEF", "DEF\nABC");
}
void MultipleSelectionDuplication(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool line : {false, true}) {
        TestEditor::ResetStream(editor, "AB\nCD", 0, 1, Scintilla::EndOfLine::Lf);
        editor->Send(Message::SetMultipleSelection, 1);
        editor->Send(Message::AddSelection, 4, 3);
        Menu(frame, line ? "Duplicate Line" : "Duplicate");
        const char* after = line ? "AB\nAB\nCD\nCD" : "AAB\nCCD";
        Require(TestEditor::Text(editor) == after && editor->Send(Message::GetSelections) == 2,
                "duplication processes all independent selections");
        Require(editor->Send(Message::GetSelectionNAnchor, 0) == 0 &&
                    editor->Send(Message::GetSelectionNCaret, 0) == 1 &&
                    editor->Send(Message::GetSelectionNAnchor, 1) == (line ? 6 : 4) &&
                    editor->Send(Message::GetSelectionNCaret, 1) == (line ? 7 : 5),
                "all selection endpoints track original inserted bytes");
        History(editor, "AB\nCD", after);
    }
    TestEditor::ResetStream(editor, "AB\nCD", 0, 1);
    editor->Send(Message::AddSelection, 4, 3);
    Menu(frame, "Transpose");
    Require(TestEditor::Text(editor) == "CD\nAB" && editor->Send(Message::GetSelections) == 1,
            "transpose uses only the main caret and clears additional ranges");
    TestEditor::RequireSelection(editor, 3, 3, "multiple-selection transpose collapses at original computed start");
    History(editor, "AB\nCD", "CD\nAB");
    editor->Send(Message::SetMultipleSelection, 0);
}
void VirtualSpaceDuplication(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const char* label : {"Duplicate", "Duplicate Line"}) {
        TestEditor::ResetStream(editor, "AB\nCD", 2, 2, Scintilla::EndOfLine::Lf);
        editor->Send(Message::SetVirtualSpaceOptions, static_cast<uintptr_t>(Scintilla::VirtualSpace::UserAccessible));
        editor->Send(Message::SetSelectionNAnchorVirtualSpace, 0, 3);
        editor->Send(Message::SetSelectionNCaretVirtualSpace, 0, 3);
        Menu(frame, label);
        Require(TestEditor::Text(editor) == "AB\nAB\nCD", "empty virtual-space range duplicates line without padding");
        // SelectionPosition::MoveForInsertDelete consumes three virtual columns
        // across the inserted LF and AB, without adding padding bytes.
        TestEditor::RequireSelection(editor, 5, 5, "original insert notifications consume virtual columns");
        Require(editor->Send(Message::GetSelectionNAnchorVirtualSpace, 0) == 0 &&
                    editor->Send(Message::GetSelectionNCaretVirtualSpace, 0) == 0,
                "original virtual columns become positions within inserted text");
        History(editor, "AB\nCD", "AB\nAB\nCD");
    }
    TestEditor::ResetStream(editor, "AB\nCD", 2, 2);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSelectionNAnchorVirtualSpace, 0, 2);
    editor->Send(Message::SetSelectionNCaretVirtualSpace, 0, 4);
    Menu(frame, "Duplicate");
    Require(TestEditor::Text(editor) == "AB\nCD" && editor->Send(Message::CanRedo) && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "nonempty virtual-only selection duplicates zero bytes and retains pending Redo");
    TestEditor::RequireSelection(editor, 2, 2, "virtual-only byte endpoints survive unchanged duplication");
    Require(editor->Send(Message::GetSelectionNAnchorVirtualSpace, 0) == 2 &&
                editor->Send(Message::GetSelectionNCaretVirtualSpace, 0) == 4,
            "unchanged virtual-only duplication retains distinct virtual endpoints");
    TestEditor::ResetStream(editor, "AB\nCD", 5, 5);
    editor->Send(Message::SetSelectionNAnchorVirtualSpace, 0, 2);
    editor->Send(Message::SetSelectionNCaretVirtualSpace, 0, 2);
    Menu(frame, "Transpose");
    Require(TestEditor::Text(editor) == "CD\nAB", "transpose ignores virtual columns when choosing logical line");
    TestEditor::RequireSelection(editor, 3, 3, "transpose resets caret to logical line start");
    Require(editor->Send(Message::GetSelectionNCaretVirtualSpace, 0) == 0, "transpose clears virtual caret columns");
    editor->Send(Message::SetVirtualSpaceOptions, 0);
    TestEditor::ResetStream(editor, "AB\nCD", 0, 0, Scintilla::EndOfLine::Lf);
    Rectangle(editor, 1, 4);
    Menu(frame, "Duplicate");
    Require(TestEditor::Text(editor) == "AB\nAB\nCD\nCD" && editor->Send(Message::SelectionIsRectangle),
            "empty rectangle forces complete-line duplication for all ranges");
    Require(editor->Send(Message::GetRectangularSelectionAnchor) == 1 &&
                editor->Send(Message::GetRectangularSelectionCaret) == 10,
            "empty rectangle extends over inserted physical lines at original column");
}
void ShapedSelections(EditorFrame* frame) {
    RectangleDuplication(frame);
    MultipleSelectionDuplication(frame);
    VirtualSpaceDuplication(frame);
    std::puts("PASS: rectangle/multiple/virtual selections, exact endpoints and grouped history");
}
void WrappedLines(EditorFrame* frame) {
    auto* editor = frame->Editor();
    frame->SetSize(320, 300);
    TestEditor::ResetStream(editor,
                            "one two three four five six seven eight nine ten eleven twelve thirteen fourteen\nZ", 0,
                            30, Scintilla::EndOfLine::Lf);
    editor->Send(Message::SetWrapMode, static_cast<uintptr_t>(Scintilla::Wrap::Word));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (editor->Send(Message::WrapCount, 0) <= 1 && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(editor->Send(Message::WrapCount, 0) > 1, "fixture has actual wrapped display lines");
    Menu(frame, "Transpose");
    Require(TestEditor::Text(editor) ==
                    "one two three four five six seven eight nine ten eleven twelve thirteen fourteen\nZ" &&
                !editor->Send(Message::CanUndo),
            "first logical line transpose ignores wrapped display rows");
    TestEditor::RequireSelection(editor, 0, 30, "first logical line retains selection despite wrapping");
    Menu(frame, "Duplicate Line");
    Require(TestEditor::Text(editor) ==
                "one two three four five six seven eight nine ten eleven twelve thirteen fourteen\n"
                "one two three four five six seven eight nine ten eleven twelve thirteen fourteen\nZ",
            "line duplication copies complete logical line under display wrapping");
    TestEditor::RequireSelection(editor, 0, 30, "wrapping does not alter original duplication endpoints");
    TestEditor::ResetStream(editor,
                            "one two three four five six seven eight nine ten eleven twelve thirteen fourteen\nZ", 0, 3,
                            Scintilla::EndOfLine::Lf);
    Menu(frame, "Duplicate");
    Require(TestEditor::Text(editor) ==
                "oneone two three four five six seven eight nine ten eleven twelve thirteen fourteen\nZ",
            "selection duplication under wrapping retains exact selected-byte scope");
    TestEditor::RequireSelection(editor, 0, 3, "wrapped selection duplication retains endpoints");
    editor->Send(Message::SetWrapMode, static_cast<uintptr_t>(Scintilla::Wrap::None));
    std::puts("PASS: wrapping retains logical-line scope and first-line boundary");
}
void SavedBytes(EditorFrame* frame) {
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::array<std::string, 3> after;
    };
    const Fixture fixtures[] = {
        {"utf8-bom",
         FileEncoding::Utf8,
         "\xef\xbb\xbf中\r\nB",
         {"\xef\xbb\xbf中中\r\nB", "\xef\xbb\xbf中\r\n中\r\nB",
          "\xef\xbb\xbf"
          "B\r\n中"}},
        {"gbk",
         FileEncoding::Gbk,
         "\xd6\xd0\r\nB",
         {"\xd6\xd0\xd6\xd0\r\nB", "\xd6\xd0\r\n\xd6\xd0\r\nB", "B\r\n\xd6\xd0"}},
        {"utf16le-bom",
         FileEncoding::Utf16Le,
         std::string("\xff\xfe\x2d\x4e\r\0\n\0B\0", 10),
         {std::string("\xff\xfe\x2d\x4e\x2d\x4e\r\0\n\0B\0", 12),
          std::string("\xff\xfe\x2d\x4e\r\0\n\0\x2d\x4e\r\0\n\0B\0", 16),
          std::string("\xff\xfe"
                      "B\0\r\0\n\0\x2d\x4e",
                      10)}},
    };
    const std::array<const char*, 3> commands = {"Duplicate", "Duplicate Line", "Transpose"};
    const std::array<const char*, 3> text = {"中中\r\nB", "中\r\n中\r\nB", "B\r\n中"};
    const auto directory = TestFiles::TemporaryDirectory("duplicate-transpose");
    std::filesystem::create_directory(directory);
    for (const auto& fixture : fixtures) {
        for (size_t operation = 0; operation < commands.size(); ++operation) {
            const auto path = directory / (std::string(fixture.name) + std::to_string(operation) + ".txt");
            TestFiles::WriteBytes(path, fixture.before);
            auto* editor = frame->Editor();
            editor->Send(Message::SetSavePoint);
            Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                    "encoded fixture opens through application file operation");
            editor->Send(Message::SetSel, operation == 2 ? 5 : 0, operation == 0 ? 3 : operation == 2 ? 5 : 0);
            Menu(frame, commands[operation]);
            Require(TestEditor::Text(editor) == text[operation] && frame->DocumentEncoding() == fixture.encoding,
                    "core commands preserve document codec and literal Unicode content");
            const TestFiles::EncodedFixture expected{fixture.name, fixture.encoding, fixture.before,
                                                     fixture.after[operation]};
            TestFiles::RequireSavedUndoRedo(frame, path, expected,
                                            {"Save produces independent original encoded bytes",
                                             "Undo and Save restore independent source bytes",
                                             "Redo and Save restore independent transformed bytes"});
            std::printf("PASS: %s %s saved bytes/BOM/Undo/Redo\n", fixture.name, commands[operation]);
        }
    }
    std::filesystem::remove_all(directory);
}
void NativeKey(EditorFrame* frame, int key, bool control, bool alt, bool shift = false) {
#ifdef __WXMSW__
    auto* editor = frame->Editor();
    editor->SetFocus();
    wxYield();
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read native test thread keyboard state");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    modified[VK_CONTROL] = control ? 0x80 : 0;
    modified[VK_MENU] = alt ? 0x80 : 0;
    modified[VK_SHIFT] = shift ? 0x80 : 0;
    Require(SetKeyboardState(modified), "set native test thread shortcut modifiers");
    MSG message{};
    message.hwnd = static_cast<HWND>(editor->GetHandle());
    message.message = alt ? WM_SYSKEYDOWN : WM_KEYDOWN;
    message.wParam = key;
    // wxMSW reads Alt from the message context, including an unhandled accelerator.
    message.lParam = alt ? (LPARAM{KF_ALTDOWN} << 16) : 0;
    if (!frame->MSWTranslateMessage(&message))
        SendMessageW(message.hwnd, message.message, message.wParam, message.lParam);
    SendMessageW(message.hwnd, alt ? WM_SYSKEYUP : WM_KEYUP, key, message.lParam);
    SetKeyboardState(previous);
#else
    NativeKeys::Shortcut(frame, key, control, alt, shift);
#endif
    wxYield();
}
void Shortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "AB\nCD", 0, 1, Scintilla::EndOfLine::Lf);
    NativeKey(frame, 'D', false, true);
    Require(TestEditor::Text(editor) == "AAB\nCD", "native Alt+D duplicates selected bytes exactly once");
    History(editor, "AB\nCD", "AAB\nCD");
    TestEditor::ResetStream(editor, "AB\nCD", 0, 1, Scintilla::EndOfLine::Lf);
    NativeKey(frame, 'D', true, false);
    Require(TestEditor::Text(editor) == "AB\nAB\nCD", "native Ctrl+D duplicates complete caret line exactly once");
    History(editor, "AB\nCD", "AB\nAB\nCD");
    TestEditor::ResetStream(editor, "AB\nCD", 0, 4);
    NativeKey(frame, 'S', false, true);
    Require(TestEditor::Text(editor) == "CD\nAB", "native Alt+S transposes main caret line exactly once");
    History(editor, "AB\nCD", "CD\nAB");
    TestEditor::ResetStream(editor, "AB\nCD", 0, 1);
    NativeKey(frame, 'D', true, false, true);
    Require(TestEditor::Text(editor) == "CD", "native Ctrl+Shift+D remains original whole-line deletion");
    History(editor, "AB\nCD", "CD");
    TestEditor::ResetStream(editor, "0x10\nCD", 0, 4);
    NativeKey(frame, 'D', true, true);
    Require(TestEditor::Text(editor) == "16\nCD", "native Ctrl+Alt+D routes the original decimal conversion");
    History(editor, "0x10\nCD", "16\nCD");
    TestEditor::ResetStream(editor, "AB\nCD", 0, 4);
    NativeKey(frame, 'D', true, true);
    Require(TestEditor::Text(editor) == "AB\nCD", "Ctrl+Alt+D preserves nonnumeric bytes without duplication");
    History(editor, "AB\nCD", "AB\nCD");
    for (const auto& [key, gesture] :
         {std::pair{'D', "alt+shift+d"}, std::pair{'S', "alt+shift+s"}, std::pair{'D', "ctrl+alt+shift+d"}}) {
        TestEditor::ResetStream(editor, "AB\nCD", 0, 4);
        NativeKey(frame, key, std::string(gesture) == "ctrl+alt+shift+d", true, true);
        if (TestEditor::Text(editor) != "AB\nCD" || editor->Send(Message::CanUndo))
            std::fprintf(stderr, "Native %s produced <%s>; CanUndo=%lld\n", gesture, TestEditor::Text(editor).c_str(),
                         static_cast<long long>(editor->Send(Message::CanUndo)));
        Require(TestEditor::Text(editor) == "AB\nCD" && !editor->Send(Message::CanUndo),
                "extra modifiers do not route duplication/transposition");
    }
    std::puts("PASS: native Alt+D/Ctrl+D/Alt+S, distinct Ctrl+Shift+D/Ctrl+Alt+D and exact extra-modifier routing");
}
class DuplicateTransposeApp final : public wxApp {
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
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                FirstSlice(frame);
                LineCommands(frame);
                StreamFixtures(frame);
                AvailabilityAndNoChange(frame);
                ShapedSelections(frame);
                WrappedLines(frame);
                SavedBytes(frame);
                Shortcuts(frame);
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
wxIMPLEMENT_APP(DuplicateTransposeApp);
