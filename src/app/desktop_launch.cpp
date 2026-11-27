#include "app/desktop_launch.h"
#include <wx/window.h>
#include <wx/uri.h>
#include <wx/utils.h>
#include <wx/log.h>
#include <wx/translation.h>
#ifdef __WXMSW__
#include <windows.h>
#include <shellapi.h>
#else
#include <gio/gdesktopappinfo.h>
#endif

#ifdef __WXGTK__
bool LaunchDesktopApplication(GAppInfo* application, const wxString& target, const wxString& workingDirectory,
                              wxString& error) {
    GError* failure = nullptr;
    GFile* file = target.empty() ? nullptr : g_file_new_for_commandline_arg(target.utf8_str());
    gchar* uri = file ? g_file_get_uri(file) : nullptr;
    GList* uris = uri ? g_list_append(nullptr, uri) : nullptr;
    bool launched = false;
    if (G_IS_DESKTOP_APP_INFO(application)) {
        auto* desktop = G_DESKTOP_APP_INFO(g_object_ref(application));
        if (!workingDirectory.empty()) {
            // Let GIO pass the containing directory as g_spawn's working
            // directory. Its startup pipe then reports chdir and exec errors.
            // A child-setup callback exiting on chdir failure would look like
            // successful startup to that pipe.
            GKeyFile* entry = g_key_file_new();
            const auto* filename = g_desktop_app_info_get_filename(desktop);
            const bool loaded =
                filename && g_key_file_load_from_file(entry, filename, G_KEY_FILE_KEEP_TRANSLATIONS, &failure);
            g_object_unref(desktop);
            desktop = nullptr;
            if (loaded) {
                g_key_file_set_string(entry, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_PATH,
                                      workingDirectory.utf8_str());
                desktop = g_desktop_app_info_new_from_keyfile(entry);
            }
            g_key_file_unref(entry);
            // TRANSLATORS: Linux IDM_FILE_LAUNCH/OPENWITH error loading a native .desktop entry; no parameters.
            if (!desktop && !failure) error = _("Could not load the desktop application entry.");
        }
        if (desktop) {
            launched = g_desktop_app_info_launch_uris_as_manager(desktop, uris, nullptr, G_SPAWN_SEARCH_PATH, nullptr,
                                                                 nullptr, nullptr, nullptr, &failure) != FALSE;
            g_object_unref(desktop);
        }
    } else {
        GList* files = file ? g_list_append(nullptr, file) : nullptr;
        launched = g_app_info_launch(application, files, nullptr, &failure) != FALSE;
        g_list_free(files);
    }
    if (failure) {
        error = wxString::FromUTF8(failure->message);
        g_error_free(failure);
    }
    g_list_free(uris);
    g_free(uri);
    if (file) g_object_unref(file);
    return launched;
}
#endif

bool LaunchDesktopTarget(wxWindow& parent, const wxString& target, const wxString& workingDirectory, wxString& error) {
#ifdef __WXMSW__
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_FLAG_NO_UI;
    launch.hwnd = static_cast<HWND>(parent.GetHandle());
    launch.lpFile = target.wc_str();
    launch.lpDirectory = workingDirectory.empty() ? nullptr : workingDirectory.wc_str();
    launch.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&launch)) return true;
    error = wxSysErrorMsg();
    return false;
#else
    (void)parent;
    GError* failure = nullptr;
    GFile* file = g_file_new_for_commandline_arg(target.utf8_str());
    GAppInfo* application = g_file_query_default_handler(file, nullptr, &failure);
    g_object_unref(file);
    if (!application) {
        if (failure) {
            error = wxString::FromUTF8(failure->message);
            g_error_free(failure);
        }
        return false;
    }
    const bool launched = LaunchDesktopApplication(application, target, workingDirectory, error);
    g_object_unref(application);
    return launched;
#endif
}
