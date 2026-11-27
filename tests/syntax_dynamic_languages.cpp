#include "syntax/syntax_detection.h"
#include "diagnostics/print.h"
#include "syntax_language_test_helpers.h"
#include "native_paint_helpers.h"
#include "native_renderer_helpers.h"
#include "settings/settings_store.h"
#include "native_dialog_helpers.h"
#include "tab_settings_helpers.h"
#include <wx/wx.h>
#include <array>
#include <cstdio>
#include <tuple>

namespace {
using Scintilla::Message;
using TestLanguages::Choose;
using TestLanguages::Require;

void Paint(EditorFrame* frame) {
    auto* editor = frame->Editor();
    NativePaint::Complete(editor);
    const auto length = editor->Send(Message::GetLength);
    if (editor->Send(Message::GetEndStyled) < length) {
        // The 21-line Ruby fixture can extend below the Windows viewport at
        // high DPI. Scroll its final byte into view to receive real native
        // incremental styling, then restore the original viewport and selection.
        const auto firstLine = editor->Send(Message::GetFirstVisibleLine);
        editor->Send(Message::ScrollRange, length, length);
        NativePaint::Complete(editor);
        editor->Send(Message::SetFirstVisibleLine, firstLine);
        NativePaint::Complete(editor);
    }
    Require(editor->Send(Message::GetEndStyled) >= length, "native rendering styles the whole script fixture");
}
std::array<TestLanguages::SchemeFixture, 3> Schemes() {
    return {{{"Ruby Script", LanguageForPath("sample.rb"), SCLEX_RUBY, "rb",
              "module Sample\n  class Item\n    def run\n      puts 'hello'\n    end\n  end\nend\n", "#", nullptr,
              nullptr, "Ruby Script"},
             {"Perl Script", LanguageForPath("sample.pl"), SCLEX_PERL, "pl", "sub run {\n  print 'hello';\n}\n", "#",
              nullptr, nullptr, "Perl Script"},
             {"Tcl Script", LanguageForPath("sample.tcl"), SCLEX_TCL, "tcl", "proc run {} {\n  puts hello\n}\n", "#",
              nullptr, nullptr, "Tcl Script"}}};
}
void Recognition(EditorFrame* frame) {
    for (const auto& [suffix, lexer] : {std::pair{"rb", SCLEX_RUBY},
                                        {"ruby", SCLEX_RUBY},
                                        {"rbw", SCLEX_RUBY},
                                        {"rake", SCLEX_RUBY},
                                        {"rjs", SCLEX_RUBY},
                                        {"gemspec", SCLEX_RUBY},
                                        {"podspec", SCLEX_RUBY},
                                        {"rbs", SCLEX_RUBY},
                                        {"cr", SCLEX_RUBY},
                                        {"pl", SCLEX_PERL},
                                        {"pm", SCLEX_PERL},
                                        {"cgi", SCLEX_PERL},
                                        {"pod", SCLEX_PERL},
                                        {"plx", SCLEX_PERL},
                                        {"stp", SCLEX_PERL},
                                        {"tcl", SCLEX_TCL},
                                        {"itcl", SCLEX_TCL},
                                        {"tm", SCLEX_TCL}}) {
        const auto path = TestFiles::TemporaryDirectory(std::string("recognition.") + suffix);
        TestFiles::WriteBytes(path, "echo hello\n");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "script document opens");
        Require(frame->Editor()->Send(Message::GetLexer) == lexer,
                "every original script extension selects its dedicated lexer");
    }
    for (const auto& [name, text, lexer] :
         {std::tuple{"Rakefile", "#!/usr/bin/python", SCLEX_RUBY}, std::tuple{"Podfile", "", SCLEX_RUBY},
          std::tuple{"Steepfile", "", SCLEX_RUBY}, std::tuple{"sample.rb.in", "#!/usr/bin/perl", SCLEX_RUBY},
          std::tuple{"sample.cgi", "#!/usr/bin/python3", SCLEX_PYTHON},
          std::tuple{"sample.fcgi", "#!/usr/bin/ruby", SCLEX_RUBY},
          std::tuple{"perl.extension", "#!/usr/bin/env perl5", SCLEX_PERL},
          std::tuple{"ruby.extension", "#!/usr/bin/env ruby3", SCLEX_RUBY},
          std::tuple{"tcl.extension", "#!/bin/tclsh8.6", SCLEX_TCL},
          std::tuple{"wish.extension", "#!/bin/wish", SCLEX_TCL}}) {
        const auto path = TestFiles::TemporaryDirectory(name);
        TestFiles::WriteBytes(path, text);
        const auto opened = frame->Open(TestFiles::Filename(path)).status;
        if (opened != OpenStatus::Opened || frame->Editor()->Send(Message::GetLexer) != lexer)
            std::println(stderr, "recognition name={} expected={} actual={}", name, lexer,
                         frame->Editor()->Send(Message::GetLexer));
        Require(opened == OpenStatus::Opened && frame->Editor()->Send(Message::GetLexer) == lexer,
                "original filename, template and interpreter priorities reach the actual open document");
    }
    SyntaxExtensions ambiguous = {{LanguageForPath("file.rb"), "shared"},
                                  {LanguageForPath("file.pl"), "shared"},
                                  {LanguageForPath("file.tcl"), "shared"}};
    Require(LanguageForPath("file.shared", ambiguous) == LanguageForPath("file.pl") &&
                LanguageForPath("file.shared", ambiguous, "35,33,31") == LanguageForPath("file.tcl") &&
                LanguageForPath("file.shared", ambiguous, "33,35,31") == LanguageForPath("file.rb"),
            "ambiguous associations retain original default and configured favorite priorities");
    Require(LanguageForPath("file.txt") == SyntaxLanguage::PlainText &&
                LanguageForPath("file.in") == SyntaxLanguage::Bash &&
                DetectLanguage("file.rb", "#!/usr/bin/python")->schemeLanguage == LanguageForPath("file.rb"),
            "explicit script extensions precede sniffing and preserve existing associations");
}
void Choosers(EditorFrame* frame, SettingsStore& settings) {
    settings.values.favoriteSchemes = "33,31,35,2";
    const std::array favorites = {"Ruby Script", "Perl Script", "Tcl Script", "Java Source"};
    TestLanguages::ScriptChoosers(frame, settings, Schemes(), favorites);
}
void OriginalOracle(EditorFrame* frame) {
    const auto rows = TestLanguages::OriginalStyleFoldStates(
        frame, std::filesystem::path(DYNAMIC_LANGUAGES_REFERENCE),
        [frame](const std::string& name, const std::string& text) {
            const auto path = TestFiles::TemporaryDirectory(name);
            TestFiles::WriteBytes(path, text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                    "independent original script fixture opens");
        },
        Paint, "every original script style byte matches", "every original script fold level matches");
    Require(rows == 24, "six complete/incomplete script fixtures each replay edit, Undo and Redo");
}
void Operations(EditorFrame* frame) {
    const auto rows =
        TestLanguages::OriginalScriptOperations(frame, std::filesystem::path(DYNAMIC_OPERATIONS_REFERENCE), Paint);
    Require(rows == 44, "all independent script completion, insertion, comment and protection operations run");
}
void Workflows(EditorFrame* frame, SettingsStore& settings) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const auto font = editor->Send(StyleGetSizeFractional, static_cast<uintptr_t>(Scintilla::StylesCommon::Default));
    for (const auto& fixture : Schemes()) {
        editor->Send(SetSavePoint);
        Choose(frame, "Automatic");
        const auto path = TestFiles::TemporaryDirectory(std::string("workflow.") + fixture.extension);
        TestFiles::WriteBytes(path, fixture.text);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "script workflow opens");
        Paint(frame);
        Require(editor->Send(GetTabWidth) == 7 && editor->Send(GetIndent) == 3 && !editor->Send(GetUseTabs),
                "scripts inherit global tab settings");
        TestLanguages::ResumeHighlighting(frame, fixture, path, font);
        const auto numberStyle = fixture.language == SyntaxLanguage::Ruby   ? SCE_RB_NUMBER
                                 : fixture.language == SyntaxLanguage::Perl ? SCE_PL_NUMBER
                                                                            : SCE_TCL_NUMBER;
        Require(editor->Send(StyleGetFore, numberStyle) == 0x0000ff,
                "Default theme retains the pinned original number color");
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Dark"));
        Require(editor->Send(StyleGetFore, numberStyle) == 0x4c4cf8 &&
                    editor->Send(StyleGetSizeFractional, static_cast<uintptr_t>(Scintilla::StylesCommon::Default)) ==
                        font,
                "Dark uses the distributed original number color and inherits the code font");
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Default"));

        TestLanguages::ScriptSchemeHistory(frame, fixture, Paint);
        TabDialog::Show(frame, [](wxDialog* dialog) {
            Require(TabDialog::Option(dialog, "Use Global Tab Settings")->GetValue(),
                    "original dynamic schemes inherit global tab settings by default");
            TabDialog::Toggle(TabDialog::Option(dialog, "Use Global Tab Settings"), false);
            Require(TabDialog::Width(dialog, "Scheme Tab Width")->GetValue() == 4 &&
                        TabDialog::Width(dialog, "Scheme Indentation Width")->GetValue() == 4,
                    "original dynamic schemes retain independent 4/4 defaults");
            TabDialog::Width(dialog, "Scheme Tab Width")->SetValue(5);
            TabDialog::Width(dialog, "Scheme Indentation Width")->SetValue(2);
        });
        Require(editor->Send(GetTabWidth) == 5 && editor->Send(GetIndent) == 2,
                "native Tab Settings apply to each dynamic scheme");
        SettingsStore reloaded(settings.Path());
        const auto& saved = reloaded.values.schemeTabs[static_cast<size_t>(std::to_underlying(fixture.language))];
        Require(saved.tabWidth == 5 && saved.indentWidth == 2 && !saved.useGlobal,
                "native scheme tab overrides persist under the original identity");
        const auto variables = TestFiles::TemporaryDirectory(std::string("variables.") + fixture.extension);
        const std::string variableText = R"(# tab-width: 6; c-basic-offset: 2; indent-tabs-mode: nil
)";
        TestFiles::WriteBytes(variables, variableText + fixture.text);
        Require(frame->Open(TestFiles::Filename(variables)).status == OpenStatus::Opened &&
                    editor->Send(GetTabWidth) == 6 && editor->Send(GetIndent) == 2 && !editor->Send(GetUseTabs),
                "file variables override scheme tab settings");
        TestLanguages::SaveNaming(frame, fixture, path);
        const auto savedBytes = TestFiles::Bytes(path);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                    frame->ReloadAsUtf8().status == OpenStatus::Opened && editor->Send(GetLexer) == fixture.lexer &&
                    TestFiles::Bytes(path) == savedBytes && !editor->Send(CanUndo),
                "source-encoding reload retains scheme, actual bytes and clean replacement history");
        Require(frame->NewDocument(), "clean script permits New");
        Require(frame->OpenRestartDocument(TestFiles::Filename(path), EncodingChoice{FileEncoding::Utf8, true},
                                           fixture.language, false, 0, 0)
                            .status == OpenStatus::Opened &&
                    editor->Send(GetLexer) == fixture.lexer && frame->DocumentHasUtf8Bom(),
                "script restart restores override and BOM identity");
    }
    TestLanguages::NativeFiles(frame, Schemes());
}
class DynamicApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_DYNAMIC_DPI96"))
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "script fixture enters process-local complementary 96 DPI mode");
#endif
        wxInitAllImageHandlers();
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory("Notepad4.ini")));
        settings_->values.warnLineEndings = false;
        settings_->values.detectIndentation = false;
        settings_->values.wordWrap = false;
        auto* frame = new EditorFrame(settings_.get());
        TestRenderer::Select(frame->Editor());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "owned native script frame is presented");
                Recognition(frame);
                Choosers(frame, *settings_);
                Choose(frame, "Automatic");
                OriginalOracle(frame);
                Operations(frame);
                settings_->values.globalTabs = {7, 3, true};
                Workflows(frame, *settings_);
                std::puts("PASS: original script styles/folds/edits/completion and document workflows");
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

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(DynamicApp);
