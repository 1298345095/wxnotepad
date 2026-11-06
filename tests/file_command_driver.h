#pragma once

#include "native_dialog_helpers.h"
#include <wx/timer.h>
#include <utility>

namespace NativeDialogs {
enum class FileDialogProfile { Open, Save };

// Drives only this test's native file commands. Each scenario retains its own
// command sequence, expected dialog counts and document/disk assertions.
class FileCommandDriver final : public wxTimer {
public:
    FileCommandDriver(FileDialogProfile profile, int choice, wxString destination = {})
        : profile_(profile), choice_(choice), destination_(std::move(destination)) {
        Start(100);
    }
    int prompts = 0;
    int choosers = 0;
    int errors = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<FileCommandDriver*>(data);
                if (!IsWindowVisible(window)) return TRUE;
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                if (wxString(title) == "Save changes?") {
                    ++driver.prompts;
                    const int button = driver.choice_ == wxID_YES ? IDYES : driver.choice_ == wxID_NO ? IDNO : IDCANCEL;
                    SendMessageW(window, WM_USER + 102, button, 0); // TDM_CLICK_BUTTON
                } else if (driver.IsChooserTitle(title)) {
                    if (!driver.chooserStage_++) {
                        ++driver.choosers;
                        ChooseFile(window, driver.destination_);
                    }
                } else if (driver.IsErrorTitle(title)) {
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
            if (GTK_IS_FILE_CHOOSER(widget)) {
                if (!chooserStage_++) {
                    ++choosers;
                    ChooseFile(widget, destination_);
                } else if (chooserStage_ == 2 && !destination_.empty()) {
                    Respond(widget, GTK_RESPONSE_ACCEPT);
                }
                continue;
            }
            if (!GTK_IS_MESSAGE_DIALOG(widget)) continue;
            const auto title = wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget)));
            if (title == "Save changes?") {
                ++prompts;
                Respond(widget, choice_ == wxID_YES  ? GTK_RESPONSE_YES
                                : choice_ == wxID_NO ? GTK_RESPONSE_NO
                                                     : GTK_RESPONSE_CANCEL);
            } else if (IsErrorTitle(title)) {
                ++errors;
                Respond(widget, GTK_RESPONSE_OK);
            }
        }
        g_list_free(windows);
#endif
    }

private:
    bool IsChooserTitle(const wxString& title) const {
        return title == "Save file" || title == (profile_ == FileDialogProfile::Open ? "Open file" : "Save As");
    }
    bool IsErrorTitle(const wxString& title) const {
        return title == "Could not save file" ||
               (profile_ == FileDialogProfile::Open && title == "Could not open file");
    }
    FileDialogProfile profile_;
    int choice_;
    wxString destination_;
    int chooserStage_ = 0;
};
} // namespace NativeDialogs
