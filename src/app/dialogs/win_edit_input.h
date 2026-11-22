#pragma once

#include <wx/string.h>

class wxDialog;
class wxSizer;
class wxTextCtrl;

// Multiline fields retain original WinEdit CRLF text between dialog sessions.
wxTextCtrl* AddWinEditInput(wxDialog& dialog, wxSizer& contents, const wxString& label, const wxString& name,
                            const wxString& value);
wxString ReadWinEditInput(const wxTextCtrl& input);
