#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include <wx/dialog.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/radiobox.h>
#include <wx/sizer.h>

void EditorFrame::SetFileWatchingOptions(const FileWatchingOptions& options) {
    fileWatchingOptions_ = options;
    RefreshFileWatching();
}

void EditorFrame::RefreshFileWatching() {
    fileChangeMonitor_.Refresh(document_.Path(), fileWatchingOptions_);
}

void EditorFrame::ShowFileChangeNotificationOptions() {
    wxDialog dialog(this, wxID_ANY, "File Change Notification");
    auto* layout = new wxBoxSizer(wxVERTICAL);
    const wxString choices[] = {"&None.", "&Show message.", "&Auto-reload (unmodified)."};
    auto* mode =
        new wxRadioBox(&dialog, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 3, choices, 1, wxRA_SPECIFY_COLS);
    mode->SetSelection(static_cast<int>(fileWatchingOptions_.mode));
    layout->Add(mode, 1, wxEXPAND | wxALL, dialog.FromDIP(10));
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(layout);
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return;
    auto options = fileWatchingOptions_;
    options.mode = static_cast<FileWatchingMode>(mode->GetSelection());
    SetFileWatchingOptions(options);
}

void EditorFrame::HandleFileChange() {
    bool terminate = false;
    if (wxFileName::FileExists(document_.Path())) {
        const bool automatic = fileWatchingOptions_.mode == FileWatchingMode::AutoReload;
        if ((automatic && !editor_->Send(Scintilla::Message::GetModify)) ||
            wxMessageBox("The current file has been modified by an external program. Reload?",
                         "File Change Notification", wxYES_NO | wxICON_WARNING, this) == wxYES) {
            const bool followEnd = automatic && editor_->Send(Scintilla::Message::LineFromPosition,
                                                              editor_->Send(Scintilla::Message::GetCurrentPos)) +
                                                        1 ==
                                                    editor_->Send(Scintilla::Message::GetLineCount);
            const auto result = ReloadCurrentFile(followEnd);
            if (result.status == OpenStatus::Failed)
                wxMessageBox(result.error, "Could not open file", wxOK | wxICON_ERROR, this);
        }
    } else {
        const int choice = wxMessageBox("The current file has been deleted. Save now?", "File Change Notification",
                                        wxYES_NO | wxCANCEL | wxICON_WARNING, this);
        terminate = choice == wxCANCEL;
        if (choice == wxYES) {
            const auto result = Save();
            if (result.status == SaveStatus::Failed)
                wxMessageBox(result.error, "Could not save file", wxOK | wxICON_ERROR, this);
        }
    }
    if (!terminate) RefreshFileWatching();
}
