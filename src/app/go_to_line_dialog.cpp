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
        wxString::Format("&Line (empty or 1 - %lld):", static_cast<long long>(editor.Send(Message::GetLineCount))));
    range->SetName("Go to line range");
    contents->Add(range, 0, wxALL, dialog.FromDIP(10));
    auto* input = new wxTextCtrl(
        &dialog, wxID_ANY,
        wxString::Format(
            "%lld",
            static_cast<long long>(editor.Send(Message::LineFromPosition, editor.Send(Message::GetCurrentPos))) + 1));
    input->SetName("Go to line number");
    input->SetMaxLength(20);
    contents->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    auto* columnRange = new wxStaticText(
        &dialog, wxID_ANY,
        wxString::Format("&Column (empty or 1 - %lld):", static_cast<long long>(editor.Send(Message::GetLength))));
    columnRange->SetName("Go to column range");
    contents->Add(columnRange, 0, wxALL, dialog.FromDIP(10));
    auto* column = new wxTextCtrl(&dialog, wxID_ANY);
    column->SetName("Go to column number");
    column->SetMaxLength(20);
    contents->Add(column, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    dialog.Bind(
        wxEVT_BUTTON,
        [&](wxCommandEvent&) {
            const auto result = GoToLocation(editor, input->GetValue(), column->GetValue());
            if (result == GoToResult::Moved) {
                dialog.EndModal(wxID_OK);
                return;
            }
            auto* invalid = result == GoToResult::InvalidColumn ? column : input;
            // Like the original posted WM_NEXTDLGCTL, move focus after the
            // command and pending native dialog initialization have completed.
            dialog.CallAfter([invalid] {
                invalid->SetSelection(-1, -1);
                invalid->SetFocus();
            });
        },
        wxID_OK);
    input->SetSelection(-1, -1);
    input->SetFocus();
    dialog.CentreOnParent();
    dialog.ShowModal();
    editor.SetFocus();
}
