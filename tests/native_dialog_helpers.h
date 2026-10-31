#pragma once

#include <wx/defs.h>
#include <wx/string.h>
#ifdef __WXMSW__
#include <windows.h>
#elif defined(__WXGTK__)
#include <wx/filename.h>
#include <gtk/gtk.h>
#include <stdexcept>
#endif

// Low-level operations on native modal widgets owned by the calling test.
// Drivers retain their own dialog recognition, timer stages and assertions.
namespace NativeDialogs {
#ifdef __WXMSW__
inline void ChooseFile(HWND window, const wxString& destination) {
    if (destination.empty()) SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
    else {
        EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL {
            wchar_t type[64]{};
            GetClassNameW(child, type, 64);
            if (IsWindowVisible(child) && wxString(type) == "Edit") {
                SetWindowTextW(child, reinterpret_cast<const wxString*>(data)->wc_str());
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&destination));
        PostMessageW(window, WM_COMMAND, IDOK, 0);
    }
}
#elif defined(__WXGTK__)
inline void Respond(GtkWidget* dialog, int response) {
    auto* button = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), response);
    if (!button || !GTK_IS_BUTTON(button))
        throw std::runtime_error("native dialog must expose the expected button");
    gtk_button_clicked(GTK_BUTTON(button));
}

inline void ChooseFile(GtkWidget* widget, const wxString& destination) {
    if (destination.empty()) Respond(widget, GTK_RESPONSE_CANCEL);
    else if (gtk_file_chooser_get_action(GTK_FILE_CHOOSER(widget)) == GTK_FILE_CHOOSER_ACTION_OPEN)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(widget), destination.utf8_str());
    else {
        const wxFileName target(destination);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(widget), target.GetPath().utf8_str());
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(widget), target.GetFullName().utf8_str());
    }
}
#endif
}
