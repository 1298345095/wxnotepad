#pragma once

#include <wx/string.h>

class EditorControl;

enum class GoToResult : unsigned char { Moved, InvalidLine, InvalidColumn };

// Original Go To fields: decimal prefixes, tab/character columns, or byte position.
GoToResult GoToLocation(EditorControl& editor, const wxString& line, const wxString& column);

// Accepts a 1-based logical line, with the original empty-column default.
bool GoToLogicalLine(EditorControl& editor, const wxString& input);
