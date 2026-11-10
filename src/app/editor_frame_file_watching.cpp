#include "app/editor_frame.h"
#include "app/dialogs/document_dialogs.h"
#include "editor/editor_control.h"
#include <wx/filename.h>
#include <wx/msgdlg.h>

void EditorFrame::SetFileWatchingOptions(const FileWatchingOptions& options) {
    fileWatchingOptions_ = options;
    RefreshFileWatching();
}

void EditorFrame::RefreshFileWatching() {
    fileChangeMonitor_.Refresh(document_.Path(), fileWatchingOptions_);
}

void EditorFrame::ShowFileChangeNotificationOptions() {
    if (const auto options = ChooseFileWatchingOptions(this, fileWatchingOptions_)) SetFileWatchingOptions(*options);
}

void EditorFrame::HandleFileChange() {
    bool terminate = false;
    if (wxFileName::FileExists(document_.Path())) {
        const bool automatic = fileWatchingOptions_.mode == FileWatchingMode::AutoReload;
        if ((automatic && !document_.IsModified()) ||
            wxMessageBox("The current file has been modified by an external program. Reload?",
                         "File Change Notification", wxYES_NO | wxICON_WARNING, this) == wxYES) {
            const bool followEnd = automatic && ((fileWatchingOptions_.flags & FileWatchingOption_KeepAtEnd) ||
                                                 editor_->Send(Scintilla::Message::LineFromPosition,
                                                               editor_->Send(Scintilla::Message::GetCurrentPos)) +
                                                         1 ==
                                                     editor_->Send(Scintilla::Message::GetLineCount));
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
