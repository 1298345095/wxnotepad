#pragma once

#include <wx/string.h>
#include <vector>

namespace RecentFiles {
inline constexpr int MinimumCapacity = 32;
bool Equal(const wxString& left, const wxString& right);
void Add(std::vector<wxString>& history, const wxString& path, int capacity);
wxString ExpandPath(wxString path);
wxString PortablePath(const wxString& path, bool relative, bool portableDocuments);
} // namespace RecentFiles
