#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "syntax_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "file_command_driver.h"
#include "tab_settings_helpers.h"
#include <SciLexer.h>
#include <wx/wx.h>
#include <wx/statusbr.h>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <memory>
#include <string_view>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}
void Choose(EditorFrame* frame, const char* name) {
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
};
constexpr SchemeFixture schemes[] = {
    {"Rust", SyntaxLanguage::Rust, SCLEX_RUST, "rs", "fn main() {\n    let value = 42;\n}\n", "//", "/*", "*/"},
    {"Go", SyntaxLanguage::Go, SCLEX_GO, "go", "package main\nfunc main() {\n    println(42)\n}\n", "//", "/*", "*/"},
    {"Lua", SyntaxLanguage::Lua, SCLEX_LUA, "lua", "function main()\n    print(42)\nend\n", "--", "--[[", "]]"},
};

void Workflows(EditorFrame* frame, SettingsStore& settings) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetSavePoint);
    constexpr auto defaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
    const auto fontSize = editor->Send(Message::StyleGetSizeFractional, defaultStyle);
    for (const auto& fixture : schemes) {
        const auto path = TestFiles::TemporaryDirectory(std::string("workflow.") + fixture.extension);
        TestFiles::WriteBytes(path, fixture.text);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "language workflow must Open");
        TestSyntax::Paint(frame);
        Require(editor->Send(Message::GetLexer) == fixture.lexer &&
                    frame->GetStatusBar()->GetStatusText().Contains(fixture.name),
                "Open must install original dedicated lexer and report its language");
        Require(editor->Send(Message::GetTabWidth) == 7 && editor->Send(Message::GetIndent) == 3 &&
                    !editor->Send(Message::GetUseTabs),
                "all three original schemes inherit the selected global indentation defaults");
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
        Choose(frame, "Automatic");
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

        TestEditor::ResetStream(editor, "call()\n");
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment"));
        Require(TestEditor::Text(editor) == std::string(fixture.lineComment) + " call()\n",
                "shared comment command must use original language delimiter");
        TestSyntax::Command(frame, wxID_UNDO);
        editor->Send(Message::SetSel, 0, 6);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment"));
        const auto commented = TestEditor::Text(editor);
        Require(commented.starts_with(fixture.blockOpen) && commented.find(fixture.blockClose) != std::string::npos,
                "shared block-comment command must use original language delimiters");
        TestSyntax::Command(frame, wxID_UNDO);
        Require(TestEditor::Text(editor) == "call()\n", "language comment edits must remain undoable");
        editor->Send(Message::SetSavePoint);

        TabDialog::Show(frame, [](wxDialog* dialog) {
            Require(TabDialog::Option(dialog, "Use Global Tab Settings")->GetValue(),
                    "new scheme must inherit global tab settings");
            TabDialog::Toggle(TabDialog::Option(dialog, "Use Global Tab Settings"), false);
            Require(TabDialog::Width(dialog, "Scheme Tab Width")->GetValue() == 4 &&
                        TabDialog::Width(dialog, "Scheme Indentation Width")->GetValue() == 4,
                    "all three schemes retain original independent 4/4 defaults");
            TabDialog::Width(dialog, "Scheme Tab Width")->SetValue(5);
            TabDialog::Width(dialog, "Scheme Indentation Width")->SetValue(2);
        });
        Require(editor->Send(Message::GetTabWidth) == 5 && editor->Send(Message::GetIndent) == 2,
                "native Tab Settings must apply the language's independent settings");
        SettingsStore reloaded;
        const auto& saved = reloaded.values.schemeTabs[static_cast<size_t>(fixture.language)];
        Require(saved.tabWidth == 5 && saved.indentWidth == 2 && !saved.useGlobal,
                "Tab Settings must persist to the original language section");
        settings.values.schemeTabs[static_cast<size_t>(fixture.language)] = OriginalTabs{};
        Require(settings.SaveSchemeTabs(fixture.language), "fixture tab defaults must restore");

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
    for (const auto* extension : {"RS", "GO", "gop", "ql", "wlua", "nse", "luadoc", "luax", "LuA"}) {
        const auto path = TestFiles::TemporaryDirectory(std::string("extra.") + extension);
        TestFiles::WriteBytes(path, "\n");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "original extension must Open");
        const std::string_view suffix(extension);
        const int lexer = suffix == "RS"                                        ? SCLEX_RUST
                          : suffix == "GO" || suffix == "gop" || suffix == "ql" ? SCLEX_GO
                                                                                : SCLEX_LUA;
        Require(editor->Send(Message::GetLexer) == lexer,
                "all original additional extensions select the dedicated lexer");
    }
    std::puts("PASS: language matrix covers recognition, history, fonts, highlighting, folds, comments, tabs and saved "
              "bytes");
}

void NativeFiles(EditorFrame* frame) {
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
    std::puts("PASS: native Open and Save As for Rust, Go and Lua");
}
std::string Unhex(const std::string& hex) {
    std::string bytes;
    for (size_t index = 0; index < hex.size(); index += 2)
        bytes += static_cast<char>(std::stoi(hex.substr(index, 2), nullptr, 16));
    return bytes;
}
void OriginalOracle(EditorFrame* frame) {
    std::ifstream input(std::filesystem::path(NATIVE_LANGUAGES_REFERENCE));
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
            const auto path = TestFiles::TemporaryDirectory(name);
            TestFiles::WriteBytes(path, text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                    "original Rust/Go/Lua fixture Open must succeed");
        } else if (state == "edit") {
            editor->Send(Message::GotoPos, std::stoul(deleted));
            wxKeyEvent event(wxEVT_KEY_DOWN);
            event.m_keyCode = WXK_DELETE;
            event.SetEventObject(editor);
            editor->GetEventHandler()->ProcessEvent(event);
        } else {
            TestSyntax::Command(frame, state == "undo" ? wxID_UNDO : wxID_REDO);
        }
        TestSyntax::Paint(frame);
        Require(TestEditor::Text(editor) == text, "incremental edit/Undo/Redo bytes must match the original");
        const auto styles = Unhex(styleHex);
        Require(styles.size() == text.size(), "original styles must cover every UTF-8 byte");
        for (size_t position = 0; position < styles.size(); ++position) {
            if (editor->Send(Message::GetStyleIndexAt, position) != static_cast<unsigned char>(styles[position])) {
                std::fprintf(stderr, "fixture=%s state=%s position=%zu expected=%u actual=%zd\n", name.c_str(),
                             state.c_str(), position, static_cast<unsigned char>(styles[position]),
                             editor->Send(Message::GetStyleIndexAt, position));
                throw std::runtime_error("Rust/Go/Lua styles must match independent original observations");
            }
        }
        std::istringstream folds(foldText);
        size_t foldLine = 0;
        while (std::getline(folds, line, ',')) {
            if (editor->Send(Message::GetFoldLevel, foldLine) != std::stoi(line)) {
                std::fprintf(stderr, "fixture=%s state=%s line=%zu expected=%d actual=%zd\n", name.c_str(),
                             state.c_str(), foldLine, std::stoi(line), editor->Send(Message::GetFoldLevel, foldLine));
                throw std::runtime_error("Rust/Go/Lua folds must match independent original observations");
            }
            ++foldLine;
        }
        ++rows;
    }
    Require(rows == 12, "all three original fixtures and four editing states must run");
    std::puts("PASS: 12 independent original Rust/Go/Lua style/fold states including incremental edit and Undo/Redo");
}
class LanguagesApp final : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        settings_->values.warnLineEndings = false;
        settings_->values.detectIndentation = false;
        settings_->values.globalTabs = {7, 3, true};
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                OriginalOracle(frame);
                Workflows(frame, *settings_);
                NativeFiles(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s", error.what());
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
wxIMPLEMENT_APP(LanguagesApp);
