#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "syntax_language_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "file_command_driver.h"
#include "tab_settings_helpers.h"
#include "native_paint_helpers.h"
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
void PaintDocument(EditorFrame* frame) {
    // These short fixtures fit the native viewport, and the oracle checks every
    // byte. The shared prefix-only wait can finish before an edit past byte 128
    // has received its asynchronous Wayland paint/style pass.
    NativePaint::Complete(frame->Editor());
    Require(frame->Editor()->Send(Message::GetEndStyled) >= frame->Editor()->Send(Message::GetLength),
            "native paint must style the complete visible original fixture");
}
using TestLanguages::Choose;
using TestLanguages::SchemeFixture;
constexpr SchemeFixture schemes[] = {
    {"TOML", SyntaxLanguage::Toml, SCLEX_TOML, "toml",
     "[package]\nname = \"sample\"\n[package.build]\nenabled = true\n", "#", nullptr, nullptr, "TOML File"},
    {"YAML", SyntaxLanguage::Yaml, SCLEX_YAML, "yaml", "root:\n  child:\n    value: true\n", "#", nullptr, nullptr,
     "YAML Document"},
    {"CMake", SyntaxLanguage::CMake, SCLEX_CMAKE, "cmake", "function(build)\n  message(\"sample\")\nendfunction()\n",
     "#", "#[[", "]]", "CMake Script"},
};

void SchemeWorkflow(EditorFrame* frame, SettingsStore& settings, const SchemeFixture& fixture, intptr_t fontSize) {
    auto* editor = frame->Editor();
    const auto path = TestFiles::TemporaryDirectory(std::string("workflow.") + fixture.extension);
    TestFiles::WriteBytes(path, fixture.text);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "language workflow must Open");
    PaintDocument(frame);
    Require(editor->Send(Message::GetLexer) == fixture.lexer &&
                frame->GetStatusBar()->GetStatusText().Contains(fixture.name),
            "Open must install original dedicated lexer and report its language");
    const bool inherited = fixture.language == SyntaxLanguage::CMake;
    Require(editor->Send(Message::GetTabWidth) == (inherited ? 7 : 2) &&
                editor->Send(Message::GetIndent) == (inherited ? 3 : 2) && !editor->Send(Message::GetUseTabs),
            "TOML/YAML use original independent 2/2 spaces; CMake inherits global settings");
    if (fixture.language == SyntaxLanguage::Yaml) {
        Require(editor->Send(Message::GetIndentationGuides) ==
                    static_cast<intptr_t>(Scintilla::IndentView::LookForward),
                "YAML indentation guides look forward across blank lines");
    }
    const auto original = TestLanguages::ResumeHighlighting(frame, fixture, path, fontSize);
    editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
    TestEditor::Append(frame, "x");
    Choose(frame, "Plain Text");
    Choose(frame, fixture.name);
    Require(TestEditor::Text(editor) == original + "x" && editor->Send(Message::CanUndo),
            "scheme switches preserve live text and undo history");
    TestSyntax::Command(frame, wxID_UNDO);
    Require(TestEditor::Text(editor) == original && editor->Send(Message::CanRedo),
            "Undo remains available after highlighting switches");
    TestSyntax::Command(frame, wxID_REDO);
    Require(TestEditor::Text(editor) == original + "x", "Redo retains the pre-switch edit");
    TestSyntax::Command(frame, wxID_UNDO);
    Choose(frame, "Automatic");
    TestLanguages::FoldCommands(frame);

    TestLanguages::CommentCommands(frame, fixture,
                                   "shared CMake block-comment command uses original bracket delimiter");

    TabDialog::Show(frame, [inherited](wxDialog* dialog) {
        Require(TabDialog::Option(dialog, "Use Global Tab Settings")->GetValue() == inherited,
                "native dialog retains original per-language inheritance decision");
        TabDialog::Toggle(TabDialog::Option(dialog, "Use Global Tab Settings"), false);
        Require(TabDialog::Width(dialog, "Scheme Tab Width")->GetValue() == (inherited ? 4 : 2) &&
                    TabDialog::Width(dialog, "Scheme Indentation Width")->GetValue() == (inherited ? 4 : 2),
                "schemes retain their original independent tab and indent defaults");
        TabDialog::Width(dialog, "Scheme Tab Width")->SetValue(5);
        TabDialog::Width(dialog, "Scheme Indentation Width")->SetValue(2);
    });
    Require(editor->Send(Message::GetTabWidth) == 5 && editor->Send(Message::GetIndent) == 2,
            "native Tab Settings must apply the language's independent settings");
    SettingsStore reloaded;
    const auto& saved = reloaded.values.schemeTabs[static_cast<size_t>(fixture.language)];
    Require(saved.tabWidth == 5 && saved.indentWidth == 2 && !saved.useGlobal,
            "Tab Settings must persist to the original language section");
    const auto diskSettings = TestFiles::Bytes(TestFiles::Utf8Path(settings.Path().ToStdString(wxConvUTF8)));
    Require(diskSettings.starts_with("\xFF\xFE"), "original settings file retains UTF-16LE BOM");
    std::string settingsText;
    for (size_t byte = 2; byte + 1 < diskSettings.size(); byte += 2)
        settingsText += diskSettings[byte];
    const auto section = settingsText.find(std::string("[") + fixture.settingsSection + "]");
    Require(section != std::string::npos, "independent settings bytes contain original section identity");
    const auto nextSection = settingsText.find("\n[", section);
    const auto stored = settingsText.substr(section, nextSection - section);
    Require(stored.find("TabWidth=5") != std::string::npos,
            "independent original section stores the native dialog's tab width");
    settings.values.schemeTabs[static_cast<size_t>(fixture.language)] =
        OriginalSettings{}.schemeTabs[static_cast<size_t>(fixture.language)];
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
    const std::pair<const char*, int> recognized[] = {
        {"Cargo.ToMl", SCLEX_TOML},
        {".pylintrc", SCLEX_TOML},
        {".clang-format", SCLEX_YAML},
        {".clang-tidy", SCLEX_YAML},
        {".clangd", SCLEX_YAML},
        {"case.yml", SCLEX_YAML},
        {"case.mir", SCLEX_YAML},
        {"case.apinotes", SCLEX_YAML},
        {"case.ifs", SCLEX_YAML},
        {"case.gyp", SCLEX_YAML},
        {"case.gypi", SCLEX_YAML},
        {"case.cff", SCLEX_YAML},
        {"case.winget", SCLEX_YAML},
        {"CMakeLists.txt", SCLEX_CMAKE},
        {"cmakecache.TXT", SCLEX_CMAKE},
        {"case.ctest", SCLEX_CMAKE},
        {"CMakeLists.txt.in", SCLEX_CMAKE},
        {".clang-format.in", SCLEX_YAML},
        {"Cargo.toml.in", SCLEX_TOML},
        {"ordinary.txt", SCLEX_NULL},
    };
    for (const auto& [name, lexer] : recognized) {
        const auto path = TestFiles::TemporaryDirectory(name);
        // An associated configuration filename takes precedence over shebang/markup guesses.
        TestFiles::WriteBytes(path, "#!/bin/bash\n<xml/>\n");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "original filename must Open");
        Require(editor->Send(Message::GetLexer) == lexer,
                "original filename and template precedence selects the correct dedicated lexer");
    }
    std::puts("PASS: language matrix covers recognition, history, fonts, highlighting, folds, comments, tabs and saved "
              "bytes");
}

using TestLanguages::Unhex;
void CheckHistory(EditorFrame* frame, wxStandardID command, const std::string& states) {
    auto* editor = frame->Editor();
    const auto canApply = command == wxID_UNDO ? Message::CanUndo : Message::CanRedo;
    std::istringstream expected(states);
    std::string text;
    while (std::getline(expected, text, ',')) {
        Require(editor->Send(canApply), "typed edit retains each original history step");
        TestSyntax::Command(frame, command);
        Require(TestEditor::Text(editor) == Unhex(text), "typed edit history matches the independent original state");
    }
    Require(!editor->Send(canApply), "typed edit has the original number of history steps");
}
void OriginalOperations(EditorFrame* frame) {
    std::ifstream input(std::filesystem::path(CONFIG_OPERATIONS_REFERENCE));
    Require(input.is_open(), "original navigation/character fixture must open");
    constexpr const char* navigation[] = {"Goto Block Start",
                                          "Goto Block End",
                                          "Goto Previous Block",
                                          "Goto Next Block",
                                          "Goto Previous Sibling Block",
                                          "Goto Next Sibling Block",
                                          "Select Lines in Current Block"};
    std::string line;
    size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream row(line);
        std::string fields[10];
        for (auto& field : fields)
            std::getline(row, field, '\t');
        const auto path = TestFiles::TemporaryDirectory(fields[0]);
        const auto before = Unhex(fields[2]);
        TestFiles::WriteBytes(path, before);
        auto* editor = frame->Editor();
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                "original operation document opens through application lifecycle");
        PaintDocument(frame);
        const auto position = std::stoul(fields[3]);
        editor->Send(Message::SetSel, position, position);
        const auto action = static_cast<size_t>(std::stoul(fields[4]));
        if (fields[1] == "navigation") {
            TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Search", navigation[action]));
        } else {
            wxKeyEvent event(wxEVT_CHAR);
            event.m_keyCode = static_cast<int>(action);
            event.m_uniChar = static_cast<int>(action);
            event.SetEventObject(editor);
            editor->GetEventHandler()->ProcessEvent(event);
        }
        if (TestEditor::Text(editor) != Unhex(fields[5]) || editor->Send(Message::GetAnchor) != std::stoll(fields[6]) ||
            editor->Send(Message::GetCurrentPos) != std::stoll(fields[7])) {
            std::fprintf(stderr, "operation fixture=%s kind=%s action=%zu actual=%zd/%zd expected=%s/%s\n",
                         fields[0].c_str(), fields[1].c_str(), action, editor->Send(Message::GetAnchor),
                         editor->Send(Message::GetCurrentPos), fields[6].c_str(), fields[7].c_str());
            throw std::runtime_error("native operation must match independently captured original result");
        }
        if (fields[1] == "character") {
            CheckHistory(frame, wxID_UNDO, fields[8]);
            CheckHistory(frame, wxID_REDO, fields[9]);
        }
        ++rows;
    }
    Require(rows == 33, "all original navigation and character cases must run");
    std::puts("PASS: 33 independent original navigation and typed-character operations");
}
void OriginalOracle(EditorFrame* frame) {
    const auto rows = TestLanguages::OriginalStyleFoldStates(
        frame, std::filesystem::path(CONFIG_LANGUAGES_REFERENCE),
        [frame](const std::string& name, const std::string& text) {
            const auto path = TestFiles::TemporaryDirectory(name);
            TestFiles::WriteBytes(path, text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                    "original TOML/YAML/CMake fixture Open must succeed");
        },
        PaintDocument, "TOML/YAML/CMake styles must match independent original observations",
        "TOML/YAML/CMake folds must match independent original observations");
    Require(rows == 12, "all three original fixtures and four editing states must run");
    std::puts(
        "PASS: 12 independent original TOML/YAML/CMake style/fold states including incremental edit and Undo/Redo");
}
class LanguagesApp final : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        settings_->values.showIndentGuides = true;
        settings_->values.warnLineEndings = false;
        settings_->values.detectIndentation = false;
        settings_->values.globalTabs = {7, 3, true};
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                OriginalOracle(frame);
                OriginalOperations(frame);
                Workflows(frame, *settings_);
                TestLanguages::NativeFiles(frame, schemes);
                std::puts("PASS: native Open and Save As for TOML, YAML and CMake");
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
