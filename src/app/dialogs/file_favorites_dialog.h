#pragma once
#include <wx/string.h>
#include <optional>
class wxWindow;
std::optional<wxString> ChooseFileFavorite(wxWindow* parent, wxString& directory);
std::optional<wxString> NameFileFavorite(wxWindow* parent, const wxString& initialName);
