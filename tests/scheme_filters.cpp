#include "diagnostics/print.h"
#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include "native_search_helpers.h"
#include "status_helpers.h"
#include "file_helpers.h"
#include <wx/timer.h>
#include <cstdio>
#include <print>
#include <functional>
#include <vector>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class FileFilters final : public wxTimer {
public:
    std::vector<wxString> expected;
    wxString error;
    int shown = 0;
    FileFilters() { Start(40); }
    void Check(const std::vector<wxString>& types, const wxString& current) {
        Stop();
        ++shown;
        try {
            Require(types.size() == expected.size(), "native filter count matches independent original capture");
            for (size_t index = 0; index < types.size(); ++index) {
                Require(types[index].BeforeFirst('(').Trim() == expected[index],
                        "native current/text/favorite filters match independent original order without duplicates");
                if (expected[index] == "All Files") {
#ifdef __WXMSW__
                    Require(types[index] == "All Files (*.*)", "native All Files caption exactly matches the original "
                                                               "Windows wildcard without duplicate suffixes");
#else
                    Require(types[index] == "All Files (*)",
                            "native All Files caption retains the Unix all-file wildcard without duplicate suffixes");
#endif
                }
            }
            Require(current == types.front(), "original Open/Save selects its first native filter");
        } catch (const std::exception& failure) {
            error = failure.what();
            for (const auto& type : types)
                std::println(stderr, "native filter: {}", type.utf8_str().data());
        }
    }
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<FileFilters*>(data);
                if (!IsWindowVisible(window)) return TRUE;
                wchar_t title[128]{};
                GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                if (wxString(title) != "Open file" && wxString(title) != "Save As") return TRUE;
                const auto types = NativeDialogs::FileTypes(window);
                if (types.empty()) return TRUE;
                driver.Check(types, NativeDialogs::CurrentFileType(window));
                NativeDialogs::ChooseFile(window, {});
                return FALSE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            const bool visible = gtk_widget_get_visible(widget);
            const bool fileChooser = GTK_IS_FILE_CHOOSER(widget);
            if (!visible || !fileChooser) continue;
            const auto types = NativeDialogs::FileTypes(widget);
            if (types.empty()) continue;
            Check(types, NativeDialogs::CurrentFileType(widget));
            NativeDialogs::Respond(widget, GTK_RESPONSE_CANCEL);
            break;
        }
        g_list_free(windows);
#endif
    }
};
void Case(const wxString& favorites, std::initializer_list<wxString> open, std::initializer_list<wxString> save) {
    SettingsStore settings(TestFiles::Filename(TestFiles::TemporaryDirectory("scheme-filters.ini")));
    settings.values = OriginalSettings{};
    settings.values.defaultScheme = 32;
    settings.values.favoriteSchemes = favorites;
    auto* frame = new EditorFrame(&settings);
    frame->Show();
    auto* editor = frame->Editor();
    editor->Send(Scintilla::Message::AddText, 4, reinterpret_cast<intptr_t>("data"));
    editor->Send(Scintilla::Message::SetSavePoint);
    editor->Send(Scintilla::Message::SetSel, 1, 3);
    const auto undo = editor->Send(Scintilla::Message::GetUndoActions);
    for (const bool opening : {true, false}) {
        FileFilters driver;
        driver.expected.assign(opening ? open : save);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", opening ? "Open..." : "Save As..."));
        driver.Stop();
        Require(driver.error.empty(), driver.error.utf8_str());
        Require(driver.shown == 1, "real Open/Save command exposes exactly one native filter chooser");
        Require(TestEditor::Text(editor) == "data" && !frame->DocumentIsModified() && frame->DocumentPath().empty() &&
                    editor->Send(Scintilla::Message::GetAnchor) == 1 &&
                    editor->Send(Scintilla::Message::GetCurrentPos) == 3 &&
                    editor->Send(Scintilla::Message::GetUndoActions) == undo &&
                    StatusMetadata(frame).Contains("Python"),
                "native filter observation/cancel preserves exact document, scheme, selection and history");
    }
    frame->Destroy();
}
class FiltersApp final : public wxApp {
public:
    bool OnInit() override {
        auto* lifetime = new wxFrame(nullptr, wxID_ANY, "Native scheme filters");
        CallAfter([this, lifetime] {
            try {
                // Frozen original-filter-summary.json: original 2nd Text File is
                // not delivered; existing compact application captions are retained.
                Case("67 1 32", {"All Files", "Python", "Plain Text", "Rust", "C/C++"},
                     {"Python", "Plain Text", "Rust", "C/C++", "All Files"});
                Case("32 1 67", {"All Files", "Python", "Plain Text", "C/C++", "Rust"},
                     {"Python", "Plain Text", "C/C++", "Rust", "All Files"});
                Case("67 1", {"All Files", "Python", "Plain Text", "Rust", "C/C++"},
                     {"Python", "Plain Text", "Rust", "C/C++", "All Files"});
                // Independent #268 original-filters-01 capture, excluding the
                // undelivered original 2nd Text File as in the cases above.
                Case("50 48 45", {"All Files", "Python", "Plain Text", "Diff File", "Ini Config File", "Makefile"},
                     {"Python", "Plain Text", "Diff File", "Ini Config File", "Makefile", "All Files"});
                Case("33 31 35", {"All Files", "Python", "Plain Text", "Ruby Script", "Perl Script", "Tcl Script"},
                     {"Python", "Plain Text", "Ruby Script", "Perl Script", "Tcl Script", "All Files"});
                Case({}, {"All Files", "Python", "Plain Text"}, {"Python", "Plain Text", "All Files"});
                result_ = 0;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
            }
            lifetime->Destroy();
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
wxIMPLEMENT_APP(FiltersApp);
