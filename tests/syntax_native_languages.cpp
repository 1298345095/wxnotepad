#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "syntax_language_test_helpers.h"
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
using TestLanguages::Choose;
using TestLanguages::SchemeFixture;
constexpr SchemeFixture schemes[] = {
    {"Rust", SyntaxLanguage::Rust, SCLEX_RUST, "rs", "fn main() {\n    let value = 42;\n}\n", "//", "/*", "*/"},
    {"Go", SyntaxLanguage::Go, SCLEX_GO, "go", "package main\nfunc main() {\n    println(42)\n}\n", "//", "/*", "*/"},
    {"Lua", SyntaxLanguage::Lua, SCLEX_LUA, "lua", "function main()\n    print(42)\nend\n", "--", "--[[", "]]"},
};

void SchemeWorkflow(EditorFrame* frame, SettingsStore& settings, const SchemeFixture& fixture, intptr_t fontSize) {
    auto* editor = frame->Editor();
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
    TestLanguages::ResumeHighlighting(frame, fixture, path, fontSize);
    Choose(frame, "Automatic");
    TestLanguages::FoldCommands(frame);

    TestLanguages::CommentCommands(frame, fixture,
                                   "shared block-comment command must use original language delimiters");

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
    SettingsStore reloaded(settings.Path());
    const auto& saved = reloaded.values.schemeTabs[static_cast<size_t>(fixture.language)];
    Require(saved.tabWidth == 5 && saved.indentWidth == 2 && !saved.useGlobal,
            "Tab Settings must persist to the original language section");
    settings.values.schemeTabs[static_cast<size_t>(fixture.language)] = OriginalTabs{};
    Require(settings.SaveSchemeTabs(fixture.language), "fixture tab defaults must restore");

    TestLanguages::SaveNaming(frame, fixture, path);
}
void Workflows(EditorFrame* frame, SettingsStore& settings) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetSavePoint);
    constexpr auto defaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
    const auto fontSize = editor->Send(Message::StyleGetSizeFractional, defaultStyle);
    for (const auto& fixture : schemes)
        SchemeWorkflow(frame, settings, fixture, fontSize);
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

void OriginalOracle(EditorFrame* frame) {
    const auto rows = TestLanguages::OriginalStyleFoldStates(
        frame, std::filesystem::path(NATIVE_LANGUAGES_REFERENCE),
        [frame](const std::string& name, const std::string& text) {
            const auto path = TestFiles::TemporaryDirectory(name);
            TestFiles::WriteBytes(path, text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                    "original Rust/Go/Lua fixture Open must succeed");
        },
        TestSyntax::Paint, "Rust/Go/Lua styles must match independent original observations",
        "Rust/Go/Lua folds must match independent original observations");
    Require(rows == 12, "all three original fixtures and four editing states must run");
    std::puts("PASS: 12 independent original Rust/Go/Lua style/fold states including incremental edit and Undo/Redo");
}
class LanguagesApp final : public wxApp {
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
                OriginalOracle(frame);
                Workflows(frame, *settings_);
                TestLanguages::NativeFiles(frame, schemes);
                std::puts("PASS: native Open and Save As for Rust, Go and Lua");
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
