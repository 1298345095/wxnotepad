#pragma once

#include <wx/frame.h>
#include "document_session.h"

class EditorControl;

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
    void UpdateDocumentState();
    bool ConfirmDiscardChanges(const wxString& action);
    GbkLossChoice ConfirmGbkLoss();
};
