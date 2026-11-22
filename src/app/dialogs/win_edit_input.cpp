#include "app/dialogs/win_edit_input.h"
#include "app/localization.h"
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

wxTextCtrl* AddWinEditInput(wxDialog& dialog, wxSizer& contents, const wxString& label, const wxString& name,
                            const wxString& value) {
    contents.Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
    auto display = value;
    display.Replace("\r\n", "\n");
    auto* input = new wxTextCtrl(&dialog, wxID_ANY, display, wxDefaultPosition,
                                 wxSize(dialog.FromDIP(400), dialog.GetCharHeight() + dialog.FromDIP(12)),
                                 wxTE_MULTILINE | wxTE_PROCESS_ENTER);
    input->SetName(name);
    AppLocalization::SetAccessibleName(*input, wxControl::GetLabelText(label));
    contents.Add(input, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
    return input;
}

wxString ReadWinEditInput(const wxTextCtrl& input) {
    // wx multiline controls expose LF on every platform. Reconstruct the
    // WinEdit separators consumed by the original editing operations.
    auto text = input.GetValue();
    text.Replace("\n", "\r\n");
    return text;
}
