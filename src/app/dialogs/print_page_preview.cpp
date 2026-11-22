#include "app/dialogs/print_page_preview.h"
#include "app/dialogs/print_page_setup_dialog.h"
#include <wx/dc.h>
#include <wx/cmndata.h>
#include <wx/paper.h>
#include <algorithm>
#include <utility>

namespace {
wxSize PaperSize(const wxPrintData& data) {
    auto size = data.GetPaperSize();
    if (size.x <= 0 || size.y <= 0) {
        // Standard paper IDs need not have custom dimensions in printer data.
        if (const auto* paper = wxThePrintPaperDatabase->FindPaperType(data.GetPaperId())) size = paper->GetSizeMM();
    }
    if (data.GetOrientation() == wxLANDSCAPE) std::swap(size.x, size.y);
    return size;
}
} // namespace

void DrawPrintPagePreview(wxDC& dc, const wxRect& bounds, const wxPrintData& data,
                          const std::array<double, 4>& displayMargins, const PrintMarginDisplay& units) {
    const auto paper = PaperSize(data);
    if (paper.x <= 0 || paper.y <= 0) return;
    const double scale =
        std::min(static_cast<double>(bounds.width) / paper.x, static_cast<double>(bounds.height) / paper.y);
    const wxSize page(static_cast<int>(paper.x * scale), static_cast<int>(paper.y * scale));
    wxRect rectangle(bounds.x + (bounds.width - page.x) / 2, bounds.y + (bounds.height - page.y) / 2, page.x, page.y);
    dc.SetBrush(*wxWHITE_BRUSH);
    dc.SetPen(*wxBLACK_PEN);
    dc.DrawRectangle(rectangle);
    std::array<int, 4> inset{};
    constexpr double MillimetersPerInch = 25.4;
    const double millimetersPerDisplayUnit = units.metric ? 1 : MillimetersPerInch;
    for (size_t index = 0; index < inset.size(); ++index)
        inset[index] = static_cast<int>(displayMargins[index] * millimetersPerDisplayUnit * scale);
    rectangle.x += inset[0];
    rectangle.y += inset[1];
    rectangle.width -= inset[0] + inset[2];
    rectangle.height -= inset[1] + inset[3];
    if (rectangle.width > 0 && rectangle.height > 0) {
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.SetPen(wxPen(*wxBLACK, 1, wxPENSTYLE_DOT));
        dc.DrawRectangle(rectangle);
    }
}
