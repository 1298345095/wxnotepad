#include "app/dialogs/zoom_level_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/dialogs/zoom_input.h"
#include "app/localization.h"
#include "editor/editor_control.h"
#include <memory>
#include <wx/button.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/translation.h>

void ShowZoomLevelDialog(wxWindow* parent, EditorControl& editor) {
    using Scintilla::Message;
    // Original Dialogs.cpp::ZoomLevelDlgProc and InitZoomLevelComboBox.
    wxDialog dialog(parent, wxID_ANY, _("Zoom Level"));
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY,
                                   wxString::Format(_("Zoom level (%d%% to %d%%):"), Scintilla::MinZoomLevel,
                                                    Scintilla::MaxZoomLevel)),
                  0, wxALL, dialog.FromDIP(10));
    auto* input = CreateZoomInput(&dialog, static_cast<int>(editor.Send(Message::GetZoom)));
    input->SetName("Zoom level");
    AppLocalization::SetAccessibleName(*input, _("Zoom level"));
    auto zoomRow = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
    zoomRow->Add(input, 1, wxRIGHT, dialog.FromDIP(10));
    zoomRow->Add(new wxButton(&dialog, wxID_APPLY));
    contents->Add(zoomRow.release(), 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    const auto apply = [input, &editor, &dialog](wxCommandEvent& event) {
        if (const auto level = ReadZoomLevel(*input)) editor.Send(Message::SetZoom, *level);
        // As in the original, invalid OK closes without changing zoom; Apply
        // keeps the dialog open, and Cancel retains any already applied zoom.
        if (event.GetId() == wxID_OK) dialog.EndModal(wxID_OK);
    };
    dialog.Bind(wxEVT_BUTTON, apply, wxID_OK);
    dialog.Bind(wxEVT_BUTTON, apply, wxID_APPLY);
    DialogLayout::Fit(dialog, contents);
    input->SetSelection(-1, -1);
    input->SetFocus();
    dialog.CentreOnParent();
    dialog.ShowModal();
    editor.SetFocus();
}
