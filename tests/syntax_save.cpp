#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include <SciLexer.h>
#include <wx/wx.h>
#include <wx/statusbr.h>
#include <wx/timer.h>
#include "native_dialog_helpers.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "file_helpers.h"

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
using TestFiles::Bytes;
using TestFiles::Filename;
std::string Text(EditorControl* editor) {
    const auto length = static_cast<size_t>(editor->Send(Message::GetLength));
    std::string text(length + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
void Paint(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto visiblePrefix = std::min(editor->Send(Message::GetLength), intptr_t{128});
    // Observe the normal style pass, including asynchronous Wayland frames.
    for (int attempt = 0; attempt < 100; ++attempt) {
        wxYield();
        editor->Refresh();
        editor->Update();
        wxYield();
        if (editor->Send(Message::GetEndStyled) >= visiblePrefix) return;
        wxMilliSleep(10);
    }
}
void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "real editor command must be handled");
    Paint(frame);
}
// Drive only modal widgets owned by this test, without global input or focus.
class SaveChooser final : public wxTimer {
public:
    SaveChooser(const wxString& path, SyntaxLanguage current, const wxString& type)
        : path_(path), type_(type), current_(LanguageName(current)) {
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
            if (!gtk_widget_get_visible(widget) || !GTK_IS_FILE_CHOOSER(widget)) continue;
            if (!stage_++) {
                ++choosers;
                const auto* current = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(widget));
                GSList* filters = gtk_file_chooser_list_filters(GTK_FILE_CHOOSER(widget));
                initialType = wxString::FromUTF8(gtk_file_filter_get_name(GTK_FILE_FILTER(filters->data)));
                Require(g_slist_length(filters) == 5 && current == filters->data &&
                            initialType.StartsWith(current_ + " (") &&
                            wxString::FromUTF8(gtk_file_filter_get_name(
                                GTK_FILE_FILTER(g_slist_last(filters)->data))) == "All Files (*)",
                        "native save types must initially select current scheme first, with All Files last");
                GSList* remaining = filters->next;
                for (const auto* name : {"Plain Text", "C/C++", "Python", "JSON"}) {
                    if (current_ == name) continue;
                    Require(remaining && wxString::FromUTF8(gtk_file_filter_get_name(GTK_FILE_FILTER(remaining->data)))
                                             .StartsWith(wxString(name) + " ("),
                            "remaining native file types must follow catalog order without duplicates");
                    remaining = remaining->next;
                }
                g_slist_free(filters);
                if (!type_.empty()) NativeDialogs::ChooseFileType(widget, type_);
                NativeDialogs::ChooseFile(widget, path_);
            } else if (stage_ == 2 && !path_.empty())
                NativeDialogs::Respond(widget, GTK_RESPONSE_ACCEPT);
        }
        g_list_free(windows);
#endif
    }

private:
    wxString path_;
    wxString type_;
    wxString current_;
    int stage_ = 0;
};
wxString SaveCommand(EditorFrame* frame, int id, const wxString& path = {}, const wxString& type = {}) {
    const auto lexer = frame->Editor()->Send(Message::GetLexer);
    const auto current = lexer == SCLEX_CPP      ? SyntaxLanguage::Cpp
                         : lexer == SCLEX_PYTHON ? SyntaxLanguage::Python
                         : lexer == SCLEX_JSON   ? SyntaxLanguage::Json
                                                 : SyntaxLanguage::PlainText;
    SaveChooser chooser(path, current, type);
    Command(frame, id);
    chooser.Stop();
    Require(chooser.choosers == 1, "real save command must use its native chooser");
    return chooser.initialType;
}
void Language(EditorFrame* frame, bool cpp) {
    Paint(frame);
    Require(frame->Editor()->Send(Message::GetStyleIndexAt, 0) == (cpp ? SCE_C_WORD2 : 0),
            "saved extension must select the expected public syntax style");
    Require(frame->GetStatusBar()->GetStatusText().Contains(cpp ? "C/C++" : "Plain Text"),
            "status must follow the effective language");
}
void Choose(EditorFrame* frame, const char* label) {
    const auto id = frame->GetMenuBar()->FindMenuItem("Language", label);
    Require(id != wxNOT_FOUND, "supported language choice must exist");
    Command(frame, id);
    Require(frame->GetMenuBar()->IsChecked(id) && frame->GetStatusBar()->GetStatusText().Contains("override"),
            "manual language command must check its item and display the override");
}
void Automatic(EditorFrame* frame, const char* language, int lexer) {
    Paint(frame);
    const auto status = frame->GetStatusBar()->GetStatusText();
    Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Language", "Automatic")) &&
                status.Contains(language) && !status.Contains("override") &&
                frame->Editor()->Send(Message::GetLexer) == lexer &&
                (frame->Editor()->Send(Message::GetMarginWidthN, 1) != 0) == (lexer != SCLEX_NULL),
            "successful naming must select Automatic and install the filename's lexer and fold capability");
}
void Selected(EditorFrame* frame, const char* language, int lexer) {
    Paint(frame);
    Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Language", language)) &&
                frame->GetStatusBar()->GetStatusText().Contains(wxString(language) + " (override)") &&
                frame->Editor()->Send(Message::GetLexer) == lexer,
            "specific save type must install its language and reflect the explicit choice in menu and status");
}
class SyntaxSaveApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-syntax-save-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto cpp = directory / "first.CpP";
                const auto plain = directory / "plain.txt";
                const auto back = directory / "back.cpp";
                auto* editor = frame->Editor();
                const std::string source = "int value; // 中文\r\nint next;\n";
                editor->Send(Message::AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
                editor->Send(Message::SetSel, 7, 2);
                const auto eol = editor->Send(Message::GetEOLMode);
                SaveCommand(frame, wxID_SAVE, Filename(cpp));
                Language(frame, true);
                Require(NativeDialogs::PathEquals(frame->DocumentPath(), Filename(cpp)) && Bytes(cpp) == source &&
                            Text(editor) == source && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::CanUndo) && editor->Send(Message::GetAnchor) == 7 &&
                            editor->Send(Message::GetCurrentPos) == 2 && editor->Send(Message::GetEOLMode) == eol &&
                            frame->DocumentEncoding() == FileEncoding::Utf8,
                        "first Save must preserve text, selection, EOL, encoding and history at its save point");
                Command(frame, wxID_UNDO);
                Require(Text(editor).empty() && editor->Send(Message::GetModify),
                        "one Undo after first Save must undo the original insertion, not syntax");
                Command(frame, wxID_REDO);
                Require(Text(editor) == source && !editor->Send(Message::GetModify),
                        "Redo must restore the saved text and save point");
                std::puts("PASS: native first Save selects C++ without an extra history action");

                // Load BOM/mixed-EOL bytes before the extension transitions.
                const std::string bytes = "\xef\xbb\xbf" + source;
                std::ofstream(cpp, std::ios::binary) << bytes;
                Require(frame->Open(Filename(cpp)).status == OpenStatus::Opened, "BOM fixture must open");
                Require(frame->SaveAs(Filename(directory / "default.txt")).status == SaveStatus::Saved,
                        "default Save As must save using the current scheme");
                Language(frame, true);
                Require(frame->GetStatusBar()->GetStatusText().Contains("override"),
                        "named C++ default Save As to .txt must explicitly retain C++");
                Require(frame->Open(Filename(cpp)).status == OpenStatus::Opened, "BOM fixture must reopen");
                editor->Send(Message::GotoPos, source.size());
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("x"));
                Command(frame, wxID_UNDO);
                editor->Send(Message::SetSel, 8, 1);
                const auto loadedEol = editor->Send(Message::GetEOLMode);
                SaveCommand(frame, wxID_SAVEAS, Filename(plain), "All Files");
                Language(frame, false);
                Require(Bytes(plain) == bytes && Bytes(cpp) == bytes && Text(editor) == source &&
                            frame->DocumentHasUtf8Bom() && editor->Send(Message::CanRedo) &&
                            !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::GetAnchor) == 8 && editor->Send(Message::GetCurrentPos) == 1 &&
                            editor->Send(Message::GetEOLMode) == loadedEol,
                        "C++ to Plain Text Save As must preserve BOM/EOL, selection and the pending redo branch");
                SaveCommand(frame, wxID_SAVEAS, Filename(back), "All Files");
                Language(frame, true);
                Require(Bytes(back) == bytes && editor->Send(Message::CanRedo) && !editor->Send(Message::CanUndo),
                        "Plain Text to C++ Save As must retain exact bytes and redo history");
                Command(frame, wxID_REDO);
                Require(Text(editor) == source + "x" && editor->Send(Message::GetModify),
                        "Save As must not discard pending Redo");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == source && !editor->Send(Message::GetModify),
                        "Undo must return to the new saved path's save point");
                std::puts("PASS: native Save As switches both directions and preserves BOM, mixed EOL and Redo");

                editor->Send(Message::SetSel, 9, 3);
                editor->Send(Message::StyleSetFore, SCE_C_WORD2, 0x123456);
                Command(frame, wxID_SAVE);
                Require(editor->Send(Message::StyleGetFore, SCE_C_WORD2) == 0x123456 &&
                            editor->Send(Message::GetAnchor) == 9 && editor->Send(Message::GetCurrentPos) == 3,
                        "ordinary Save to the same path must not reinstall the scheme or move selection");
                Require(frame->SaveAs(Filename(back)).status == SaveStatus::Saved &&
                            editor->Send(Message::StyleGetFore, SCE_C_WORD2) == 0x123456,
                        "successful Save As to the unchanged path must not reinstall the scheme");
                SaveCommand(frame, wxID_SAVEAS, {}, "JSON");
                Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                            frame->SaveAs(Filename(directory / "missing" / "failed.txt"), {}, SyntaxLanguage::Python)
                                    .status == SaveStatus::Failed &&
                            frame->DocumentPath() == Filename(back) && Bytes(back) == bytes && Text(editor) == source &&
                            frame->DocumentHasUtf8Bom() && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::CanRedo) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::GetAnchor) == 9 && editor->Send(Message::GetCurrentPos) == 3 &&
                            editor->Send(Message::StyleGetFore, SCE_C_WORD2) == 0x123456,
                        "chooser cancellation and write failure must preserve path, scheme and document state");
                Language(frame, true);
                std::puts("PASS: unchanged-path saves, cancelled chooser and failed write preserve scheme and state");

                struct NamingCase {
                    const char* manual;
                    const char* extension;
                    const char* automatic;
                    int lexer;
                };
                for (const auto& naming : {NamingCase{"C/C++", "PyW", "Python", SCLEX_PYTHON},
                                           NamingCase{"Python", "JsOn", "JSON", SCLEX_JSON},
                                           NamingCase{"JSON", "unknown", "Plain Text", SCLEX_NULL},
                                           NamingCase{"Plain Text", "CxX", "C/C++", SCLEX_CPP}}) {
                    Command(frame, wxID_NEW);
                    editor->Send(Message::AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
                    editor->Send(Message::SetSel, 9, 3);
                    Choose(frame, naming.manual);
                    const auto first = directory / (std::string("matrix-first.") + naming.extension);
                    const auto renamed = directory / (std::string("matrix-renamed.") + naming.extension);
                    Require(frame->SaveFirst(Filename(first), {}, SaveType::AllFiles).status == SaveStatus::Saved,
                            "first Save of every manual choice must succeed");
                    Automatic(frame, naming.automatic, naming.lexer);
                    Require(Bytes(first) == source && Text(editor) == source && !editor->Send(Message::GetModify) &&
                                editor->Send(Message::CanUndo) && editor->Send(Message::GetAnchor) == 9 &&
                                editor->Send(Message::GetCurrentPos) == 3,
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
                                !editor->Send(Message::GetModify) && editor->Send(Message::CanUndo) &&
                                editor->Send(Message::GetAnchor) == 9 && editor->Send(Message::GetCurrentPos) == 3,
                            "both explicit Save As forms must preserve exact bytes, selection and history");
                }
                std::puts(
                    "PASS: explicit All Files reidentifies every manual language on first Save and all Save As forms");

                Command(frame, wxID_NEW);
                Choose(frame, "Plain Text");
                editor->Send(Message::AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
                const auto exception = directory / "untitled-plain.cpp";
                SaveCommand(frame, wxID_SAVE, Filename(exception), "Plain Text");
                Automatic(frame, "C/C++", SCLEX_CPP);
                Require(Bytes(exception) == source, "untitled Plain exception must preserve exact bytes");

                Command(frame, wxID_NEW);
                Choose(frame, "C/C++");
                editor->Send(Message::AddText, source.size(), reinterpret_cast<intptr_t>(source.data()));
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
                    Require(Bytes(automaticPython) == source && Text(editor) == source &&
                                editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                            "specific types on the same path must retain bytes, history and save point");
                }
                std::puts("PASS: each specific type applies even on an identical path");

                const auto unknown = directory / "current.odd";
                std::ofstream(unknown, std::ios::binary) << source;
                Require(frame->Open(Filename(unknown)).status == OpenStatus::Opened,
                        "custom extension fixture must open");
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SyntaxSaveApp);
