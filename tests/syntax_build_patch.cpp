#include "diagnostics/print.h"
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
#include <fstream>
#include <memory>
#include <sstream>

namespace {
using Scintilla::Message;
using TestLanguages::Choose;
using TestLanguages::Require;
using TestLanguages::Unhex;
auto Schemes() {
    // The public path detector supplies identities so this test also builds
    // against the pre-registration application for its native RED run.
    return std::array<TestLanguages::SchemeFixture, 3>{{
        {"Ini Config File", LanguageForPath("case.ini"), SCLEX_PROPERTIES, "ini",
         "[section]\nkey=value\n[next]\nvalue=two\n", ";", nullptr, nullptr, "Ini Config File"},
        {"Makefile", LanguageForPath("case.mak"), SCLEX_MAKEFILE, "mak",
         "ifdef DEBUG\nall: input\n\t$(CC) input\nendif\n", "#", nullptr, nullptr, "Makefile"},
        {"Diff File", LanguageForPath("case.diff"), SCLEX_DIFF, "diff",
         "diff --git a/a b/a\n--- a/a\n+++ b/a\n@@ -1 +1 @@\n-old\n+new\n", "", nullptr, nullptr, "Diff File"},
    }};
}
void Recognition(EditorFrame* frame) {
    const auto names = std::to_array<std::pair<const char*, int>>({
        {"case.INI", SCLEX_PROPERTIES},     {"case.inf", SCLEX_PROPERTIES},   {"case.reg", SCLEX_PROPERTIES},
        {"case.oem", SCLEX_PROPERTIES},     {"case.sif", SCLEX_PROPERTIES},   {"case.url", SCLEX_PROPERTIES},
        {"case.sed", SCLEX_PROPERTIES},     {"case.theme", SCLEX_PROPERTIES}, {"case.clw", SCLEX_PROPERTIES},
        {"case.abnf", SCLEX_PROPERTIES},    {"case.mak", SCLEX_MAKEFILE},     {"case.make", SCLEX_MAKEFILE},
        {"case.mk", SCLEX_MAKEFILE},        {"case.dsp", SCLEX_MAKEFILE},     {"case.dsw", SCLEX_MAKEFILE},
        {"case.am", SCLEX_MAKEFILE},        {"case.pro", SCLEX_MAKEFILE},     {"case.pri", SCLEX_MAKEFILE},
        {"case.gmk", SCLEX_MAKEFILE},       {"case.ninja", SCLEX_MAKEFILE},   {"Makefile", SCLEX_MAKEFILE},
        {"mAkEfIlE.local", SCLEX_MAKEFILE}, {"Kbuild", SCLEX_MAKEFILE},       {"Kbuild.include", SCLEX_MAKEFILE},
        {"Makefile.ini", SCLEX_PROPERTIES}, {"Makefile.cmake", SCLEX_CMAKE},  {"Makefile.in", SCLEX_MAKEFILE},
        {"case.ini.in", SCLEX_PROPERTIES},  {"case.patch.in", SCLEX_DIFF},    {"case.diff", SCLEX_DIFF},
        {"case.patch", SCLEX_DIFF},         {"case.rej", SCLEX_DIFF},         {"CMakeLists.txt", SCLEX_CMAKE},
        {"CMakeCache.txt", SCLEX_CMAKE},    {"my.ini", SCLEX_BASH},           {"my.cnf", SCLEX_BASH},
        {"case.cfg", SCLEX_BASH},           {"case.conf", SCLEX_BASH},        {"ordinary.txt", SCLEX_NULL},
        {"GNUmakefile", SCLEX_BASH},
    });
    for (const auto& [name, lexer] : names) {
        frame->Editor()->Send(Message::SetSavePoint);
        const auto path = TestFiles::TemporaryDirectory(name);
        TestFiles::WriteBytes(path, "#!/bin/bash\n<xml/>\n");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "recognition fixture opens");
        if (frame->Editor()->Send(Message::GetLexer) != lexer)
            DiagnosticPrint::Print(stderr, "recognition={} expected={} actual={}\n", name, lexer,
                                   frame->Editor()->Send(Message::GetLexer));
        Require(frame->Editor()->Send(Message::GetLexer) == lexer,
                "original extensions, special filenames and precedence select the original lexer");
    }
}
void OriginalOracle(EditorFrame* frame) {
    const auto rows = TestLanguages::OriginalStyleFoldStates(
        frame, std::filesystem::path(BUILD_PATCH_REFERENCE),
        [frame](const std::string& name, const std::string& text) {
            const auto path = TestFiles::TemporaryDirectory(name);
            TestFiles::WriteBytes(path, text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "original fixture opens");
        },
        [](EditorFrame* current) { NativePaint::Complete(current->Editor()); },
        "INI/Make/Diff byte styles match the independent original",
        "INI/Make/Diff folds match the independent original");
    Require(rows == 20, "five original fixtures and all incremental edit/Undo/Redo states execute");
}
void History(EditorFrame* frame, wxStandardID command, const std::string& states) {
    const auto available = command == wxID_UNDO ? Message::CanUndo : Message::CanRedo;
    std::istringstream expected(states);
    for (std::string text; std::getline(expected, text, ',');) {
        Require(frame->Editor()->Send(available), "each independent original history step exists");
        TestSyntax::Command(frame, command);
        Require(TestEditor::Text(frame->Editor()) == Unhex(text), "history restores exact original bytes");
    }
    Require(!frame->Editor()->Send(available), "history contains exactly the original step count");
}
void OpenBytes(EditorFrame* frame, const std::string& name, const std::string& bytes) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetSavePoint);
    const auto path = TestFiles::TemporaryDirectory(name);
    TestFiles::WriteBytes(path, bytes);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "operation fixture opens");
    NativePaint::Complete(editor);
}
void SavedOperation(EditorFrame* frame, const std::string& expected) {
    Require(frame->Save().status == SaveStatus::Saved, "edited original operation saves");
    Require(TestFiles::Bytes(TestFiles::Utf8Path(frame->DocumentPath().ToStdString(wxConvUTF8))) == expected,
            "independently read saved operation bytes match the original");
}
void OriginalOperations(EditorFrame* frame) {
    std::ifstream input(std::filesystem::path(BUILD_PATCH_OPERATIONS_REFERENCE));
    Require(input.is_open(), "independent original operation fixture opens");
    size_t count = 0;
    for (std::string line; std::getline(input, line);) {
        if (line.empty() || line.front() == '#') continue;
        std::array<std::string, 12> fields;
        std::istringstream row(line);
        for (auto& field : fields)
            std::getline(row, field, '\t');
        auto* editor = frame->Editor();
        OpenBytes(frame, fields[0], Unhex(fields[2]));
        // The original retains its fixture's selected insertion EOL when the
        // public text reset has no newline; replay that independently recorded state.
        editor->Send(Message::SetEOLMode, std::stoul(fields[11]));
        const auto position = std::stoul(fields[3]);
        editor->Send(Message::SetSel, fields[1] == "comment" ? 0 : position, position);
        if (fields[1] == "comment") {
            TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment"));
        } else {
            const int character = std::stoi(fields[4]);
            wxKeyEvent event(character == WXK_RETURN ? wxEVT_KEY_DOWN : wxEVT_CHAR);
            event.m_keyCode = character;
            event.m_uniChar = event.m_keyCode;
            event.SetEventObject(editor);
            editor->GetEventHandler()->ProcessEvent(event);
        }
        std::vector<std::string> expectedCandidates;
        std::istringstream words(fields[10]);
        for (std::string word; words >> word;)
            expectedCandidates.push_back(word);
        Require(NativeCompletion::Candidates(editor) == expectedCandidates,
                "typed automatic completion candidates match the independent original");
        editor->Send(Message::AutoCCancel);
        if (TestEditor::Text(editor) != Unhex(fields[5]))
            DiagnosticPrint::Print(stderr, "operation={} action={} expected=[{}] actual=[{}]\n", fields[0], fields[4],
                                   Unhex(fields[5]), TestEditor::Text(editor));
        Require(TestEditor::Text(editor) == Unhex(fields[5]) &&
                    editor->Send(Message::GetAnchor) == std::stoll(fields[6]) &&
                    editor->Send(Message::GetCurrentPos) == std::stoll(fields[7]),
                "comment, newline indentation and paired insertion match original bytes and selection");
        History(frame, wxID_UNDO, fields[8]);
        History(frame, wxID_REDO, fields[9]);
        SavedOperation(frame, Unhex(fields[5]));
        ++count;
    }
    Require(count == 19, "all original comment/indent/pairing operations execute");
}
void OriginalCompletions(EditorFrame* frame) {
    std::ifstream input(std::filesystem::path(BUILD_PATCH_COMPLETIONS_REFERENCE));
    Require(input.is_open(), "independent original completion fixture opens");
    size_t count = 0;
    for (std::string line; std::getline(input, line);) {
        if (line.empty() || line.front() == '#') continue;
        std::array<std::string, 9> fields;
        std::istringstream row(line);
        for (auto& field : fields)
            std::getline(row, field, '\t');
        auto* editor = frame->Editor();
        OpenBytes(frame, fields[0], Unhex(fields[1]));
        editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Complete Word"));
        std::vector<std::string> expected;
        std::istringstream words(fields[8]);
        for (std::string word; words >> word;)
            expected.push_back(word);
        Require(NativeCompletion::Candidates(editor) == expected && TestEditor::Text(editor) == Unhex(fields[2]),
                "completion candidates and immediate bytes match the independent original");
        if (!expected.empty()) {
            editor->Send(Message::AutoCSelect, 0, reinterpret_cast<intptr_t>(expected.front().c_str()));
#ifdef __WXMSW__
            NativeCompletion::Key(editor, VK_RETURN);
#else
            NativeCompletion::Key(editor, GDK_KEY_Return);
#endif
        }
        Require(TestEditor::Text(editor) == Unhex(fields[3]) &&
                    editor->Send(Message::GetAnchor) == std::stoll(fields[4]) &&
                    editor->Send(Message::GetCurrentPos) == std::stoll(fields[5]),
                "completion Enter matches original bytes and selection");
        editor->Send(Message::AutoCCancel);
        History(frame, wxID_UNDO, fields[6]);
        History(frame, wxID_REDO, fields[7]);
        SavedOperation(frame, Unhex(fields[3]));
        ++count;
    }
    Require(count == 5, "all independent completion cases execute");
}
void Workflows(EditorFrame* frame, SettingsStore& settings) {
    auto* editor = frame->Editor();
    for (const auto& fixture : Schemes()) {
        OpenBytes(frame, std::string("workflow.") + fixture.extension, fixture.text);
        const bool make = fixture.lexer == SCLEX_MAKEFILE;
        Require(editor->Send(Message::GetTabWidth) == (make ? 4 : 7) &&
                    editor->Send(Message::GetIndent) == (make ? 4 : 3) &&
                    (editor->Send(Message::GetUseTabs) != 0) == make,
                "Make retains original independent 4/4 literal tabs; INI/Diff inherit global settings");
        constexpr auto style = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
        const auto font = editor->Send(Message::StyleGetSizeFractional, style);
        const auto path = TestFiles::Utf8Path(frame->DocumentPath().ToStdString(wxConvUTF8));
        TestLanguages::ResumeHighlighting(frame, fixture, path, font);
        Choose(frame, "Automatic");
        Require(frame->Revert().status == OpenStatus::Opened, "Revert retains original automatic scheme");
        NativePaint::Complete(editor);
        TestLanguages::FoldCommands(frame);
        TabDialog::Show(frame, [make](wxDialog* dialog) {
            Require(TabDialog::Option(dialog, "Use Global Tab Settings")->GetValue() == !make,
                    "native Tab Settings exposes the original inheritance policy");
            TabDialog::Toggle(TabDialog::Option(dialog, "Use Global Tab Settings"), false);
            TabDialog::Width(dialog, "Scheme Tab Width")->SetValue(5);
            TabDialog::Width(dialog, "Scheme Indentation Width")->SetValue(2);
        });
        SettingsStore reloaded(settings.Path());
        const auto& tabs = reloaded.values.schemeTabs[static_cast<size_t>(fixture.language)];
        Require(!tabs.useGlobal && tabs.tabWidth == 5 && tabs.indentWidth == 2,
                "native independent scheme tabs survive settings reload");
        Require(TestEditor::Text(editor) == fixture.text && !editor->Send(Message::CanUndo),
                "scheme/tab selection preserves recipe tabs, indentation bytes and history");
        settings.values.schemeTabs[static_cast<size_t>(fixture.language)] =
            OriginalSettings{}.schemeTabs[static_cast<size_t>(fixture.language)];
        Require(settings.SaveSchemeTabs(fixture.language), "original defaults restore after native settings edit");
        TestLanguages::SaveNaming(frame, fixture, path);
    }
    TestLanguages::NativeFiles(frame, Schemes());
    const std::string variableRecipe = "# tab-width: 6; c-basic-offset: 2; indent-tabs-mode: nil\nall:\n\tcommand\n";
    OpenBytes(frame, "variables.mak", variableRecipe);
    Require(editor->Send(Message::GetTabWidth) == 6 && editor->Send(Message::GetIndent) == 2 &&
                !editor->Send(Message::GetUseTabs),
            "complete file variables override Makefile's independent literal-tab defaults");
    Choose(frame, "Ini Config File");
    Choose(frame, "Makefile");
    Require(editor->Send(Message::GetTabWidth) == 6 && editor->Send(Message::GetIndent) == 2 &&
                !editor->Send(Message::GetUseTabs) && TestEditor::Text(editor) == variableRecipe,
            "manual scheme switches preserve file variables and existing literal recipe tabs");
    TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Insert Tabs as Spaces"));
    Choose(frame, "Diff File");
    Choose(frame, "Makefile");
    Require(editor->Send(Message::GetTabWidth) == 6 && editor->Send(Message::GetIndent) == 2 &&
                editor->Send(Message::GetUseTabs) && TestEditor::Text(editor) == variableRecipe &&
                !editor->Send(Message::CanUndo),
            "explicit document override precedes variables and scheme settings without rewriting indentation");
    SavedOperation(frame, variableRecipe);
}
class BuildPatchApp final : public wxApp {
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
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native compositor presents INI/Make/Diff test editor");
#ifdef __WXMSW__
                const bool gdi = wxGetEnv("WXNOTEPAD_BUILD_PATCH_GDI", nullptr);
                TestSyntax::Command(frame,
                                    frame->GetMenuBar()->FindMenuItem("Settings", gdi ? "Legacy GDI" : "Direct2D"));
                Require(
                    frame->Editor()->Send(Message::GetTechnology) ==
                        std::to_underlying(gdi ? Scintilla::Technology::Default : Scintilla::Technology::DirectWrite),
                    "native Settings selects the requested GDI/Direct2D renderer");
#endif
                Recognition(frame);
                OriginalOracle(frame);
                OriginalOperations(frame);
                OriginalCompletions(frame);
                Workflows(frame, *settings_);
                std::puts("PASS: original INI/Make/Diff recognition, incremental styles/folds, edits/history, tabs and "
                          "files");
                result_ = 0;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
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
    int OnExit() override { return result_; }

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(BuildPatchApp);
