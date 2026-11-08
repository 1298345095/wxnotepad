#include "app/dialogs/column_wrap_dialog.h"
#include "app/dialogs/column_input.h"
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>

bool ShowColumnWrapDialog(wxWindow* parent, int& column, int boundary) {
    wxDialog dialog(parent, wxID_ANY, "Column Wrap");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY, "&Boundary:"), 0, wxALL, dialog.FromDIP(10));
    auto* input = new wxTextCtrl(&dialog, wxID_ANY, wxString::Format("%d", column ? column : boundary));
    input->SetName("Boundary");
    input->SetMaxLength(15);
    input->Bind(wxEVT_CHAR, [](wxKeyEvent& event) {
        const wxUniChar ch(event.GetUnicodeKey());
        if (ch.GetValue() < 32 || ColumnInputDigit(ch.GetValue()) >= 0 || event.ControlDown() || event.AltDown())
            event.Skip();
    });
    contents->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    input->SetFocus();
    input->SelectAll();
    dialog.CentreOnParent();
    dialog.Bind(
        wxEVT_BUTTON,
        [input, &column, &dialog](wxCommandEvent&) {
            const auto number = ReadColumnInput(input->GetValue());
            if (!number.has_value()) {
                input->SetFocus();
                input->SelectAll();
                return;
            }
            column = std::clamp(*number, 1, 512);
            dialog.EndModal(wxID_OK);
        },
        wxID_OK);
    return dialog.ShowModal() == wxID_OK;
}
