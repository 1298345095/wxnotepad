#include "syntax/syntax_detection.h"
#include "diagnostics/print.h"
#include "syntax_language_test_helpers.h"
#include "native_paint_helpers.h"
#include "native_renderer_helpers.h"
#include "settings/settings_store.h"
#include "native_dialog_helpers.h"
#include <wx/wx.h>
#include <array>

namespace {
using Scintilla::Message;
using TestLanguages::Choose;
using TestLanguages::Require;

void Paint(EditorFrame* frame) {
    NativePaint::Complete(frame->Editor());
    Require(frame->Editor()->Send(Message::GetEndStyled) >= frame->Editor()->Send(Message::GetLength),
            "native rendering styles the whole script fixture");
}
std::array<TestLanguages::SchemeFixture, 3> Schemes() {
    return {{{"Batch File", LanguageForPath("sample.bat"), SCLEX_BATCH, "bat",
              ":label\nif exist file (\n  echo hello\n)\n", "@rem ", nullptr, nullptr, "Batch File"},
             {"PowerShell Script", LanguageForPath("sample.ps1"), SCLEX_POWERSHELL, "ps1",
              "function Test {\n  Write-Host 'hello'\n}\n", "#", "<#", "#>", "PowerShell Script"},
             {"AutoHotkey Script", LanguageForPath("sample.ahk"), SCLEX_AUTOHOTKEY, "ahk",
              "class Sample {\n  Run() {\n    MsgBox('hello')\n  }\n}\n", ";", "/*", "*/", "AutoHotkey Script"}}};
}
void Recognition(EditorFrame* frame) {
    for (const auto& [suffix, lexer] : {std::pair{"bat", SCLEX_BATCH},
                                        {"CMD", SCLEX_BATCH},
                                        {"ps1", SCLEX_POWERSHELL},
                                        {"psc1", SCLEX_POWERSHELL},
                                        {"psd1", SCLEX_POWERSHELL},
                                        {"psm1", SCLEX_POWERSHELL},
                                        {"ahk", SCLEX_AUTOHOTKEY},
                                        {"ia", SCLEX_AUTOHOTKEY},
                                        {"scriptlet", SCLEX_AUTOHOTKEY}}) {
        const auto path = TestFiles::TemporaryDirectory(std::string("recognition.") + suffix);
        TestFiles::WriteBytes(path, "echo hello\n");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "script document opens");
        Require(frame->Editor()->Send(Message::GetLexer) == lexer,
                "every original script extension selects its dedicated lexer");
    }
    Require(LanguageForPath("file.txt") == SyntaxLanguage::PlainText &&
                LanguageForPath("file.in") == SyntaxLanguage::Bash &&
                DetectLanguage("file.ps1", "#!/usr/bin/python")->schemeLanguage == LanguageForPath("file.ps1"),
            "explicit script extensions precede sniffing and preserve existing associations");
}
void Choosers(EditorFrame* frame, SettingsStore& settings) {
    settings.values.favoriteSchemes = "95,36,38,2";
    const std::array favorites = {"AutoHotkey Script", "Batch File", "PowerShell Script", "Java Source"};
    TestLanguages::ScriptChoosers(frame, settings, Schemes(), favorites);
}
void OriginalOracle(EditorFrame* frame) {
    const auto rows = TestLanguages::OriginalStyleFoldStates(
        frame, std::filesystem::path(SCRIPT_LANGUAGES_REFERENCE),
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
        TestLanguages::OriginalScriptOperations(frame, std::filesystem::path(SCRIPT_OPERATIONS_REFERENCE), Paint);
    Require(rows == 47, "all independent script completion, insertion, comment and protection operations run");
}
void Workflows(EditorFrame* frame) {
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
        TestLanguages::ScriptSchemeHistory(frame, fixture, Paint);
        TestLanguages::SaveNaming(frame, fixture, path);
        Require(frame->NewDocument(), "clean script permits New");
        Require(frame->OpenRestartDocument(TestFiles::Filename(path), EncodingChoice{FileEncoding::Utf8, true},
                                           fixture.language, false, 0, 0)
                            .status == OpenStatus::Opened &&
                    editor->Send(GetLexer) == fixture.lexer && frame->DocumentHasUtf8Bom(),
                "script restart restores override and BOM identity");
    }
    TestLanguages::NativeFiles(frame, Schemes());
}
class ScriptsApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_SCRIPTS_DPI96"))
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
                Workflows(frame);
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
wxIMPLEMENT_APP(ScriptsApp);
