#include "syntax/syntax_detection.h"
#include "diagnostics/print.h"
#include "syntax_language_test_helpers.h"
#include <print>
#include "native_paint_helpers.h"
#include "native_completion_helpers.h"
#include "native_renderer_helpers.h"
#include "settings/settings_store.h"
#include "tab_settings_helpers.h"
#include <wx/wx.h>
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
            "native rendering styles every byte of the visible original fixture");
}

std::array<TestLanguages::SchemeFixture, 3> Schemes() {
    return {
        {{"Java Source", LanguageForPath("sample.java"), SCLEX_JAVA, "java",
          "class Sample<T> {\n  void run() {\n    System.out.println(42);\n  }\n}\n", "//", "/*", "*/", "Java Source"},
         {"C# Source", LanguageForPath("sample.cs"), SCLEX_CSHARP, "cs",
          "class Sample<T> {\n  void Run() {\n    Console.WriteLine(42);\n  }\n}\n", "//", "/*", "*/", "C# Source"},
         {"Kotlin Source", LanguageForPath("sample.kt"), SCLEX_KOTLIN, "kt",
          "class Sample<T> {\n  fun run() {\n    println(42)\n  }\n}\n", "//", "/*", "*/", "Kotlin Source"}}};
}

void OriginalOracle(EditorFrame* frame) {
    const auto rows = TestLanguages::OriginalStyleFoldStates(
        frame, std::filesystem::path(JAVA_LANGUAGES_REFERENCE),
        [frame](const std::string& name, const std::string& text) {
            const auto path = TestFiles::TemporaryDirectory(name);
            TestFiles::WriteBytes(path, text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                    "independent original language fixture opens");
        },
        Paint, "every original Java/C#/Kotlin style byte matches", "every original Java/C#/Kotlin fold level matches");
    Require(rows == 24, "six complete/incomplete fixtures each replay edit, Undo and Redo");
    std::puts("PASS: 24 independent original full style/fold/edit/Undo/Redo states");
}

void Operations(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    std::ifstream input(std::filesystem::path(JAVA_OPERATIONS_REFERENCE));
    Require(input.is_open(), "independent original operation fixtures open");
    std::string line;
    size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream row(line);
        constexpr size_t InitialCaretColumn = 13;
        std::array<std::string, InitialCaretColumn + 1> fields;
        for (auto& field : fields)
            std::getline(row, field, '\t');
        std::println(stderr, "original operation {} {} {}", fields[0].c_str(), fields[1].c_str(), fields[2].c_str());
        editor->Send(AutoCCancel);
        Choose(frame, (fields[0] + " Source").c_str());
        const auto before = TestLanguages::Unhex(fields[2]);
        const auto initialCaret = fields[InitialCaretColumn].empty() ? static_cast<intptr_t>(before.size())
                                                                     : std::stol(fields[InitialCaretColumn]);
        TestEditor::ResetStream(editor, before.c_str(), initialCaret, initialCaret);
        Paint(frame);
        const auto character = std::stoi(fields[3]);
        if (character) {
            wxKeyEvent event(wxEVT_CHAR);
            event.m_keyCode = character;
            event.m_uniChar = character;
            event.SetEventObject(editor);
            editor->GetEventHandler()->ProcessEvent(event);
            wxYield();
        } else {
            TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Complete Word"));
        }
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(fields[4]),
                "manual/automatic completion and typing retain original bytes");
        TestEditor::RequireSelection(editor, std::stol(fields[5]), std::stol(fields[6]),
                                     "original completion/typing caret and anchor");
        std::vector<std::string> expected;
        std::istringstream words(fields[7]);
        std::string word;
        while (std::getline(words, word, ','))
            expected.push_back(TestLanguages::Unhex(word));
        const auto actual = NativeCompletion::Candidates(editor);
        if (actual != expected) {
            for (const auto& candidate : actual)
                std::println(stderr, "actual candidate [{}]", candidate.c_str());
        }
        Require(actual == expected, "manual/automatic native completion order matches independent original");
        if (!expected.empty() && fields[1] == "completion") {
            editor->Send(AutoCSelect, 0, reinterpret_cast<intptr_t>(expected.front().c_str()));
#ifdef __WXMSW__
            NativeCompletion::Key(editor, VK_RETURN);
#else
            NativeCompletion::Key(editor, GDK_KEY_Return);
#endif
        }
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(fields[8]), "native Enter confirms original bytes");
        TestEditor::RequireSelection(editor, std::stol(fields[9]), std::stol(fields[10]),
                                     "native Enter retains original caret");
        editor->Send(AutoCCancel);
        TestSyntax::Command(frame, wxID_UNDO);
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(fields[11]), "original operation Undo boundary");
        TestSyntax::Command(frame, wxID_REDO);
        Require(TestEditor::Text(editor) == TestLanguages::Unhex(fields[12]), "original operation Redo bytes");
        editor->Send(SetSavePoint);
        ++rows;
    }
    Require(rows == 23, "all original language completion and brace/quote operations run");
    std::puts("PASS: 23 independent original manual/automatic/documentation completion, generic braces and raw/escaped "
              "quote cases");
}

void Workflows(EditorFrame* frame, SettingsStore& settings) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const auto fontSize =
        editor->Send(StyleGetSizeFractional, static_cast<uintptr_t>(Scintilla::StylesCommon::Default));
    for (const auto& fixture : Schemes()) {
        editor->Send(SetSavePoint);
        const auto path = TestFiles::TemporaryDirectory(std::string("workflow.") + fixture.extension);
        TestFiles::WriteBytes(path, fixture.text);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                "complete language workflow opens");
        Paint(frame);
        Require(editor->Send(GetTabWidth) == 7 && editor->Send(GetIndent) == 3 && !editor->Send(GetUseTabs),
                "original language defaults inherit global indentation");
        TestLanguages::ResumeHighlighting(frame, fixture, path, fontSize);
        editor->Send(SetSel, 9, 3);
        editor->Send(ReplaceSel, 0, reinterpret_cast<intptr_t>("changed"));
        const auto edited = TestEditor::Text(editor);
        const auto anchor = editor->Send(GetAnchor);
        const auto caret = editor->Send(GetCurrentPos);
        Choose(frame, "Plain Text");
        Choose(frame, fixture.name);
        Require(TestEditor::Text(editor) == edited && editor->Send(CanUndo), "scheme changes retain edits and history");
        TestEditor::RequireSelection(editor, anchor, caret, "scheme changes retain selection");
        TestSyntax::Command(frame, wxID_UNDO);
        Require(TestEditor::Text(editor) == fixture.text && editor->Send(CanRedo), "Undo survives scheme changes");
        TestSyntax::Command(frame, wxID_REDO);
        Require(TestEditor::Text(editor) == edited, "Redo survives scheme changes");
        TestSyntax::Command(frame, wxID_UNDO);
        Choose(frame, "Automatic");
        Paint(frame);
        TestLanguages::FoldCommands(frame);
        Require(frame->ReloadCurrentFile().status == OpenStatus::Opened && editor->Send(GetLexer) == fixture.lexer,
                "native reload restores the automatic dedicated lexer");
        TestLanguages::CommentCommands(frame, fixture, "original language block comments use slash-star delimiters");
        TabDialog::Show(frame, [](wxDialog* dialog) {
            Require(TabDialog::Option(dialog, "Use Global Tab Settings")->GetValue(), "original tabs inherit global");
            TabDialog::Toggle(TabDialog::Option(dialog, "Use Global Tab Settings"), false);
            Require(TabDialog::Width(dialog, "Scheme Tab Width")->GetValue() == 4 &&
                        TabDialog::Width(dialog, "Scheme Indentation Width")->GetValue() == 4,
                    "native dialog preserves original language 4/4 independent defaults");
            TabDialog::Width(dialog, "Scheme Tab Width")->SetValue(5);
            TabDialog::Width(dialog, "Scheme Indentation Width")->SetValue(2);
        });
        SettingsStore reloaded(settings.Path());
        const auto& saved = reloaded.values.schemeTabs[static_cast<size_t>(std::to_underlying(fixture.language))];
        Require(saved.tabWidth == 5 && saved.indentWidth == 2 && !saved.useGlobal,
                "reloaded original language settings retain native dialog changes");
        const auto bytes = TestFiles::Bytes(TestFiles::Utf8Path(settings.Path().ToStdString(wxConvUTF8)));
        std::string ini;
        for (size_t byte = 2; byte + 1 < bytes.size(); byte += 2)
            ini += bytes[byte];
        Require(ini.contains(std::string("[") + fixture.settingsSection + "]"),
                "settings persist in the literal original language section");
        settings.values.schemeTabs[static_cast<size_t>(std::to_underlying(fixture.language))] = OriginalTabs{};
        Require(settings.SaveSchemeTabs(fixture.language), "restore original tab defaults");
        TestLanguages::SaveNaming(frame, fixture, path);
        Require(frame->NewDocument(), "new document after save remains available");
        Require(frame->OpenRestartDocument(TestFiles::Filename(path), EncodingChoice{FileEncoding::Utf8, true},
                                           fixture.language, false, 0, 0)
                            .status == OpenStatus::Opened &&
                    editor->Send(GetLexer) == fixture.lexer && frame->DocumentHasUtf8Bom(),
                "restart document restores original scheme override and encoding/BOM identity");
        for (const auto& [encoding, encoded] :
             {std::pair{FileEncoding::Gbk, std::string("// \x{d6}\x{d0}\r\n")},
              {FileEncoding::Utf16Le, std::string("\x{ff}\x{fe}/\0/\0 \0\x{2d}\x{4e}\r\0\n\0", 14)}}) {
            const auto encodedPath = TestFiles::TemporaryDirectory(std::string("encoded.") + fixture.extension);
            TestFiles::WriteBytes(encodedPath, encoded);
            Require(frame->Open(TestFiles::Filename(encodedPath), encoding).status == OpenStatus::Opened,
                    "encoded language document opens through public source encoding selection");
            const auto eol = editor->Send(GetEOLMode);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(encodedPath) == encoded &&
                        frame->DocumentEncoding() == encoding && editor->Send(GetEOLMode) == eol &&
                        editor->Send(GetLexer) == fixture.lexer,
                    "each original language preserves GBK/UTF-16LE, BOM, EOL and saved bytes");
            Require(frame->ReloadCurrentFile().status == OpenStatus::Opened && frame->DocumentEncoding() == encoding &&
                        editor->Send(GetEOLMode) == eol,
                    "reload preserves each language's selected source encoding identity");
        }
    }
    TestLanguages::NativeFiles(frame, Schemes());
    std::puts(
        "PASS: native file/edit/filter/selection/history/fold/font/comment/indentation/settings/restart workflows");
}

void SchemeSwitches(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    for (const auto& fixture : Schemes()) {
        Choose(frame, "TypeScript");
        Choose(frame, fixture.name);
        Require(editor->Send(GetPropertyInt, reinterpret_cast<uintptr_t>("lexer.lang")) == 0,
                "language installation clears the preceding dialect property");
        TestEditor::ResetStream(editor, "alpha alpine al", 15, 15);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Complete Word"));
        Require(editor->Send(AutoCActive), "document word candidates open a real native popup");
        Choose(frame, "Plain Text");
        Require(!editor->Send(AutoCActive) && TestEditor::Text(editor) == "alpha alpine al" && !editor->Send(CanUndo),
                "switching away clears the completion popup without modifying bytes/history");
        TestEditor::RequireSelection(editor, 15, 15, "switching away retains caret");
        TestEditor::ResetStream(editor, "pri", 3, 3);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Complete Word"));
        Require(!editor->Send(AutoCActive) && TestEditor::Text(editor) == "pri",
                "Plain Text does not retain Java/C#/Kotlin completion groups");
        Choose(frame, fixture.name);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Complete Word"));
        Require(editor->Send(AutoCActive) || TestEditor::Text(editor) != "pri",
                "returning to the language restores its original completion groups");
        editor->Send(AutoCCancel);
        editor->Send(SetSavePoint);
    }
    std::puts("PASS: scheme switches clear dialect properties, completion popups and keyword groups");
}

void Recognition(EditorFrame* frame) {
    for (const auto& [suffix, lexer] : {std::pair{"java", SCLEX_JAVA},
                                        {"jad", SCLEX_JAVA},
                                        {"aidl", SCLEX_JAVA},
                                        {"bsh", SCLEX_JAVA},
                                        {"cs", SCLEX_CSHARP},
                                        {"csx", SCLEX_CSHARP},
                                        {"vala", SCLEX_CSHARP},
                                        {"vapi", SCLEX_CSHARP},
                                        {"cake", SCLEX_CSHARP},
                                        {"kt", SCLEX_KOTLIN},
                                        {"kts", SCLEX_KOTLIN},
                                        {"KTS", SCLEX_KOTLIN}}) {
        const auto path = TestFiles::TemporaryDirectory(std::string("recognition.") + suffix);
        TestFiles::WriteBytes(path, "// original language\n");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                "native language extension opens successfully");
        Require(frame->Editor()->Send(Message::GetLexer) == lexer,
                "every original Java/C#/Kotlin extension selects its dedicated lexer");
    }
    std::puts("PASS: original Java/C#/Kotlin secondary and script extension recognition");
}

class LanguagesApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_JAVA_DPI96"))
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "language fixture enters process-local complementary 96 DPI mode");
#endif
        wxInitAllImageHandlers();
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory("Notepad4.ini")));
        settings_->values.warnLineEndings = false;
        settings_->values.detectIndentation = false;
        settings_->values.wordWrap = false;
        settings_->values.globalTabs = {7, 3, true};
        auto* frame = new EditorFrame(settings_.get());
        TestRenderer::Select(frame->Editor());
        frame->Show();
        const bool completionOnly = wxGetenv("WXNOTEPAD_JAVA_COMPLETION_ONLY") != nullptr;
        NativeKeys::AfterPresentation(frame, [this, frame, completionOnly](bool presented) {
            try {
                Require(presented, "owned native frame must be presented before language workflows");
                if (!completionOnly) {
                    Recognition(frame);
                    OriginalOracle(frame);
                }
                Operations(frame);
                if (!completionOnly) {
                    SchemeSwitches(frame);
                    Workflows(frame, *settings_);
                }
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
wxIMPLEMENT_APP(LanguagesApp);
