#pragma once

#include <wx/gdicmn.h>
#include <array>

class wxDC;
class wxPrintData;
struct PrintMarginDisplay;

// Draw the current paper and printable area into the supplied logical bounds.
// Interpret margins in the same metric/inch display units as the real dialog.
void DrawPrintPagePreview(wxDC& dc, const wxRect& bounds, const wxPrintData& data,
                          const std::array<double, 4>& displayMargins, const PrintMarginDisplay& units);
