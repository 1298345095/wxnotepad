#include "settings/environment_expansion.h"

#ifdef __WXMSW__
#include <windows.h>
#include <vector>

wxString ExpandWindowsEnvironment(const wxString& value) {
    const auto length = ExpandEnvironmentStringsW(value.wc_str(), nullptr, 0);
    if (!length) return value;
    std::vector<wchar_t> expanded(length);
    const auto written = ExpandEnvironmentStringsW(value.wc_str(), expanded.data(), length);
    return written && written <= length ? wxString(expanded.data()) : value;
}
#else
#include <wx/utils.h>

wxString ExpandWindowsEnvironment(const wxString& value) {
    wxString expanded;
    size_t start = 0;
    while (start < value.length()) {
        const auto opening = value.find('%', start);
        if (opening == wxString::npos) break;
        const auto closing = value.find('%', opening + 1);
        if (closing == wxString::npos) break;
        expanded += value.Mid(start, opening - start);
        wxString environment;
        if (wxGetEnv(value.Mid(opening + 1, closing - opening - 1), &environment)) {
            expanded += environment;
            start = closing + 1;
        } else {
            // ExpandEnvironmentStrings reconsiders an unknown pair's closing
            // percent. URI %xx bytes must not hide a later known %NAME% field.
            expanded += '%';
            start = opening + 1;
        }
    }
    return expanded + value.Mid(start);
}
#endif
