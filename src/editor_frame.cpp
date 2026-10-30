#include "editor_frame.h"
#include "editor_control.h"
#include <wx/menu.h>

EditorFrame::EditorFrame()
    : wxFrame(nullptr, wxID_ANY, "Untitled - wxnotepad", wxDefaultPosition, wxSize(900, 600)),
      editor_(new EditorControl(this)) {
    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, "&Undo\tCtrl+Z");
    edit->Append(wxID_REDO, "&Redo\tCtrl+Y");
    auto* menus = new wxMenuBar;
    menus->Append(edit, "&Edit");
    SetMenuBar(menus);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Undo); }, wxID_UNDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Redo); }, wxID_REDO);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanUndo)); }, wxID_UNDO);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanRedo)); }, wxID_REDO);
    Bind(wxEVT_EDITOR_STATE_CHANGED, [this](wxCommandEvent&) { UpdateDocumentState(); });
    UpdateDocumentState();
    editor_->SetFocus();
}

void EditorFrame::UpdateDocumentState() {
    SetTitle(editor_->Send(Scintilla::Message::GetModify) ? "*Untitled - wxnotepad" : "Untitled - wxnotepad");
}
