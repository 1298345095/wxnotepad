#pragma once

class EditorControl;
class wxPrintData;
class wxString;
class wxWindow;
struct PrintOptions;

namespace EditorPrinting {
void Print(wxWindow& parent, EditorControl& editor, const wxString& title, PrintOptions& options, wxPrintData& driver);
}
