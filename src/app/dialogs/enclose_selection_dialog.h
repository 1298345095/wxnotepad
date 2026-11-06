#pragma once

#include <wx/string.h>

class wxWindow;
class EditorControl;

void ShowEncloseSelectionDialog(wxWindow* parent, EditorControl& editor, wxString& before, wxString& after);
