#include "editor_test_helpers.h"
#include "native_key_helpers.h"
#include "escape_text_fixtures.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include "syntax/syntax_catalog.h"
#include <wx/wx.h>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}
void EscapeC(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Escape C Chars");
    Require(id != wxNOT_FOUND, "native Convert menu exposes Escape C Chars");
    TestEditor::ResetStream(editor, "a\t\"'\\\r\nb", 0, 9);
    NativeSearch::Command(frame, id);
    Require(TestEditor::Text(editor) == "a\\t\\\"\\'\\\\\\r\\nb", "C escaping preserves original byte rules");
}
const char* Label(int mapping) {
    constexpr const char* labels[]{"Escape C Chars", "Unescape C Chars", "Escape HTML/XML Chars",
                                   "Unescape HTML/XML Chars"};
    return labels[mapping - 40204];
}
void ResetBytes(EditorControl* editor, std::string_view before) {
    TestEditor::ResetStream(editor, "");
    editor->Send(Message::SetMultipleSelection, 0);
    editor->Send(Message::AddText, before.size(), reinterpret_cast<intptr_t>(before.data()));
    editor->Send(Message::SetSel, 0, before.size());
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
void ReferenceFixtures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const auto& fixture : EscapeFixtures) {
        ResetBytes(editor, fixture.before);
        const auto eol = editor->Send(Message::GetEOLMode);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", fixture.label));
        if (TestEditor::Text(editor) != fixture.after)
            std::fprintf(stderr, "Original escape fixture mapping=%d before length=%zu\n", fixture.mapping,
                         fixture.before.size());
        Require(TestEditor::Text(editor) == fixture.after, "escape output matches literal original Windows bytes");
        TestEditor::RequireSelection(editor, 0, fixture.after.size(), "escape selection follows original byte length");
        const bool changed = fixture.before != fixture.after;
        Require(bool(editor->Send(Message::GetModify)) == changed && bool(editor->Send(Message::CanUndo)) == changed &&
                    editor->Send(Message::GetEOLMode) == eol,
                "escape results preserve original save point, undo availability and EOL mode");
        if (changed) {
            editor->Send(Message::Undo);
            Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::GetModify) &&
                        !editor->Send(Message::CanUndo),
                    "one Undo restores complete original bytes and clean history");
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == fixture.after && editor->Send(Message::GetModify),
                    "one Redo restores complete original escaped bytes");
        }
    }
    std::puts(
        "PASS: 40 original escape byte fixtures including controls, limited C escapes, entities, Unicode and NUL");
}
void SelectionStates(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const auto& fixture : EscapeStateFixtures) {
        ResetBytes(editor, fixture.before);
        const std::string_view kind = fixture.kind;
        const auto length = fixture.before.size();
        const auto firstEnd = fixture.before.find('\n');
        editor->Send(Message::SetSel, kind == "reversed" ? length : 0,
                     kind == "empty" || kind == "reversed" ? 0 : length);
        if (kind == "multiple") {
            editor->Send(Message::SetMultipleSelection, 1);
            editor->Send(Message::SetSel, 0, firstEnd);
            editor->Send(Message::AddSelection, firstEnd + 1, length);
            editor->Send(Message::AddSelection, firstEnd, firstEnd);
            editor->Send(Message::SetMainSelection, 0);
        }
        if (kind.starts_with("rectangle")) {
            editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
            editor->Send(Message::SetRectangularSelectionAnchor, kind == "rectangle-reversed" ? length : 0);
            editor->Send(Message::SetRectangularSelectionCaret, kind == "rectangle-reversed" ? 0 : length);
            // Compare the same original byte ranges: native font metrics can
            // put equal rectangle x positions at different character columns.
            for (int index = 0; index < fixture.count; ++index) {
                editor->Send(Message::SetSelectionNAnchor, index, fixture.initialRanges[index][0]);
                editor->Send(Message::SetSelectionNCaret, index, fixture.initialRanges[index][1]);
            }
        }
        if (kind == "pending" || kind == "readonly") {
            editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
            editor->Send(Message::Undo);
            editor->Send(Message::SetSel, 0, length);
        }
        if (kind == "readonly") editor->Send(Message::SetReadOnly, 1);
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", Label(fixture.mapping));
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(id) == (kind != "empty"),
                "escape menu follows original selection and read-only availability");
        // The disabled empty menu cannot dispatch; exercise its real editor command.
        if (kind == "empty")
            editor->Send(Message::CustomCaseMapping, fixture.mapping);
        else
            NativeSearch::Command(frame, id);
        if (TestEditor::Text(editor) != fixture.after || editor->Send(Message::GetAnchor) != fixture.anchor ||
            editor->Send(Message::GetCurrentPos) != fixture.caret) {
            std::fprintf(stderr, "Original escape state mapping=%d kind=%s\n", fixture.mapping, fixture.kind);
            for (const unsigned char byte : TestEditor::Text(editor))
                std::fprintf(stderr, "%02x", byte);
            std::fprintf(stderr, " actual; expected ");
            for (const unsigned char byte : fixture.after)
                std::fprintf(stderr, "%02x", byte);
            std::fprintf(stderr, "\n");
        }
        Require(TestEditor::Text(editor) == fixture.after, "selection conversion matches original literal bytes");
        TestEditor::RequireSelection(editor, fixture.anchor, fixture.caret,
                                     "original selection endpoints after escape");
        Require(editor->Send(Message::GetSelections) == fixture.count &&
                    editor->Send(Message::GetMainSelection) == fixture.main &&
                    editor->Send(Message::GetModify) == fixture.modified &&
                    editor->Send(Message::CanUndo) == fixture.undo && editor->Send(Message::CanRedo) == fixture.redo,
                "escape selection count, main selection, save point and history match original state");
        for (int index = 0; index < fixture.count; ++index)
            Require(editor->Send(Message::GetSelectionNAnchor, index) == fixture.ranges[index][0] &&
                        editor->Send(Message::GetSelectionNCaret, index) == fixture.ranges[index][1],
                    "each original forward, reversed, empty or rectangle endpoint matches");
        editor->Send(Message::SetReadOnly, 0);
        if (kind == "readonly") {
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == std::string(fixture.before) + "!",
                    "read-only conversion retains executable pending redo");
        } else if (fixture.modified) {
            editor->Send(Message::Undo);
            Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::GetModify) &&
                        !editor->Send(Message::CanUndo),
                    "all converted selections form one Undo group");
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == fixture.after, "Redo reapplies every converted selection");
        }
    }
    for (int mapping = 40204; mapping <= 40207; ++mapping) {
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", Label(mapping));
        TestEditor::ResetStream(editor, "ordinary", 8, 0);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        NativeSearch::Command(frame, id);
        Require(!editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                "unchanged escape preserves clean save point and pending redo");
        TestEditor::RequireSelection(editor, 8, 0, "unchanged escape retains reversed endpoints");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == "ordinary!", "unchanged escape retains executable redo");
        TestEditor::RequireVirtualRectangleUnchanged(
            editor, [frame, id] { NativeSearch::Command(frame, id); },
            "virtual-only escape rectangle preserves history and columns");
    }
    std::puts("PASS: 28 original selection/history states plus unchanged redo and virtual-only rectangles");
}
void ShortcutsAndLanguages(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Shortcut {
        int key;
        bool control;
        bool shift;
        const char* before;
        const char* after;
        const char* label;
    };
    const Shortcut shortcuts[]{{'E', true, false, "\"", "\\\"", "Esca&pe C Chars\tCtrl+Alt+E"},
                               {'R', true, false, "\\t", "\t", "&Unescape C Chars\tCtrl+Alt+R"},
                               {'X', false, true, "<", "&lt;", "Escape HT&ML/XML Chars\tAlt+Shift+X"},
                               {'H', false, true, "&lt;", "<", "Unescape HTML/&XML Chars\tAlt+Shift+H"}};
    for (int index = 0; index < 4; ++index) {
        const auto& shortcut = shortcuts[index];
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", Label(40204 + index));
        Require(frame->GetMenuBar()->FindItem(id)->GetItemLabel() == shortcut.label,
                "escape menu retains original name, mnemonic and exact accelerator");
        ResetBytes(editor, shortcut.before);
        NativeKeys::Shortcut(frame, shortcut.key, shortcut.control, true, shortcut.shift);
        Require(TestEditor::Text(editor) == shortcut.after, "original native escape shortcut invokes command");
        ResetBytes(editor, shortcut.before);
        NativeKeys::Shortcut(frame, shortcut.key, true, true, true);
        Require(TestEditor::Text(editor) == shortcut.before && !editor->Send(Message::CanUndo),
                "extra shortcut modifier preserves original exact matching");
    }
    for (const auto& language : SyntaxCatalog()) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", wxString(language.name)));
        for (int mapping = 40204; mapping <= 40207; ++mapping) {
            const auto& shortcut = shortcuts[mapping - 40204];
            ResetBytes(editor, shortcut.before);
            NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", Label(mapping)));
            Require(TestEditor::Text(editor) == shortcut.after,
                    "current supported languages share original escape rules");
        }
    }
    std::puts("PASS: four native shortcuts, exact modifiers, original labels and all supported languages");
}
void SavedBytes(EditorFrame* frame) {
    const auto directory = TestFiles::TemporaryDirectory("escape-text");
    std::filesystem::create_directory(directory);
    const std::array<std::array<TestFiles::EncodedFixture, 3>, 4> fixtures{{
        {{{"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中\"", "\xef\xbb\xbf中\\\""},
          {"gbk", FileEncoding::Gbk, "\xd6\xd0\"", "\xd6\xd0\\\""},
          {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x22\0", 6),
           std::string("\xff\xfe\x2d\x4e\x5c\0\x22\0", 8)}}},
        {{{"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中\\t", "\xef\xbb\xbf中\t"},
          {"gbk", FileEncoding::Gbk, "\xd6\xd0\\t", "\xd6\xd0\t"},
          {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x5c\0\x74\0", 8),
           std::string("\xff\xfe\x2d\x4e\x09\0", 6)}}},
        {{{"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中<", "\xef\xbb\xbf中&lt;"},
          {"gbk", FileEncoding::Gbk, "\xd6\xd0<", "\xd6\xd0&lt;"},
          {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x3c\0", 6),
           std::string("\xff\xfe\x2d\x4e\x26\0\x6c\0\x74\0\x3b\0", 12)}}},
        {{{"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中&lt;", "\xef\xbb\xbf中<"},
          {"gbk", FileEncoding::Gbk, "\xd6\xd0&lt;", "\xd6\xd0<"},
          {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x26\0\x6c\0\x74\0\x3b\0", 12),
           std::string("\xff\xfe\x2d\x4e\x3c\0", 6)}}},
    }};
    auto* editor = frame->Editor();
    for (int command = 0; command < 4; ++command) {
        for (const auto& fixture : fixtures[command]) {
            const auto path = directory / fixture.name;
            TestFiles::WriteBytes(path, fixture.before);
            editor->Send(Message::SetSavePoint);
            Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                    "open independently encoded escape fixture through application file command");
            const auto bom = frame->DocumentHasBom();
            const auto eol = editor->Send(Message::GetEOLMode);
            editor->Send(Message::SelectAll);
            NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", Label(40204 + command)));
            TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                            {"escape command saves independently reopened literal bytes",
                                             "Undo saves independently reopened original bytes",
                                             "Redo saves independently reopened converted bytes"});
            Require(frame->DocumentEncoding() == fixture.encoding && frame->DocumentHasBom() == bom &&
                        editor->Send(Message::GetEOLMode) == eol && !editor->Send(Message::GetModify),
                    "escape conversion retains document encoding, BOM, EOL mode and saved state");
        }
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: all four escape commands save literal UTF-8, GBK and UTF-16 bytes through Undo/Redo");
}
class EscapeTextApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                EscapeC(frame);
                ReferenceFixtures(frame);
                SelectionStates(frame);
                ShortcutsAndLanguages(frame);
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
wxIMPLEMENT_APP(EscapeTextApp);
