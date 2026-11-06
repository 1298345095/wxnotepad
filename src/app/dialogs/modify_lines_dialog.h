#pragma once

#include <wx/string.h>
class wxWindow;
class EditorControl;

void ShowModifyLinesDialog(wxWindow* parent, EditorControl& editor, wxString& before, wxString& after, bool& skipEmpty);
