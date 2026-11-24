#pragma once

#include <wx/string.h>

// Expand native %NAME% variables, retaining the input when expansion fails.
wxString ExpandWindowsEnvironment(const wxString& value);
