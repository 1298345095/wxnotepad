#include "editor_test_helpers.h"
#include <print>
#include <array>
#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <memory>
#include <tuple>
#include <utility>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Mapping(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "Straße ﬃ ΐ", 0, 14);
    editor->Send(Message::CustomCaseMapping, 1);
    Require(TestEditor::Text(editor) == "STRASSE FFI Ϊ́", "pinned Unicode mapping expands sharp s, ligature and Greek");
    TestEditor::RequireSelection(editor, 0, 18, "expansion preserves full selection");
}
void Menus(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const int upper = frame->GetMenuBar()->FindMenuItem("Edit", "Upper Case");
    const int lower = frame->GetMenuBar()->FindMenuItem("Edit", "Lower Case");
    Require(upper != wxNOT_FOUND && lower != wxNOT_FOUND, "native Edit menu exposes Upper Case and Lower Case");
    TestEditor::ResetStream(editor, "é σς 𐐨 é中😀\r\n", 0, 25);
    NativeSearch::Command(frame, upper);
    Require(TestEditor::Text(editor) == "É ΣΣ 𐐀 É中😀\r\n",
            "native upper maps accents, Greek and supplementary letters");
    TestEditor::RequireSelection(editor, 0, 25, "equal-byte upper preserves selection");
    TestEditor::ResetStream(editor, "É İ K ΣΩ 𐐀 Á中😀\r\n", 32, 0);
    NativeSearch::Command(frame, lower);
    Require(TestEditor::Text(editor) == "é i̇ k σω 𐐨 á中😀\r\n", "native lower uses pinned expansion and contraction");
    TestEditor::RequireSelection(editor, 31, 0, "reversed lower selection tracks changed UTF-8 byte length");
}
void Shortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "Straße ﬃ ΐ", 0, 14);
    NativeKeys::Shortcut(frame, 'U', true, false, true);
    Require(TestEditor::Text(editor) == "STRASSE FFI Ϊ́", "native Ctrl+Shift+U invokes upper");
    TestEditor::ResetStream(editor, "É İ K ΣΩ 𐐀 Á中😀\r\n", 0, 32);
    NativeKeys::Shortcut(frame, 'U', true);
    Require(TestEditor::Text(editor) == "é i̇ k σω 𐐨 á中😀\r\n", "native Ctrl+U invokes lower");
    for (const bool shift : {false, true}) {
        TestEditor::ResetStream(editor, "aB", 0, 2);
        NativeKeys::Shortcut(frame, 'U', true, true, shift);
        Require(TestEditor::Text(editor) == (shift ? "aB" : "Ab") && bool(editor->Send(Message::CanUndo)) == !shift,
                "Ctrl+Alt+U invokes Invert while additional Shift invokes no case command");
    }
    std::puts("PASS: native Ctrl+Shift+U, Ctrl+U and exact Ctrl+Alt+U routing");
}
void History(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    for (const auto& [label, before, after] : {std::tuple{"Upper Case", "Straße ﬃ ΐ", "STRASSE FFI Ϊ́"},
                                               std::tuple{"Lower Case", "STRASSE FFI Ϊ́", "strasse ffi ΐ"}}) {
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
        TestEditor::ResetStream(editor, before);
        editor->Send(SelectAll);
        const auto eol = editor->Send(GetEOLMode);
        const auto encoding = frame->DocumentEncoding();
        NativeSearch::Command(frame, id);
        Require(TestEditor::Text(editor) == after && editor->Send(CanUndo) && editor->Send(GetModify),
                "case change leaves save point with undo history");
        editor->Send(Undo);
        Require(TestEditor::Text(editor) == before && !editor->Send(CanUndo) && !editor->Send(GetModify),
                "one Undo restores entire original case and save point");
        editor->Send(Redo);
        Require(TestEditor::Text(editor) == after && editor->Send(GetModify) && editor->Send(GetEOLMode) == eol &&
                    frame->DocumentEncoding() == encoding,
                "one Redo restores case change and preserves EOL and external encoding");
    }
    for (const int mapping : {1, 2}) {
        TestEditor::ResetStream(editor, "中😀\r\n", 0, 9);
        editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Undo);
        editor->Send(SetSel, 0, 9);
        editor->Send(CustomCaseMapping, mapping);
        Require(TestEditor::Text(editor) == "中😀\r\n" && !editor->Send(CanUndo) && editor->Send(CanRedo) &&
                    !editor->Send(GetModify),
                "uncased selection preserves clean history and pending redo");
        TestEditor::RequireSelection(editor, 0, 9, "unchanged mapping retains selection");
        editor->Send(SetSel, 3, 3);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(!frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", "Upper Case")) &&
                    !frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", "Lower Case")),
                "original case menus require a nonempty selection");
        editor->Send(CustomCaseMapping, mapping);
        TestEditor::RequireSelection(editor, 3, 3, "empty selection maps no text and keeps caret");
        editor->Send(Redo);
        Require(TestEditor::Text(editor) == "中😀\r\n!", "unchanged and empty commands retain executable redo");
    }
    TestEditor::ResetStream(editor, "aB", 0, 2);
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Undo);
    editor->Send(SetSel, 0, 2);
    editor->Send(SetReadOnly, 1);
    const int upper = frame->GetMenuBar()->FindMenuItem("Edit", "Upper Case");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(upper), "original case menu remains enabled for read-only selection");
    NativeSearch::Command(frame, upper);
    Require(TestEditor::Text(editor) == "aB" && !editor->Send(GetModify) && !editor->Send(CanUndo),
            "read-only core retains text, save point and undo availability");
    // The pinned core repairs the range using the rejected insertion's zero length.
    TestEditor::RequireSelection(editor, 0, 1, "read-only case retains original core selection repair");
    editor->Send(SetReadOnly, 0);
    Require(editor->Send(CanRedo), "read-only conversion retains pending redo");
    editor->Send(Redo);
    Require(TestEditor::Text(editor) == "aB!", "read-only conversion retains executable redo");
    std::puts("PASS: grouped Undo/Redo, save point, unchanged and empty selections, original read-only range repair");
}
void SelectionShapes(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const int upper = frame->GetMenuBar()->FindMenuItem("Edit", "Upper Case");
    // CaseConvertString's pinned capacity check rejects a result exactly three times
    // the input size. The original host passes that empty result to the live core.
    TestEditor::ResetStream(editor, "ΐ", 0, 2);
    NativeSearch::Command(frame, upper);
    Require(TestEditor::Text(editor).empty() && editor->Send(GetModify),
            "exact maximum-expansion span retains original empty conversion result");
    TestEditor::RequireSelection(editor, 0, 0, "empty maximum-expansion result repairs selection");
    editor->Send(Undo);
    Require(TestEditor::Text(editor) == "ΐ" && !editor->Send(GetModify),
            "original maximum-expansion result is undoable");
    TestEditor::ResetStream(editor, "ΐa x ΐb", 0, 3);
    editor->Send(SetMultipleSelection, 1);
    editor->Send(AddSelection, 6, 9);
    editor->Send(AddSelection, 4, 4);
    editor->Send(SetMainSelection, 0);
    NativeSearch::Command(frame, upper);
    Require(TestEditor::Text(editor) == "Ϊ́A x Ϊ́B" && editor->Send(GetSelections) == 3 &&
                editor->Send(GetMainSelection) == 0,
            "multiple selections convert their own spans and retain empty range and main selection");
    Require(editor->Send(GetSelectionNAnchor, 0) == 0 && editor->Send(GetSelectionNCaret, 0) == 7 &&
                editor->Send(GetSelectionNAnchor, 1) == 17 && editor->Send(GetSelectionNCaret, 1) == 10 &&
                editor->Send(GetSelectionNAnchor, 2) == 8 && editor->Send(GetSelectionNCaret, 2) == 8,
            "expansion moves forward, reversed and empty multiple-selection byte endpoints");
    editor->Send(Undo);
    Require(TestEditor::Text(editor) == "ΐa x ΐb" && !editor->Send(GetModify),
            "one Undo restores all multiple-selection case conversions");
    editor->Send(Redo);
    Require(TestEditor::Text(editor) == "Ϊ́A x Ϊ́B", "one Redo restores all multiple selections");
    for (const bool reversed : {false, true}) {
        TestEditor::ResetStream(editor, "aß\nbé");
        editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Rectangle)));
        editor->Send(SetRectangularSelectionAnchor, reversed ? 7 : 0);
        editor->Send(SetRectangularSelectionCaret, reversed ? 0 : 7);
        NativeSearch::Command(frame, upper);
        Require(TestEditor::Text(editor) == "ASS\nBÉ" && editor->Send(GetSelections) == 2 &&
                    editor->Send(GetSelectionMode) ==
                        static_cast<intptr_t>(std::to_underlying(Scintilla::SelectionMode::Rectangle)),
                "forward and reverse rectangles convert each row with original shape");
        // ChangeCaseOfSelection restores each row, while the rectangular endpoints
        // retain Selection::MovePositions' separate deletion/insertion movement.
        TestEditor::RequireSelection(editor, reversed ? 4 : 3, reversed ? 3 : 4,
                                     "rectangle keeps original automatic endpoint movement");
        editor->Send(Undo);
        Require(TestEditor::Text(editor) == "aß\nbé" && !editor->Send(GetModify),
                "rectangle conversion is one undo group");
    }
    TestEditor::RequireVirtualRectangleUnchanged(
        editor, [frame, upper] { NativeSearch::Command(frame, upper); },
        "virtual-only rectangle keeps empty byte ranges, virtual columns and history");
    std::puts("PASS: multiple forward/reversed/empty selections, expansion endpoints, rectangles and virtual space");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        TestFiles::EncodedFixture file;
        bool upper;
    };
    const std::array<Fixture, 5> fixtures = {{
        {{"utf8-upper", FileEncoding::Utf8, "\x{ef}\x{bb}\x{bf}Straße ﬃ ΐ\r\n", "\x{ef}\x{bb}\x{bf}STRASSE FFI Ϊ́\r\n"},
         true},
        {{"gbk-upper", FileEncoding::Gbk,
          "\x{d6}\x{d0}"
          "a\r\nB",
          "\x{d6}\x{d0}"
          "A\r\nB"},
         true},
        {{"gbk-lower", FileEncoding::Gbk,
          "\x{d6}\x{d0}"
          "a\r\nB",
          "\x{d6}\x{d0}"
          "a\r\nb"},
         false},
        {{"utf16-upper", FileEncoding::Utf16Le,
          std::string("\x{ff}\x{fe}\x{61}\0\x{e9}\0\x{01}\x{d8}\x{28}\x{dc}\r\0\n\0", 14),
          std::string("\x{ff}\x{fe}\x{41}\0\x{c9}\0\x{01}\x{d8}\0\x{dc}\r\0\n\0", 14)},
         true},
        {{"utf16-lower", FileEncoding::Utf16Le,
          std::string("\x{ff}\x{fe}\x{41}\0\x{30}\x{01}\x{2a}\x{21}\x{01}\x{d8}\0\x{dc}\r\0\n\0", 16),
          std::string("\x{ff}\x{fe}\x{61}\0\x{69}\0\x{07}\x{03}\x{6b}\0\x{01}\x{d8}\x{28}\x{dc}\r\0\n"
                      "\0",
                      18)},
         false},
    }};
    const auto directory = TestFiles::TemporaryDirectory("case-conversion");
    std::filesystem::create_directory(directory);
    for (const auto& fixture : fixtures) {
        using enum Scintilla::Message;
        const auto path = directory / fixture.file.name;
        TestFiles::WriteBytes(path, fixture.file.before);
        editor->Send(SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.file.encoding).status == OpenStatus::Opened,
                "open case fixture through application file operation");
        const auto bom = frame->DocumentHasBom();
        const auto eol = editor->Send(GetEOLMode);
        editor->Send(SelectAll);
        NativeSearch::Command(frame,
                              frame->GetMenuBar()->FindMenuItem("Edit", fixture.upper ? "Upper Case" : "Lower Case"));
        TestFiles::RequireSavedUndoRedo(frame, path, fixture.file,
                                        {"case Save matches independently reopened encoded bytes",
                                         "one Undo restores independently reopened original bytes",
                                         "one Redo restores independently reopened converted bytes"});
        Require(frame->DocumentEncoding() == fixture.file.encoding && frame->DocumentHasBom() == bom &&
                    editor->Send(GetEOLMode) == eol && !editor->Send(GetModify),
                "case Save retains external encoding, BOM, EOL mode and final save point");
    }
    const auto path = directory / "gbk-unrepresentable";
    TestFiles::WriteBytes(path, "\x{a8}\x{a6}"); // CP936 é has no representable uppercase É.
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
            "open GBK letter with unrepresentable uppercase");
    editor->Send(Message::SelectAll);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Upper Case"));
    Require(TestEditor::Text(editor) == "É", "case mapping remains UTF-8 despite external GBK codec");
    bool asked = false;
    const auto cancelled = frame->Save([&asked] {
        asked = true;
        return EncodingLossChoice::Cancel;
    });
    Require(asked && cancelled.status == SaveStatus::Cancelled && TestFiles::Bytes(path) == "\x{a8}\x{a6}" &&
                editor->Send(Message::GetModify) && frame->DocumentEncoding() == FileEncoding::Gbk,
            "existing GBK loss cancellation preserves disk bytes and unsaved Unicode result");
    Require(frame->Save([] { return EncodingLossChoice::Replace; }).status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == "?" && TestEditor::Text(editor) == "É" && !editor->Send(Message::GetModify),
            "existing explicit GBK replacement saves literal replacement byte and retains Unicode editor text");
    std::puts("PASS: exact UTF-8 BOM, GBK, UTF-16LE BOM bytes, Undo/Redo and unrepresentable-GBK decisions");
}
class CaseConversionApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                Mapping(frame);
                Menus(frame);
                Shortcuts(frame);
                History(frame);
                SelectionShapes(frame);
                SavedBytes(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
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
wxIMPLEMENT_APP(CaseConversionApp);
