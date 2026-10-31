#include "app/go_to_line_dialog.h"
#include "editor/editor_control.h"
#include "editor/line_navigation.h"
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

void ShowGoToLineDialog(wxWindow* parent, EditorControl& editor) {
    using Scintilla::Message;
    wxDialog dialog(parent, wxID_ANY, "Go To Line");
    dialog.SetName("Go to line");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    auto* range = new wxStaticText(
        &dialog, wxID_ANY,
        wxString::Format("Line number (1 to %lld):", static_cast<long long>(editor.Send(Message::GetLineCount))));
    range->SetName("Go to line range");
    contents->Add(range, 0, wxALL, dialog.FromDIP(10));
    auto* input = new wxTextCtrl(
        &dialog, wxID_ANY,
        wxString::Format(
            "%lld",
            static_cast<long long>(editor.Send(Message::LineFromPosition, editor.Send(Message::GetCurrentPos))) + 1));
    input->SetName("Go to line number");
    contents->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    auto* error = new wxStaticText(&dialog, wxID_ANY, "");
    error->SetName("Go to line error");
    contents->Add(error, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    dialog.Bind(
        wxEVT_BUTTON,
        [&](wxCommandEvent&) {
            if (GoToLogicalLine(editor, input->GetValue())) {
                dialog.EndModal(wxID_OK);
                return;
            }
            error->SetLabel(wxString::Format("Enter a whole line number from 1 to %lld.",
                                             static_cast<long long>(editor.Send(Message::GetLineCount))));
            contents->Fit(&dialog);
            dialog.Layout();
            input->SetSelection(-1, -1);
            input->SetFocus();
        },
        wxID_OK);
    input->SetSelection(-1, -1);
    input->SetFocus();
    dialog.CentreOnParent();
    dialog.ShowModal();
    editor.SetFocus();
}
