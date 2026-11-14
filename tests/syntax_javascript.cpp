#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "syntax_test_helpers.h"
#include "file_helpers.h"
#include "fixtures/javascript/original.h"
#include "settings/settings_store.h"
#include "search/search_session.h"
#include <wx/ffile.h>
#include <SciLexer.h>
#include <wx/statusbr.h>
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <span>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Choose(EditorFrame* frame, const char* name) {
    const int id = frame->GetMenuBar()->FindMenuItem("Scheme", name);
    Require(id != wxNOT_FOUND, "native Scheme menu must offer JavaScript, JSX, TypeScript and TSX");
    TestSyntax::Command(frame, id);
    Require(frame->GetStatusBar()->GetStatusText().Contains(name), "status must identify the effective dialect");
}
void Compare(EditorFrame* frame, std::span<const int> styles, std::span<const int> folds) {
    auto* editor = frame->Editor();
    TestSyntax::Paint(frame);
    Require(editor->Send(Message::GetLength) == static_cast<intptr_t>(styles.size()),
            "fixture length must match original");
    for (size_t index = 0; index < styles.size(); ++index)
        TestSyntax::Style(editor, index, styles[index]);
    Require(editor->Send(Message::GetLineCount) == static_cast<intptr_t>(folds.size()),
            "fixture line count must match original");
    for (size_t line = 0; line < folds.size(); ++line) {
        const auto actual = editor->Send(Message::GetFoldLevel, line);
        if (actual != folds[line])
            std::fprintf(stderr, "line=%zu expected_fold=%d actual_fold=%zd\n", line, folds[line], actual);
        Require(actual == folds[line], "every fold level must match original full application");
    }
}
struct Fixture {
    const char* extension;
    const char* name;
    std::span<const int> styles;
    std::span<const int> folds;
    std::span<const int> editedStyles;
    std::span<const int> editedFolds;
};
using namespace JavaScriptReference;
const Fixture fixtures[] = {
    {"js", "JavaScript", js_before_styles, js_before_folds, js_edited_styles, js_edited_folds},
    {"jsx", "JSX", jsx_before_styles, jsx_before_folds, jsx_edited_styles, jsx_edited_folds},
    {"ts", "TypeScript", ts_before_styles, ts_before_folds, ts_edited_styles, ts_edited_folds},
    {"tsx", "TSX", tsx_before_styles, tsx_before_folds, tsx_edited_styles, tsx_edited_folds},
};
void Workflows(EditorFrame* frame, const std::filesystem::path& directory) {
    auto* editor = frame->Editor();
    for (const auto* extension : {"js", "jse", "jsm", "cjs", "mjs", "qs", "JS", "mJs", "jsx", "JsX", "ts", "cts", "mts",
                                  "ets", "osts", "TS", "cTs", "tsx", "TsX"}) {
        const auto path = directory / (std::string("association.") + extension);
        TestFiles::WriteBytes(path, "const value = 1;\n");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                "each original filename variant must Open");
        TestSyntax::Paint(frame);
        Require(editor->Send(Message::GetLexer) == SCLEX_JAVASCRIPT, "original extensions use the shared JS lexer");
        TestSyntax::Style(editor, 0, SCE_JS_WORD);
    }
    Choose(frame, "JSX");
    const auto destination = TestFiles::Filename(directory / "association.TsX");
    Require(frame->Open(destination).status == OpenStatus::Opened &&
                frame->GetStatusBar()->GetStatusText().Contains("JSX"),
            "same-path reopen preserves manual dialect");
    Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                frame->SaveAs(TestFiles::Filename(directory / "missing/failed.ts")).status == SaveStatus::Failed &&
                frame->GetStatusBar()->GetStatusText().Contains("JSX"),
            "failed/cancelled Save As retain the manual dialect");
    Require(frame->SaveAs(destination, {}, SyntaxLanguage::Tsx).status == SaveStatus::Saved &&
                frame->GetStatusBar()->GetStatusText().Contains("TSX"),
            "same-path Save As applies explicit TSX filter");
    Choose(frame, "JavaScript");
    Require(frame->SaveAs(destination, {}, SaveType::AllFiles).status == SaveStatus::Saved &&
                frame->GetStatusBar()->GetStatusText().Contains("TSX"),
            "All Files restores automatic extension selection");
    TestSyntax::Command(frame, wxID_NEW);
    TestEditor::Append(frame, "const first = 1;\n");
    const auto first = directory / "first.mts";
    Require(frame->SaveFirst(TestFiles::Filename(first)).status == SaveStatus::Saved &&
                frame->GetStatusBar()->GetStatusText().Contains("TypeScript") &&
                TestFiles::Bytes(first) == "const first = 1;\n",
            "first Save selects TypeScript and preserves disk bytes");
    std::puts("PASS: all original extensions, same-path reopen, Save As failure/cancel/filter and first Save");
    for (const auto* interpreter :
         {"#!/usr/bin/node\n", "#!/usr/bin/env node\n", "#!C:\\tools\\node.exe\n", "#!/usr/bin/nodejs\n"}) {
        const auto path = directory / "node-script.unknown";
        TestFiles::WriteBytes(path, std::string(interpreter) + "const value = 1;\n");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                    frame->GetStatusBar()->GetStatusText().Contains("JavaScript"),
                "original node shebang selects JavaScript");
        Choose(frame, "Plain Text");
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Automatic"));
        Require(frame->GetStatusBar()->GetStatusText().Contains("JavaScript"),
                "Automatic restores original node sniffing");
    }
}
void SharedCommands(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const auto* dialect : {"JavaScript", "JSX", "TypeScript", "TSX"}) {
        Choose(frame, dialect);
        TestEditor::ResetStream(editor, "value", 0, 5);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment"));
        Require(TestEditor::Text(editor) == "/*value*/", "shared block comment command uses original JS delimiters");
        TestSyntax::Command(frame, wxID_UNDO);
        TestSyntax::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment"));
        Require(TestEditor::Text(editor) == "// value", "shared line comment command uses original JS prefix");
        TestSyntax::Command(frame, wxID_UNDO);
        TestEditor::ResetStream(editor, "function f() {\n  return 42;\n}\n");
        TestSyntax::Paint(frame);
        editor->Send(Message::ToggleFold, 0);
        Require(!editor->Send(Message::GetLineVisible, 1), "single-block folding hides nested JS lines");
        SearchSession search(*editor);
        search.Configure({.query = "return", .replacement = "yield"});
        Require(search.Find(SearchDirection::Forward).status == FindStatus::Found &&
                    editor->Send(Message::GetLineVisible, 1),
                "shared Find reveals matched folded content");
        editor->Send(Message::ToggleFold, 0);
        const auto replaced = search.ReplaceAll();
        Require(replaced.count == 1 && TestEditor::Text(editor) == "function f() {\n  yield 42;\n}\n",
                "shared Replace All edits folded content");
        TestSyntax::Command(frame, wxID_UNDO);
        editor->Send(Message::SetSavePoint);
    }
    std::puts("PASS: shared comments, single-fold and folded Find/Replace for all four dialects");
}
void SchemeSettings(SettingsStore& settings) {
    const auto original = TestFiles::Bytes(TestFiles::Utf8Path(settings.Path().ToStdString(wxConvUTF8)));
    struct Restore {
        wxString path;
        std::string bytes;
        Restore(const wxString& restorePath, const std::string& restoreBytes)
            : path(restorePath), bytes(restoreBytes) {}
        Restore(const Restore&) = delete;
        Restore& operator=(const Restore&) = delete;
        Restore(Restore&&) = delete;
        Restore& operator=(Restore&&) = delete;
        ~Restore() {
            wxFFile file(path, "wb");
            if (file.IsOpened()) file.Write(bytes.data(), bytes.size());
        }
    } restore{settings.Path(), original};
    {
        wxFFile file(settings.Path(), "wb");
        Require(
            file.IsOpened() &&
                file.Write("[Notepad4]\n[JavaScript]\nTabWidth=3\nIndentWidth=3\nTabsAsSpaces=1\nUseGlobalTabSettings="
                           "0\n[TypeScript]\nTabWidth=6\nIndentWidth=6\nTabsAsSpaces=0\nUseGlobalTabSettings=0\n"),
            "write independent original INI fixture");
    }
    SettingsStore loaded(settings.Path());
    const auto js = static_cast<size_t>(SyntaxLanguage::JavaScript);
    const auto ts = static_cast<size_t>(SyntaxLanguage::TypeScript);
    Require(loaded.values.schemeTabs[js].tabWidth == 3 && loaded.values.schemeTabs[ts].tabWidth == 6,
            "original JS/TS sections load independently");
    loaded.values.schemeTabs[js].tabWidth = 7;
    Require(loaded.SaveSchemeTabs(SyntaxLanguage::Jsx), "JSX saves the original JavaScript section");
    loaded.values.schemeTabs[ts].tabWidth = 8;
    Require(loaded.SaveSchemeTabs(SyntaxLanguage::Tsx), "TSX saves the original TypeScript section");
    SettingsStore reloaded(settings.Path());
    Require(reloaded.values.schemeTabs[js].tabWidth == 7 && reloaded.values.schemeTabs[ts].tabWidth == 8,
            "dialect saves persist in independent canonical sections");
    std::puts("PASS: original independent JavaScript/TypeScript settings load/save through JSX/TSX aliases");
}
void TabPrecedence(EditorFrame* frame, SettingsStore& settings, const std::filesystem::path& directory) {
    auto* editor = frame->Editor();
    TestSyntax::Command(frame, wxID_NEW);
    settings.values.globalTabs = {9, 9, false};
    settings.values.schemeTabs[static_cast<size_t>(SyntaxLanguage::JavaScript)] = {3, 3, true, false};
    settings.values.schemeTabs[static_cast<size_t>(SyntaxLanguage::TypeScript)] = {6, 6, false, false};
    Choose(frame, "JSX");
    Require(editor->Send(Message::GetTabWidth) == 3 && !editor->Send(Message::GetUseTabs),
            "JSX inherits independent JavaScript settings");
    Choose(frame, "TSX");
    Require(editor->Send(Message::GetTabWidth) == 6 && editor->Send(Message::GetUseTabs),
            "TSX inherits independent TypeScript settings");
    settings.values.schemeTabs[static_cast<size_t>(SyntaxLanguage::JavaScript)].useGlobal = true;
    Choose(frame, "JSX");
    Require(editor->Send(Message::GetTabWidth) == 9 && editor->Send(Message::GetUseTabs),
            "explicit scheme inheritance uses global settings");
    const auto path = directory / "file-tabs.js";
    TestFiles::WriteBytes(path, "// tab-width: 5; indent-width: 5; indent-tabs-mode: nil\nconst value = 1;\n");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "file indentation fixture must Open");
    Require(editor->Send(Message::GetTabWidth) == 5 && !editor->Send(Message::GetUseTabs),
            "file variables override inherited global settings");
    Choose(frame, "TSX");
    Require(editor->Send(Message::GetTabWidth) == 5 && !editor->Send(Message::GetUseTabs),
            "file overrides survive dialect changes");
    std::puts("PASS: independent scheme, global inheritance and file indentation precedence");
}
class JavaScriptApp final : public wxApp {
public:
    bool OnInit() override {
        const auto ini = TestFiles::TemporaryDirectory("Notepad4.ini");
        TestFiles::WriteBytes(ini, "[Notepad4]\n");
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(ini));
        settings_->values = OriginalSettings{};
        settings_->values.detectIndentation = false;
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                const char* source = "function outer() {\n  const value = `hello ${1 + 2}`;\n  return "
                                     "/hello/.test(value) ? value.length / 2 : 0;\n}\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(source));
                Choose(frame, "JavaScript");
                Require(editor->Send(Message::GetLexer) == SCLEX_JAVASCRIPT,
                        "JavaScript must use the original shared lexer");
                TestSyntax::Style(editor, 0, SCE_JS_WORD);
                Require(editor->Send(Message::GetTabWidth) == 2 && editor->Send(Message::GetIndent) == 2 &&
                            !editor->Send(Message::GetUseTabs),
                        "JavaScript defaults to two-space indentation");
                std::puts("PASS: JavaScript native choice, original lexer and indentation");
                editor->Send(Message::SetSavePoint);
                const auto directory = TestFiles::TemporaryDirectory("javascript");
                std::filesystem::create_directory(directory);
                const auto fixtureRoot = std::filesystem::path(__FILE__).parent_path() / "fixtures/javascript";
                for (const auto& fixture : fixtures) {
                    const auto path = directory / (std::string("sample.") + fixture.extension);
                    const auto bytes = TestFiles::Bytes(fixtureRoot / path.filename());
                    TestFiles::WriteBytes(path, bytes);
                    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                            "fixture Open must succeed");
                    Require(frame->GetStatusBar()->GetStatusText().Contains(fixture.name),
                            "extension selects the original dialect");
                    Compare(frame, fixture.styles, fixture.folds);
                    Require(editor->Send(Message::GetTabWidth) == 2 && editor->Send(Message::GetIndent) == 2 &&
                                !editor->Send(Message::GetUseTabs),
                            "all JS/TS dialects default to two spaces");
                    editor->Send(Message::DeleteRange, 0, 1);
                    Compare(frame, fixture.editedStyles, fixture.editedFolds);
                    TestSyntax::Command(frame, wxID_UNDO);
                    Compare(frame, fixture.styles, fixture.folds);
                    Require(!editor->Send(Message::GetModify), "Undo restores the original save point");
                    TestSyntax::Command(frame, wxID_REDO);
                    Compare(frame, fixture.editedStyles, fixture.editedFolds);
                    TestSyntax::Command(frame, wxID_UNDO);
                    const int collapse = frame->GetMenuBar()->FindMenuItem("View", "Collapse All");
                    const int expand = frame->GetMenuBar()->FindMenuItem("View", "Expand All");
                    TestSyntax::Command(frame, collapse);
                    bool hidden = false;
                    for (size_t line = 0; line < fixture.folds.size(); ++line)
                        hidden |= !editor->Send(Message::GetLineVisible, line);
                    Require(hidden, "Collapse All must hide an original JS/TS fold");
                    TestSyntax::Command(frame, expand);
                    for (size_t line = 0; line < fixture.folds.size(); ++line)
                        Require(editor->Send(Message::GetLineVisible, line), "Expand All reveals every folded line");
                    Require(TestEditor::Text(editor) == bytes && !editor->Send(Message::GetModify),
                            "folding preserves loaded bytes and save point");
                    editor->Send(Message::SetZoom, 125);
                    const auto pointSize = editor->Send(Message::StyleGetSizeFractional,
                                                        static_cast<uintptr_t>(Scintilla::StylesCommon::Default));
                    Choose(frame, "Plain Text");
                    Choose(frame, fixture.name);
                    Compare(frame, fixture.styles, fixture.folds);
                    Require(editor->Send(Message::GetZoom) == 125 &&
                                editor->Send(Message::StyleGetSizeFractional, SCE_JS_WORD) == pointSize,
                            "scheme switches preserve native point size and zoom");
                    Require(editor->Send(Message::CanRedo), "non-editing scheme changes preserve redo");
                    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes,
                            "Save preserves independent disk bytes");
                    std::printf("PASS: original %s styles/folds, edit/Undo/Redo, folding, switching and Save\n",
                                fixture.name);
                }
                Workflows(frame, directory);
                SharedCommands(frame);
                SchemeSettings(*settings_);
                TabPrecedence(frame, *settings_, directory);
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
wxIMPLEMENT_APP(JavaScriptApp);
