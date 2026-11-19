#include "app/native_launch.h"
#include <wx/log.h>
#include <wx/utils.h>
#include <wx/log.h>
#ifdef __WXGTK__
#include <glib.h>
#endif

bool NativeLaunch::SpawnArguments(const std::vector<wxString>& arguments, const wxString& workingDirectory,
                                  wxString& error) {
    if (arguments.empty()) return false;
#ifdef __WXGTK__
    std::vector<std::string> utf8;
    for (const auto& argument : arguments)
        utf8.push_back(argument.utf8_string());
    std::vector<char*> argv;
    for (auto& argument : utf8)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    GError* failure = nullptr;
    // Unlike wxExecute's async Unix fork result, GLib reports exec failures.
    const bool launched = g_spawn_async(workingDirectory.empty() ? nullptr : workingDirectory.utf8_str().data(),
                                       argv.data(), nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr,
                                       &failure) != FALSE;
    if (failure) {
        error = wxString::FromUTF8(failure->message);
        g_error_free(failure);
    }
    return launched;
#else
    std::vector<const wchar_t*> argv;
    for (const auto& argument : arguments)
        argv.push_back(argument.wc_str());
    argv.push_back(nullptr);
    wxExecuteEnv environment;
    environment.cwd = workingDirectory;
    const wxLogNull noGuiLog;
    const bool launched = wxExecute(argv.data(), wxEXEC_ASYNC, nullptr, &environment) > 0;
    if (!launched) error = wxSysErrorMsg();
    return launched;
#endif
}
