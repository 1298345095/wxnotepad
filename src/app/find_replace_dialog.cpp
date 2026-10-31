#include "app/find_replace_dialog.h"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <utility>

FindReplaceDialog::FindReplaceDialog(wxWindow* parent, wxWindow& editor, SearchSession& search,
                                     std::function<void(const FindResult&)> reportFind,
                                     std::function<void(wxKeyEvent&)> onKey)
    : wxDialog(parent, wxID_ANY, "Find"), search_(search), reportFind_(std::move(reportFind)) {
    const auto& request = search_.Request();
    auto* layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(new wxStaticText(this, wxID_ANY, "Find &what:"), 0, wxALL, 8);
    query_ = new wxTextCtrl(this, wxID_ANY, request.query, wxDefaultPosition, wxSize(320, -1), wxTE_PROCESS_ENTER);
    query_->SetName("Find query");
    layout->Add(query_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    matchCase_ = new wxCheckBox(this, wxID_ANY, "Match &case");
    matchCase_->SetName("Match case");
    matchCase_->SetValue(request.options.matchCase);
    layout->Add(matchCase_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    wholeWord_ = new wxCheckBox(this, wxID_ANY, "Whole &word");
    wholeWord_->SetName("Whole word");
    wholeWord_->SetValue(request.options.wholeWord);
    wholeWord_->SetToolTip("Applies to literal searches. Use word boundaries in a regular expression.");
    wholeWord_->Enable(!request.options.regularExpression);
    layout->Add(wholeWord_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    regularExpression_ = new wxCheckBox(this, wxID_ANY, "Regular &expression");
    regularExpression_->SetName("Regular expression");
    regularExpression_->SetValue(request.options.regularExpression);
    layout->Add(regularExpression_, 0, wxLEFT | wxRIGHT | wxTOP, 8);
    replacementLabel_ = new wxStaticText(this, wxID_ANY, "Replace &with:");
    layout->Add(replacementLabel_, 0, wxALL, 8);
    replacement_ = new wxTextCtrl(this, wxID_ANY, request.replacement, wxDefaultPosition, wxSize(320, -1));
    replacement_->SetName("Replacement text");
    layout->Add(replacement_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
    wrapAround_ = new wxCheckBox(this, wxID_ANY, "&Wrap around");
    wrapAround_->SetName("Wrap around");
    wrapAround_->SetValue(request.wrapAround);
    layout->Add(wrapAround_, 0, wxALL, 8);
    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* next = new wxButton(this, wxID_FORWARD, "Find &Next");
    next->SetDefault();
    buttons->Add(next, 0, wxRIGHT, 8);
    replaceButton_ = new wxButton(this, wxID_REPLACE, "&Replace");
    buttons->Add(replaceButton_, 0, wxRIGHT, 8);
    replaceAllButton_ = new wxButton(this, wxID_REPLACE_ALL, "Replace &All");
    buttons->Add(replaceAllButton_, 0, wxRIGHT, 8);
    buttons->Add(new wxButton(this, wxID_BACKWARD, "Find &Previous"), 0, wxRIGHT, 8);
    buttons->Add(new wxButton(this, wxID_CANCEL, "Close"));
    layout->Add(buttons, 0, wxALL | wxALIGN_RIGHT, 8);
    replacementResult_ = new wxStaticText(this, wxID_ANY, "");
    replacementResult_->SetName("Replacement result");
    layout->Add(replacementResult_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    SetSizerAndFit(layout);

    query_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        search_.ResetMatch();
        ConfigureSearch();
        event.Skip();
    });
    replacement_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        ConfigureSearch();
        event.Skip();
    });
    for (auto* option : {matchCase_, wholeWord_, regularExpression_}) {
        option->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
            search_.ResetMatch();
            wholeWord_->Enable(!regularExpression_->GetValue());
            ConfigureSearch();
        });
    }
    wrapAround_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { ConfigureSearch(); });
    const wxAcceleratorEntry searchKeys[] = {{wxACCEL_NORMAL, WXK_F3, wxID_FORWARD},
                                             {wxACCEL_SHIFT, WXK_F3, wxID_BACKWARD}};
    SetAcceleratorTable(wxAcceleratorTable(2, searchKeys));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Find(SearchDirection::Forward); }, wxID_FORWARD);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Find(SearchDirection::Backward); }, wxID_BACKWARD);
    Bind(wxEVT_CHAR_HOOK, [onKey = std::move(onKey)](wxKeyEvent& event) { onKey(event); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Find(SearchDirection::Forward); }, wxID_FORWARD);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Find(SearchDirection::Backward); }, wxID_BACKWARD);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceOne(); }, wxID_REPLACE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ReplaceAll(); }, wxID_REPLACE_ALL);
    query_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Find(SearchDirection::Forward); });
    const auto close = [this, &editor] {
        Hide();
        editor.SetFocus();
    };
    Bind(wxEVT_BUTTON, [close](wxCommandEvent&) { close(); }, wxID_CANCEL);
    Bind(wxEVT_CLOSE_WINDOW, [close](wxCloseEvent&) { close(); });
}

void FindReplaceDialog::Present(bool replace) {
    SetTitle(replace ? "Replace" : "Find");
    replacementLabel_->Show(replace);
    replacement_->Show(replace);
    replaceButton_->Show(replace);
    replaceAllButton_->Show(replace);
    replacementResult_->Show(replace);
    GetSizer()->Fit(this);
    Show();
    query_->SetFocus();
    query_->SelectAll();
}

void FindReplaceDialog::ConfigureSearch() {
    search_.Configure({query_->GetValue(),
                       replacement_->GetValue(),
                       {matchCase_->GetValue(), wholeWord_->GetValue(), regularExpression_->GetValue()},
                       wrapAround_->GetValue()});
}

void FindReplaceDialog::Find(SearchDirection direction) {
    ConfigureSearch();
    reportFind_(search_.Find(direction));
}

void FindReplaceDialog::ReplaceOne() {
    ConfigureSearch();
    const auto result = search_.ReplaceOne();
    if (result.next) reportFind_(*result.next);
}

void FindReplaceDialog::ReplaceAll() {
    ConfigureSearch();
    const auto result = search_.ReplaceAll();
    if (result.status == ReplaceAllStatus::ExpansionFailed) return;
    replacementResult_->SetLabel(result.status == ReplaceAllStatus::InvalidPattern
                                     ? wxString("Invalid regular expression")
                                     : wxString::Format("%zu replacements", result.count));
    GetSizer()->Fit(this);
}
