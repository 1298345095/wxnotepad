#pragma once

#include <wx/dialog.h>

class wxCheckBox;

struct SearchFeedbackChoice {
    bool accepted;
    bool suppress;
};

class SearchFeedbackDialog final : public wxDialog {
public:
    SearchFeedbackDialog(wxWindow* parent, const wxString& key, const wxString& message, bool confirm,
                         bool allowSuppression, const wxString& title = "wxnotepad", bool yesNo = false);
    SearchFeedbackChoice Choose();

private:
    wxCheckBox* suppress_;
};
