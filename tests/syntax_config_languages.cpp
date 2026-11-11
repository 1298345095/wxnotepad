#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "syntax_test_helpers.h"
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
    const char* settingsSection;
};
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
    constexpr auto defaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
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
    if (fixture.blockOpen) {
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment"));
        const auto commented = TestEditor::Text(editor);
        Require(commented.starts_with(fixture.blockOpen) && commented.find(fixture.blockClose) != std::string::npos,
                "shared CMake block-comment command uses original bracket delimiter");
        TestSyntax::Command(frame, wxID_UNDO);
    }
    Require(TestEditor::Text(editor) == "call()\n", "language comment edits must remain undoable");
    editor->Send(Message::SetSavePoint);

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
    std::puts("PASS: native Open and Save As for TOML, YAML and CMake");
}
std::string Unhex(const std::string& hex) {
    std::string bytes;
    for (size_t index = 0; index < hex.size(); index += 2)
        bytes += static_cast<char>(std::stoi(hex.substr(index, 2), nullptr, 16));
    return bytes;
}
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
    std::ifstream input(std::filesystem::path(CONFIG_LANGUAGES_REFERENCE));
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
                    "original TOML/YAML/CMake fixture Open must succeed");
        } else if (state == "edit") {
            editor->Send(Message::GotoPos, std::stoul(deleted));
            wxKeyEvent event(wxEVT_KEY_DOWN);
            event.m_keyCode = WXK_DELETE;
            event.SetEventObject(editor);
            editor->GetEventHandler()->ProcessEvent(event);
        } else {
            TestSyntax::Command(frame, state == "undo" ? wxID_UNDO : wxID_REDO);
        }
        PaintDocument(frame);
        Require(TestEditor::Text(editor) == text, "incremental edit/Undo/Redo bytes must match the original");
        const auto styles = Unhex(styleHex);
        Require(styles.size() == text.size(), "original styles must cover every UTF-8 byte");
        for (size_t position = 0; position < styles.size(); ++position) {
            if (editor->Send(Message::GetStyleIndexAt, position) != static_cast<unsigned char>(styles[position])) {
                std::fprintf(stderr, "fixture=%s state=%s position=%zu expected=%u actual=%zd\n", name.c_str(),
                             state.c_str(), position, static_cast<unsigned char>(styles[position]),
                             editor->Send(Message::GetStyleIndexAt, position));
                throw std::runtime_error("TOML/YAML/CMake styles must match independent original observations");
            }
        }
        std::istringstream folds(foldText);
        size_t foldLine = 0;
        while (std::getline(folds, line, ',')) {
            if (editor->Send(Message::GetFoldLevel, foldLine) != std::stoi(line)) {
                std::fprintf(stderr, "fixture=%s state=%s line=%zu expected=%d actual=%zd\n", name.c_str(),
                             state.c_str(), foldLine, std::stoi(line), editor->Send(Message::GetFoldLevel, foldLine));
                throw std::runtime_error("TOML/YAML/CMake folds must match independent original observations");
            }
            ++foldLine;
        }
        ++rows;
    }
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
