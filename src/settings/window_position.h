#pragma once

#include <wx/gdicmn.h>

// Original WindowInfo: outer rectangle, not editor/client dimensions. Windows
// uses WINDOWPLACEMENT workspace pixels; X11 uses GDK logical screen units.
struct WindowPosition {
    wxRect normal;
    bool maximized = false;
    bool coordinates = true;
    wxSize screen;
};
