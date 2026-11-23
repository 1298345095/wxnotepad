#include "app/editor_frame.h"
#include "settings/settings_store.h"
#include "native_key_helpers.h"
#include <wx/app.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <array>
#include <wx/menu.h>
#include <cstdio>
#include <print>
#include <stdexcept>
#ifdef __WXGTK__
#include <gtk/gtkunixprint.h>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void FileMenu(EditorFrame& frame) {
    auto* file = frame.GetMenuBar()->GetMenu(frame.GetMenuBar()->FindMenu("File"));
    int setup = wxNOT_FOUND;
    int print = wxNOT_FOUND;
    int endings = wxNOT_FOUND;
    int update = wxNOT_FOUND;
    int position = 0;
    for (const auto* item : file->GetMenuItems()) {
        const auto label = item->GetItemLabelText();
        if (label == "Page Setup...") setup = position;
        if (label == "Print...") {
            Require(item->GetItemLabel().EndsWith("\tCtrl+P"), "Print advertises the original Ctrl+P shortcut");
            print = position;
        }
        if (label == "Line Endings") endings = position;
        if (label == "Update File Time") update = position;
        ++position;
    }
    Require(endings >= 0 && setup > endings && update > setup,
            "original File menu exposes Page Setup between Line Endings and file properties");
    Require(print == setup + 1 && update > print, "original File menu puts Print immediately after Page Setup");
    std::puts("PASS: File menu Page Setup and Print commands");
}

class SetupResponse final : public wxTimer {
public:
    SetupResponse(const wxString& typedZoom, int response, int colour)
        : typedZoom_(typedZoom), response_(response), colour_(colour) {}

    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != "Page Setup") continue;
            auto* zoom = wxDynamicCast(wxWindow::FindWindowByName("Print zoom", dialog), wxComboBox);
            if (zoom) {
                before = zoom->GetValue();
                if (!typedZoom_.empty()) zoom->SetValue(typedZoom_);
            }
            if (colour_ != wxNOT_FOUND) {
                auto* mode = wxDynamicCast(wxWindow::FindWindowByName("Print color mode", dialog), wxChoice);
                if (mode) mode->SetSelection(colour_);
            }
            Stop();
            wxCommandEvent event(wxEVT_BUTTON, response_);
            dialog->ProcessWindowEvent(event);
            return;
        }
    }

    wxString before;

private:
    wxString typedZoom_;
    int response_;
    int colour_;
};

wxString PageSetup(EditorFrame& frame, const wxString& typedZoom, int response, int colour = wxNOT_FOUND) {
    const int id = frame.GetMenuBar()->FindMenuItem("File", "Page Setup...");
    Require(id != wxNOT_FOUND, "File menu exposes Page Setup");
    SetupResponse setup(typedZoom, response, colour);
    setup.Start(25);
    wxCommandEvent menu(wxEVT_MENU, id);
    Require(frame.ProcessWindowEvent(menu), "Page Setup File menu command is handled");
    Require(!setup.before.empty(), "Page Setup opens a dialog with an editable print zoom choice");
    return setup.before;
}

void Zoom(EditorFrame& frame) {
    Require(PageSetup(frame, "150%", wxID_CANCEL) == "100%", "Page Setup initial print zoom is 100 percent");
    Require(PageSetup(frame, "150%", wxID_OK) == "100%", "Page Setup Cancel leaves print zoom unchanged");
    Require(PageSetup(frame, "invalid", wxID_OK) == "150%", "Page Setup OK retains valid print zoom");
    Require(PageSetup(frame, {}, wxID_CANCEL) == "100%", "Page Setup invalid zoom falls back to 100 percent");
    PageSetup(frame, "150%", wxID_OK);
    Require(PageSetup(frame, "501", wxID_OK) == "150%", "Page Setup starts invalid input from a non-default zoom");
    Require(PageSetup(frame, {}, wxID_CANCEL) == "100%", "Page Setup out-of-range zoom falls back to 100 percent");
    std::puts("PASS: original Page Setup confirmation, cancellation and invalid zoom");
}

void SavedOptions() {
    const auto path = wxFileName::CreateTempFileName("wxnotepad-print-settings-");
    Require(!path.empty(), "create an isolated original-style INI for print settings");
    SettingsStore settings(path);
    settings.values.saveSettings = false;
    auto* frame = new EditorFrame(&settings);
    PageSetup(*frame, "125%", wxID_OK);
    Require(SettingsStore(path).values.printing.zoom == 100, "Page Setup OK alone does not write the INI");
    const auto save = frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now");
    Require(save != wxNOT_FOUND, "Settings menu has Save Settings Now");
    wxCommandEvent saveNow(wxEVT_MENU, save);
    Require(frame->ProcessWindowEvent(saveNow), "Save Settings Now handles its native menu command");
    const auto saved = SettingsStore(path).values.printing;
    Require(saved.zoom == 125 && (saved.margins[0] == 2000 || saved.margins[0] == 1000) &&
                std::ranges::all_of(saved.margins, [&](int margin) { return margin == saved.margins[0]; }),
            "Save Settings Now preserves print zoom and four default margins in original locale units");
    PageSetup(*frame, "150%", wxID_OK);
    const auto saveOnExit = frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings On Exit");
    Require(saveOnExit != wxNOT_FOUND, "Settings menu has Save Settings On Exit");
    wxCommandEvent enableOnExit(wxEVT_MENU, saveOnExit);
    Require(frame->ProcessWindowEvent(enableOnExit) && frame->Close(), "close the owned settings fixture normally");
    Require(SettingsStore(path).values.printing.zoom == 150, "Save Settings On Exit retains the new print zoom");
    Require(wxRemoveFile(path), "remove the owned test INI");
}

struct PrintFixture {
    Scintilla::Position start;
    Scintilla::Position end;
    Scintilla::Position length;
};

PrintFixture InstallFixture(EditorControl& control) {
    auto* editor = &control;
    const std::string prefix = "OutsideFirstLine\nOutsideLeading ";
    std::string content = prefix;
    for (int line = 0; line < 120; ++line)
        content += "PrintLine" + std::to_string(1000 + line) + " Unicode caf\xc3\xa9\n";
    content += "SelectedEnd";
    const auto selectionEnd = static_cast<Scintilla::Position>(content.size());
    content += " OutsideTrailing\nOutsideLastLine\n";
    editor->Send(Scintilla::Message::AddText, content.size(), reinterpret_cast<intptr_t>(content.data()));
    editor->Send(Scintilla::Message::SetSel, 0, 0);

    return {static_cast<Scintilla::Position>(prefix.size()), selectionEnd,
            static_cast<Scintilla::Position>(content.size())};
}

PrintFixture InstallWrappedFixture(EditorControl& editor) {
    // One physical line should continue across several printed pages. Its
    // unique words make duplicate or omitted display rows observable in a PDF.
    constexpr uintptr_t textStyle = 0;
    constexpr intptr_t coloredForeground = 0x2040dc;
    constexpr intptr_t coloredBackground = 0xffe080;
    editor.Send(Scintilla::Message::ClearAll);
    editor.Send(Scintilla::Message::StyleSetFore, textStyle, coloredForeground);
    editor.Send(Scintilla::Message::StyleSetBack, textStyle, coloredBackground);
    std::string wrapped;
    for (int index = 0; index < 1000; ++index)
        wrapped += "PrintWrap" + std::to_string(1000 + index) + " ";
    editor.Send(Scintilla::Message::AddText, wrapped.size(), reinterpret_cast<intptr_t>(wrapped.data()));
    editor.Send(Scintilla::Message::SetSel, 0, 0);
    return {0, 0, static_cast<Scintilla::Position>(wrapped.size())};
}

#ifdef __WXGTK__
class PrintResponse final : public wxTimer {
public:
    PrintResponse(wxString output, GtkPrintPages pages) : output_(std::move(output)), pages_(pages) {}

    void Notify() override {
        GList* windows = gtk_window_list_toplevels();
        GtkPrintUnixDialog* dialog = nullptr;
        for (auto* node = windows; node; node = node->next) {
            if (GTK_IS_PRINT_UNIX_DIALOG(node->data) && gtk_widget_get_visible(GTK_WIDGET(node->data))) {
                dialog = GTK_PRINT_UNIX_DIALOG(node->data);
                break;
            }
        }
        g_list_free(windows);
        if (!dialog) return;
        sawDialog = true;
        if (output_.empty()) {
            Stop();
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
            return;
        }
        auto* printer = gtk_print_unix_dialog_get_selected_printer(dialog);
        if (!printer && ++waiting_ < 40) {
            auto* initial = gtk_print_settings_new();
            gtk_print_settings_set_printer(initial, "Print to File");
            SetOutput(initial);
            gtk_print_unix_dialog_set_settings(dialog, initial);
            g_object_unref(initial);
            return;
        }
        if (!printer || !gtk_printer_is_virtual(printer) ||
            std::string_view(gtk_printer_get_name(printer)) != "Print to File") {
            Stop();
            gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
            return;
        }
        auto* settings = gtk_print_unix_dialog_get_settings(dialog);
        SetOutput(settings);
        gtk_print_settings_set_print_pages(settings, pages_);
        if (pages_ == GTK_PRINT_PAGES_RANGES) {
            GtkPageRange range{1, 1};
            gtk_print_settings_set_page_ranges(settings, &range, 1);
        }
        gtk_print_unix_dialog_set_settings(dialog, settings);
        g_object_unref(settings);
        auto* accepted = gtk_print_unix_dialog_get_settings(dialog);
        const auto* uri = gtk_print_settings_get(accepted, GTK_PRINT_SETTINGS_OUTPUT_URI);
        auto* acceptedPath = uri ? g_filename_from_uri(uri, nullptr, nullptr) : nullptr;
        acceptedChoice = acceptedPath && wxString::FromUTF8(acceptedPath) == output_ &&
                         gtk_print_settings_get_print_pages(accepted) == pages_;
        if (!acceptedChoice)
            std::println(stderr, "Native Print selection {:d}, URI {}",
                         static_cast<int>(gtk_print_settings_get_print_pages(accepted)), uri ? uri : "(none)");
        g_free(acceptedPath);
        g_object_unref(accepted);
        Stop();
        gtk_dialog_response(GTK_DIALOG(dialog), acceptedChoice ? GTK_RESPONSE_OK : GTK_RESPONSE_CANCEL);
    }

    bool sawDialog = false;
    bool acceptedChoice = false;

private:
    void SetOutput(GtkPrintSettings* settings) const {
        auto* uri = g_filename_to_uri(output_.utf8_str(), nullptr, nullptr);
        gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_OUTPUT_URI, uri);
        g_free(uri);
        gtk_print_settings_set(settings, GTK_PRINT_SETTINGS_OUTPUT_FILE_FORMAT, "pdf");
    }

    wxString output_;
    GtkPrintPages pages_;
    int waiting_ = 0;
};

void PrintCommand(EditorFrame& frame) {
    wxCommandEvent event(wxEVT_MENU, wxID_PRINT);
    Require(frame.ProcessWindowEvent(event), "File Print native menu command is handled");
}

void PdfPrint(EditorFrame& frame, const wxString& path, const wxString& name, GtkPrintPages pages) {
    Require(!wxFileExists(path), "the native PDF output path is new");
    PrintResponse response(path, pages);
    response.Start(25);
    PrintCommand(frame);
    Require(response.sawDialog, "Print opens GTK's native dialog");
    Require(response.acceptedChoice, "GTK selects virtual Print to File and requested page mode");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!wxFileExists(path) && std::chrono::steady_clock::now() < deadline) {
        while (g_main_context_iteration(nullptr, false)) {
        }
        wxMilliSleep(10);
    }
    wxFile output(path);
    std::array<char, 5> prefix{};
    Require(output.IsOpened() && output.Read(prefix.data(), prefix.size()) == 5 &&
                std::string_view(prefix.data(), prefix.size()) == "%PDF-",
            "native GTK Print to File produces real PDF bytes");
    output.Close();
    wxString directory;
    if (wxGetEnv("WXNOTEPAD_PRINT_TEST_OUTPUT_DIR", &directory)) {
        const auto evidence = wxFileName(directory, name + ".pdf").GetFullPath();
        Require(wxRenameFile(path, evidence, false), "retain owned PDF output under its fixture name");
    } else {
        Require(wxRemoveFile(path), "remove the owned PDF output");
    }
}

void NativePrint(EditorFrame& frame) {
    g_object_set(gtk_settings_get_default(), "gtk-print-backends", "file", nullptr);
    auto* editor = frame.Editor();
    const auto fixture = InstallFixture(*editor);

    {
        const NativeTrace::Gesture trace(*editor, 'P', wxMOD_CONTROL);
        wxString focusFile;
        NativeKeys::FocusEditor(&frame, "wxnotepad", focusFile);
        PrintResponse cancel({}, GTK_PRINT_PAGES_ALL);
        cancel.Start(25);
        {
            wxFFile file(focusFile, "wb");
            Require(file.Write(NativeTrace::Request("key:ctrl+p")), "request the native Ctrl+P shortcut");
        }
        // The dialog can consume key-up after the accelerator opened it.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!cancel.sawDialog && std::chrono::steady_clock::now() < deadline) {
            wxYield();
            wxMilliSleep(10);
        }
        Require(cancel.sawDialog, "Ctrl+P opens the native Print dialog and it can be canceled");
    }
    const auto temporary = wxFileName::CreateTempFileName("wxnotepad-print-");
    Require(!temporary.empty() && wxRemoveFile(temporary), "allocate a clean path for the virtual printer");
    const auto output = temporary + ".pdf";
    PdfPrint(frame, output, "whole", GTK_PRINT_PAGES_ALL);
    PdfPrint(frame, output, "page2", GTK_PRINT_PAGES_RANGES);
    editor->Send(Scintilla::Message::SetSel, fixture.start, fixture.end);
    PdfPrint(frame, output, "all-with-selection", GTK_PRINT_PAGES_ALL);
    Require(editor->Send(Scintilla::Message::GetLength) == fixture.length &&
                editor->Send(Scintilla::Message::GetCurrentPos) == fixture.end &&
                editor->Send(Scintilla::Message::GetAnchor) == fixture.start &&
                editor->Send(Scintilla::Message::CanUndo) && editor->Send(Scintilla::Message::GetModify),
            "printing does not change editor length, caret or history");
    std::puts("PASS: native GTK Print Cancel, whole buffer, requested page, and live selection");

    const auto wrapped = InstallWrappedFixture(*editor);
    PageSetup(frame, "125%", wxID_OK);
    PdfPrint(frame, output, "styled-wrap", GTK_PRINT_PAGES_ALL);
    const auto middle = wrapped.length / 2;
    editor->Send(Scintilla::Message::SetSel, middle, middle + 1);
    PdfPrint(frame, output, "selected-wrap", GTK_PRINT_PAGES_ALL);
    editor->Send(Scintilla::Message::SetSel, 0, 0);
    PageSetup(frame, "150%", wxID_OK, std::to_underlying(Scintilla::PrintOption::BlackOnWhite));
    PdfPrint(frame, output, "monochrome-wrap", GTK_PRINT_PAGES_ALL);
}
#endif

#ifdef __WXMSW__
class WindowsPrintCapture final : public wxTimer {
public:
    WindowsPrintCapture(EditorFrame& frame, wxString trigger, bool selection, bool directWrite, bool wrapped,
                        std::function<void(bool)> finish)
        : frame_(frame), trigger_(std::move(trigger)), finish_(std::move(finish)), selection_(selection) {
        using Scintilla::Message;
        const auto technology = directWrite ? Scintilla::Technology::DirectWrite : Scintilla::Technology::Default;
        auto* editor = frame_.Editor();
        editor->Send(Message::SetTechnology, std::to_underlying(technology));
        Require(editor->Send(Message::GetTechnology) == std::to_underlying(technology),
                "requested Windows editor technology is available");
        fixture_ = wrapped ? InstallWrappedFixture(*editor) : InstallFixture(*editor);
        PageSetup(frame_, "125%", wxID_OK);
        if (selection_) editor->Send(Message::SetSel, fixture_.start, fixture_.end);
        Start(25);
    }

    void Notify() override {
        using Scintilla::Message;
        if (!wxFileExists(trigger_)) return;
        Stop();
        try {
            NativeKeys::Shortcut(&frame_, 'P', true);
            auto* editor = frame_.Editor();
            Require(editor->Send(Message::GetLength) == fixture_.length &&
                        editor->Send(Message::GetAnchor) == (selection_ ? fixture_.start : 0) &&
                        editor->Send(Message::GetCurrentPos) == (selection_ ? fixture_.end : 0) &&
                        editor->Send(Message::GetModify) && editor->Send(Message::CanUndo),
                    "printing preserves Windows document state");
            finish_(true);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: %s\n", error.what());
            finish_(false);
        }
    }

private:
    EditorFrame& frame_;
    wxString trigger_;
    std::function<void(bool)> finish_;
    bool selection_;
    PrintFixture fixture_{};
};
#endif

class PrintingApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor presents the editor before native printing scenarios");
                FileMenu(*frame);
                Zoom(*frame);
                SavedOptions();
#ifdef __WXGTK__
                NativePrint(*frame);
#else
                wxString trigger;
                if (wxGetEnv("WXNOTEPAD_PRINT_CAPTURE_TRIGGER", &trigger)) {
                    capture_ = std::make_unique<WindowsPrintCapture>(
                        *frame, trigger, wxGetEnv("WXNOTEPAD_PRINT_CAPTURE_SELECTION", nullptr),
                        wxGetEnv("WXNOTEPAD_PRINT_CAPTURE_DIRECTWRITE", nullptr),
                        wxGetEnv("WXNOTEPAD_PRINT_CAPTURE_WRAPPED", nullptr), [this, frame](bool success) {
                            result_ = success ? 0 : 1;
                            frame->Destroy();
                            ExitMainLoop();
                        });
                    return;
                }
#endif
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
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
#ifdef __WXMSW__
    std::unique_ptr<WindowsPrintCapture> capture_;
#endif
    int result_ = 1;
};
} // namespace

wxIMPLEMENT_APP(PrintingApp);
