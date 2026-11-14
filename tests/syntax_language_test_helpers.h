#pragma once

#include "syntax_test_helpers.h"
#include "file_helpers.h"
#include "file_command_driver.h"
#include <SciLexer.h>
#include <wx/statusbr.h>
#include <fstream>
#include <span>
#include <sstream>

namespace TestLanguages {
using Scintilla::Message;
inline void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}
inline void Choose(EditorFrame* frame, const char* name) {
    const int id = frame->GetMenuBar()->FindMenuItem("Scheme", name);
    Require(id != wxNOT_FOUND, "native Scheme menu must offer the delivered language");
    TestSyntax::Command(frame, id);
    Require(frame->GetMenuBar()->IsChecked(id), "selected Scheme menu item must be checked");
}
struct SchemeFixture {
    const char* name;
    SyntaxLanguage language;
    int lexer;
    const char* extension;
    const char* text;
    const char* lineComment;
    const char* blockOpen;
    const char* blockClose;
    const char* settingsSection = nullptr;
};

inline std::string ResumeHighlighting(EditorFrame* frame, const SchemeFixture& fixture,
                                      const std::filesystem::path& path, intptr_t fontSize) {
    auto* editor = frame->Editor();
    constexpr auto defaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
    Require(editor->Send(Message::StyleGetSizeFractional, defaultStyle) == fontSize,
            "language installation must retain native font point size");
    const auto original = TestEditor::Text(editor);
    Choose(frame, "Plain Text");
    Require(editor->Send(Message::GetLexer) == SCLEX_NULL && !editor->Send(Message::CanUndo),
            "Plain Text suspends highlighting without changing history");
    Choose(frame, fixture.name);
    Require(editor->Send(Message::GetLexer) == fixture.lexer && TestEditor::Text(editor) == original &&
                !frame->DocumentIsModified(),
            "manual scheme restores highlighting without changing text or save point");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                frame->GetStatusBar()->GetStatusText().Contains("(override)"),
            "same-path reopen retains the explicit scheme");
    return original;
}

inline void FoldCommands(EditorFrame* frame) {
    auto* editor = frame->Editor();
    intptr_t header = -1;
    for (intptr_t line = 0; line < editor->Send(Message::GetLineCount); ++line) {
        if (static_cast<unsigned>(editor->Send(Message::GetFoldLevel, static_cast<uintptr_t>(line))) &
            static_cast<unsigned>(Scintilla::FoldLevel::HeaderFlag)) {
            header = line;
            break;
        }
    }
    Require(header >= 0, "fixture must expose an original fold header");
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
    Require(!editor->Send(Message::GetLineVisible, static_cast<uintptr_t>(header + 1)),
            "Collapse All hides each language's original fold body");
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Expand All"));
    Require(editor->Send(Message::GetLineVisible, static_cast<uintptr_t>(header + 1)),
            "Expand All restores each language's fold body");
}

inline void CommentCommands(EditorFrame* frame, const SchemeFixture& fixture, const char* blockMessage) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "call()\n");
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment"));
    Require(TestEditor::Text(editor) == std::string(fixture.lineComment) + " call()\n",
            "shared comment command must use original language delimiter");
    TestSyntax::Command(frame, wxID_UNDO);
    editor->Send(Message::SetSel, 0, 6);
    if (fixture.blockOpen) {
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment"));
        const auto commented = TestEditor::Text(editor);
        Require(commented.starts_with(fixture.blockOpen) && commented.find(fixture.blockClose) != std::string::npos,
                blockMessage);
        TestSyntax::Command(frame, wxID_UNDO);
    }
    Require(TestEditor::Text(editor) == "call()\n", "language comment edits must remain undoable");
    editor->Send(Message::SetSavePoint);
}

inline void SaveNaming(EditorFrame* frame, const SchemeFixture& fixture, const std::filesystem::path& path) {
    auto* editor = frame->Editor();
    // A BOM and mixed CRLF/LF must survive ordinary Save and every naming path.
    const std::string bytes =
        std::string("\xEF\xBB\xBF") + fixture.lineComment + " UTF-8 \xE7\x95\x8C\r\n" + fixture.text;
    TestFiles::WriteBytes(path, bytes);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "BOM fixture must Open");
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes,
            "ordinary Save preserves original BOM, encoding, mixed EOL and text");
    const auto renamed = TestFiles::TemporaryDirectory(std::string("renamed.") + fixture.extension);
    Choose(frame, "Plain Text");
    Require(frame->SaveAs(TestFiles::Filename(renamed), {}, SaveType::AllFiles).status == SaveStatus::Saved &&
                editor->Send(Message::GetLexer) == fixture.lexer && TestFiles::Bytes(renamed) == bytes,
            "All Files Save As recognizes the language and retains exact disk bytes");
    const auto explicitPath = TestFiles::TemporaryDirectory(std::string("explicit-") + fixture.extension + ".txt");
    Require(frame->SaveAs(TestFiles::Filename(explicitPath), {}, fixture.language).status == SaveStatus::Saved &&
                editor->Send(Message::GetLexer) == fixture.lexer && TestFiles::Bytes(explicitPath) == bytes,
            "specific Save As type overrides the extension and preserves exact bytes");
    Require(frame->NewDocument(), "clean language document must allow New");
    Choose(frame, fixture.name);
    TestEditor::Append(frame, fixture.text);
    const auto first = TestFiles::TemporaryDirectory(std::string("first.") + fixture.extension);
    Require(frame->SaveFirst(TestFiles::Filename(first)).status == SaveStatus::Saved &&
                editor->Send(Message::GetLexer) == fixture.lexer && TestFiles::Bytes(first) == fixture.text,
            "first Save retains manual language and typed bytes");
}

inline void NativeFiles(EditorFrame* frame, std::span<const SchemeFixture> schemes) {
    for (const auto& fixture : schemes) {
        const auto path = TestFiles::TemporaryDirectory(std::string("native.") + fixture.extension);
        TestFiles::WriteBytes(path, fixture.text);
        NativeDialogs::FileCommandDriver open(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL,
                                              TestFiles::Filename(path));
        TestSyntax::Command(frame, wxID_OPEN);
        open.Stop();
        Require(open.choosers == 1 && frame->Editor()->Send(Message::GetLexer) == fixture.lexer,
                "native Open dialog must recognize the selected original language");
        const auto destination = TestFiles::TemporaryDirectory(std::string("native-saved.") + fixture.extension);
        NativeDialogs::FileCommandDriver save(NativeDialogs::FileDialogProfile::Save, wxID_CANCEL,
                                              TestFiles::Filename(destination));
        TestSyntax::Command(frame, wxID_SAVEAS);
        save.Stop();
        Require(save.choosers == 1 && save.initialType.StartsWith(wxString(fixture.name) + " (") &&
                    TestFiles::Bytes(destination) == fixture.text,
                "native Save As offers current language filter and retains saved bytes");
    }
}
inline std::string Unhex(const std::string& hex) {
    std::string bytes;
    for (size_t index = 0; index < hex.size(); index += 2)
        bytes += static_cast<char>(std::stoi(hex.substr(index, 2), nullptr, 16));
    return bytes;
}
template <class OpenFixture, class PaintDocument>
size_t OriginalStyleFoldStates(EditorFrame* frame, const std::filesystem::path& reference, OpenFixture openFixture,
                               PaintDocument paintDocument, const char* styleMessage, const char* foldMessage) {
    std::ifstream input(reference);
    Require(input.is_open(), "independently captured original style/fold fixture must open");
    std::string line;
    size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream row(line);
        std::string name, state, deleted, textHex, styleHex, foldText;
        std::getline(row, name, '\t');
        std::getline(row, state, '\t');
        std::getline(row, deleted, '\t');
        std::getline(row, textHex, '\t');
        std::getline(row, styleHex, '\t');
        std::getline(row, foldText);
        auto* editor = frame->Editor();
        const auto text = Unhex(textHex);
        if (state == "base") {
            editor->Send(Message::SetSavePoint);
            openFixture(name, text);
        } else if (state == "edit") {
            editor->Send(Message::GotoPos, std::stoul(deleted));
            wxKeyEvent event(wxEVT_KEY_DOWN);
            event.m_keyCode = WXK_DELETE;
            event.SetEventObject(editor);
            editor->GetEventHandler()->ProcessEvent(event);
        } else {
            TestSyntax::Command(frame, state == "undo" ? wxID_UNDO : wxID_REDO);
        }
        paintDocument(frame);
        Require(TestEditor::Text(editor) == text, "incremental edit/Undo/Redo bytes must match the original");
        const auto styles = Unhex(styleHex);
        Require(styles.size() == text.size(), "original styles must cover every UTF-8 byte");
        for (size_t position = 0; position < styles.size(); ++position) {
            if (editor->Send(Message::GetStyleIndexAt, position) != static_cast<unsigned char>(styles[position])) {
                std::fprintf(stderr, "fixture=%s state=%s position=%zu expected=%u actual=%zd\n", name.c_str(),
                             state.c_str(), position, static_cast<unsigned char>(styles[position]),
                             editor->Send(Message::GetStyleIndexAt, position));
                throw std::runtime_error(styleMessage);
            }
        }
        std::istringstream folds(foldText);
        size_t foldLine = 0;
        while (std::getline(folds, line, ',')) {
            if (editor->Send(Message::GetFoldLevel, foldLine) != std::stoi(line)) {
                std::fprintf(stderr, "fixture=%s state=%s line=%zu expected=%d actual=%zd\n", name.c_str(),
                             state.c_str(), foldLine, std::stoi(line), editor->Send(Message::GetFoldLevel, foldLine));
                throw std::runtime_error(foldMessage);
            }
            ++foldLine;
        }
        ++rows;
    }
    return rows;
}
} // namespace TestLanguages
