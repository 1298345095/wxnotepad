#include "status_helpers.h"
#include <print>
#pragma once

#include "native_search_helpers.h"
#include "syntax_test_helpers.h"
#include "native_search_helpers.h"
#include "file_helpers.h"
#include "file_command_driver.h"
#include "native_completion_helpers.h"
#include "native_key_helpers.h"
#include "native_tree_input.h"
#include "settings/settings_store.h"
#include <SciLexer.h>
#include <wx/statusbr.h>
#include <wx/checkbox.h>
#include <wx/modalhook.h>
#include <array>
#include <fstream>
#include <span>
#include <sstream>
#include <string_view>
#include <utility>

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
    const char* filterName = nullptr;
};

inline std::string ResumeHighlighting(EditorFrame* frame, const SchemeFixture& fixture,
                                      const std::filesystem::path& path, intptr_t fontSize) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    constexpr auto defaultStyle = static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Default));
    Require(editor->Send(StyleGetSizeFractional, defaultStyle) == fontSize,
            "language installation must retain native font point size");
    const auto original = TestEditor::Text(editor);
    Choose(frame, "Plain Text");
    Require(editor->Send(GetLexer) == SCLEX_NULL && !editor->Send(CanUndo),
            "Plain Text suspends highlighting without changing history");
    Choose(frame, fixture.name);
    Require(editor->Send(GetLexer) == fixture.lexer && TestEditor::Text(editor) == original &&
                !frame->DocumentIsModified(),
            "manual scheme restores highlighting without changing text or save point");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                StatusMetadata(frame).Contains("(override)"),
            "same-path reopen retains the explicit scheme");
    return original;
}

inline void FoldCommands(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    intptr_t header = -1;
    for (intptr_t line = 0; line < editor->Send(GetLineCount); ++line) {
        if (static_cast<unsigned>(editor->Send(GetFoldLevel, static_cast<uintptr_t>(line))) &
            static_cast<unsigned>(std::to_underlying(Scintilla::FoldLevel::HeaderFlag))) {
            header = line;
            break;
        }
    }
    Require(header >= 0, "fixture must expose an original fold header");
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "All Levels"));
    Require(!editor->Send(GetLineVisible, static_cast<uintptr_t>(header + 1)),
            "Collapse All hides each language's original fold body");
    NativeSearch::RevealAllFolds(frame);
    Require(editor->Send(GetLineVisible, static_cast<uintptr_t>(header + 1)),
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
        Require(commented.starts_with(fixture.blockOpen) && commented.contains(fixture.blockClose), blockMessage);
        TestSyntax::Command(frame, wxID_UNDO);
    }
    Require(TestEditor::Text(editor) == "call()\n", "language comment edits must remain undoable");
    editor->Send(Message::SetSavePoint);
}

inline void SaveNaming(EditorFrame* frame, const SchemeFixture& fixture, const std::filesystem::path& path) {
    auto* editor = frame->Editor();
    // A BOM and mixed CRLF/LF must survive ordinary Save and every naming path.
    const std::string bytes =
        std::string("\x{EF}\x{BB}\x{BF}") + fixture.lineComment + " UTF-8 \x{E7}\x{95}\x{8C}\r\n" + fixture.text;
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
        const auto* filterName = fixture.filterName ? fixture.filterName : fixture.name;
        Require(save.choosers == 1 && save.initialType.StartsWith(wxString(filterName) + " (") &&
                    TestFiles::Bytes(destination) == fixture.text,
                "native Save As offers current language filter and retains saved bytes");
    }
}
inline std::string Unhex(std::string_view hex) {
    std::string bytes;
    for (size_t index = 0; index < hex.size(); index += 2)
        bytes += static_cast<char>(std::stoi(std::string(hex.substr(index, 2)), nullptr, 16));
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
        std::string name;
        std::string state;
        std::string deleted;
        std::string textHex;
        std::string styleHex;
        std::string foldText;
        std::getline(row, name, '\t');
        std::getline(row, state, '\t');
        std::getline(row, deleted, '\t');
        std::getline(row, textHex, '\t');
        std::getline(row, styleHex, '\t');
        std::getline(row, foldText, '	');
        std::string lineStates;
        std::getline(row, lineStates);
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
                std::println(stderr, "fixture={} state={} position={:d} expected={:d} actual={:d}", name.c_str(),
                             state.c_str(), position, static_cast<unsigned char>(styles[position]),
                             editor->Send(Message::GetStyleIndexAt, position));
                throw TestEditor::Failure(styleMessage);
            }
        }
        std::istringstream folds(foldText);
        size_t foldLine = 0;
        while (std::getline(folds, line, ',')) {
            if (editor->Send(Message::GetFoldLevel, foldLine) != std::stoi(line)) {
                std::println(stderr, "fixture={} state={} line={:d} expected={:d} actual={:d}", name.c_str(),
                             state.c_str(), foldLine, std::stoi(line), editor->Send(Message::GetFoldLevel, foldLine));
                throw TestEditor::Failure(foldMessage);
            }
            ++foldLine;
        }
        if (!lineStates.empty()) {
            std::istringstream states(lineStates);
            size_t stateLine = 0;
            while (std::getline(states, line, ',')) {
                Require(editor->Send(Message::GetLineState, stateLine) == std::stol(line),
                        "every incremental line state must match the pinned original");
                ++stateLine;
            }
            Require(stateLine == static_cast<size_t>(editor->Send(Message::GetLineCount)),
                    "original line states cover the whole document");
        }
        ++rows;
    }
    return rows;
}
inline void ScriptChoosers(EditorFrame* frame, SettingsStore& settings, std::span<const SchemeFixture> schemes,
                           std::span<const char* const> favorites) {
    for (const auto& fixture : schemes) {
        wxTimer timer;
        std::exception_ptr failure;
        int shown = 0;
        timer.Bind(wxEVT_TIMER, [frame, &timer, &failure, &shown, &fixture, favorites](wxTimerEvent&) {
            for (auto* window : wxTopLevelWindows) {
                auto* dialog = wxDynamicCast(window, wxDialog);
                if (!dialog || !dialog->IsModal() || dialog->GetTitle() != "Select Scheme") continue;
                timer.Stop();
                ++shown;
                try {
                    auto* tree = wxDynamicCast(wxWindow::FindWindowByName("Syntax schemes", dialog), wxTreeCtrl);
                    Require(tree, "script F12 chooser has its native tree");
                    Require(NativeDialogs::SelectTreeLabel(*tree, "Favorite Schemes"), "favorites group exists");
                    wxTreeItemIdValue cookie;
                    auto item = tree->GetFirstChild(tree->GetSelection(), cookie);
                    for (const auto* name : favorites) {
                        Require(item.IsOk() && tree->GetItemText(item) == name,
                                "script favorites retain original identity order alongside existing schemes");
                        item = tree->GetNextChild(tree->GetSelection(), cookie);
                    }
                    Require(!item.IsOk(), "script favorites contain no duplicated or aliased identities");
                    Require(NativeDialogs::SelectTreeLabel(*tree, fixture.name), "F12 offers each script scheme");
                    auto* selected = wxDynamicCast(wxWindow::FindWindowByName("Default scheme", dialog), wxCheckBox);
                    Require(selected, "script chooser offers original default selection");
                    NativeSearch::Toggle(selected, true);
                    NativeTreeInput::Present(*dialog);
                    tree->SetFocus();
                    NativeTreeInput::Enter(*dialog);
                } catch (...) {
                    failure = std::current_exception();
                    dialog->EndModal(wxID_CANCEL);
                }
                return;
            }
        });
        timer.Start(40);
        NativeKeys::Shortcut(frame, WXK_F12, false);
        timer.Stop();
        if (failure) std::rethrow_exception(failure);
        Require(shown == 1 && frame->Editor()->Send(Message::GetLexer) == fixture.lexer &&
                    settings.values.defaultScheme == SyntaxFor(fixture.language).original.schemeId,
                "native F12/Enter selects each script and commits its original default identity");
    }
    settings.values.defaultScheme = 0;
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
template <class PaintDocument>
size_t OriginalScriptOperations(EditorFrame* frame, const std::filesystem::path& reference,
                                PaintDocument paintDocument) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
#ifdef __WXGTK__
    // Completion needs acknowledged native focus, including when wx's logical
    // focus still names this editor after a scheme chooser has closed.
    wxString target = "wxnotepad";
    auto* widget = GTK_WIDGET(frame->GetHandle());
    if (GDK_IS_X11_DISPLAY(gtk_widget_get_display(widget)))
        target = wxString::Format("x11-window:%lu", gdk_x11_window_get_xid(gtk_widget_get_window(widget)));
    wxString focusFile;
    NativeKeys::FocusEditor(frame, target, focusFile);
#endif
    std::ifstream input(reference);
    Require(input.is_open(), "independent original script operation fixtures open");
    std::string line;
    size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream row(line);
        constexpr size_t InitialCaretColumn = 13;
        constexpr size_t InitialAnchorColumn = 14;
        constexpr size_t ReadOnlyColumn = 15;
        constexpr size_t SelectionModeColumn = 16;
        constexpr size_t NoticeColumn = 17;
        constexpr size_t EnabledColumn = 18;
        constexpr size_t UndoAnchorColumn = 19;
        constexpr size_t UndoCaretColumn = 20;
        constexpr size_t RedoAnchorColumn = 21;
        constexpr size_t RedoCaretColumn = 22;
        std::array<std::string, RedoCaretColumn + 1> fields;
        for (auto& field : fields)
            std::getline(row, field, '\t');
        std::println(stderr, "script operation {:?} {:?} {:?}", fields[0], fields[1], fields[2]);
        editor->Send(AutoCCancel);
        Choose(frame, fields[0].c_str());
        const auto before = TestLanguages::Unhex(fields[2]);
        const auto caret = std::stol(fields[InitialCaretColumn]);
        editor->Send(SetReadOnly, 0);
        // Each original operation has a fresh editor. Match EditNewText's reset,
        // including selection snapshots retained separately from the byte history.
        const auto history = editor->Send(GetUndoSelectionHistory);
        editor->Send(SetUndoSelectionHistory, 0);
        TestEditor::ResetStream(editor, before.c_str(), std::stol(fields[InitialAnchorColumn]), caret,
                                Scintilla::EndOfLine::CrLf);
        editor->Send(SetUndoSelectionHistory, static_cast<uintptr_t>(history));
        editor->Send(SetSelectionMode, std::stoul(fields[SelectionModeColumn]));
        if (fields[SelectionModeColumn] == "1") {
            editor->Send(SetRectangularSelectionAnchor, std::stoul(fields[InitialAnchorColumn]));
            editor->Send(SetRectangularSelectionCaret, caret);
        }
        editor->Send(SetReadOnly, std::stoul(fields[ReadOnlyColumn]));
        paintDocument(frame);
        RectangleFeedback feedback;
        const auto character = std::stoi(fields[3]);
        if (character == 13) {
            editor->Send(NewLine);
        } else if (character) {
            wxKeyEvent event(wxEVT_CHAR);
            event.m_keyCode = character;
            event.m_uniChar = character;
            event.SetEventObject(editor);
            editor->GetEventHandler()->ProcessEvent(event);
            wxYield();
        } else {
            const auto command = fields[1] == "completion" ? "Complete Word" : fields[1].c_str();
            const auto identifier = frame->GetMenuBar()->FindMenuItem("Edit", command);
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            const bool enabled = fields[EnabledColumn] == "1";
            Require(frame->GetMenuBar()->IsEnabled(identifier) == enabled,
                    "original script commands retain their supported/unsupported native menu state");
            if (enabled) TestSyntax::Command(frame, identifier);
        }
        Require(feedback.count == std::stoi(fields[NoticeColumn]), "original rectangular-selection feedback count");
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(fields[4]), "original completion/typing bytes");
        TestEditor::RequireSelection(editor, std::stol(fields[5]), std::stol(fields[6]), "original typing selection");
        std::vector<std::string> expected;
        std::istringstream words(fields[7]);
        std::string word;
        while (std::getline(words, word, ','))
            expected.push_back(TestLanguages::Unhex(word));
        const auto actual = NativeCompletion::Candidates(editor);
        if (actual != expected)
            for (const auto& candidate : actual)
                std::println(stderr, "actual candidate [{}]", candidate.c_str());
        Require(actual == expected, "original manual/automatic native completion order");
        if (!expected.empty() && fields[1] == "completion") {
            editor->Send(AutoCSelect, 0, reinterpret_cast<intptr_t>(expected.front().c_str()));
#ifdef __WXMSW__
            NativeCompletion::Key(editor, VK_RETURN);
#else
            NativeCompletion::Key(editor, GDK_KEY_Return);
#endif
        }
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(fields[8]), "original completion confirmation bytes");
        TestEditor::RequireSelection(editor, std::stol(fields[9]), std::stol(fields[10]), "original confirmed caret");
        editor->Send(AutoCCancel);
        TestSyntax::Command(frame, wxID_UNDO);
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(fields[11]), "original single-step Undo bytes");
        TestEditor::RequireSelection(editor, std::stol(fields[UndoAnchorColumn]), std::stol(fields[UndoCaretColumn]),
                                     "original Undo caret/anchor");
        TestSyntax::Command(frame, wxID_REDO);
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(fields[12]), "original Redo bytes");
        TestEditor::RequireSelection(editor, std::stol(fields[RedoAnchorColumn]), std::stol(fields[RedoCaretColumn]),
                                     "original Redo caret/anchor");
        editor->Send(SetReadOnly, 0);
        editor->Send(SetSelectionMode, 0);
        editor->Send(SetSavePoint);
        ++rows;
    }
    return rows;
}

template <class PaintDocument>
void ScriptSchemeHistory(EditorFrame* frame, const SchemeFixture& fixture, PaintDocument paintDocument) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    editor->Send(SetSel, 1, 4);
    editor->Send(ReplaceSel, 0, reinterpret_cast<intptr_t>("changed"));
    const auto changed = TestEditor::Text(editor);
    const auto caret = editor->Send(GetCurrentPos);
    Choose(frame, "C/C++");
    Choose(frame, fixture.name);
    Require(TestEditor::Text(editor) == changed && editor->Send(CanUndo), "scheme switches preserve history");
    TestEditor::RequireSelection(editor, caret, caret, "scheme switches preserve caret");
    TestSyntax::Command(frame, wxID_UNDO);
    Require(TestEditor::Text(editor) == fixture.text, "Undo survives script switch");
    TestSyntax::Command(frame, wxID_REDO);
    Require(TestEditor::Text(editor) == changed, "Redo survives script switch");
    TestSyntax::Command(frame, wxID_UNDO);
    Choose(frame, "Automatic");
    paintDocument(frame);
    TestLanguages::FoldCommands(frame);
    Require(frame->ReloadCurrentFile().status == OpenStatus::Opened && editor->Send(GetLexer) == fixture.lexer,
            "script reload restores automatic lexer");
}
} // namespace TestLanguages
