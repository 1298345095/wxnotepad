#pragma once

#include <wx/string.h>

class EditorControl;

// Accepts a 1-based absolute logical line in the current core document.
bool GoToLogicalLine(EditorControl& editor, const wxString& input);
