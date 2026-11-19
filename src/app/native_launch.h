#pragma once

#include <wx/string.h>
#include <vector>

namespace NativeLaunch {
bool SpawnArguments(const std::vector<wxString>& arguments, const wxString& workingDirectory, wxString& error);
}
