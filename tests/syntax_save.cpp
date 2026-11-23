#include "diagnostics/print.h"
#include "status_helpers.h"
#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "syntax_test_helpers.h"
#include "settings/settings_store.h"
#include <memory>
#include <SciLexer.h>
#include <wx/wx.h>
#include <wx/statusbr.h>
#include <wx/timer.h>
#include "native_dialog_helpers.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <print>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "file_helpers.h"

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
using TestEditor::Text;
using TestFiles::Bytes;
using TestFiles::Filename;
using TestSyntax::Command;
using TestSyntax::Paint;
// Drive only modal widgets owned by this test, without global input or focus.
class SaveChooser final : public wxTimer {
public:
    SaveChooser(const wxString& path, SyntaxLanguage current, const wxString& type)
        : path_(path), type_(type), current_(SyntaxSettingsLanguage(current) == SyntaxLanguage::MarkdownGitHub
                                                 ? wxString("Markdown")
                                                 : LanguageName(SyntaxSettingsLanguage(current))) {
        Start(150);
    }
    int choosers = 0;
    wxString initialType;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<SaveChooser*>(data);
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                if (IsWindowVisible(window) && (wxString(title) == "Save file" || wxString(title) == "Save As") &&
                    !driver.stage_++) {
                    ++driver.choosers;
                    if (!driver.type_.empty()) NativeDialogs::ChooseFileType(window, driver.type_);
                    NativeDialogs::ChooseFile(window, driver.path_);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget)) continue;
            if (!GTK_IS_FILE_CHOOSER(widget)) continue;
            if (stage_ == 0) {
                stage_ = 1;
                ++choosers;
                VerifyFilters(widget);
                if (!type_.empty()) NativeDialogs::ChooseFileType(widget, type_);
                NativeDialogs::ChooseFile(widget, path_);
            } else if (stage_ == 1 && !path_.empty() && NativeDialogs::CanAcceptFile(widget, path_)) {
                stage_ = 2;
                NativeDialogs::Respond(widget, GTK_RESPONSE_ACCEPT);
            }
        }
        g_list_free(windows);
#endif
    }

private:
#ifdef __WXGTK__
    void VerifyFilters(GtkWidget* widget) {
        const auto* current = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(widget));
        GSList* filters = gtk_file_chooser_list_filters(GTK_FILE_CHOOSER(widget));
        initialType = wxString::FromUTF8(gtk_file_filter_get_name(GTK_FILE_FILTER(filters->data)));
        // This workflow explicitly favorites every delivered base scheme. The
        // independent original capture establishes current/text/favorite order;
        // dialect aliases remain current selections, not duplicate base filters.
        const auto baseCount = std::ranges::count_if(SyntaxCatalog(), [](const auto& definition) {
            return SyntaxSettingsLanguage(definition.language) == definition.language;
        });
        Require(g_slist_length(filters) == baseCount + 1 && current == filters->data &&
                    initialType.StartsWith(current_ + " (") &&
                    wxString::FromUTF8(gtk_file_filter_get_name(GTK_FILE_FILTER(g_slist_last(filters)->data))) ==
                        "All Files (*)",
                "native save types must initially select current scheme first, with All Files last");
        GSList* remaining = filters->next;
        for (const auto& definition : SyntaxCatalog()) {
            using enum SyntaxLanguage;
            if (SyntaxSettingsLanguage(definition.language) != definition.language) continue;
            const auto* name = definition.language == MarkdownGitHub ? "Markdown" : definition.name;
            if (current_ == name) continue;
            Require(remaining && wxString::FromUTF8(gtk_file_filter_get_name(GTK_FILE_FILTER(remaining->data)))
                                     .StartsWith(wxString(name) + " ("),
                    "remaining native file types follow fixed text then explicit favorite order without duplicates");
            remaining = remaining->next;
        }
        g_slist_free(filters);
    }
#endif
    wxString path_;
    wxString type_;
    wxString current_;
    int stage_ = 0;
};
wxString SaveCommand(EditorFrame* frame, int id, const wxString& path = {}, const wxString& type = {},
                     std::optional<SyntaxLanguage> expectedCurrent = std::nullopt) {
    using enum SyntaxLanguage;
    const auto lexer = frame->Editor()->Send(Message::GetLexer);
    auto current = PlainText;
    if (lexer == SCLEX_CPP)
        current = Cpp;
    else if (lexer == SCLEX_PYTHON)
        current = Python;
    else if (lexer == SCLEX_JSON)
        current = Json;
    if (lexer == SCLEX_JAVASCRIPT) {
        constexpr std::array dialects{JavaScript, Jsx, TypeScript, Tsx};
        const auto dialect = frame->Editor()->Send(Message::GetPropertyInt, reinterpret_cast<uintptr_t>("lexer.lang"));
        Require(dialect >= 0 && dialect < 4, "native Save chooser must retain JS dialect");
        current = dialects[dialect];
    }
    SaveChooser chooser(path, expectedCurrent.value_or(current), type);
    Command(frame, id);
    chooser.Stop();
    Require(chooser.choosers == 1, "real save command must use its native chooser");
    return chooser.initialType;
}
void Language(EditorFrame* frame, bool cpp) {
    Paint(frame);
    Require(frame->Editor()->Send(Message::GetStyleIndexAt, 0) == (cpp ? SCE_C_WORD2 : 0),
            "saved extension must select the expected public syntax style");
    Require(StatusMetadata(frame).Contains(cpp ? "C/C++" : "Plain Text"), "status must follow the effective language");
}
void Choose(EditorFrame* frame, const char* label) {
    const auto id = frame->GetMenuBar()->FindMenuItem("Scheme", label);
    Require(id != wxNOT_FOUND, "supported language choice must exist");
    Command(frame, id);
    Require(frame->GetMenuBar()->IsChecked(id) && StatusMetadata(frame).Contains("override"),
            "manual language command must check its item and display the override");
}
void Automatic(EditorFrame* frame, const char* language, int lexer) {
    Paint(frame);
    const auto status = StatusMetadata(frame);
    Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Scheme", "Automatic")) &&
                status.Contains(language) && !status.Contains("override") &&
                frame->Editor()->Send(Message::GetLexer) == lexer &&
                frame->Editor()->Send(Message::GetMarginWidthN, 2) > 0,
            "successful naming must select Automatic, install the filename's lexer and retain the folding margin");
}
void Selected(EditorFrame* frame, const char* language, int lexer) {
    Paint(frame);
    const auto checked = frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Scheme", language));
    const auto status = StatusMetadata(frame);
    const auto actualLexer = frame->Editor()->Send(Message::GetLexer);
    if (!checked || !status.Contains(wxString(language) + " (override)") || actualLexer != lexer)
        std::println(stderr, "SELECTED: language={} checked={:d} status={} expectedLexer={:d} actualLexer={:d}",
                     language, checked, status.utf8_str().data(), lexer, actualLexer);
    Require(checked && status.Contains(wxString(language) + " (override)") && actualLexer == lexer,
            "specific save type must install its language and reflect the explicit choice in menu and status");
}
void CurrentFilterReachability(EditorFrame* frame, const std::filesystem::path& directory, const std::string& source) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    editor->Send(SetSel, 7, 2);
    const auto history = editor->Send(GetUndoActions);
    Require(frame->SaveAs(Filename(directory / "reachability-seed.txt")).status == SaveStatus::Saved,
            "name the reachability document before explicit current-filter selections");
    for (const auto& definition : SyntaxCatalog()) {
        if (SyntaxSettingsLanguage(definition.language) != definition.language) continue;
        const auto label = LanguageName(definition.language).utf8_string();
        std::println("REACHABILITY: {}", label.c_str());
        Choose(frame, label.c_str());
        const auto path = directory / ("reachable-" + std::to_string(definition.original.schemeId) + "." +
                                       definition.extensions.front());
        SaveCommand(frame, wxID_SAVEAS, Filename(path), {}, definition.language);
        Selected(frame, label.c_str(), definition.lexer);
        Require(Bytes(path) == source && Text(editor) == source && editor->Send(GetAnchor) == 7 &&
                    editor->Send(GetCurrentPos) == 2 && editor->Send(GetUndoActions) == history,
                "every delivered base remains reachable through current scheme and explicit favorites");
    }
}
class SyntaxSaveApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        // Preserve every existing specific-filetype/save/codec/history scenario
        // by making its delivered base filters explicitly available, as original.
        for (const auto& definition : SyntaxCatalog()) {
            if (definition.language != SyntaxLanguage::PlainText &&
                SyntaxSettingsLanguage(definition.language) == definition.language) {
                if (!settings_->values.favoriteSchemes.empty()) settings_->values.favoriteSchemes += ' ';
                settings_->values.favoriteSchemes += wxString::Format("%d", definition.original.schemeId);
            }
        }
        settings_->values.wordWrap = false;
        // Mixed-EOL warning choices are covered by mixed_line_ending_open.
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        const bool reachability = wxGetenv("WXNOTEPAD_BASE_FILTER_REACHABILITY") != nullptr;
        CallAfter([this, frame, reachability] { RunScenarios(frame, reachability); });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    void RunScenarios(EditorFrame* frame, bool reachability) {
        const auto directory = TestFiles::TemporaryDirectory("wxnotepad-syntax-save");
        try {
            using enum Scintilla::Message;
            std::filesystem::create_directory(directory);
            const auto cpp = directory / "first.CpP";
            const auto plain = directory / "plain.txt";
            const auto back = directory / "back.cpp";
            auto* editor = frame->Editor();
            const std::string source = "int value; // 中文\r\nint next;\n";
            editor->Send(AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
            if (reachability) {
                CurrentFilterReachability(frame, directory, source);
                result_ = 0;
                std::filesystem::remove_all(directory);
                frame->Destroy();
                ExitMainLoop();
                return;
            }
            editor->Send(SetSel, 7, 2);
            const auto eol = editor->Send(GetEOLMode);
            SaveCommand(frame, wxID_SAVE, Filename(cpp));
            Language(frame, true);
            Require(NativeDialogs::PathEquals(frame->DocumentPath(), Filename(cpp)) && Bytes(cpp) == source &&
                        Text(editor) == source && !editor->Send(GetModify) && editor->Send(CanUndo) &&
                        editor->Send(GetAnchor) == 7 && editor->Send(GetCurrentPos) == 2 &&
                        editor->Send(GetEOLMode) == eol && frame->DocumentEncoding() == FileEncoding::Utf8,
                    "first Save must preserve text, selection, EOL, encoding and history at its save point");
            Command(frame, wxID_UNDO);
            Require(Text(editor).empty() && editor->Send(GetModify),
                    "one Undo after first Save must undo the original insertion, not syntax");
            Command(frame, wxID_REDO);
            Require(Text(editor) == source && !editor->Send(GetModify),
                    "Redo must restore the saved text and save point");
            std::puts("PASS: native first Save selects C++ without an extra history action");

            // Load BOM/mixed-EOL bytes before the extension transitions.
            const std::string bytes = "\x{ef}\x{bb}\x{bf}" + source;
            std::ofstream(cpp, std::ios::binary) << bytes;
            Require(frame->Open(Filename(cpp)).status == OpenStatus::Opened, "BOM fixture must open");
            Require(frame->SaveAs(Filename(directory / "default.txt")).status == SaveStatus::Saved,
                    "default Save As must save using the current scheme");
            Language(frame, true);
            Require(StatusMetadata(frame).Contains("override"),
                    "named C++ default Save As to .txt must explicitly retain C++");
            Require(frame->Open(Filename(cpp)).status == OpenStatus::Opened, "BOM fixture must reopen");
            editor->Send(GotoPos, source.size());
            editor->Send(AddText, 1, reinterpret_cast<intptr_t>("x"));
            Command(frame, wxID_UNDO);
            editor->Send(SetSel, 8, 1);
            const auto loadedEol = editor->Send(GetEOLMode);
            SaveCommand(frame, wxID_SAVEAS, Filename(plain), "All Files");
            Language(frame, false);
            Require(Bytes(plain) == bytes && Bytes(cpp) == bytes && Text(editor) == source &&
                        frame->DocumentHasUtf8Bom() && editor->Send(CanRedo) && !editor->Send(CanUndo) &&
                        !editor->Send(GetModify) && editor->Send(GetAnchor) == 8 && editor->Send(GetCurrentPos) == 1 &&
                        editor->Send(GetEOLMode) == loadedEol,
                    "C++ to Plain Text Save As must preserve BOM/EOL, selection and the pending redo branch");
            SaveCommand(frame, wxID_SAVEAS, Filename(back), "All Files");
            Language(frame, true);
            Require(Bytes(back) == bytes && editor->Send(CanRedo) && !editor->Send(CanUndo),
                    "Plain Text to C++ Save As must retain exact bytes and redo history");
            Command(frame, wxID_REDO);
            Require(Text(editor) == source + "x" && editor->Send(GetModify), "Save As must not discard pending Redo");
            Command(frame, wxID_UNDO);
            Require(Text(editor) == source && !editor->Send(GetModify),
                    "Undo must return to the new saved path's save point");
            std::puts("PASS: native Save As switches both directions and preserves BOM, mixed EOL and Redo");

            editor->Send(SetSel, 9, 3);
            editor->Send(StyleSetFore, SCE_C_WORD2, 0x123456);
            Command(frame, wxID_SAVE);
            Require(editor->Send(StyleGetFore, SCE_C_WORD2) == 0x123456 && editor->Send(GetAnchor) == 9 &&
                        editor->Send(GetCurrentPos) == 3,
                    "ordinary Save to the same path must not reinstall the scheme or move selection");
            Require(frame->SaveAs(Filename(back)).status == SaveStatus::Saved &&
                        editor->Send(StyleGetFore, SCE_C_WORD2) == 0x123456,
                    "successful Save As to the unchanged path must not reinstall the scheme");
            SaveCommand(frame, wxID_SAVEAS, {}, "JSON");
            Require(
                frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                    frame->SaveAs(Filename(directory / "missing" / "failed.txt"), {}, SyntaxLanguage::Python).status ==
                        SaveStatus::Failed &&
                    frame->DocumentPath() == Filename(back) && Bytes(back) == bytes && Text(editor) == source &&
                    frame->DocumentHasUtf8Bom() && !editor->Send(GetModify) && editor->Send(CanRedo) &&
                    !editor->Send(CanUndo) && editor->Send(GetAnchor) == 9 && editor->Send(GetCurrentPos) == 3 &&
                    editor->Send(StyleGetFore, SCE_C_WORD2) == 0x123456,
                "chooser cancellation and write failure must preserve path, scheme and document state");
            Language(frame, true);
            std::puts("PASS: unchanged-path saves, cancelled chooser and failed write preserve scheme and state");

            struct NamingCase {
                const char* manual;
                const char* extension;
                const char* automatic;
                int lexer;
            };
            for (const auto& naming :
                 {NamingCase{"C/C++", "PyW", "Python", SCLEX_PYTHON}, NamingCase{"Python", "JsOn", "JSON", SCLEX_JSON},
                  NamingCase{"JSON", "unknown", "Plain Text", SCLEX_NULL},
                  NamingCase{"Plain Text", "CxX", "C/C++", SCLEX_CPP}}) {
                Command(frame, wxID_NEW);
                editor->Send(AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
                editor->Send(SetSel, 9, 3);
                Choose(frame, naming.manual);
                const auto first = directory / (std::string("matrix-first.") + naming.extension);
                const auto renamed = directory / (std::string("matrix-renamed.") + naming.extension);
                Require(frame->SaveFirst(Filename(first), {}, SaveType::AllFiles).status == SaveStatus::Saved,
                        "first Save of every manual choice must succeed");
                Automatic(frame, naming.automatic, naming.lexer);
                Require(Bytes(first) == source && Text(editor) == source && !editor->Send(GetModify) &&
                            editor->Send(CanUndo) && editor->Send(GetAnchor) == 9 && editor->Send(GetCurrentPos) == 3,
                        "first Save must preserve independent bytes, selection and existing edit history");
                Choose(frame, naming.manual);
                Require(frame->SaveAs(Filename(renamed), {}, SaveType::AllFiles).status == SaveStatus::Saved,
                        "explicit Save As with unchanged extension must succeed");
                Automatic(frame, naming.automatic, naming.lexer);
                Choose(frame, naming.manual);
                Require(frame->SaveAs(Filename(renamed), {}, SaveType::AllFiles).status == SaveStatus::Saved,
                        "explicit Save As to the identical pathname must succeed");
                Automatic(frame, naming.automatic, naming.lexer);
                Require(Bytes(first) == source && Bytes(renamed) == source && Text(editor) == source &&
                            !editor->Send(GetModify) && editor->Send(CanUndo) && editor->Send(GetAnchor) == 9 &&
                            editor->Send(GetCurrentPos) == 3,
                        "both explicit Save As forms must preserve exact bytes, selection and history");
            }
            std::puts(
                "PASS: explicit All Files reidentifies every manual language on first Save and all Save As forms");

            Command(frame, wxID_NEW);
            Choose(frame, "Plain Text");
            editor->Send(AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
            const auto exception = directory / "untitled-plain.cpp";
            SaveCommand(frame, wxID_SAVE, Filename(exception), "Plain Text");
            Automatic(frame, "C/C++", SCLEX_CPP);
            Require(Bytes(exception) == source, "untitled Plain exception must preserve exact bytes");

            Command(frame, wxID_NEW);
            Choose(frame, "C/C++");
            editor->Send(AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
            const auto forcedPlain = directory / "forced-plain.cpp";
            SaveCommand(frame, wxID_SAVE, Filename(forcedPlain), "Plain Text");
            Selected(frame, "Plain Text", SCLEX_NULL);
            const auto namedPlain = directory / "named-plain.cpp";
            SaveCommand(frame, wxID_SAVEAS, Filename(namedPlain), "Plain Text");
            Selected(frame, "Plain Text", SCLEX_NULL);
            Require(Bytes(forcedPlain) == source && Bytes(namedPlain) == source,
                    "non-Plain current and named Plain must force selected Plain Text despite .cpp");

            Choose(frame, "C/C++");
            const auto forcedPython = directory / "forced-python.cpp";
            const auto cppType = SaveCommand(frame, wxID_SAVEAS, Filename(forcedPython), "Python");
#ifdef __WXGTK__
            Require(cppType == "C/C++ (*.c;*.cpp;*.cxx;*.cc;*.h;*.hpp;*.hxx;*.hh)",
                    "current catalog extension must not be duplicated in native filter patterns");
#endif
            Selected(frame, "Python", SCLEX_PYTHON);
            Require(Bytes(forcedPython) == source, "native Python type must preserve bytes despite .cpp");
            const auto automaticPython = directory / "automatic-python.py";
            Choose(frame, "JSON");
            SaveCommand(frame, wxID_SAVEAS, Filename(automaticPython), "All Files");
            Automatic(frame, "Python", SCLEX_PYTHON);
            Require(Bytes(automaticPython) == source, "native All Files must classify the returned .py path");
            std::puts("PASS: actual native filter selection covers untitled exception, forced types and All Files");

            for (const auto& naming : {std::pair{SyntaxLanguage::PlainText, SCLEX_NULL},
                                       {SyntaxLanguage::Cpp, SCLEX_CPP},
                                       {SyntaxLanguage::Python, SCLEX_PYTHON},
                                       {SyntaxLanguage::Json, SCLEX_JSON}}) {
                Require(frame->SaveAs(Filename(automaticPython), {}, naming.first).status == SaveStatus::Saved,
                        "specific type on identical path must save");
                Selected(frame, SyntaxFor(naming.first).name, naming.second);
                Require(Bytes(automaticPython) == source && Text(editor) == source && editor->Send(CanUndo) &&
                            !editor->Send(GetModify),
                        "specific types on the same path must retain bytes, history and save point");
            }
            std::puts("PASS: each specific type applies even on an identical path");

            const auto unknown = directory / "current.odd";
            std::ofstream(unknown, std::ios::binary) << source;
            Require(frame->Open(Filename(unknown)).status == OpenStatus::Opened, "custom extension fixture must open");
            Choose(frame, "Python");
            const auto custom = directory / "copy.any";
            const auto pythonType = SaveCommand(frame, wxID_SAVEAS, Filename(custom));
#ifdef __WXGTK__
            Require(pythonType == "Python (*.py;*.pyw;*.pyi;*.odd)",
                    "native current-scheme filter must retain the current filename extension");
#endif
            Selected(frame, "Python", SCLEX_PYTHON);
#ifdef __WXMSW__
            // The native dialog retains the initial filename's extra extension.
            const auto savedCustom = directory / "copy.any.odd";
#else
            const auto& savedCustom = custom;
#endif
            Require(frame->DocumentPath() == Filename(savedCustom) && Bytes(savedCustom) == source,
                    "default filter must force current Python and preserve native extension handling");
            std::puts("PASS: native current filter retains an extra extension and forces its current scheme");
            for (const auto* dialect : {"JavaScript", "JSX", "TypeScript", "TSX"}) {
                Choose(frame, dialect);
                const auto languagePath = directory / (std::string("native-") + dialect + ".js");
                const wxString filter = wxString(dialect).StartsWith("T") ? "TypeScript" : "JavaScript";
                SaveCommand(frame, wxID_SAVEAS, Filename(languagePath), filter);
                Selected(frame, dialect, SCLEX_JAVASCRIPT);
                Require(Text(editor) == source, "native JS/TS Save As preserves document bytes");
            }
            std::puts("PASS: actual native JavaScript/JSX/TypeScript/TSX filter selection");
            result_ = 0;
        } catch (const std::exception& error) {
            DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
        }
        std::filesystem::remove_all(directory);
        frame->Destroy();
        ExitMainLoop();
    }
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SyntaxSaveApp);
