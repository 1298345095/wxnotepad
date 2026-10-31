#pragma once

#include <wx/defs.h>
#include <wx/string.h>
#include <cstdio>
#include <stdexcept>
#ifdef __WXMSW__
#include <windows.h>
#elif defined(__WXGTK__)
#include <wx/filename.h>
#include <gtk/gtk.h>
#endif

// Low-level operations on native modal widgets owned by the calling test.
// Drivers retain their own dialog recognition, timer stages and assertions.
namespace NativeDialogs {
inline bool PathEquals(const wxString& actual, const wxString& expected) {
    if (actual == expected) return true;
    std::fprintf(stderr, "Document path mismatch: expected='%s', actual='%s'\n", expected.utf8_str().data(),
                 actual.utf8_str().data());
    return false;
}
#ifdef __WXMSW__
inline void ChooseFileType(HWND window, const wxString& choice) {
    struct Selection {
        const wxString& name;
        bool found = false;
        wxString observed;
    } selection{choice};
    EnumChildWindows(
        window,
        [](HWND child, LPARAM data) -> BOOL {
            auto& selection = *reinterpret_cast<Selection*>(data);
            wchar_t type[64]{};
            GetClassNameW(child, type, 64);
            if (!IsWindowVisible(child) || wxString(type) != "ComboBox") return TRUE;
            const auto count = SendMessageW(child, CB_GETCOUNT, 0, 0);
            selection.observed +=
                wxString::Format(" ComboBox[id=%d,count=%lld]", GetDlgCtrlID(child), static_cast<long long>(count));
            for (LRESULT index = 0; index < count; ++index) {
                const auto length = SendMessageW(child, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0);
                if (length < 0 || length >= 1024) continue;
                wchar_t label[1024]{};
                SendMessageW(child, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(label));
                if (selection.observed.length() < 768) selection.observed += " {" + wxString(label).Left(96) + "}";
                if (!wxString(label).StartsWith(selection.name + " (")) continue;
                if (SendMessageW(child, CB_GETCURSEL, 0, 0) != index) {
                    SendMessageW(child, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
                    SendMessageW(GetParent(child), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(child), CBN_SELCHANGE),
                                 reinterpret_cast<LPARAM>(child));
                    // The modern native dialog commits its file type on selection completion.
                    SendMessageW(GetParent(child), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(child), CBN_SELENDOK),
                                 reinterpret_cast<LPARAM>(child));
                }
                selection.found = true;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&selection));
    if (!selection.found)
        throw std::runtime_error(
            ("Owned native file-type selector missing " + choice + "; observed:" + selection.observed.Left(1024))
                .ToStdString());
}
inline void ChooseFile(HWND window, const wxString& destination) {
    if (destination.empty())
        SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
    else {
        EnumChildWindows(
            window,
            [](HWND child, LPARAM data) -> BOOL {
                wchar_t type[64]{};
                GetClassNameW(child, type, 64);
                if (IsWindowVisible(child) && wxString(type) == "Edit") {
                    // Replace as an edit so the dialog updates its filename after a type change.
                    SendMessageW(child, EM_SETSEL, 0, -1);
                    SendMessageW(child, EM_REPLACESEL, TRUE,
                                 reinterpret_cast<LPARAM>(reinterpret_cast<const wxString*>(data)->wc_str()));
                    return FALSE;
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&destination));
        PostMessageW(window, WM_COMMAND, IDOK, 0);
    }
}
#elif defined(__WXGTK__)
inline void ChooseFileType(GtkWidget* widget, const wxString& choice) {
    GSList* filters = gtk_file_chooser_list_filters(GTK_FILE_CHOOSER(widget));
    GtkFileFilter* selected = nullptr;
    for (GSList* item = filters; item; item = item->next) {
        auto* filter = GTK_FILE_FILTER(item->data);
        if (wxString::FromUTF8(gtk_file_filter_get_name(filter)).StartsWith(choice + " (")) selected = filter;
    }
    g_slist_free(filters);
    if (!selected) throw std::runtime_error("native file-type selector must expose requested choice");
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(widget), selected);
}
inline void Respond(GtkWidget* dialog, int response) {
    auto* button = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), response);
    if (!button || !GTK_IS_BUTTON(button)) throw std::runtime_error("native dialog must expose the expected button");
    gtk_button_clicked(GTK_BUTTON(button));
}

inline void ChooseFile(GtkWidget* widget, const wxString& destination) {
    if (destination.empty())
        Respond(widget, GTK_RESPONSE_CANCEL);
    else if (gtk_file_chooser_get_action(GTK_FILE_CHOOSER(widget)) == GTK_FILE_CHOOSER_ACTION_OPEN)
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(widget), destination.utf8_str());
    else {
        const wxFileName target(destination);
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(widget), target.GetPath().utf8_str());
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(widget), target.GetFullName().utf8_str());
    }
}
#endif
} // namespace NativeDialogs
