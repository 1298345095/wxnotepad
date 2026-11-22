#pragma once

#include <wx/string.h>

#ifdef __WXMSW__
// Expand native %NAME% variables, retaining the input when expansion fails.
wxString ExpandWindowsEnvironment(const wxString& value);
#endif
