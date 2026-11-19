#pragma once

#include <wx/display.h>
#include <wx/window.h>
#include <algorithm>

namespace NativePopup {
// Shared native-coordinate placement for completion, CallTip and editor menus.
inline wxRect FitToWorkArea(wxWindow& relative, wxPoint point, wxSize size) {
    int display = wxDisplay::GetFromPoint(point);
    if (display == wxNOT_FOUND) display = wxDisplay::GetFromWindow(&relative);
    if (display == wxNOT_FOUND) display = 0;
    const auto bounds = wxDisplay(static_cast<unsigned>(display)).GetClientArea();
    size.x = std::min(size.x, bounds.width);
    size.y = std::min(size.y, bounds.height);
    point.x = std::clamp(point.x, bounds.x, bounds.GetRight() + 1 - size.x);
    point.y = std::clamp(point.y, bounds.y, bounds.GetBottom() + 1 - size.y);
    return wxRect(point, size);
}
} // namespace NativePopup
