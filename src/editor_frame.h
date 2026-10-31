#pragma once

#include <wx/frame.h>
#include "document_session.h"
#include "search_options.h"
#include "syntax_scheme.h"
#include "ScintillaTypes.h"

class EditorControl;
class wxDialog;
class wxTextCtrl;
class wxStaticText;
class wxButton;
class wxCheckBox;

class EditorFrame final : public wxFrame {
public:
    EditorFrame();
    EditorControl* Editor() const { return editor_; }
    const wxString& DocumentPath() const { return document_.Path(); }
    SaveResult Save(const GbkLossDecision& decideLoss = {});
    SaveResult SaveAs(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss = {});
    SaveResult SaveAs();
    SaveResult SaveFirst(const std::optional<wxString>& destination, const GbkLossDecision& decideLoss = {});
    OpenResult Open(const std::optional<wxString>& source, std::optional<FileEncoding> encoding = std::nullopt);
    OpenResult Open();
    OpenResult OpenAs(FileEncoding encoding);
    bool DocumentHasUtf8Bom() const { return document_.HasUtf8Bom(); }
    FileEncoding DocumentEncoding() const { return document_.Encoding(); }
    bool DocumentHasBom() const { return document_.HasBom(); }
    bool New();

private:
    EditorControl* editor_;
    DocumentSession document_;
    SyntaxLanguage language_ = SyntaxLanguage::PlainText;
    wxDialog* findDialog_ = nullptr;
    wxTextCtrl* findQuery_ = nullptr;
    SearchOptions searchOptions_;
    struct EmptyMatch {
        intptr_t position;
        uint64_t documentRevision;
        uint64_t selectionRevision;
    };
    std::optional<EmptyMatch> lastEmptyMatch_;
    struct RegexMatch {
        intptr_t start;
        intptr_t end;
        uint64_t documentRevision;
        uint64_t selectionRevision;
    };
    std::optional<RegexMatch> regexMatch_;
    Scintilla::FindOption SearchFlags() const;
    wxTextCtrl* replacement_ = nullptr;
    wxStaticText* replacementLabel_ = nullptr;
    wxButton* replaceButton_ = nullptr;
    wxButton* replaceAllButton_ = nullptr;
    wxStaticText* replacementResult_ = nullptr;
    wxCheckBox* wrapAround_ = nullptr;
    void ShowFind(bool replace = false);
    void FindNext();
    void ReplaceOne();
    void ReplaceAll();
    void FindPrevious();
    void Search(bool forward);
    void OnSearchKey(wxKeyEvent& event);
    void UpdateDocumentState();
    void RefreshLanguageAfterSave(const wxString& previousPath, const SaveResult& result);
    bool ConfirmDiscardChanges(const wxString& action);
    GbkLossChoice ConfirmGbkLoss();
};
