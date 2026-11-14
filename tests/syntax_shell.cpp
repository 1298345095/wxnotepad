#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "syntax_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "search/search_session.h"
#include "tab_settings_helpers.h"
#include <wx/ffile.h>
#include <SciLexer.h>
#include <wx/wx.h>
#include <wx/statusbr.h>
#include <fstream>
#include <memory>
#include <array>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
using TestEditor::Text;
using TestSyntax::Command;
using TestSyntax::Paint;
using TestSyntax::Style;
void Choose(EditorFrame* frame, const wxString& name) {
    const int id = frame->GetMenuBar()->FindMenuItem("Scheme", name);
    Require(id != wxNOT_FOUND, "Scheme menu must offer the original shell dialect");
    Command(frame, id);
    Require(frame->GetMenuBar()->FindItem(id)->IsChecked(), "selected shell dialect must be checked");
}
struct StyleRun {
    size_t end;
    int style;
};
void OriginalRuns(EditorControl* editor, std::span<const StyleRun> runs, std::span<const int> folds) {
    size_t start = 0;
    for (const auto& run : runs) {
        for (size_t position = start; position < run.end; ++position)
            Style(editor, position, run.style);
        start = run.end;
    }
    for (size_t line = 0; line < folds.size(); ++line)
        Require(editor->Send(Message::GetFoldLevel, line) == folds[line],
                "native fold level must match the independently captured pinned original");
}
void OriginalShell(EditorFrame* frame, const std::filesystem::path& directory) {
    // Captured from pinned Notepad4.exe; see issue-178/original-shell.json in
    // the external acceptance evidence. End offsets cover every fixture byte.
    const std::string bash = "#!/bin/bash\n# comment\nif true; then\n\techo \"hello $HOME ${name:-world} $(date)\"\n"
                             "\techo 'single' `pwd`\n\tcat <<'EOF'\nheredoc $literal\nEOF\nfi\n";
    constexpr StyleRun bashRuns[] = {{11, 1},  {12, 0},  {21, 1},  {22, 0},  {24, 5},  {25, 0},  {29, 5},  {30, 2},
                                     {31, 0},  {35, 5},  {37, 0},  {41, 5},  {42, 0},  {49, 10}, {54, 6},  {55, 10},
                                     {69, 11}, {70, 10}, {72, 2},  {76, 5},  {77, 2},  {78, 10}, {80, 0},  {84, 5},
                                     {85, 0},  {93, 9},  {94, 0},  {99, 12}, {101, 0}, {104, 5}, {105, 0}, {112, 7},
                                     {130, 8}, {133, 7}, {134, 0}, {136, 5}, {137, 0}};
    constexpr int bashFolds[] = {9216, 1025, 9216, 1025, 1025, 9217, 1026, 1026, 1025, 1024};
    const auto path = directory / "original.sh";
    std::ofstream(path, std::ios::binary) << bash;
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "original fixture must open");
    Paint(frame);
    auto* editor = frame->Editor();
    OriginalRuns(editor, bashRuns, bashFolds);
    Require(editor->Send(Message::StyleGetEOLFilled, SCE_SH_HERE_DELIM) &&
                editor->Send(Message::StyleGetEOLFilled, SCE_SH_HERE_Q),
            "original heredoc style groups must fill the line ending");
    const int collapse = frame->GetMenuBar()->FindMenuItem("View", "Collapse All");
    const int expand = frame->GetMenuBar()->FindMenuItem("View", "Expand All");
    Command(frame, collapse);
    Require(!editor->Send(Message::GetLineVisible, 3), "Collapse All must hide the shell body");
    SearchSession search(*editor);
    search.Configure({"$literal"});
    Require(search.Find(SearchDirection::Forward).status == FindStatus::Found &&
                editor->Send(Message::GetLineVisible, 6),
            "search must reveal a match in a folded shell heredoc");
    Command(frame, expand);
    for (int line = 0; line < 10; ++line)
        Require(editor->Send(Message::GetLineVisible, line), "Expand All must reveal shell lines");
    editor->Send(Message::SetSel, 0, 0);
    editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("# edit\n"));
    Command(frame, wxID_UNDO);
    OriginalRuns(editor, bashRuns, bashFolds);
    Command(frame, wxID_REDO);
    Style(editor, 0, SCE_SH_COMMENTLINE);
    Command(frame, wxID_UNDO);
    Choose(frame, "JSON");
    Require(!editor->Send(Message::StyleGetEOLFilled, SCE_SH_HERE_Q), "scheme change must clear stale shell styles");
    Choose(frame, "C Shell");
    Choose(frame, "Bash");
    OriginalRuns(editor, bashRuns, bashFolds);
    const auto cshPath = directory / "original.csh";
    std::ofstream(cshPath, std::ios::binary) << "#!/bin/csh\nforeach item (a b)\n\techo \"$item\"\nend\n";
    Require(frame->Open(TestFiles::Filename(cshPath)).status == OpenStatus::Opened, "C Shell fixture must open");
    Paint(frame);
    constexpr StyleRun cshRuns[] = {{10, 1},  {11, 0}, {18, 5},  {19, 0}, {23, 14}, {24, 0}, {25, 2},
                                    {26, 4},  {27, 0}, {28, 4},  {29, 2}, {31, 0},  {35, 5}, {36, 0},
                                    {37, 10}, {42, 6}, {43, 10}, {44, 0}, {47, 5},  {48, 0}};
    constexpr int cshFolds[] = {1024, 9216, 1025, 1025, 1024};
    OriginalRuns(editor, cshRuns, cshFolds);
    const auto m4Path = directory / "original.m4";
    std::ofstream(m4Path, std::ios::binary) << "dnl comment\ndefine(`name', `value')\nifdef(`name', `yes', `no')\n";
    Require(frame->Open(TestFiles::Filename(m4Path)).status == OpenStatus::Opened, "m4 fixture must open");
    Paint(frame);
    constexpr StyleRun m4Runs[] = {{11, 1}, {12, 0},  {18, 14}, {19, 2}, {25, 12}, {26, 2}, {27, 0},  {33, 12},
                                   {48, 9}, {51, 12}, {54, 4},  {55, 0}, {56, 2},  {57, 0}, {60, 12}, {63, 9}};
    constexpr int m4Folds[] = {1024, 1024, 1024, 1024};
    OriginalRuns(editor, m4Runs, m4Folds);
    std::puts(
        "PASS: all original shell style runs/fold levels, incremental Undo/Redo, fold commands and folded search");
}
void ShellOperations(EditorFrame* frame, SettingsStore& settings, const std::filesystem::path& directory) {
    auto* editor = frame->Editor();
    for (const auto* dialect : {"Bash", "C Shell", "m4"}) {
        TestEditor::ResetStream(editor, "call()\n", 0, 0);
        Choose(frame, dialect);
        const bool m4 = wxString(dialect) == "m4";
        const int lineComment = frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment");
        const int blockComment = frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment");
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(lineComment) && !frame->GetMenuBar()->IsEnabled(blockComment),
                "shell schemes must offer line comments and disable block comments");
        Command(frame, lineComment);
        Require(Text(editor) == (m4 ? "dnl call()\n" : "# call()\n"),
                "shell comment command must retain original prefix and mandatory m4 padding");
        Command(frame, lineComment);
        Require(Text(editor) == "call()\n", "shell comment command must remove its original prefix");
        if (m4) {
            const auto mask = settings.values.autoInsertMask;
            settings.values.autoInsertMask &= ~AutoInsertMask_SpaceAfterComment;
            Command(frame, lineComment);
            Require(Text(editor) == "dnl call()\n",
                    "m4 must retain its mandatory pad when optional spacing is disabled");
            Command(frame, lineComment);
            settings.values.autoInsertMask = mask;
        }
        Require(editor->Send(Message::BraceMatch, 4, -1) == 5, "shell operator braces must use native matching");
        TestEditor::ResetStream(editor, "", 0, 0);
        editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
        Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Script Shebang Line"));
        Require(Text(editor) == (m4                            ? "#!/usr/bin/env m4\n"
                                 : wxString(dialect) == "Bash" ? "#!/bin/bash\n"
                                                               : "#!/bin/csh\n"),
                "each dialect must insert the original shebang");
    }
    TestEditor::ResetStream(editor, "echo test\n", 0, 0);
    Choose(frame, "C Shell");
    TabDialog::Show(frame, [](wxDialog* dialog) {
        Require(TabDialog::Option(dialog, "Use Global Tab Settings")->GetValue(),
                "Shell Script inherits global tabs by default");
        TabDialog::Toggle(TabDialog::Option(dialog, "Use Global Tab Settings"), false);
        TabDialog::Width(dialog, "Scheme Tab Width")->SetValue(7);
        TabDialog::Width(dialog, "Scheme Indentation Width")->SetValue(3);
        TabDialog::Toggle(TabDialog::Option(dialog, "Scheme Tabs as Spaces"), true);
    });
    for (const auto* dialect : {"Bash", "C Shell", "m4"}) {
        Choose(frame, dialect);
        Require(editor->Send(Message::GetTabWidth) == 7 && editor->Send(Message::GetIndent) == 3 &&
                    !editor->Send(Message::GetUseTabs),
                "all shell dialects share the original Shell Script section");
    }
    SettingsStore reloaded(settings.Path());
    const auto& saved = reloaded.values.schemeTabs[static_cast<size_t>(SyntaxLanguage::Bash)];
    Require(saved.tabWidth == 7 && saved.indentWidth == 3 && saved.tabsAsSpaces && !saved.useGlobal,
            "accepting Tab Settings must immediately persist the canonical Shell Script section");
    wxFFile ini(settings.Path(), "rb");
    wxString iniText;
    Require(ini.ReadAll(&iniText, wxMBConvUTF16LE()) && iniText.Contains("[Shell Script]") &&
                !iniText.Contains("[C Shell]") && !iniText.Contains("[m4]"),
            "settings disk sections must preserve the original scheme identity without dialect duplicates");
    const auto variables = directory / "variables.csh";
    const std::string bytes = "# tab-width: 8; c-basic-offset: 2; indent-tabs-mode: true\necho test\n";
    std::ofstream(variables, std::ios::binary) << bytes;
    Require(frame->Open(TestFiles::Filename(variables)).status == OpenStatus::Opened,
            "shell file variables fixture must open");
    Choose(frame, "m4");
    Require(editor->Send(Message::GetTabWidth) == 8 && editor->Send(Message::GetIndent) == 2 &&
                editor->Send(Message::GetUseTabs),
            "original file variables must take precedence over shared shell tabs");
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(variables) == bytes,
            "shell tab settings must preserve file bytes");
    Require(frame->NewDocument(), "clean shell document must allow New");
    Choose(frame, "Bash");
    editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("echo first\n"));
    const auto first = directory / "first.unknown";
    Require(frame->SaveFirst(TestFiles::Filename(first)).status == SaveStatus::Saved &&
                TestFiles::Bytes(first) == "echo first\n" &&
                frame->GetStatusBar()->GetStatusText().Contains("Bash (override)"),
            "first Save must retain a selected shell scheme and LF bytes");
    const auto renamed = directory / "renamed.m4";
    Require(frame->SaveAs(TestFiles::Filename(renamed), {}, SaveType::AllFiles).status == SaveStatus::Saved &&
                TestFiles::Bytes(renamed) == "echo first\n" &&
                frame->GetStatusBar()->GetStatusText().Contains(" | m4 | "),
            "All Files Save As must resolve the original m4 association");
    Choose(frame, "C Shell");
    Require(frame->Open(TestFiles::Filename(renamed)).status == OpenStatus::Opened &&
                frame->GetStatusBar()->GetStatusText().Contains("C Shell (override)"),
            "same-file reopen must retain a manual shell dialect");
    Choose(frame, "Automatic");
    Require(frame->GetStatusBar()->GetStatusText().Contains(" | m4 | "),
            "explicit Automatic must restore filename dialect selection");
    std::puts("PASS: native shell comments, brace matching, dialect shebangs, settings persistence and file-variable "
              "precedence");
}
void EmptyShellBoms(EditorFrame* frame, EditorControl* editor, const std::filesystem::path& directory) {
    for (const auto& bom : {std::string("\xef\xbb\xbf"), std::string("\xff\xfe")}) {
        const auto bomPath = directory / (bom.size() == 3 ? "bom.sh" : "utf16.sh");
        std::ofstream(bomPath, std::ios::binary) << bom;
        Require(frame->Open(TestFiles::Filename(bomPath)).status == OpenStatus::Opened, "BOM-only shell must open");
        Require(frame->DocumentHasBom() &&
                    editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::Lf),
                "fresh shell must retain its encoding BOM while applying LF");
        Choose(frame, "m4");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(bomPath) == bom,
                "dialect changes must preserve BOM-only disk bytes");
        if (bom.size() == 3) {
            frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8));
            Require(frame->DocumentIsModified(), "removing a named empty file's BOM must be a format change");
            Choose(frame, "C Shell");
            Require(frame->DocumentIsModified(), "unchanged shell defaults must retain a pending BOM removal");
            frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8, true));
            Require(!frame->DocumentIsModified(), "restoring the original BOM must restore clean identity");
        }
    }
}
class ShellApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory("Notepad4.ini")));
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-shell");
            try {
                std::filesystem::create_directory(directory);
                const auto path = directory / "example.sh";
                const std::string source = "#!/bin/bash\nif true; then\n\techo \"hello $HOME\"\nfi\n";
                std::ofstream(path, std::ios::binary) << source;
                Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "shell file must open");
                Paint(frame);
                auto* editor = frame->Editor();
                Require(editor->Send(Message::GetLexer) == SCLEX_BASH, "shell extension must select pinned Bash lexer");
                Style(editor, source.find("if"), SCE_SH_WORD);
                Require(frame->GetStatusBar()->GetStatusText() == "UTF-8 | Bash | EOL: LF", "status must name Bash");
                Choose(frame, "C Shell");
                Choose(frame, "m4");
                Choose(frame, "Bash");
                Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == source,
                        "shell choices and save must preserve disk bytes");
                std::puts("PASS: shell automatic selection, native dialect choices, styling and saved bytes");
                struct Fixture {
                    const char* name;
                    const char* prefix;
                    const char* language;
                    int dialect;
                };
                size_t sequence = 0;
                for (const auto& fixture : {
                         Fixture{"script", "#!/bin/bash", "Bash", 0},
                         {"env", "#!/usr/bin/env tcsh", "C Shell", 1},
                         {"winpty", "#! C:\\bin\\winpty.exe C:\\bin\\csh.exe", "C Shell", 1},
                         {"envprefix", "#!/bin/environment dash", "Bash", 0},
                         {"zsh", "#!/bin/ZSH5", "Bash", 0},
                         {"ksh", "#!/bin/ksh93", "Bash", 0},
                         {"ash", "#!/bin/ash", "Bash", 0},
                         {"prefix", "#!/bin/shanything", "Bash", 0},
                         {"envoptions", "#!/bin/env -S bash", "Plain Text", 0},
                         {"envcase", "#!/bin/ENV bash", "Plain Text", 0},
                         {"quotedpath", "#!\"/bin/bash\"", "Bash", 0},
                         {"quotedname", "#!\"bash\"", "Plain Text", 0},
                         {"leading", " #!/bin/bash", "Plain Text", 0},
                         {"script.cgi", "#!/bin/csh", "C Shell", 1},
                         {"script.fcgi", "#!/bin/zsh", "Bash", 0},
                         {"script.sh", "#!/bin/python", "Bash", 0},
                         {"script.csh", "#!/bin/bash", "C Shell", 1},
                         {"script.TCSH", "", "C Shell", 1},
                         {"script.m4", "", "m4", 2},
                         {"configure.ac", "", "m4", 2},
                         {"script.in", "#!/bin/python", "Bash", 0},
                         {"config.py.in", "#!/bin/bash", "Python", 0},
                         {"config.cpp.in", "#!/bin/bash", "C/C++", 0},
                         {"config.csh.in", "#!/bin/bash", "C Shell", 1},
                         {"config.txt.in", "#!/bin/bash", "Plain Text", 0},
                         {"config.js.in", "#!/bin/bash", "JavaScript", 0},
                         {".bashrc", "#!/bin/python", "Bash", 0},
                         {".bash_history", "", "Bash", 0},
                         {".profile", "", "Bash", 0},
                         {".BASHRC", "#!/bin/python", "Python", 0},
                         {"APKBUILD", "#!/bin/python", "Bash", 0},
                         {"PKGBUILD", "", "Bash", 0},
                         {"mozconfig", "", "Bash", 0},
                         {"script.txt", "#!/bin/bash", "Plain Text", 0},
                         {"script.py", "#!/bin/bash", "Python", 0},
                         {"script.json", "#!/bin/bash", "JSON", 0},
                         {"script.js", "#!/bin/bash", "JavaScript", 0},
                         {"script.cfg", "#!/bin/bash", "Bash", 0},
                         {"my.ini", "#!/bin/bash", "Bash", 0},
                         {"Makefile", "#!/bin/bash", "Plain Text", 0},
                     }) {
                    const auto folder = directory / std::to_string(++sequence);
                    std::filesystem::create_directory(folder);
                    const auto candidate = folder / fixture.name;
                    const std::string bytes = std::string(fixture.prefix) + "\necho 42\n";
                    std::ofstream(candidate, std::ios::binary) << bytes;
                    Require(frame->Open(TestFiles::Filename(candidate)).status == OpenStatus::Opened,
                            "shell detection fixture must open");
                    Paint(frame);
                    std::printf("CASE: %s\n", fixture.name);
                    Require(frame->GetStatusBar()->GetStatusText().Contains(" | " + wxString(fixture.language) + " | "),
                            "filename and shebang precedence must match the original");
                    if (editor->Send(Message::GetLexer) == SCLEX_BASH)
                        Require(editor->Send(Message::GetPropertyInt, reinterpret_cast<uintptr_t>("lexer.lang")) ==
                                    fixture.dialect,
                                "automatic shell dialect property must match original");
                    Choose(frame, "Plain Text");
                    Choose(frame, "Automatic");
                    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(candidate) == bytes,
                            "Automatic and Save must preserve shell fixture bytes");
                }
                const auto emptyPath = directory / "empty.sh";
                std::ofstream(emptyPath, std::ios::binary);
                Require(frame->Open(TestFiles::Filename(emptyPath)).status == OpenStatus::Opened,
                        "fresh empty shell fixture must open");
                Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::Lf) &&
                            frame->DocumentEncoding() == FileEncoding::Utf8,
                        "fresh empty shell must select original UTF-8/LF defaults");
                Choose(frame, "Plain Text");
                Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::CrLf),
                        "leaving a fresh shell scheme must restore the configured default EOL");
                Choose(frame, "C Shell");
                Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Script Shebang Line"));
                Require(Text(editor) == "#!/bin/csh\n", "shell shebang command must add original LF at position zero");
                Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(emptyPath) == "#!/bin/csh\n",
                        "fresh shell EOL must reach independent saved bytes");
                editor->Send(Message::Undo);
                Choose(frame, "Plain Text");
                Require(editor->Send(Message::CanRedo) &&
                            editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::Lf),
                        "an empty document with edit history must retain EOL and pending redo on scheme changes");
                editor->Send(Message::Redo);
                Require(frame->Save().status == SaveStatus::Saved, "history fixture must save before loading BOMs");
                EmptyShellBoms(frame, editor, directory);
                std::puts("PASS: original empty-document UTF-8/LF, edit history, BOM and shebang behavior");
                const auto legacyEmpty = directory / "empty-utf16.txt";
                std::ofstream(legacyEmpty, std::ios::binary);
                Require(frame->Open(TestFiles::Filename(legacyEmpty), FileEncoding::Utf16Le).status ==
                                OpenStatus::Opened &&
                            frame->DocumentEncoding() == FileEncoding::Utf16Le,
                        "explicit no-BOM empty encoding fixture must open");
                Choose(frame, "Bash");
                Require(frame->DocumentEncoding() == FileEncoding::Utf8 && !frame->DocumentIsModified() &&
                            !editor->Send(Message::GetUndoActions),
                        "shell defaults must replace a no-BOM empty encoding without edit history");
                OriginalShell(frame, directory);
                ShellOperations(frame, *settings_, directory);
                settings_->values.noCgiGuess = true;
                auto* gated = new EditorFrame(settings_.get());
                gated->ShowWithoutActivating();
                const auto cgi = directory / "gated.cgi";
                std::ofstream(cgi, std::ios::binary) << "#!/bin/csh\necho gated\n";
                Require(gated->Open(TestFiles::Filename(cgi)).status == OpenStatus::Opened &&
                            gated->GetStatusBar()->GetStatusText().Contains(" | Plain Text | "),
                        "NoCGIGuess must suppress shell interpreter selection");
                Require(gated->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                            gated->GetStatusBar()->GetStatusText().Contains(" | Bash | "),
                        "NoCGIGuess must retain original filename associations");
                gated->Destroy();
                settings_->values.autoSelect = false;
                gated = new EditorFrame(settings_.get());
                gated->ShowWithoutActivating();
                Require(gated->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                            gated->GetStatusBar()->GetStatusText().Contains(" | Plain Text | "),
                        "AutoSelect=0 must suppress shell filename selection");
                Choose(gated, "C Shell");
                gated->Destroy();
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            std::filesystem::remove_all(directory);
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
wxIMPLEMENT_APP(ShellApp);
