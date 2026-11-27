#pragma once
#include <wx/string.h>
#include <optional>

namespace FileFavorites {
wxString Directory(const wxString& configured);
wxString DefaultName(const wxString& target);
// Returns a localized error; an empty result means the new link was created.
wxString Add(const wxString& directory, const wxString& name, const wxString& target);
std::optional<wxString> Resolve(const wxString& entry);
wxString DisplayName(const wxString& entry);
} // namespace FileFavorites
