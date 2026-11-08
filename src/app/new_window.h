#pragma once

#include <wx/string.h>

class wxTopLevelWindow;

// New Window passes normal geometry and maximized state to ordinary startup.
// Linux requests size/state only; the compositor chooses the position.
bool ApplyInitialWindowPosition(wxTopLevelWindow& window, const wxString& argument);
