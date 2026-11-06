#include "app/dialogs/align_lines_dialog.h"
#include "editor/align_lines.h"
#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <array>

bool ShowAlignLinesDialog(wxWindow* parent, AlignmentMode& mode) {
    // Notepad4 89b8b36 EditAlignDlgProc and IDD_ALIGN. The accepted choice is
    // committed on OK, and the caller applies it after the modal dialog closes.
    wxDialog dialog(parent, wxID_ANY, "Align Lines");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    const std::array<const char*, 5> labels{"&Left.", "&Right.", "&Center.", "&Justify.", "Justify (&Paragraph mode)."};
    const std::array<const char*, 5> names{"Left", "Right", "Center", "Justify", "Justify (Paragraph mode)"};
    std::array<wxRadioButton*, 5> choices{};
    for (size_t i = 0; i < choices.size(); ++i) {
        choices[i] =
            new wxRadioButton(&dialog, wxID_ANY, labels[i], wxDefaultPosition, wxDefaultSize, i == 0 ? wxRB_GROUP : 0);
        choices[i]->SetName(names[i]);
        contents->Add(choices[i], 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
    }
    choices[static_cast<size_t>(mode)]->SetValue(true);
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return false;
    for (size_t i = 0; i < choices.size(); ++i) {
        if (choices[i]->GetValue()) mode = static_cast<AlignmentMode>(i);
    }
    return true;
}
