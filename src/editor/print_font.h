#pragma once

#include <wx/dc.h>
#include <wx/display.h>
#include <wx/font.h>

// wxPrinterDC uses the standard screen PPI for wxFont point sizes. Compensate
// with this printer's PPI before the DC measures or draws the font.
inline wxFont FontForPrinter(wxFont font, const wxDC& dc) {
    const auto scale = static_cast<double>(dc.GetPPI().y) / wxDisplay::GetStdPPIValue();
    font.SetFractionalPointSize(font.GetFractionalPointSize() * scale);
    return font;
}
