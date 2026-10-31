#include "editor_frame.h"
#include "editor_control.h"
#include <wx/menu.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>

EditorFrame::EditorFrame()
    : wxFrame(nullptr, wxID_ANY, "Untitled - wxnotepad", wxDefaultPosition, wxSize(900, 600)),
      editor_(new EditorControl(this)), document_(*editor_) {
    auto* file = new wxMenu;
    file->Append(wxID_NEW, "&New\tCtrl+N");
    file->Append(wxID_OPEN, "&Open...\tCtrl+O");
    file->Append(wxID_SAVE, "&Save...\tCtrl+S");
    auto* edit = new wxMenu;
    edit->Append(wxID_UNDO, "&Undo\tCtrl+Z");
    edit->Append(wxID_REDO, "&Redo\tCtrl+Y");
    edit->AppendSeparator();
    edit->Append(wxID_CUT, "Cu&t\tCtrl+X");
    edit->Append(wxID_COPY, "&Copy\tCtrl+C");
    edit->Append(wxID_PASTE, "&Paste\tCtrl+V");
    auto* menus = new wxMenuBar;
    menus->Append(file, "&File");
    menus->Append(edit, "&Edit");
    SetMenuBar(menus);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { New(); }, wxID_NEW);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        const auto result = Open();
        if (result.status == OpenStatus::Failed)
            wxMessageBox(result.error, "Could not open file", wxOK | wxICON_ERROR, this);
    }, wxID_OPEN);
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
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Copy); }, wxID_COPY);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Cut); }, wxID_CUT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { editor_->Send(Scintilla::Message::Paste); }, wxID_PASTE);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) {
        event.Enable(editor_->Send(Scintilla::Message::GetSelectionStart) != editor_->Send(Scintilla::Message::GetSelectionEnd));
    }, wxID_COPY);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) {
        event.Enable(!editor_->Send(Scintilla::Message::GetReadOnly) &&
            editor_->Send(Scintilla::Message::GetSelectionStart) != editor_->Send(Scintilla::Message::GetSelectionEnd));
    }, wxID_CUT);
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(editor_->Send(Scintilla::Message::CanPaste)); }, wxID_PASTE);
    Bind(wxEVT_EDITOR_STATE_CHANGED, [this](wxCommandEvent&) { UpdateDocumentState(); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        if (event.CanVeto() && !ConfirmDiscardChanges("closing")) {
            event.Veto();
            return;
        }
        Destroy();
    });
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
    if (!document_.Path().empty()) {
        const auto result = document_.Save();
        UpdateDocumentState();
        return result;
    }
    wxFileDialog dialog(this, "Save file", {}, {}, "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return SaveFirst(std::nullopt);
    return SaveFirst(dialog.GetPath());
}

OpenResult EditorFrame::Open(const std::optional<wxString>& source) {
    const auto result = document_.Open(source, [this] { return ConfirmDiscardChanges("opening another file"); });
    UpdateDocumentState();
    return result;
}

OpenResult EditorFrame::Open() {
    wxFileDialog dialog(this, "Open file", {}, {}, "Text files (*.txt)|*.txt|All files (*.*)|*.*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return Open(std::nullopt);
    return Open(dialog.GetPath());
}

bool EditorFrame::New() {
    if (!ConfirmDiscardChanges("creating a new document")) return false;
    document_.New();
    UpdateDocumentState();
    return true;
}

bool EditorFrame::ConfirmDiscardChanges(const wxString& action) {
    if (!editor_->Send(Scintilla::Message::GetModify)) return true;
    const wxString name = document_.Path().empty() ? wxString("Untitled") : document_.Path();
    wxMessageDialog dialog(this, "Save changes to " + name + " before " + action + "?",
                           "Save changes?", wxYES_NO | wxCANCEL | wxICON_QUESTION);
    dialog.SetYesNoCancelLabels("&Save", "&Discard", "&Cancel");
    const int choice = dialog.ShowModal();
    if (choice == wxID_NO) return true;
    if (choice != wxID_YES) return false;
    const auto result = Save();
    if (result.status == SaveStatus::Failed)
        wxMessageBox(result.error, "Could not save file", wxOK | wxICON_ERROR, this);
    return result.status == SaveStatus::Saved;
}
