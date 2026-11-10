#include "editor_test_helpers.h"
#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "custom_case_fixtures.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}
void ReferenceFixtures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const auto& fixture : CustomCaseFixtures) {
        TestEditor::ResetStream(editor, fixture.before);
        editor->Send(Message::SetSel, 0, editor->Send(Message::GetLength));
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", fixture.command);
        NativeSearch::Command(frame, id);
        if (TestEditor::Text(editor) != fixture.after) {
            std::fprintf(stderr, "%s: before=%s actual=%s expected=%s\n", fixture.command, fixture.before,
                         TestEditor::Text(editor).c_str(), fixture.after);
            throw TestEditor::Failure("custom case matches original Windows fixture");
        }
        const bool changed = std::string_view(fixture.before) != fixture.after;
        Require(bool(editor->Send(Message::GetModify)) == changed && bool(editor->Send(Message::CanUndo)) == changed,
                "original changed and unchanged results preserve history and save point");
        TestEditor::RequireSelection(editor, 0, static_cast<intptr_t>(std::string_view(fixture.after).size()),
                                     "original conversion updates UTF-8 selection endpoints");
        if (changed) {
            editor->Send(Message::Undo);
            Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::GetModify),
                    "one Undo restores original text and save point");
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == fixture.after && editor->Send(Message::GetModify),
                    "one Redo restores mapped text");
        }
    }
    std::puts("PASS: 36 literal original Windows custom-case fixtures and history");
}
void EmbeddedNulHistory(EditorFrame* frame) {
    auto* editor = frame->Editor();
    // Notepad4 89b8b36 Edit.cpp:1596 compares Title/Sentence through the first
    // UTF-16 NUL, after mapping the complete length-aware selection.
    for (const auto& [label, before] :
         {std::pair{"Sentence Case", std::string("\0ABC", 4)}, std::pair{"Title Case", std::string("A\0b", 3)},
          std::pair{"Sentence Case", std::string("A\0b", 3)}, std::pair{"Title Case", std::string("\0ABC", 4)},
          std::pair{"Title Case", std::string("A\0B", 3)}}) {
        TestEditor::ResetStream(editor, "");
        editor->Send(Message::AddText, before.size(), reinterpret_cast<intptr_t>(before.data()));
        editor->Send(Message::EmptyUndoBuffer);
        editor->Send(Message::SetSavePoint);
        Require(TestEditor::Text(editor) == before, "length-aware setup retains literal embedded NUL bytes");
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, before.size(), 0);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", label));
        Require(TestEditor::Text(editor) == before, "original NUL-sensitive no-change gate retains literal bytes");
        TestEditor::RequireSelection(editor, static_cast<intptr_t>(before.size()), 0,
                                     "NUL-sensitive no-change gate retains reversed endpoints");
        Require(!editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                "NUL-sensitive no-change gate retains save point and pending redo");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == before + "!" && editor->Send(Message::GetModify),
                "pending redo executes after unchanged NUL-sensitive conversion");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == before && !editor->Send(Message::GetModify),
                "Undo restores original embedded NUL bytes and save point");
    }
    std::puts("PASS: embedded NUL Title/Sentence no-change bytes, reversed endpoints, save point and pending redo");
}
void EmbeddedNulChanges(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* label;
        std::string before;
        std::string after;
    };
    // Literal results captured with the pinned EditMapTextCase Windows APIs,
    // original UTF-16 loops and original no-change gate (zh-SG reference host).
    for (const auto& fixture : {Fixture{"Title Case", std::string("a\0b", 3), std::string("A\0B", 3)},
                                Fixture{"Sentence Case", std::string("a\0b", 3), std::string("A\0b", 3)},
                                Fixture{"Invert Case", std::string("a\0b", 3), std::string("A\0B", 3)},
                                Fixture{"Invert Case", std::string("A\0b", 3), std::string("a\0B", 3)},
                                Fixture{"Invert Case", std::string("\0ABC", 4), std::string("\0abc", 4)},
                                Fixture{"Title Case", std::string("a\0𐐀B", 7), std::string("A\0𐐀B", 7)},
                                Fixture{"Sentence Case", std::string("a\0𐐀B", 7), std::string("A\0𐐨b", 7)},
                                Fixture{"Invert Case", std::string("a\0𐐀B", 7), std::string("A\0𐐀b", 7)}}) {
        TestEditor::ResetStream(editor, "");
        editor->Send(Message::AddText, fixture.before.size(), reinterpret_cast<intptr_t>(fixture.before.data()));
        editor->Send(Message::EmptyUndoBuffer);
        editor->Send(Message::SetSavePoint);
        Require(TestEditor::Text(editor) == fixture.before, "length-aware setup retains NUL and supplementary bytes");
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, 0, static_cast<intptr_t>(fixture.before.size()));
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", fixture.label));
        Require(TestEditor::Text(editor) == fixture.after, "embedded NUL mapping matches literal original bytes");
        TestEditor::RequireSelection(editor, 0, static_cast<intptr_t>(fixture.after.size()),
                                     "changed embedded NUL mapping retains full UTF-8 endpoints");
        Require(editor->Send(Message::GetModify) && editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo),
                "changed embedded NUL mapping replaces pending redo and leaves the save point");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::GetModify) &&
                    !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                "one Undo restores original embedded NUL bytes and save point");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == fixture.after && editor->Send(Message::GetModify),
                "one Redo restores literal mapped embedded NUL bytes");
    }
    std::puts("PASS: embedded NUL word boundaries and per-command UTF-16 behavior, endpoints, save point, Undo/Redo");
}
void Shortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const auto& [key, expected] : {std::pair{'U', "Ab"}, std::pair{'I', "Ab"}, std::pair{'O', "Ab"}}) {
        TestEditor::ResetStream(editor, "aB", 0, 2);
        NativeKeys::Shortcut(frame, key, true, true);
        if (TestEditor::Text(editor) != expected)
            std::fprintf(stderr, "native Ctrl+Alt+%c: actual=%s expected=%s\n", key, TestEditor::Text(editor).c_str(),
                         expected);
        Require(TestEditor::Text(editor) == expected, "exact native custom-case shortcut invokes conversion");
        TestEditor::ResetStream(editor, "aB", 0, 2);
        NativeKeys::Shortcut(frame, key, true, true, true);
        Require(TestEditor::Text(editor) == "aB" && !editor->Send(Message::CanUndo),
                "additional Shift does not invoke custom-case shortcut");
    }
    std::puts("PASS: native Ctrl+Alt+U/I/O and exact extra-Shift routing");
}
void UnchangedHistory(EditorFrame* frame, const char* label, int mapping) {
    auto* editor = frame->Editor();
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    TestEditor::ResetStream(editor, "中😀\r\n", 0, 9);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    NativeSearch::Command(frame, id);
    Require(!editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
            "unchanged custom mapping preserves clean pending redo");
    TestEditor::RequireSelection(editor, 0, 9, "unchanged custom mapping retains range");
    editor->Send(Message::SetSel, 3, 3);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(id), "empty selection disables original custom menu");
    editor->Send(Message::CustomCaseMapping, mapping);
    TestEditor::RequireSelection(editor, 3, 3, "empty custom mapping retains caret");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "中😀\r\n!", "unchanged and empty mapping retain executable redo");
}
void ReadOnlyHistory(EditorFrame* frame, const char* label, int mapping) {
    auto* editor = frame->Editor();
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    TestEditor::ResetStream(editor, "aB", 0, 2);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 0, 2);
    editor->Send(Message::SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(id), "read-only selection retains original menu enablement");
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == "aB" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
            "read-only mapping retains text and save point");
    // Both bytes differ in aB -> Ab, so the original trimmed replacement
    // repairs the endpoint by -2 after the read-only insertion returns zero.
    TestEditor::RequireSelection(editor, 0, 0, "read-only mapping retains original rejected-insertion repair");
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "aB!", "read-only mapping retains pending redo");
    TestEditor::ResetStream(editor, "ab", 0, 2);
    editor->Send(Message::SetReadOnly, 1);
    NativeSearch::Command(frame, id);
    TestEditor::RequireSelection(editor, 0, mapping == 40208 ? 0 : 1,
                                 "read-only endpoint follows each command's original trimmed replacement");
    editor->Send(Message::SetReadOnly, 0);
}
void MultipleSelections(EditorFrame* frame, const char* label, int mapping) {
    auto* editor = frame->Editor();
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    TestEditor::ResetStream(editor, "İA", 3, 0);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == (mapping == 40208 ? "İa" : "Ia"),
            "reversed selection uses Windows custom mappings");
    TestEditor::RequireSelection(editor, mapping == 40208 ? 3 : 2, 0, "reversed range follows contraction");
    TestEditor::ResetStream(editor, "İA x İB", 0, 3);
    editor->Send(Message::SetMultipleSelection, 1);
    editor->Send(Message::AddSelection, 6, 9);
    editor->Send(Message::AddSelection, 4, 4);
    editor->Send(Message::SetMainSelection, 0);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == (mapping == 40208 ? "İa x İb" : "Ia x Ib") &&
                editor->Send(Message::GetSelections) == 3 && editor->Send(Message::GetMainSelection) == 0,
            "custom mapping preserves multiple forward reversed and empty ranges");
    Require(editor->Send(Message::GetSelectionNAnchor, 0) == 0 &&
                editor->Send(Message::GetSelectionNCaret, 0) == (mapping == 40208 ? 3 : 2) &&
                editor->Send(Message::GetSelectionNAnchor, 1) == (mapping == 40208 ? 9 : 7) &&
                editor->Send(Message::GetSelectionNCaret, 1) == (mapping == 40208 ? 6 : 5) &&
                editor->Send(Message::GetSelectionNAnchor, 2) == (mapping == 40208 ? 4 : 3) &&
                editor->Send(Message::GetSelectionNCaret, 2) == (mapping == 40208 ? 4 : 3),
            "multiple-selection endpoints preserve original core repair after contraction");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "İA x İB" && !editor->Send(Message::GetModify),
            "multiple custom conversions share one Undo group");
}
void RectangularSelections(EditorFrame* frame, const char* label, int mapping) {
    auto* editor = frame->Editor();
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    for (const bool reversed : {false, true}) {
        TestEditor::ResetStream(editor, "aß\nbé");
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, reversed ? 7 : 0);
        editor->Send(Message::SetRectangularSelectionCaret, reversed ? 0 : 7);
        NativeSearch::Command(frame, id);
        Require(TestEditor::Text(editor) == (mapping == 40208 ? "Aß\nBÉ" : "Aß\nBé") &&
                    editor->Send(Message::GetSelections) == 2,
                "custom case maps original rectangle rows independently");
        const int rectangleCaret = mapping == 40208 ? 4 : 7;
        TestEditor::RequireSelection(editor, reversed ? rectangleCaret : 1, reversed ? 1 : rectangleCaret,
                                     "rectangle retains original automatic endpoint movement");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "aß\nbé" && !editor->Send(Message::GetModify),
                "rectangle custom conversion shares one Undo group");
    }
    TestEditor::RequireVirtualRectangleUnchanged(
        editor, [frame, id] { NativeSearch::Command(frame, id); },
        "virtual-only rectangle keeps original columns and history");
}
void SelectionAndHistory(EditorFrame* frame) {
    for (const auto& [label, mapping] :
         {std::pair{"Invert Case", 40208}, std::pair{"Title Case", 40210}, std::pair{"Sentence Case", 40209}}) {
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
        const char* expectedLabel = "&Sentence Case\tCtrl+Alt+O";
        if (mapping == 40208)
            expectedLabel = "In&vert Case\tCtrl+Alt+U";
        else if (mapping == 40210)
            expectedLabel = "Title &Case\tCtrl+Alt+I";
        Require(frame->GetMenuBar()->FindItem(id)->GetItemLabel() == expectedLabel,
                "custom menu retains original mnemonic and exact accelerator label");
        UnchangedHistory(frame, label, mapping);
        ReadOnlyHistory(frame, label, mapping);
        MultipleSelections(frame, label, mapping);
        RectangularSelections(frame, label, mapping);
    }
    std::puts("PASS: empty, reversed, multiple, rectangular, virtual-space, read-only and unchanged history outcomes");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("custom-case");
    std::filesystem::create_directory(directory);
    for (const auto& [label, mapping] :
         {std::pair{"Invert Case", 40208}, std::pair{"Title Case", 40210}, std::pair{"Sentence Case", 40209}}) {
        std::string utf8After = "\xef\xbb\xbfÉ i a\r\n";
        std::string utf16After("\xff\xfe\x41\0\x62\0 \0\x69\0\r\0\n\0", 14);
        if (mapping == 40208) {
            utf8After = "\xef\xbb\xbfÉ İ a\r\n";
            utf16After = std::string("\xff\xfe\x41\0\x62\0 \0\x30\x01\r\0\n\0", 14);
        } else if (mapping == 40210) {
            utf8After = "\xef\xbb\xbfÉ I A\r\n";
            utf16After = std::string("\xff\xfe\x41\0\x62\0 \0\x49\0\r\0\n\0", 14);
        }
        const std::array<TestFiles::EncodedFixture, 3> fixtures{{
            {"utf8", FileEncoding::Utf8, "\xef\xbb\xbfé İ A\r\n", utf8After},
            {"gbk", FileEncoding::Gbk, "aB \xd6\xd0\r\n", "Ab \xd6\xd0\r\n"},
            {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x61\0\x42\0 \0\x30\x01\r\0\n\0", 14), utf16After},
        }};
        for (const auto& fixture : fixtures) {
            const auto path = directory / fixture.name;
            TestFiles::WriteBytes(path, fixture.before);
            editor->Send(Message::SetSavePoint);
            Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                    "open custom-case fixture through application file command");
            const auto bom = frame->DocumentHasBom();
            const auto eol = editor->Send(Message::GetEOLMode);
            editor->Send(Message::SelectAll);
            NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", label));
            TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                            {"custom case saves independently reopened expected bytes",
                                             "Undo saves independently reopened original bytes",
                                             "Redo saves independently reopened mapped bytes"});
            Require(frame->DocumentEncoding() == fixture.encoding && frame->DocumentHasBom() == bom &&
                        editor->Send(Message::GetEOLMode) == eol && !editor->Send(Message::GetModify),
                    "custom mapping retains encoding BOM EOL and saved state");
        }
        const auto path = directory / "unrepresentable";
        TestFiles::WriteBytes(path, "\xa8\xa6"); // GBK é has no uppercase É.
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
                "open GBK custom-case loss fixture");
        editor->Send(Message::SelectAll);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", label));
        Require(TestEditor::Text(editor) == "É", "all three custom commands produce original unrepresentable É");
        bool asked = false;
        Require(frame->Save([&asked] {
                         asked = true;
                         return EncodingLossChoice::Cancel;
                     }).status == SaveStatus::Cancelled &&
                    asked && TestFiles::Bytes(path) == "\xa8\xa6" && editor->Send(Message::GetModify),
                "GBK loss cancellation preserves disk bytes and unsaved custom case");
        Require(frame->Save([] { return EncodingLossChoice::Replace; }).status == SaveStatus::Saved &&
                    TestFiles::Bytes(path) == "?" && TestEditor::Text(editor) == "É" &&
                    !editor->Send(Message::GetModify),
                "GBK replacement retains existing explicit-loss behavior");
    }
    std::puts("PASS: each custom command saves exact UTF-8 GBK UTF-16 bytes, Undo/Redo and GBK loss decisions");
}
void Menus(EditorFrame* frame) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Invert Case");
    Require(id != wxNOT_FOUND, "Convert exposes original Invert Case");
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "aB Straße 𐐀", 0, 16);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == "Ab sTRAßE 𐐀", "Invert follows original UTF-16 character casing");
    const int title = frame->GetMenuBar()->FindMenuItem("Edit", "Title Case");
    Require(title != wxNOT_FOUND, "Convert exposes original Title Case");
    TestEditor::ResetStream(editor, "123ABC DON'T O’NEILL foo_bar FOO-BAR");
    editor->Send(Message::SelectAll);
    NativeSearch::Command(frame, title);
    Require(TestEditor::Text(editor) == "123Abc Don't O’neill Foo_Bar Foo-Bar",
            "Title follows original NLS word and apostrophe rules");
    const int sentence = frame->GetMenuBar()->FindMenuItem("Edit", "Sentence Case");
    Require(sentence != wxNOT_FOUND, "Convert exposes original Sentence Case");
    TestEditor::ResetStream(editor, "a.b:a;b!b?b/b@b#b");
    editor->Send(Message::SelectAll);
    NativeSearch::Command(frame, sentence);
    Require(TestEditor::Text(editor) == "A.B:a;B!B?B/b@b#b", "Sentence follows original terminators");
}
class CustomCaseApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                EmbeddedNulHistory(frame);
                EmbeddedNulChanges(frame);
                Menus(frame);
                ReferenceFixtures(frame);
                Shortcuts(frame);
                SelectionAndHistory(frame);
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
wxIMPLEMENT_APP(CustomCaseApp);
