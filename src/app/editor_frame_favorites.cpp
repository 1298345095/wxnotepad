#include "app/editor_frame.h"
#include "app/dialogs/document_dialogs.h"
#include "app/dialogs/file_favorites_dialog.h"
#include "app/file_favorites.h"
#include "app/desktop_launch.h"
#include "settings/settings_store.h"
#include <wx/filefn.h>
#include <wx/file.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/translation.h>

void EditorFrame::AppendFileFavorites(wxMenu* file) {
    auto* favorites = new wxMenu;
    const int open = wxWindow::NewControlId();
    const int add = wxWindow::NewControlId();
    const int manage = wxWindow::NewControlId();
    favorites->Append(open, _("&Open Favorites...\tAlt+I"));
    favorites->Append(add, _("&Add Current File...\tAlt+K"));
    favorites->Append(manage, _("&Manage...\tAlt+F9"));
    file->AppendSubMenu(favorites, _("&Favorites"));
    Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& event) { event.Enable(!document_.Path().empty()); }, add);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OpenFileFavorites(); }, open);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { AddFileFavorite(); }, add);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ManageFileFavorites(); }, manage);
}

void EditorFrame::ManageFileFavorites() {
    const auto directory = FileFavorites::Directory((settings_ ? settings_->values : defaults_).favoritesDirectory);
    wxString error;
    if (!LaunchDesktopTarget(*this, directory, directory, error))
        wxMessageBox(_("Could not open the favorites directory."), _("Favorites"), wxOK | wxICON_ERROR, this);
}

void EditorFrame::AddFileFavorite() {
    if (document_.Path().empty()) return;
    const auto name = NameFileFavorite(this, FileFavorites::DefaultName(document_.Path()));
    if (!name) return;
    const auto directory = FileFavorites::Directory((settings_ ? settings_->values : defaults_).favoritesDirectory);
    const auto error = FileFavorites::Add(directory, *name, document_.Path());
    if (!error.empty())
        wxMessageBox(error, _("Add to Favorites"), wxOK | wxICON_ERROR, this);
    else
        wxMessageBox(_("The favorite was added successfully."), _("Add to Favorites"), wxOK | wxICON_INFORMATION, this);
}

void EditorFrame::OpenFileFavorites() {
    if (!ConfirmDiscardChanges(_("Save changes to %s before opening favorites?"))) return;
    auto& preferences = settings_ ? settings_->values : defaults_;
    auto selected = ChooseFileFavorite(this, preferences.favoritesDirectory);
    if (!selected) return;
    if (!wxFileExists(*selected) && !wxDirExists(*selected)) {
        const auto choice =
            wxMessageBox(wxString::Format(_("\"%s\" not found.\nWould you like to create this file?"), *selected),
                         _("Create file?"), wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
        // Approved defect fix: original Cancel exits; keep this window/document.
        if (choice != wxYES) return;
        bool created;
        {
            const wxLogNull quiet;
            wxFile file;
            created = file.Create(*selected, false) && file.Close();
        }
        if (!created) {
            wxMessageBox(_("Could not create the file."), _("Could not open file"), wxOK | wxICON_ERROR, this);
            return;
        }
    }
    if (wxDirExists(*selected)) {
        OpenFileDialog dialog(this, document_.Language(), document_.Path(), preferences.favoriteSchemes,
                              preferences.schemeExtensions);
        dialog.SetDirectory(*selected);
        selected = dialog.Choose();
    }
    if (!selected) return;
    const auto result = LoadFile(DocumentLoad::Open{selected});
    if (result.status == OpenStatus::Failed)
        wxMessageBox(result.error, _("Could not open file"), wxOK | wxICON_ERROR, this);
}
