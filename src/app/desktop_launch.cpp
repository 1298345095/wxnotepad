#include "app/desktop_launch.h"
#include <wx/window.h>
#include <wx/uri.h>
#include <wx/utils.h>
#include <wx/log.h>
#ifdef __WXMSW__
#include <windows.h>
#include <shellapi.h>
#else
#include <gio/gdesktopappinfo.h>
#include <unistd.h>
#endif

#ifdef __WXGTK__
namespace {
void SetChildDirectory(gpointer directory) {
    // GIO performs this setup in the child before its exec-confirmed launch.
    if (directory && chdir(static_cast<const char*>(directory)) != 0) _exit(127);
}
}
bool LaunchDesktopApplication(GAppInfo* application, const wxString& target, const wxString& workingDirectory,
                              wxString& error) {
    GError* failure = nullptr;
    GFile* file = target.empty() ? nullptr : g_file_new_for_commandline_arg(target.utf8_str());
    gchar* uri = file ? g_file_get_uri(file) : nullptr;
    GList* uris = uri ? g_list_append(nullptr, uri) : nullptr;
    const auto directory = workingDirectory.utf8_string();
    bool launched = false;
    if (G_IS_DESKTOP_APP_INFO(application)) {
        launched = g_desktop_app_info_launch_uris_as_manager(G_DESKTOP_APP_INFO(application), uris, nullptr,
                                                            G_SPAWN_SEARCH_PATH, SetChildDirectory,
                                                            directory.empty() ? nullptr : const_cast<char*>(directory.c_str()),
                                                            nullptr, nullptr, &failure) != FALSE;
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

bool LaunchDesktopTarget(wxWindow* parent, const wxString& target, const wxString& workingDirectory, wxString& error) {
#ifdef __WXMSW__
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_FLAG_NO_UI;
    launch.hwnd = parent ? static_cast<HWND>(parent->GetHandle()) : nullptr;
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
