#pragma once

#include <wx/frame.h>
#include "document_session.h"

class EditorControl;

class EditorFrame final : public wxFrame {
public:
    EditorFrame();
    EditorControl* Editor() const { return editor_; }
    const wxString& DocumentPath() const { return document_.Path(); }
    SaveResult Save();
    SaveResult SaveFirst(const std::optional<wxString>& destination);

private:
    EditorControl* editor_;
    DocumentSession document_;
    void UpdateDocumentState();
};
