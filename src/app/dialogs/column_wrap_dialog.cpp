#include "app/dialogs/column_wrap_dialog.h"
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>
#include <charconv>
#include <cstdint>

bool ShowColumnWrapDialog(wxWindow* parent, int& column, int boundary) {
    wxDialog dialog(parent, wxID_ANY, "Column Wrap");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY, "&Boundary:"), 0, wxALL, dialog.FromDIP(10));
    auto* input = new wxTextCtrl(&dialog, wxID_ANY, wxString::Format("%d", column ? column : boundary));
    input->SetName("Boundary");
    input->SetMaxLength(15);
    input->Bind(wxEVT_CHAR, [](wxKeyEvent& event) {
        const auto ch = event.GetUnicodeKey();
        if ((ch >= 0 && ch < 32) || (ch >= '0' && ch <= '9') || event.ControlDown() || event.AltDown()) event.Skip();
    });
    contents->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    input->SetFocus();
    input->SelectAll();
    dialog.CentreOnParent();
    dialog.Bind(
        wxEVT_BUTTON,
        [&](wxCommandEvent&) {
            // Original unsigned GetDlgItemInt accepts leading spaces, requires a
            // complete decimal value, then stores its UINT result in a signed int.
            const auto value = input->GetValue().ToStdString(wxConvUTF8);
            const auto first = value.find_first_not_of(' ');
            uint32_t number = 0;
            bool valid = first != std::string::npos;
            if (valid) {
                const auto result = std::from_chars(value.data() + first, value.data() + value.size(), number);
                valid = result.ec == std::errc{} && result.ptr == value.data() + value.size();
            }
            if (!valid) {
                input->SetFocus();
                input->SelectAll();
                return;
            }
            column = std::clamp(static_cast<int32_t>(number), 1, 512);
            dialog.EndModal(wxID_OK);
        },
        wxID_OK);
    return dialog.ShowModal() == wxID_OK;
}
