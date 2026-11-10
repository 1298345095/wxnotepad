#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <memory>
#include <wx/wx.h>
#include <wx/timer.h>
#include <wx/statusbr.h>
#include "native_dialog_helpers.h"
#include "native_search_helpers.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
using TestEditor::Append;
using TestEditor::Text;
using TestFiles::Bytes;
using TestFiles::Filename;
#ifdef __WXGTK__
using NativeDialogs::Respond;
#endif
// Only activate native modal widgets owned by this test process. No global input.
class NativeDriver final : public wxTimer {
public:
    explicit NativeDriver(int choice, wxString path = {}) : choice_(choice), path_(path) { Start(150); }
    int losses = 0, choosers = 0, errors = 0;
    wxString selectedType;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != "Encoding character loss") continue;
            ++losses;
            Require(dialog->FindWindow(wxID_OK) && dialog->FindWindow(wxID_CANCEL),
                    "original loss prompt exposes OK/Cancel native controls");
            NativeSearch::Click(dialog, choice_);
            return;
        }
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<NativeDriver*>(data);
                if (!IsWindowVisible(window)) return TRUE;
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                if (wxString(title) == "GBK character loss") {
                    ++driver.losses;
                    struct Buttons {
                        int count = 0;
                        bool alternative = false;
                    } buttons;
                    EnumChildWindows(
                        window,
                        [](HWND child, LPARAM data) -> BOOL {
                            wchar_t className[32]{};
                            GetClassNameW(child, className, 32);
                            wchar_t title[256]{};
                            GetWindowTextW(child, title, 256);
                            if (wxString(className) != "Button") return TRUE;
                            auto& buttons = *reinterpret_cast<Buttons*>(data);
                            ++buttons.count;
                            buttons.alternative |=
                                wxString(title).Contains("UTF-8") || wxString(title).Contains("Replace in GBK");
                            return TRUE;
                        },
                        reinterpret_cast<LPARAM>(&buttons));
                    // Common TaskDialog buttons have localized labels and ID 0 child windows.
                    // The response below exercises their semantic IDOK/IDCANCEL commands.
                    Require(buttons.count == 2 && !buttons.alternative,
                            "native loss prompt must expose original OK/Cancel choices");
                    SendMessageW(window, WM_USER + 102, driver.choice_ == wxID_OK ? IDOK : IDCANCEL, 0);
                } else if (wxString(title) == "Open file" || wxString(title) == "Save As") {
                    if (!driver.stage_++) {
                        ++driver.choosers;
                        if (!driver.selectedType.empty()) NativeDialogs::ChooseFileType(window, driver.selectedType);
                        NativeDialogs::ChooseFile(window, driver.path_);
                    }
                } else if (wxString(title) == "Could not save file" || wxString(title) == "Could not open file") {
                    ++driver.errors;
                    SendMessageW(window, WM_USER + 102, IDOK, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget)) continue;
            HandleWidget(widget);
        }
        g_list_free(windows);
#endif
    }

private:
#ifdef __WXGTK__
    void HandleWidget(GtkWidget* widget) {
        if (GTK_IS_FILE_CHOOSER(widget)) {
            HandleFileChooser(widget);
        } else if (GTK_IS_MESSAGE_DIALOG(widget)) {
            HandleMessageDialog(widget);
        }
    }
    void HandleFileChooser(GtkWidget* widget) {
        if (stage_ == 0) {
            stage_ = 1;
            ++choosers;
            if (!selectedType.empty()) NativeDialogs::ChooseFileType(widget, selectedType);
            NativeDialogs::ChooseFile(widget, path_);
        } else if (stage_ == 1 && !path_.empty() && NativeDialogs::CanAcceptFile(widget, path_)) {
            stage_ = 2;
            Respond(widget, GTK_RESPONSE_ACCEPT);
        }
    }
    void HandleMessageDialog(GtkWidget* widget) {
        const auto title = wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget)));
        if (title == "GBK character loss") {
            ++losses;
            const auto label = [widget](int response) {
                auto* button = gtk_dialog_get_widget_for_response(GTK_DIALOG(widget), response);
                bool valid = false;
                if (button) valid = GTK_IS_BUTTON(button);
                Require(valid, "native loss button must exist");
                return wxString::FromUTF8(gtk_button_get_label(GTK_BUTTON(button)));
            };
            Require(label(GTK_RESPONSE_OK).Contains("OK") && label(GTK_RESPONSE_CANCEL).Contains("Cancel"),
                    "native loss choices must be original OK/Cancel");
            Require(!gtk_dialog_get_widget_for_response(GTK_DIALOG(widget), GTK_RESPONSE_YES) &&
                        !gtk_dialog_get_widget_for_response(GTK_DIALOG(widget), GTK_RESPONSE_NO),
                    "native loss prompt must not offer a UTF8 alternative");
            Respond(widget, choice_ == wxID_OK ? GTK_RESPONSE_OK : GTK_RESPONSE_CANCEL);
        } else if (title == "Could not save file" || title == "Could not open file") {
            ++errors;
            Respond(widget, GTK_RESPONSE_OK);
        }
    }
#endif
    int choice_, stage_ = 0;
    wxString path_;
};
void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "menu command must be handled");
}
class GbkNativeApp final : public wxApp {
public:
    bool OnInit() override {
        SetExitOnFrameDelete(false);
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        // Mixed-EOL warning choices are covered by mixed_line_ending_open.
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-gbk-native");
            try {
                std::filesystem::create_directory(directory);
                const auto source = directory / "source.txt";
                const std::string original = "Native\r\n\xd6\xd0\xce\xc4\n\x80\r";
                {
                    std::ofstream stream(source, std::ios::binary);
                    stream << original;
                }
                const auto openGbk = frame->GetMenuBar()->FindMenuItem("File", "Open As GBK...");
                Require(openGbk != wxNOT_FOUND, "File menu must expose explicit GBK open");
                NativeDriver opening(wxID_CANCEL, Filename(source));
                Command(frame, openGbk);
                opening.Stop();
                Require(opening.choosers == 1 && frame->DocumentEncoding() == FileEncoding::Gbk &&
                            frame->GetStatusBar()->GetStatusText().Contains("GBK"),
                        "native explicit open must record and display GBK");
                Append(frame, "¥☃😀𝄞");
                const auto python = frame->GetMenuBar()->FindMenuItem("Scheme", "Python");
                Command(frame, python);
                const auto rich = Text(frame);
                NativeDriver cancelAs(wxID_CANCEL);
                Command(frame, wxID_SAVEAS);
                cancelAs.Stop();
                Require(cancelAs.choosers == 1 && cancelAs.losses == 0 && Bytes(source) == original &&
                            Text(frame) == rich,
                        "cancelled native Save As must not prompt about loss or change bytes/text");
                const auto cancelled = directory / "cancelled.CPP";
                NativeDriver cancelLoss(wxID_CANCEL, Filename(cancelled));
                cancelLoss.selectedType = "JSON";
                Command(frame, wxID_SAVEAS);
                cancelLoss.Stop();
                Require(cancelLoss.choosers == 1 && cancelLoss.losses == 1 && !std::filesystem::exists(cancelled) &&
                            NativeDialogs::PathEquals(frame->DocumentPath(), Filename(source)) &&
                            frame->DocumentEncoding() == FileEncoding::Gbk && Bytes(source) == original &&
                            Text(frame) == rich && frame->GetMenuBar()->IsChecked(python) &&
                            frame->GetStatusBar()->GetStatusText().Contains("Python (override)"),
                        "GBK loss cancellation after Save As naming must retain path, override, text and disk bytes");
                NativeDriver cancel(wxID_CANCEL);
                Command(frame, wxID_SAVE);
                cancel.Stop();
                Require(cancel.losses == 1 && Bytes(source) == original && Text(frame) == rich &&
                            frame->Editor()->Send(Scintilla::Message::GetModify),
                        "native Cancel must retain all text/disk state");
                NativeDriver replace(wxID_OK);
                Command(frame, wxID_SAVE);
                replace.Stop();
                Require(replace.losses == 1 && Bytes(source) == original + "??????" && Text(frame) == rich &&
                            !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "native OK must save original placeholder bytes and retain clean rich text");
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(Text(frame) == "Native\r\n中文\n€\r" && frame->Editor()->Send(Scintilla::Message::GetModify),
                        "native loss Undo must undo the preceding user edit");
                frame->Editor()->Send(Scintilla::Message::Redo);
                Require(Text(frame) == rich && !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "native loss Redo must reach the rich text save point");
                const auto target = directory / "lossy.CPP";
                NativeDriver saveAs(wxID_OK, Filename(target));
                Command(frame, wxID_SAVEAS);
                saveAs.Stop();
                Require(saveAs.choosers == 1 && saveAs.losses == 1 && Bytes(target) == original + "??????" &&
                            Text(frame) == rich && frame->DocumentEncoding() == FileEncoding::Gbk &&
                            !frame->DocumentHasBom() && !frame->Editor()->Send(Scintilla::Message::GetModify) &&
                            frame->GetMenuBar()->IsChecked(python) &&
                            frame->GetStatusBar()->GetStatusText().Contains("Python (override)"),
                        "native lossy Save As must preserve rich text/GBK and apply current Python despite .CPP");
                const auto invalid = directory / "invalid.txt";
                {
                    std::ofstream stream(invalid, std::ios::binary);
                    stream << "\x81";
                }
                NativeDriver invalidOpen(wxID_CANCEL, Filename(invalid));
                Command(frame, openGbk);
                invalidOpen.Stop();
                Require(invalidOpen.choosers == 1 && invalidOpen.errors == 1 && Text(frame) == rich &&
                            frame->DocumentPath() == Filename(target) && frame->DocumentEncoding() == FileEncoding::Gbk,
                        "native malformed GBK must show error and retain current document and format");
                std::puts("PASS: native GBK menu/picker/status, loss OK/Cancel Save/Save As and Undo/Redo");
                {
                    auto* failure = new EditorFrame(settings_.get());
                    failure->Show();
                    Require(failure->Open(Filename(source), FileEncoding::Gbk).status == OpenStatus::Opened,
                            "failure fixture must open");
                    Append(failure, "☃😀");
                    const auto before = Text(failure);
#ifdef __WXMSW__
                    HANDLE lock = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                              FILE_ATTRIBUTE_NORMAL, nullptr);
                    Require(lock != INVALID_HANDLE_VALUE, "must lock file against replacement");
#else
                    std::filesystem::permissions(directory, std::filesystem::perms::owner_read |
                                                                std::filesystem::perms::owner_exec);
#endif
                    NativeDriver failed(wxID_OK);
                    Command(failure, wxID_SAVE);
                    failed.Stop();
#ifdef __WXMSW__
                    CloseHandle(lock);
#else
                    std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
#endif
                    Require(failed.losses == 1 && failed.errors == 1 && Text(failure) == before &&
                                failure->DocumentPath() == Filename(source) &&
                                failure->DocumentEncoding() == FileEncoding::Gbk &&
                                failure->Editor()->Send(Scintilla::Message::GetModify),
                            "native failed accepted-loss save must retain rich text, identity, GBK and dirty state");
                    Require(Bytes(source) == original + "??????", "native failed save must preserve disk bytes");
                    failure->Destroy();
                }
                std::puts("PASS: native accepted-loss Save write failure preserves disk and session");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
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
    int OnExit() override { return result_; }

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(GbkNativeApp);
