#pragma once

#include <wx/frame.h>

class EditorControl;

class EditorFrame final : public wxFrame {
public:
    EditorFrame();
    EditorControl* Editor() const { return editor_; }

private:
    EditorControl* editor_;
    void UpdateDocumentState();
};
