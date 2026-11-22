#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include "syntax_language_test_helpers.h"
#include "native_completion_helpers.h"
#include "native_paint_helpers.h"
#include "tab_settings_helpers.h"
#include <wx/wx.h>
#include <array>
#include <cstdio>
#include <memory>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
using Scintilla::Message;
using TestLanguages::Require;
constexpr auto extensions = std::to_array<const char*>({"css", "scss", "less", "hss", "wxss", "sql", "mysql", "hsql"});
constexpr std::array<TestLanguages::SchemeFixture, 5> schemes = {{
    {"CSS", SyntaxLanguage::Css, SCLEX_CSS, "css", "a {\n  color: #123456;\n}\n", "", "/*", "*/"},
    {"SCSS", SyntaxLanguage::Scss, SCLEX_CSS, "scss", "$x: red;\na {\n  color: $x;\n}\n", "//", "/*", "*/"},
    {"Less", SyntaxLanguage::Less, SCLEX_CSS, "less", "@x: red;\na {\n  color: @x;\n}\n", "//", "/*", "*/"},
    {"HSS", SyntaxLanguage::Hss, SCLEX_CSS, "hss", "a {\n  color: #123456;\n}\n", "//", "/*", "*/"},
    {"SQL", SyntaxLanguage::Sql, SCLEX_SQL, "sql", "BEGIN\n SELECT COUNT(*) FROM sample;\nEND;\n", "--", "/*", "*/"},
}};
void FileRecognition(EditorFrame* frame) {
    for (const auto* extension : extensions) {
        const auto path = TestFiles::TemporaryDirectory(std::string("case.") + extension);
        TestFiles::WriteBytes(path, "/* file recognition */\n");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "original extension must Open");
        const int expected = std::string_view(extension).ends_with("sql") ? SCLEX_SQL : SCLEX_CSS;
        Require(frame->Editor()->Send(Message::GetLexer) == expected,
                "standalone original CSS dialects and SQL must install their dedicated lexer");
    }
}
void OriginalOracle(EditorFrame* frame) {
    const auto rows = TestLanguages::OriginalStyleFoldStates(
        frame, std::filesystem::path(CSS_SQL_REFERENCE),
        [frame](const std::string& name, const std::string& text) {
            const auto path = TestFiles::TemporaryDirectory(name);
            TestFiles::WriteBytes(path, text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                    "original CSS dialect/SQL fixture must Open");
        },
        [](EditorFrame* current) { NativePaint::Complete(current->Editor()); },
        "CSS dialect/SQL styles must match independent pinned original",
        "CSS dialect/SQL folds must match independent pinned original");
    Require(rows == 20, "all five original languages and four incremental editing states must run");
    std::puts("PASS: 20 independent original CSS/SCSS/Less/HSS/SQL incremental style/fold states");
}
void DialectWorkflows(EditorFrame* frame, SettingsStore& settings) {
    auto* editor = frame->Editor();
    for (const auto& fixture : schemes) {
        editor->Send(Message::SetSavePoint);
        const auto path = TestFiles::TemporaryDirectory(std::string("workflow.") + fixture.extension);
        TestFiles::WriteBytes(path, fixture.text);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "language workflow must Open");
        TestSyntax::Paint(frame);
        const bool css = fixture.lexer == SCLEX_CSS;
        Require(editor->Send(Message::GetTabWidth) == (css ? 2 : 7) &&
                    editor->Send(Message::GetIndent) == (css ? 2 : 3) && !editor->Send(Message::GetUseTabs),
                "CSS retains independent original 2/2 spaces; SQL inherits global indentation");
        TestLanguages::Choose(frame, "Plain Text");
        TestLanguages::Choose(frame, fixture.name);
        Require(TestEditor::Text(editor) == fixture.text && !frame->DocumentIsModified() &&
                    !editor->Send(Message::CanUndo),
                "manual scheme installation preserves bytes and history");
        TestLanguages::Choose(frame, "Automatic");
        TestLanguages::FoldCommands(frame);
        TestEditor::ResetStream(editor, "call()\n");
        if (*fixture.lineComment) {
            TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment"));
            Require(TestEditor::Text(editor) == std::string(fixture.lineComment) + " call()\n",
                    "dialect line comments use original delimiter");
            TestSyntax::Command(frame, wxID_UNDO);
        }
        editor->Send(Message::SetSel, 0, 6);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment"));
        const auto commented = TestEditor::Text(editor);
        Require(commented.starts_with("/*") && commented.contains("*/"),
                "all five schemes share original block comments");
        TestSyntax::Command(frame, wxID_UNDO);
        Require(TestEditor::Text(editor) == "call()\n", "comments undo to exact original bytes");
        TestSyntax::Command(frame, wxID_REDO);
        Require(TestEditor::Text(editor) == commented, "comments redo to exact original bytes");
        editor->Send(Message::SetSavePoint);
        TestLanguages::SaveNaming(frame, fixture, path);
    }
    editor->Send(Message::SetSavePoint);
    TestEditor::ResetStream(editor, "a { color: $tone; }\n");
    for (const auto* name : {"SCSS", "Less", "HSS", "CSS", "SCSS", "CSS"}) {
        TestLanguages::Choose(frame, name);
        TestSyntax::Paint(frame);
        const auto position = TestEditor::Text(editor).find("$tone");
        Require(editor->Send(Message::GetStyleIndexAt, position) ==
                    (std::string_view(name) == "CSS" ? SCE_CSS_OPERATOR : SCE_CSS_VARIABLE),
                "switching CSS dialects in one document replaces the prior lexer.lang property");
    }
    TestLanguages::Choose(frame, "CSS");
    TabDialog::Show(frame, [](wxDialog* dialog) {
        Require(!TabDialog::Option(dialog, "Use Global Tab Settings")->GetValue(),
                "original CSS settings have independent indentation");
        TabDialog::Width(dialog, "Scheme Tab Width")->SetValue(5);
        TabDialog::Width(dialog, "Scheme Indentation Width")->SetValue(3);
    });
    TestLanguages::Choose(frame, "Less");
    Require(editor->Send(Message::GetTabWidth) == 5 && editor->Send(Message::GetIndent) == 3,
            "CSS dialects share the original settings identity");
    SettingsStore restored(settings.Path());
    Require(restored.values.schemeTabs[static_cast<size_t>(std::to_underlying(SyntaxLanguage::Css))].tabWidth == 5,
            "CSS settings survive configuration reload");
    editor->Send(Message::SetSavePoint);
    std::puts("PASS: original dialect switching, comments/history, indentation identity, naming and exact disk bytes");
}
std::string CurrentCandidate(EditorControl* editor) {
    const auto length = editor->Send(Message::AutoCGetCurrentText);
    std::string text(static_cast<size_t>(length) + 1, '\0');
    editor->Send(Message::AutoCGetCurrentText, 0, reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
void OriginalCompletions(EditorFrame* frame) {
    auto* editor = frame->Editor();
    std::ifstream input(std::filesystem::path(CSS_SQL_COMPLETION_REFERENCE));
    Require(input.is_open(), "independent original completion fixtures must open");
    std::string line;
    size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream row(line);
        std::string name, beforeHex, afterHex, confirmedHex, words;
        std::getline(row, name, '\t');
        std::getline(row, beforeHex, '\t');
        std::getline(row, afterHex, '\t');
        std::getline(row, confirmedHex, '\t');
        std::getline(row, words);
        editor->Send(Message::SetSavePoint);
        const auto path = TestFiles::TemporaryDirectory(name);
        TestFiles::WriteBytes(path, TestLanguages::Unhex(beforeHex));
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "completion fixture must Open");
        editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
        TestSyntax::Paint(frame);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Complete Word"));
        std::vector<std::string> actual;
        intptr_t previous = -1;
        while (editor->Send(Message::AutoCActive)) {
            const auto current = editor->Send(Message::AutoCGetCurrent);
            if (current == previous) break;
            previous = current;
            actual.push_back(CurrentCandidate(editor));
#ifdef __WXMSW__
            NativeCompletion::Key(editor, VK_DOWN);
#else
            NativeCompletion::Key(editor, GDK_KEY_Down);
#endif
        }
        std::vector<std::string> expected;
        std::istringstream candidates(words);
        for (std::string word; candidates >> word;)
            expected.push_back(word);
        Require(actual == expected && TestEditor::Text(editor) == TestLanguages::Unhex(afterHex),
                "manual completion candidate list and immediate result match independent original");
        if (!expected.empty()) {
            editor->Send(Message::AutoCSelect, 0, reinterpret_cast<intptr_t>(expected.front().c_str()));
#ifdef __WXMSW__
            NativeCompletion::Key(editor, VK_RETURN);
#else
            NativeCompletion::Key(editor, GDK_KEY_Return);
#endif
        }
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(confirmedHex),
                "native Enter inserts the independently captured original candidate");
        TestSyntax::Command(frame, wxID_UNDO);
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(beforeHex), "completion is one undoable edit");
        TestSyntax::Command(frame, wxID_REDO);
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(confirmedHex), "completion redo restores exact bytes");
        editor->Send(Message::SetSavePoint);
        ++rows;
    }
    Require(rows == 12, "all independent CSS dialect/SQL completion cases must execute");
    std::puts("PASS: original native completion lists, Enter insertion and Undo/Redo");
}
class CssSqlApp final : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory("Notepad4.ini")));
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        settings_->values.warnLineEndings = false;
        settings_->values.detectIndentation = false;
        settings_->values.globalTabs = {7, 3, true};
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                FileRecognition(frame);
                OriginalOracle(frame);
                DialectWorkflows(frame, *settings_);
                OriginalCompletions(frame);
                TestLanguages::NativeFiles(frame, schemes);
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
wxIMPLEMENT_APP(CssSqlApp);
