#pragma once

#include "native_dialog_helpers.h"
#include <wx/timer.h>
#include <utility>
#include <cstdint>
#include <functional>

namespace NativeDialogs {
enum class FileDialogProfile : std::uint8_t { Open, Save };

// Drives only this test's native file commands. Each scenario retains its own
// command sequence, expected dialog counts and document/disk assertions.
class FileCommandDriver final : public wxTimer {
public:
    using Observer = std::function<void(const char*)>;
    FileCommandDriver(FileDialogProfile profile, int choice, wxString destination = {}, Observer observer = {})
        : profile_(profile), choice_(choice), destination_(std::move(destination)), observer_(std::move(observer)) {
        Observe("driver start");
        Start(100);
    }
    ~FileCommandDriver() override { Observe("driver destructor"); }
    int prompts = 0;
    int choosers = 0;
    int errors = 0;
    void Notify() override {
        Observe("driver callback enter");
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<FileCommandDriver*>(data);
                driver.RespondToWindow(window);
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next)
            RespondToWindow(GTK_WIDGET(item->data));
        g_list_free(windows);
#endif
        Observe("driver callback exit");
    }

private:
#ifdef __WXMSW__
    void RespondToWindow(HWND window) {
        if (!IsWindowVisible(window)) return;
        wchar_t title[256]{};
        GetWindowTextW(window, title, static_cast<int>(std::size(title)));
        if (wxString(title) == "Save changes?" || wxString(title) == "Read only file" || wxString(title) == "Revert") {
            Observe("confirmation response begin");
            ++prompts;
            const int button = choice_ == wxID_OK    ? IDOK
                               : choice_ == wxID_YES ? IDYES
                               : choice_ == wxID_NO  ? IDNO
                                                     : IDCANCEL;
            SendMessageW(window, TDM_CLICK_BUTTON, button, 0);
            Observe("confirmation response end");
        } else if (IsChooserTitle(title)) {
            if (!chooserStage_++) {
                ++choosers;
                Observe("chooser selection begin");
                ChooseFile(window, destination_);
                Observe("chooser selection end");
            }
        } else if (IsErrorTitle(title)) {
            ++errors;
            SendMessageW(window, TDM_CLICK_BUTTON, IDOK, 0);
        }
    }
#elif defined(__WXGTK__)
    void RespondToWindow(GtkWidget* widget) {
        if (!gtk_widget_get_visible(widget)) return;
        if (GTK_IS_FILE_CHOOSER(widget)) {
            if (chooserStage_ == 0) {
                chooserStage_ = 1;
                ++choosers;
                Observe("chooser selection begin");
                ChooseFile(widget, destination_);
                Observe("chooser selection end");
            } else if (chooserStage_ == 1 && !destination_.empty() && CanAcceptFile(widget, destination_)) {
                chooserStage_ = 2;
                Observe("chooser accept begin");
                Respond(widget, GTK_RESPONSE_ACCEPT);
                Observe("chooser accept end");
            }
            return;
        }
        if (!GTK_IS_MESSAGE_DIALOG(widget)) return;
        const auto title = wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget)));
        if (title == "Save changes?" || title == "Read only file" || title == "Revert") {
            Observe("confirmation response begin");
            ++prompts;
            Respond(widget, choice_ == wxID_OK    ? GTK_RESPONSE_OK
                            : choice_ == wxID_YES ? GTK_RESPONSE_YES
                            : choice_ == wxID_NO  ? GTK_RESPONSE_NO
                                                  : GTK_RESPONSE_CANCEL);
            Observe("confirmation response end");
        } else if (IsErrorTitle(title)) {
            ++errors;
            Respond(widget, GTK_RESPONSE_OK);
        }
    }
#endif
    void Observe(const char* transition) const {
        if (observer_) observer_(transition);
    }
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
    Observer observer_;
};
} // namespace NativeDialogs
