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
#endif
