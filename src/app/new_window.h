#pragma once

#include <wx/string.h>
#include <optional>
#include "settings/window_position.h"

class EditorFrame;

// New Window passes normal geometry and maximized state to ordinary startup.
// Linux requests size/state only; the compositor chooses the position.
bool ApplyInitialWindowPosition(EditorFrame& window, const wxString& argument);
bool HasGlobalWindowCoordinates();
std::optional<WindowPosition> ParseWindowPosition(const wxString& argument);
bool ApplyWindowPosition(EditorFrame& window, const WindowPosition& position);
bool ApplyPublicWindowPosition(EditorFrame& window, const wxString& argument);
void SetInitialWindowRectangle(EditorFrame& window, const wxRect& rectangle, int sizeFlags);
