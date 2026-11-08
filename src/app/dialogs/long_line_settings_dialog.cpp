#include "app/dialogs/long_line_settings_dialog.h"
#include "settings/original_settings_constants.h"
#include "app/dialogs/column_input.h"
#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>

bool ShowLongLineSettingsDialog(wxWindow* parent, int& limit, int& mode) {
    wxDialog dialog(parent, wxID_ANY, "Long Lines Settings");
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->Add(new wxStaticText(&dialog, wxID_ANY, "&Limit for long lines:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
             dialog.FromDIP(10));
    auto* input = new wxTextCtrl(&dialog, wxID_ANY, wxString::Format("%d", limit));
    input->SetName("Long line limit");
    input->SetMaxLength(15);
    input->Bind(wxEVT_CHAR, [](wxKeyEvent& event) {
        const wxUniChar character(event.GetUnicodeKey());
        if (character.GetValue() < 32 || ColumnInputDigit(character.GetValue()) >= 0 || event.ControlDown() ||
            event.AltDown())
            event.Skip();
    });
    row->Add(input, 1);
    layout->Add(row, 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    auto* line = new wxRadioButton(&dialog, wxID_ANY, "Show &edge line.", wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    auto* background = new wxRadioButton(&dialog, wxID_ANY, "Change &background color.");
    line->SetName("Edge line");
    background->SetName("Background color");
    line->SetValue(mode == 1);
    background->SetValue(mode != 1);
    layout->Add(line, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(10));
    layout->Add(background, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(10));
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(layout);
    input->SetFocus();
    input->SelectAll();
    dialog.CentreOnParent();
    dialog.Bind(
        wxEVT_BUTTON,
        [input, line, &limit, &mode, &dialog](wxCommandEvent&) {
            const auto number = ReadColumnInput(input->GetValue());
            if (!number.has_value()) {
                input->SetFocus();
                input->SelectAll();
                return;
            }
            limit = std::clamp(*number, 0, OriginalSettingLimits::MaxLongLineColumn);
            mode = line->GetValue() ? 1 : 2;
            dialog.EndModal(wxID_OK);
        },
        wxID_OK);
    return dialog.ShowModal() == wxID_OK;
}
