#pragma once

#include "search/search_session.h"
#include <wx/dialog.h>
#include <functional>

class wxTextCtrl;
class wxStaticText;
class wxButton;
class wxCheckBox;

class FindReplaceDialog final : public wxDialog {
public:
    FindReplaceDialog(wxWindow* parent, wxWindow& editor, SearchSession& search,
                      std::function<void(const FindResult&)> reportFind, std::function<void(wxKeyEvent&)> onKey);
    ~FindReplaceDialog() override;
    void Present(bool replace);
    void Find(SearchDirection direction);

private:
    SearchSession& search_;
    std::function<void(const FindResult&)> reportFind_;
    wxTextCtrl* query_;
    wxTextCtrl* replacement_;
    wxCheckBox* matchCase_;
    wxCheckBox* wholeWord_;
    wxCheckBox* regularExpression_;
    wxCheckBox* wrapAround_;
    wxCheckBox* bookmarkMatchedLines_;
    wxButton* findAllButton_;
    wxStaticText* findAllResult_;
    wxStaticText* replacementLabel_;
    wxButton* replaceButton_;
    wxButton* replaceAllButton_;
    wxButton* replaceSelectionButton_;
    wxStaticText* replacementResult_;
    void ConfigureSearch();
    void FindAll();
    void OnEditorState(wxCommandEvent& event);
    void ReplaceOne();
    void ReplaceAll(bool inSelection = false);
};
