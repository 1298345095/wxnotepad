#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "encoded_file_helpers.h"
#include <fstream>
#include <sstream>
#include <string_view>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    Require(id != wxNOT_FOUND, "native Convert menu exposes original URL command");
    NativeSearch::Command(frame, id);
}
void Encoding(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "A中文 😀B", 1, 12);
    Menu(frame, "URL Encode");
    Require(TestEditor::Text(editor) == "A%E4%B8%AD%E6%96%87%20%F0%9F%98%80B",
            "URL Encode uses original UTF-8 escapes");
}
std::string Unhex(std::string_view text) {
    Require(text.size() % 2 == 0, "captured URL bytes contain complete hex pairs");
    std::string result;
    for (size_t index = 0; index < text.size(); index += 2) {
        const auto digit = [](char ch) {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            return -1;
        };
        const int high = digit(text[index]), low = digit(text[index + 1]);
        Require(high >= 0 && low >= 0, "captured URL bytes contain literal hex digits");
        result += static_cast<char>(high * 16 + low);
    }
    return result;
}
void Fixtures(EditorFrame* frame) {
    struct Fixture {
        std::string name, input, expected;
        unsigned commands;
        int anchor, caret, undoAnchor, undoCaret, redoAnchor, redoCaret;
        bool modified, canUndo, canRedo, undoModified, undoCanUndo, undoCanRedo, redoModified, redoCanUndo, redoCanRedo;
    };
    std::ifstream capture{std::filesystem::path(URL_REFERENCE_FIXTURES)};
    Require(capture.is_open(), "open independently captured URL reference fixtures");
    std::string line, countLabel;
    std::getline(capture, line); // Original revision/platform provenance.
    std::getline(capture, line); // Explicit command-bit meanings.
    std::getline(capture, line);
    unsigned capturedCaseCount = 0;
    std::istringstream count(line);
    Require(bool(count >> countLabel >> capturedCaseCount) && countLabel == "command_cases" && capturedCaseCount == 360,
            "read original URL command-case count");
    std::getline(capture, line); // Named tabular columns.
    auto* editor = frame->Editor();
    const auto requireState = [editor](bool modified, bool undo, bool redo) {
        Require(bool(editor->Send(Message::GetModify)) == modified && bool(editor->Send(Message::CanUndo)) == undo &&
                    bool(editor->Send(Message::CanRedo)) == redo,
                "URL conversion preserves captured original modification and Undo/Redo state");
    };
    constexpr const char* labels[] = {"URL Encode", "URL Component Encode", "URL Decode"};
    unsigned commandCases = 0;
    while (std::getline(capture, line)) {
        Fixture fixture;
        std::istringstream row(line);
        Require(bool(row >> fixture.name >> fixture.input >> fixture.expected >> fixture.commands >> fixture.anchor >>
                     fixture.caret >> fixture.undoAnchor >> fixture.undoCaret >> fixture.redoAnchor >>
                     fixture.redoCaret >> fixture.modified >> fixture.canUndo >> fixture.canRedo >>
                     fixture.undoModified >> fixture.undoCanUndo >> fixture.undoCanRedo >> fixture.redoModified >>
                     fixture.redoCanUndo >> fixture.redoCanRedo),
                "read literal URL fixture bytes and state");
        row >> std::ws;
        Require(row.eof() && fixture.commands > 0 && fixture.commands < 8,
                "URL fixture has exact fields and command bits");
        const auto input = Unhex(fixture.input);
        const auto expected = Unhex(fixture.expected);
        for (unsigned command = 0; command < 3; ++command) {
            if (!(fixture.commands & (1U << command))) continue;
            ++commandCases;
            TestEditor::ResetStream(editor, "", 0, 0, Scintilla::EndOfLine::CrLf);
            editor->Send(Message::AddText, input.size(), reinterpret_cast<intptr_t>(input.data()));
            editor->Send(Message::SetSel, 0, static_cast<intptr_t>(input.size()));
            editor->Send(Message::EmptyUndoBuffer);
            editor->Send(Message::SetSavePoint);
            Menu(frame, labels[command]);
            if (TestEditor::Text(editor) != expected) {
                std::fprintf(stderr, "URL fixture %s command %s: expected %zu bytes, got %zu\n", fixture.name.c_str(),
                             labels[command], expected.size(), TestEditor::Text(editor).size());
                throw std::runtime_error("native URL command matches captured original bytes");
            }
            TestEditor::RequireSelection(editor, fixture.anchor, fixture.caret, "captured original URL endpoints");
            requireState(fixture.modified, fixture.canUndo, fixture.canRedo);
            Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::CrLf),
                    "URL conversion preserves document EOL metadata");
            editor->Send(Message::Undo);
            Require(TestEditor::Text(editor) == input, "one Undo restores every original byte");
            requireState(fixture.undoModified, fixture.undoCanUndo, fixture.undoCanRedo);
            TestEditor::RequireSelection(editor, fixture.undoAnchor, fixture.undoCaret,
                                         "captured original Undo endpoints");
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == expected, "one Redo restores captured original URL bytes");
            requireState(fixture.redoModified, fixture.redoCanUndo, fixture.redoCanRedo);
            TestEditor::RequireSelection(editor, fixture.redoAnchor, fixture.redoCaret,
                                         "captured original Redo endpoints");
        }
    }
    Require(capture.eof() && commandCases == capturedCaseCount, "all independently captured URL command cases ran");
    std::printf("PASS: %u independently captured URL command cases\n", commandCases);
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
void State(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const char* label : {"URL Encode", "URL Component Encode", "URL Decode"}) {
        const bool decode = std::string_view(label) == "URL Decode";
        TestEditor::ResetStream(editor, "Xa bY", 4, 1);
        Menu(frame, label);
        Require(TestEditor::Text(editor) == (decode ? "Xa bY" : "Xa%20bY"),
                "reversed URL selection preserves unaffected bytes");
        TestEditor::RequireSelection(editor, decode ? 4 : 6, 1, "main selection preserves reversed direction");
        TestEditor::ResetStream(editor, "Xa bY", 3, 3);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(!frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", label)),
                "URL menu requires nonempty selection");
        Menu(frame, label);
        Require(TestEditor::Text(editor) == "Xa bY" && !editor->Send(Message::CanUndo),
                "empty URL invocation leaves document and history unchanged");
        TestEditor::RequireSelection(editor, 3, 3, "empty selection retains its endpoints");
        for (const char* text : {" \t", "abc"}) {
            TestEditor::ResetStream(editor, text, 0, 0);
            editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
            editor->Send(Message::Undo);
            editor->Send(Message::SetSel, 0, 2 + (text[0] == 'a'));
            Menu(frame, label);
            const bool unchanged = !decode && text[0] == ' ';
            Require(TestEditor::Text(editor) == text && bool(editor->Send(Message::CanRedo)) == unchanged &&
                        bool(editor->Send(Message::CanUndo)) != unchanged &&
                        bool(editor->Send(Message::GetModify)) != unchanged,
                    "original no-change predicate controls pending Redo, Undo and save point");
        }
        TestEditor::ResetStream(editor, "Xa bY\nZa bW", 1, 4, Scintilla::EndOfLine::CrLf);
        editor->Send(Message::SetMultipleSelection, 1);
        editor->Send(Message::AddSelection, 10, 7);
        Menu(frame, label);
        Require(TestEditor::Text(editor) == (decode ? "Xa bY\nZa b\r\na bW" : "Xa bY\nZa%20b%0D%0Aa%20bW") &&
                    editor->Send(Message::GetSelections) == 1,
                "all selected ranges feed original main-only replacement with document-EOL separators");
        TestEditor::RequireSelection(editor, 7, decode ? 15 : 23, "multiple-selection original result endpoints");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "Xa bY\nZa bW" && !editor->Send(Message::CanUndo),
                "one Undo restores main-only multiple-selection edit");
        editor->Send(Message::SetMultipleSelection, 0);
        for (bool empty : {false, true}) {
            TestEditor::ResetStream(editor, "a b\nc d");
            editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
            editor->Send(Message::SetRectangularSelectionAnchor, 1);
            editor->Send(Message::SetRectangularSelectionCaret, empty ? 1 : 6);
            RectangleFeedback feedback;
            Menu(frame, label);
            Require(feedback.count == (empty ? 0 : 1) && TestEditor::Text(editor) == "a b\nc d" &&
                        !editor->Send(Message::CanUndo) && editor->Send(Message::SelectionIsRectangle),
                    "URL rejects nonempty rectangles with notification after its empty-input gate");
            Require(editor->Send(Message::GetRectangularSelectionAnchor) == 1 &&
                        editor->Send(Message::GetRectangularSelectionCaret) == (empty ? 1 : 6),
                    "rectangle notification preserves endpoints");
        }
        TestEditor::ResetStream(editor, "Xa bY", 4, 1);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, 4, 1);
        editor->Send(Message::SetReadOnly, 1);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", label)),
                "selected read-only text keeps original URL menu availability");
        Menu(frame, label);
        Require(TestEditor::Text(editor) == "Xa bY" && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "read-only URL invocation retains bytes and save point");
        TestEditor::RequireSelection(editor, decode ? 4 : 5, 1, "original read-only result selection clamps to buffer");
        editor->Send(Message::SetReadOnly, 0);
        Require(editor->Send(Message::CanRedo), "read-only invocation preserves pending Redo");
    }
}
void Shortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "a b", 0, 3);
    NativeKeys::Shortcut(frame, 'E', true, false, true);
    Require(TestEditor::Text(editor) == "a%20b", "native Ctrl+Shift+E performs URL Encode");
    NativeKeys::Shortcut(frame, 'R', true, false, true);
    Require(TestEditor::Text(editor) == "a b", "native Ctrl+Shift+R performs URL Decode");
    for (const auto key : {'E', 'R'}) {
        TestEditor::ResetStream(editor, "a%20b", 0, 5);
        NativeKeys::Shortcut(frame, static_cast<unsigned char>(key), true, true, true);
        Require(TestEditor::Text(editor) == "a%20b" && !editor->Send(Message::CanUndo),
                "URL shortcuts require exact modifiers");
    }
    TestEditor::ResetStream(editor, "AbC", 0, 3);
    NativeKeys::Shortcut(frame, 'U', true);
    Require(TestEditor::Text(editor) == "abc", "existing Ctrl+U remains available alongside URL shortcuts");
}
void SavedBytes(EditorFrame* frame) {
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-url-conversion");
    std::filesystem::create_directory(directory);
    const TestFiles::EncodedFixture fixtures[] = {
        {"utf8", FileEncoding::Utf8,
         "\xef\xbb\xbf"
         "A B\r\n",
         "\xef\xbb\xbf"
         "A%20B"},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0", "%E4%B8%AD"},
        {"utf16", FileEncoding::Utf16Le,
         std::string("\xff\xfe"
                     "A\0 \0B\0",
                     8),
         std::string("\xff\xfe"
                     "A\0%\0"
                     "2\0"
                     "0\0B\0",
                     12)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        frame->Editor()->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "open encoded URL fixture through application");
        frame->Editor()->Send(Message::SelectAll);
        Menu(frame, "URL Encode");
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"URL Save preserves independent encoding/BOM bytes",
                                         "URL Undo restores independent encoded input",
                                         "URL Redo restores independent encoded output"});
    }
    std::filesystem::remove_all(directory);
}
class UrlApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame(&settings_);
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                Encoding(frame);
                Fixtures(frame);
                State(frame);
                Shortcuts(frame);
                SavedBytes(frame);
                std::puts("PASS: original URL conversion");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Editor()->Send(Message::SetSavePoint);
            frame->Destroy();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    SettingsStore settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(UrlApp);
