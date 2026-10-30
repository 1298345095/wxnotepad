#include "editor_frame.h"
#include "editor_control.h"
#include <wx/menu.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>

EditorFrame::EditorFrame()
    : wxFrame(nullptr, wxID_ANY, "Untitled - wxnotepad", wxDefaultPosition, wxSize(900, 600)),
      editor_(new EditorControl(this)), document_(*editor_) {
    auto* file = new wxMenu;
    file->Append(wxID_SAVE, "&Save...\tCtrl+S");
    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, "&Undo\tCtrl+Z");
    edit->Append(wxID_REDO, "&Redo\tCtrl+Y");
    auto* menus = new wxMenuBar;
    menus->Append(file, "&File");
    menus->Append(edit, "&Edit");
    SetMenuBar(menus);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        const auto result = Save();
        if (result.status == SaveStatus::Failed)
            wxMessageBox(result.error, "Could not save file", wxOK | wxICON_ERROR, this);
    }, wxID_SAVE);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(document_.Path().empty()); }, wxID_SAVE);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Undo); }, wxID_UNDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Redo); }, wxID_REDO);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanUndo)); }, wxID_UNDO);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanRedo)); }, wxID_REDO);
    Bind(wxEVT_EDITOR_STATE_CHANGED, [this](wxCommandEvent&) { UpdateDocumentState(); });
    UpdateDocumentState();
    editor_->SetFocus();
}

void EditorFrame::UpdateDocumentState() {
    const wxString name = document_.Path().empty() ? wxString("Untitled") : document_.Path();
    SetTitle((editor_->Send(Scintilla::Message::GetModify) ? "*" : "") + name + " - wxnotepad");
}

SaveResult EditorFrame::SaveFirst(const std::optional<wxString>& destination) {
    const auto result = document_.SaveFirst(destination);
    UpdateDocumentState();
    return result;
}

SaveResult EditorFrame::Save() {
    if (!document_.Path().empty()) return {SaveStatus::Failed, "This document already has a file name."};
    wxFileDialog dialog(this, "Save file", {}, {}, "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return SaveFirst(std::nullopt);
    return SaveFirst(dialog.GetPath());
}
