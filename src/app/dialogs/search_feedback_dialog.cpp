#include "app/dialogs/search_feedback_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include <wx/translation.h>
#include <wx/artprov.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>

SearchFeedbackDialog::SearchFeedbackDialog(wxWindow* parent, const wxString& key, const wxString& message, bool confirm,
                                           bool allowSuppression, const wxString& title, bool yesNo)
    : wxDialog(parent, wxID_ANY, title) {
    SetName(title == "wxnotepad" ? "Search feedback" : "Encoding warning");
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* contents = new wxBoxSizer(wxHORIZONTAL);
    contents->Add(
        new wxStaticBitmap(this, wxID_ANY,
                           wxArtProvider::GetBitmapBundle(
                               confirm && title == "wxnotepad" ? wxART_INFORMATION : wxART_WARNING, wxART_MESSAGE_BOX)),
        0, wxALL, FromDIP(16));
    auto* text = new wxStaticText(this, wxID_ANY, message);
    text->SetName(key);
    contents->Add(text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(16));
    layout->Add(contents);
    suppress_ = new wxCheckBox(this, wxID_ANY, _("&Don't show this message again."));
    suppress_->SetName(title == "wxnotepad" ? "Suppress search message" : "Suppress message");
    suppress_->Enable(allowSuppression);
    auto footer = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
    footer->Add(suppress_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(16));
    footer->AddStretchSpacer();
    footer->Add(CreateStdDialogButtonSizer(yesNo     ? wxYES | wxNO
                                           : confirm ? wxOK | wxCANCEL
                                                     : wxOK),
                0, wxALIGN_CENTER_VERTICAL);
    layout->Add(footer.release(), 0, wxEXPAND | wxALL, FromDIP(DialogLayout::MarginDip));
    if (yesNo) {
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_YES); }, wxID_YES);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_NO); }, wxID_NO);
    }
    DialogLayout::Fit(*this, layout);
    CentreOnParent();
}

SearchFeedbackChoice SearchFeedbackDialog::Choose() {
    const int response = ShowModal();
    return {response == wxID_OK || response == wxID_YES, suppress_->GetValue()};
}
