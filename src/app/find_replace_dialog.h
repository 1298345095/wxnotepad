#pragma once

#include "search/search_session.h"
#include <wx/dialog.h>
#include <functional>

class wxTextCtrl;
class wxStaticText;
class wxButton;
class wxCheckBox;
class EditorControl;

class FindReplaceDialog final : public wxDialog {
public:
    FindReplaceDialog(wxWindow* parent, EditorControl& editor, SearchSession& search,
                      std::function<void(const FindResult&)> reportFind, std::function<void(wxKeyEvent&)> onKey,
                      int selectionOptions = 15);
    void Present(bool replace);
    void Find(SearchDirection direction);

private:
    EditorControl& editor_;
    SearchSession& search_;
    std::function<void(const FindResult&)> reportFind_;
    wxTextCtrl* query_;
    wxTextCtrl* replacement_;
    wxCheckBox* matchCase_;
    wxCheckBox* wholeWord_;
    wxCheckBox* regularExpression_;
    wxCheckBox* transformBackslashes_;
    wxCheckBox* wrapAround_;
    wxStaticText* replacementLabel_;
    wxButton* replaceButton_;
    wxButton* replaceAllButton_;
    wxButton* replaceSelectionButton_;
    wxStaticText* replacementResult_;
    bool replace_ = false;
    int selectionOptions_;
    bool SeedQuery(bool firstTime);
    void ConfigureSearch();
    void ReplaceOne();
    void ReplaceAll(bool inSelection = false);
};
