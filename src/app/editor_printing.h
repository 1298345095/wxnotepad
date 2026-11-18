#pragma once

#include "settings/print_options.h"

class wxPrintData;
class wxWindow;

namespace EditorPrinting {
int InitializeMargins(PrintOptions& options);
wxPrintData CopyPrinterData(const wxPrintData& original);
void PageSetup(wxWindow& parent, PrintOptions& options, wxPrintData& driver);
} // namespace EditorPrinting
