#pragma once

#include <optional>
#include <wx/cmndata.h>

class wxWindow;
struct PrintOptions;

struct PrintMarginDisplay {
    int unitsPerDisplayUnit;
    unsigned decimalPlaces;
    bool metric;
};

std::optional<wxPrintData> ShowPrintPageSetupDialog(wxWindow& parent, PrintOptions& options, const wxPrintData& driver,
                                                    PrintMarginDisplay units);
