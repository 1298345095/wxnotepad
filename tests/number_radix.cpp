#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "native_key_helpers.h"
#include "native_search_helpers.h"
#include "number_radix_fixtures.h"
#include "settings/settings_store.h"
#include <wx/app.h>
#include <wx/modalhook.h>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    Require(id != wxNOT_FOUND, "Edit menu exposes original number conversion command");
    NativeSearch::Command(frame, id);
}
void Reset(EditorControl* editor, std::string_view text, intptr_t anchor, intptr_t caret) {
    TestEditor::ResetStream(editor, "");
    editor->Send(Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    editor->Send(Message::SetSel, anchor, caret);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
void Fixtures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const auto& fixture : NumberRadixReference::fixtures) {
        for (bool reverse : {false, true}) {
            const auto before = static_cast<intptr_t>(fixture.before.size());
            const auto after = static_cast<intptr_t>(fixture.after.size());
            Reset(editor, fixture.before, reverse ? before : 0, reverse ? 0 : before);
            Menu(frame, fixture.command);
            if (TestEditor::Text(editor) != fixture.after)
                std::fprintf(stderr, "%s: expected <%.*s>, got <%s>\n", fixture.command,
                             static_cast<int>(fixture.after.size()), fixture.after.data(),
                             TestEditor::Text(editor).c_str());
            Require(TestEditor::Text(editor) == fixture.after, "conversion matches captured original bytes");
            TestEditor::RequireSelection(editor, reverse ? after : 0, reverse ? 0 : after,
                                         "converted endpoints match original direction");
            Require(editor->Send(Message::GetModify) && editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::CanRedo),
                    "original replacement modifies history even when output is unchanged");
            editor->Send(Message::Undo);
            Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::GetModify) &&
                        !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                    "one Undo restores original bytes and save point");
            TestEditor::RequireSelection(editor, reverse ? before : 0, reverse ? 0 : before,
                                         "Undo restores original endpoints");
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == fixture.after && editor->Send(Message::GetModify),
                    "one Redo restores converted bytes");
            TestEditor::RequireSelection(editor, 0, 0, "Redo matches original collapsed selection");
        }
    }
    std::puts(
        "PASS: 80 original runtime fixtures: all radices, scanner quirks, wraparound, NUL, direction and history");
}
class RectangleFeedback final : public wxModalDialogHook {
public:
    RectangleFeedback() { Register(); }
    int Enter(wxDialog*) override {
        ++count;
        return wxID_OK;
    }
    int count = 0;
};
void SelectionAndHistory(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const char* command : {"To Hexadecimal", "To Decimal", "To Binary", "To Octet"}) {
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", command);
        TestEditor::ResetStream(editor, "plain text", 10, 0);
        editor->Send(Message::InsertText, 10, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        Menu(frame, command);
        Require(TestEditor::Text(editor) == "plain text" && editor->Send(Message::GetModify) &&
                    editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo),
                "captured unchanged-output replacement discards pending redo and leaves save point");
        TestEditor::RequireSelection(editor, 10, 0, "unchanged replacement retains reverse selection");
        editor->Send(Message::Undo);
        Require(!editor->Send(Message::GetModify), "Undo unchanged replacement restores save point");

        editor->Send(Message::SetSel, 3, 3);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(!frame->GetMenuBar()->IsEnabled(id), "empty selection disables original command");
        Menu(frame, command);
        Require(TestEditor::Text(editor) == "plain text" && !editor->Send(Message::GetModify) &&
                    !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                "empty command preserves bytes, history and save point");
        TestEditor::RequireSelection(editor, 3, 3, "empty command retains caret");

        TestEditor::ResetStream(editor, "12\n34");
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, 0);
        editor->Send(Message::SetRectangularSelectionCaret, 5);
        RectangleFeedback feedback;
        Menu(frame, command);
        Require(feedback.count == 1 && TestEditor::Text(editor) == "12\n34" && !editor->Send(Message::GetModify) &&
                    !editor->Send(Message::CanUndo) && editor->Send(Message::GetRectangularSelectionAnchor) == 0 &&
                    editor->Send(Message::GetRectangularSelectionCaret) == 5,
                "original rectangle warns and preserves bytes, endpoints and save point");
        editor->Send(Message::SetRectangularSelectionAnchor, 1);
        editor->Send(Message::SetRectangularSelectionCaret, 1);
        Menu(frame, command);
        Require(feedback.count == 1, "empty rectangle returns before warning");
    }
    TestEditor::ResetStream(editor, "12 a 34", 0, 2, Scintilla::EndOfLine::CrLf);
    editor->Send(Message::AddSelection, 7, 5);
    Menu(frame, "To Hexadecimal");
    Require(TestEditor::Text(editor) == "12 a 0xc\r\n0x22" && editor->Send(Message::GetSelections) == 1,
            "captured multiple-selection text is concatenated and replaces only main selection");
    TestEditor::RequireSelection(editor, 5, 14, "multiple-selection replacement has captured main endpoints");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "12 a 34" && !editor->Send(Message::GetModify),
            "multiple-selection replacement is one undo action");

    TestEditor::ResetStream(editor, "12 a 34", 0, 2);
    editor->Send(Message::SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    const int hex = frame->GetMenuBar()->FindMenuItem("Edit", "To Hexadecimal");
    Require(frame->GetMenuBar()->IsEnabled(hex), "original read-only selection keeps conversion available");
    Menu(frame, "To Hexadecimal");
    Require(TestEditor::Text(editor) == "12 a 34" && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo),
            "read-only core preserves original bytes and save point");
    TestEditor::RequireSelection(editor, 0, 3, "read-only replacement retains captured formatted endpoint");
    editor->Send(Message::SetReadOnly, 0);

    TestEditor::ResetStream(editor, "head12tail", 4, 6);
    Menu(frame, "To Hexadecimal");
    Require(TestEditor::Text(editor) == "head0xctail", "exact selection preserves adjacent bytes");
    TestEditor::RequireSelection(editor, 4, 7, "partial-selection output retains exact boundary");
    std::puts("PASS: main/multiple/empty/reverse/rectangular/read-only selections, notification and pending redo");
}
void NativeShortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        char key;
        const char* output;
    };
    for (const auto& fixture :
         {Fixture{'H', "0x10 0x2 0x8 0xa"}, Fixture{'D', "16 2 8 10"}, Fixture{'B', "0b10000 0b10 0b1000 0b1010"}}) {
        TestEditor::ResetStream(editor, "0x10 0b10 0O10 10", 0, 17);
        NativeKeys::Shortcut(frame, fixture.key, true, true);
        Require(TestEditor::Text(editor) == fixture.output, "original native Ctrl+Alt shortcut converts text");
    }
    const int octal = frame->GetMenuBar()->FindMenuItem("Edit", "To Octet");
    Require(!frame->GetMenuBar()->FindItem(octal)->GetItemLabel().Contains('\t'),
            "original To Octet has no accelerator");
    std::puts("PASS: native Ctrl+Alt+H/D/B and original shortcut-free To Octet availability");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-number-radix");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string after;
    };
    const Fixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中12\r\n", "\xef\xbb\xbf中0xc\r\n"},
        {"gbk", FileEncoding::Gbk,
         "\xd6\xd0"
         "12\r\n",
         "\xd6\xd0"
         "0xc\r\n"},
        {"utf16", FileEncoding::Utf16Le,
         std::string("\xff\xfe\x2d\x4e"
                     "1\0"
                     "2\0\r\0\n\0",
                     12),
         std::string("\xff\xfe\x2d\x4e"
                     "0\0x\0c\0\r\0\n\0",
                     14)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(wxString::FromUTF8(path.string()), fixture.encoding).status == OpenStatus::Opened,
                "open encoded fixture through application");
        const auto eol = editor->Send(Message::GetEOLMode);
        editor->Send(Message::SelectAll);
        Menu(frame, "To Hexadecimal");
        Require(frame->DocumentEncoding() == fixture.encoding && editor->Send(Message::GetEOLMode) == eol,
                "conversion preserves encoding and EOL metadata");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "independent UTF-8 BOM, GBK and UTF-16LE BOM disk bytes match");
        editor->Send(Message::Undo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "Undo and Save restore independent original disk bytes");
        editor->Send(Message::Redo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "Redo and Save restore independent converted disk bytes");
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: encoded disk bytes, BOM/EOL metadata and saved Undo/Redo");
}
class NumberRadixApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                TestEditor::ResetStream(editor, "0 255 256", 0, 9);
                Menu(frame, "To Hexadecimal");
                Require(TestEditor::Text(editor) == "0 0xff 0x100", "hexadecimal matches original runtime fixture");
                std::puts("PASS: native hexadecimal command matches original runtime fixture");
                Fixtures(frame);
                SelectionAndHistory(frame);
                NativeShortcuts(frame);
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
wxIMPLEMENT_APP(NumberRadixApp);
