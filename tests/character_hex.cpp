#include "editor_test_helpers.h"
#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "character_hex_fixtures.h"
#include "editor/platform/character_hex.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <utility>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}
void CharacterToHex(EditorFrame* frame) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Character to Hex");
    Require(id != wxNOT_FOUND, "Convert exposes original Character to Hex command");
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "A", 0, 1);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == "\\x41", "Character to Hex matches original ASCII escape");
    TestEditor::RequireSelection(editor, 0, 4, "hex expansion retains selected result");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "A" && !editor->Send(Message::GetModify),
            "one Undo restores original bytes and save point");
}
void ReferenceFixtures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const auto& fixture : CharacterHexFixtures) {
        using enum Scintilla::Message;
        TestEditor::ResetStream(editor, "");
        editor->Send(AddText, fixture.before.size(), reinterpret_cast<intptr_t>(fixture.before.data()));
        editor->Send(EmptyUndoBuffer);
        editor->Send(SetSavePoint);
        editor->Send(SetSel, 0, static_cast<intptr_t>(fixture.before.size()));
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", fixture.command == 40202 ? "Character to Hex"
                                                                                          : "Hex to Character");
        Require(id != wxNOT_FOUND, "Convert exposes original hex conversion command");
        NativeSearch::Command(frame, id);
        if (TestEditor::Text(editor) != fixture.after) {
            std::fprintf(stderr, "Original fixture mismatch: %s\n", fixture.name);
            throw TestEditor::Failure("hex conversion matches independent original bytes");
        }
        const bool changed = fixture.before != fixture.after;
        Require(bool(editor->Send(GetModify)) == changed && bool(editor->Send(CanUndo)) == changed,
                "original fixture retains save point and undo state");
        TestEditor::RequireSelection(editor, 0, static_cast<intptr_t>(fixture.after.size()),
                                     "fixture conversion retains full result selection");
        if (changed) {
            editor->Send(Undo);
            Require(TestEditor::Text(editor) == fixture.before && !editor->Send(GetModify),
                    "one Undo restores original fixture bytes and save point");
            editor->Send(Redo);
            Require(TestEditor::Text(editor) == fixture.after && editor->Send(GetModify),
                    "one Redo restores independently captured fixture bytes");
        }
    }
    std::puts("PASS: 18 independent original Character/Hex byte fixtures, endpoints and Undo/Redo");
}
void UnicodeBoundaries() {
    Require(::CharacterToHex("\x{e2}\x{82}A\x{c2}\x{c3}\x{a9}") == "\\uFFFD\\x41\\uFFFD\\xE9",
            "invalid UTF-8 prefixes leave the following ASCII or valid leading byte for the next character");
    Require(::HexToCharacter(R"(\u0000\uD800\u0041\U0010FFFF)") == std::string("\0\u{FFFD}A\u{10FFFF}", 9),
            "NUL, lone surrogate and the highest scalar preserve their explicit UTF-8 byte lengths");
}
void RectangleConversion(EditorFrame* frame, bool encode, int id, const char* rectangle, bool reverse) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, rectangle);
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Rectangle)));
    editor->Send(SetRectangularSelectionAnchor, reverse ? std::string_view(rectangle).size() : 0);
    editor->Send(SetRectangularSelectionCaret, reverse ? 0 : std::string_view(rectangle).size());
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == (encode ? "\\x41\n\\x42" : "A\nB") && editor->Send(GetSelections) == 2,
            "rectangle converts original rows independently");
    TestEditor::RequireSelection(editor, reverse ? (encode ? 5 : 2) : (encode ? 4 : 1),
                                 reverse ? (encode ? 4 : 1) : (encode ? 5 : 2),
                                 "rectangle matches original automatic endpoint movement");
    editor->Send(Undo);
    Require(TestEditor::Text(editor) == rectangle && !editor->Send(GetModify),
            "rectangle conversion shares one Undo group");
}
void RectangleConversions(EditorFrame* frame, bool encode, int id) {
    auto* editor = frame->Editor();
    const char* rectangle = encode ? "A\nB" : "\\x41\n\\x42";
    for (const bool reverse : {false, true})
        RectangleConversion(frame, encode, id, rectangle, reverse);
    TestEditor::RequireVirtualRectangleUnchanged(
        editor, [frame, id] { NativeSearch::Command(frame, id); },
        "virtual rectangle retains original columns and history");
}
void StreamConversionStates(EditorFrame* frame, bool encode) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const int mapping = encode ? 40202 : 40203;
    const char* label = encode ? "Character to Hex" : "Hex to Character";
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    const std::string before = encode ? "A x B" : "\\x41 x \\x42";
    const std::string after = encode ? "\\x41\\x20\\x78\\x20\\x42" : "A x B";
    TestEditor::ResetStream(editor, before.c_str());
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Undo);
    editor->Send(SetSel, before.size(), 0);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == after && editor->Send(GetModify) && editor->Send(CanUndo) &&
                !editor->Send(CanRedo),
            "changed conversion replaces pending redo and leaves save point");
    TestEditor::RequireSelection(editor, static_cast<intptr_t>(after.size()), 0,
                                 "reversed selection retains original direction");
    editor->Send(Undo);
    Require(TestEditor::Text(editor) == before && !editor->Send(GetModify),
            "one Undo restores reversed conversion and save point");

    TestEditor::ResetStream(editor, before.c_str());
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Undo);
    editor->Send(SetSel, before.size(), 0);
    editor->Send(SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(id), "original read-only selection retains menu availability");
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == before && !editor->Send(GetModify) && !editor->Send(CanUndo) &&
                !editor->Send(CanRedo),
            "read-only conversion retains bytes save point and original disabled history commands");
    TestEditor::RequireSelection(editor, 0, 0, "read-only conversion matches original rejected-insertion repair");
    editor->Send(SetReadOnly, 0);
    Require(editor->Send(CanRedo), "leaving read-only mode reveals original pending redo");
    editor->Send(Redo);
    Require(TestEditor::Text(editor) == before + "!", "read-only conversion preserves executable redo");

    TestEditor::ResetStream(editor, before.c_str(), 1, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(id), "empty selection disables original hex command");
    editor->Send(CustomCaseMapping, mapping);
    Require(TestEditor::Text(editor) == before && !editor->Send(CanUndo) && !editor->Send(GetModify),
            "empty conversion preserves text and history");
    TestEditor::RequireSelection(editor, 1, 1, "empty conversion preserves caret");

    const int size = encode ? 1 : 4;
    TestEditor::ResetStream(editor, before.c_str(), 0, size);
    editor->Send(SetMultipleSelection, 1);
    editor->Send(AddSelection, before.size() - size, static_cast<intptr_t>(before.size()));
    editor->Send(AddSelection, size + 1, size + 1);
    editor->Send(SetMainSelection, 0);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == (encode ? "\\x41 x \\x42" : "A x B") && editor->Send(GetSelections) == 3 &&
                editor->Send(GetMainSelection) == 0,
            "multiple forward reversed and empty ranges convert independently");
    const std::array<intptr_t, 6> endpoints =
        encode ? std::array<intptr_t, 6>{0, 4, 11, 7, 5, 5} : std::array<intptr_t, 6>{0, 1, 5, 4, 2, 2};
    for (size_t index = 0; index < 3; ++index)
        Require(editor->Send(GetSelectionNAnchor, index) == endpoints[2 * index] &&
                    editor->Send(GetSelectionNCaret, index) == endpoints[2 * index + 1],
                "multiple endpoints match independently captured original state");
    editor->Send(Undo);
    Require(TestEditor::Text(editor) == before && !editor->Send(GetModify),
            "multiple conversions share one Undo group");

    RectangleConversions(frame, encode, id);
}
void SelectionAndHistory(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    for (const bool encode : {true, false})
        StreamConversionStates(frame, encode);
    TestEditor::ResetStream(editor, "literal \\x4 \\u12ZZ", 18, 0);
    const auto original = TestEditor::Text(editor);
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Undo);
    editor->Send(SetSel, original.size(), 0);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Hex to Character"));
    Require(TestEditor::Text(editor) == original && !editor->Send(GetModify) && !editor->Send(CanUndo) &&
                editor->Send(CanRedo),
            "unchanged decode retains save point and pending redo");
    TestEditor::RequireSelection(editor, static_cast<intptr_t>(original.size()), 0,
                                 "unchanged decode retains reversed endpoints");
    editor->Send(Redo);
    Require(TestEditor::Text(editor) == original + "!", "unchanged decode retains executable redo");
    std::puts("PASS: original empty/reversed/multiple/rectangle/virtual/read-only states and pending redo");
}
void NativeCommands(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool encode : {true, false}) {
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", encode ? "Character to Hex" : "Hex to Character");
        Require(frame->GetMenuBar()->FindItem(id)->GetItemLabel() ==
                    (encode ? "C&haracter to Hex\tCtrl+Alt+X" : "Hex to Cha&racter\tCtrl+Alt+C"),
                "native menu retains original label mnemonic and shortcut");
        const char* before = encode ? "é" : "\\xE9";
        const char* after = encode ? "\\xE9" : "é";
        TestEditor::ResetStream(editor, before, 0, static_cast<intptr_t>(std::string_view(before).size()));
        NativeKeys::Shortcut(frame, encode ? 'X' : 'C', true, true);
        Require(TestEditor::Text(editor) == after, "native Ctrl+Alt+X/C invokes original hex command");
        TestEditor::ResetStream(editor, before, 0, static_cast<intptr_t>(std::string_view(before).size()));
        NativeKeys::Shortcut(frame, encode ? 'X' : 'C', true, true, true);
        Require(TestEditor::Text(editor) == before && !editor->Send(Message::CanUndo),
                "additional Shift preserves exact original shortcut routing");
    }
    std::puts("PASS: native menu labels and exact Ctrl+Alt+X/C shortcuts");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("character-hex");
    std::filesystem::create_directory(directory);
    for (const bool encode : {true, false}) {
        const std::array<TestFiles::EncodedFixture, 3> fixtures{
            {{"utf8", FileEncoding::Utf8,
              encode ? "\x{ef}\x{bb}\x{bf}"
                       "Aé中\r\n"
                     : "\x{ef}\x{bb}\x{bf}"
                       "\\x41\\xE9\\u4E2D\\x0D\\x0A",
              encode ? "\x{ef}\x{bb}\x{bf}"
                       "\\x41\\xE9\\u4E2D\\x0D\\x0A"
                     : "\x{ef}\x{bb}\x{bf}"
                       "Aé中\r\n"},
             {"gbk", FileEncoding::Gbk, encode ? "\x{a8}\x{a6}\x{d6}\x{d0}" : "\\xE9\\u4E2D",
              encode ? "\\xE9\\u4E2D" : "\x{a8}\x{a6}\x{d6}\x{d0}"},
             {"utf16", FileEncoding::Utf16Le,
              encode ? std::string("\x{ff}\x{fe}\x{41}\0", 4)
                     : std::string("\x{ff}\x{fe}\\\0x\x{00}\x{34}\0\x{31}\0", 10),
              encode ? std::string("\x{ff}\x{fe}\\\0x\x{00}\x{34}\0\x{31}\0", 10)
                     : std::string("\x{ff}\x{fe}\x{41}\0", 4)}}};
        for (const auto& fixture : fixtures) {
            using enum Scintilla::Message;
            const auto path = directory / fixture.name;
            TestFiles::WriteBytes(path, fixture.before);
            editor->Send(SetSavePoint);
            Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                    "open encoded fixture through application file command");
            const auto bom = frame->DocumentHasBom();
            const auto eol = editor->Send(GetEOLMode);
            editor->Send(SelectAll);
            NativeSearch::Command(
                frame, frame->GetMenuBar()->FindMenuItem("Edit", encode ? "Character to Hex" : "Hex to Character"));
            TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                            {"conversion saves literal expected disk bytes",
                                             "Undo saves original disk bytes", "Redo saves original conversion bytes"});
            Require(frame->DocumentEncoding() == fixture.encoding && frame->DocumentHasBom() == bom &&
                        editor->Send(GetEOLMode) == eol && !editor->Send(GetModify),
                    "hex conversion retains encoding BOM EOL metadata and saved state");
        }
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: UTF-8 GBK UTF-16 independent disk bytes, Undo/Redo and document metadata");
}
class CharacterHexApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                CharacterToHex(frame);
                ReferenceFixtures(frame);
                UnicodeBoundaries();
                SelectionAndHistory(frame);
                NativeCommands(frame);
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
wxIMPLEMENT_APP(CharacterHexApp);
