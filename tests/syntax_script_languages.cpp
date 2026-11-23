#include "diagnostics/print.h"
#include "syntax_language_test_helpers.h"
#include "native_paint_helpers.h"
#include "native_completion_helpers.h"
#include "native_renderer_helpers.h"
#include "settings/settings_store.h"
#include "native_dialog_helpers.h"
#include "native_tree_input.h"
#include <wx/wx.h>
#include <wx/modalhook.h>
#include <wx/checkbox.h>
#include <array>
#include <fstream>
#include <sstream>

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
    for (const auto& fixture : Schemes()) {
        wxTimer timer;
        std::exception_ptr failure;
        int shown = 0;
        timer.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
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
                    for (const auto* name : {"AutoHotkey Script", "Batch File", "PowerShell Script", "Java Source"}) {
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
class RectangleFeedback final : public wxModalDialogHook {
public:
    RectangleFeedback() { Register(); }
    int Enter(wxDialog*) override {
        ++count;
        return wxID_OK;
    }
    int count = 0;
};
void Operations(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    std::ifstream input(std::filesystem::path(SCRIPT_OPERATIONS_REFERENCE));
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
        std::println(stderr, "script operation {} {} {}", fields[0].c_str(), fields[1].c_str(), fields[2].c_str());
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
        Paint(frame);
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
        Paint(frame);
        TestLanguages::FoldCommands(frame);
        Require(frame->ReloadCurrentFile().status == OpenStatus::Opened && editor->Send(GetLexer) == fixture.lexer,
                "script reload restores automatic lexer");
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
