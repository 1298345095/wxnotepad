#include "app/dialogs/file_favorites_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/file_favorites.h"
#include "app/localization.h"
#include <wx/dir.h>
#include <wx/dirdlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/translation.h>
#include <algorithm>
#include <vector>

std::optional<wxString> NameFileFavorite(wxWindow* parent, const wxString& initialName) {
    wxDialog dialog(parent, wxID_ANY, _("Add to Favorites"), wxDefaultPosition, wxDefaultSize,
                    DialogLayout::Style(DialogLayout::Resize::Horizontal));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    layout->Add(new wxStaticText(&dialog, wxID_ANY, _("Enter the name for the new favorites item:")), 0,
                wxEXPAND | wxALL, dialog.FromDIP(DialogLayout::MarginDip));
    auto* name = new wxTextCtrl(&dialog, wxID_ANY, initialName, wxDefaultPosition, dialog.FromDIP(wxSize(420, -1)));
    name->SetName("Favorite name");
    AppLocalization::SetAccessibleName(*name, _("Favorite name"));
    layout->Add(name, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(DialogLayout::MarginDip));
    layout->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL,
                dialog.FromDIP(DialogLayout::MarginDip));
    name->Bind(wxEVT_TEXT, [&](wxCommandEvent&) { dialog.FindWindow(wxID_OK)->Enable(!name->IsEmpty()); });
    dialog.FindWindow(wxID_OK)->Enable(!initialName.empty());
    DialogLayout::Fit(dialog, layout, DialogLayout::Resize::Horizontal);
    name->SetSelection(-1, -1);
    name->SetFocus();
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return {};
    return name->GetValue();
}

std::optional<wxString> ChooseFileFavorite(wxWindow* parent, wxString& directory) {
    wxDialog dialog(parent, wxID_ANY, _("Favorites"), wxDefaultPosition, wxDefaultSize,
                    DialogLayout::Style(DialogLayout::Resize::Both));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* files = new wxListCtrl(&dialog, wxID_ANY, wxDefaultPosition, dialog.FromDIP(wxSize(520, 260)),
                                 wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    files->SetName("File favorites");
    AppLocalization::SetAccessibleName(*files, _("File favorites"));
    files->InsertColumn(0, _("Name"));
    std::vector<wxString> entries;
    const auto refresh = [&] {
        files->DeleteAllItems();
        entries.clear();
        wxDir folder(FileFavorites::Directory(directory));
        if (folder.IsOpened()) {
            wxString name;
            for (bool found = folder.GetFirst(&name); found; found = folder.GetNext(&name))
                entries.push_back(wxFileName(FileFavorites::Directory(directory), name).GetFullPath());
        } else {
            wxMessageBox(_("Could not read the favorites directory."), _("Favorites"), wxOK | wxICON_ERROR, &dialog);
        }
        std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            return FileFavorites::DisplayName(left).CmpNoCase(FileFavorites::DisplayName(right)) < 0;
        });
        for (const auto& entry : entries)
            files->InsertItem(files->GetItemCount(), FileFavorites::DisplayName(entry));
        files->SetColumnWidth(0, wxLIST_AUTOSIZE);
        if (files->GetItemCount()) files->SetItemState(0, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
        dialog.FindWindow(wxID_OK)->Enable(false);
    };
    layout->Add(files, 1, wxEXPAND | wxALL, dialog.FromDIP(DialogLayout::MarginDip));
    auto* directoryButton = new wxButton(&dialog, wxID_ANY, _("Choose favorites &directory..."));
    directoryButton->SetName("Favorites directory");
    layout->Add(directoryButton, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(DialogLayout::MarginDip));
    directoryButton->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        wxDirDialog choose(&dialog, _("Choose favorites directory"), FileFavorites::Directory(directory),
                           wxDD_DIR_MUST_EXIST);
        if (choose.ShowModal() == wxID_OK) {
            directory = choose.GetPath();
            refresh();
        }
    });
    layout->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_RIGHT | wxALL,
                dialog.FromDIP(DialogLayout::MarginDip));
    std::optional<wxString> selected;
    const auto accept = [&] {
        const auto index = files->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (index < 0) return;
        selected = FileFavorites::Resolve(entries[static_cast<size_t>(index)]);
        if (!selected) {
            wxMessageBox(_("Could not resolve the favorite. The link may be damaged or unsupported."), _("Favorites"),
                         wxOK | wxICON_ERROR, &dialog);
            return;
        }
        dialog.EndModal(wxID_OK);
    };
    files->Bind(wxEVT_LIST_ITEM_SELECTED, [&](wxListEvent&) { dialog.FindWindow(wxID_OK)->Enable(true); });
    files->Bind(wxEVT_LIST_ITEM_DESELECTED,
                [&](wxListEvent&) { dialog.FindWindow(wxID_OK)->Enable(files->GetSelectedItemCount() != 0); });
    files->Bind(wxEVT_LIST_ITEM_ACTIVATED, [&](wxListEvent&) { accept(); });
    dialog.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { accept(); }, wxID_OK);
    DialogLayout::Fit(dialog, layout, DialogLayout::Resize::Both);
    refresh();
    files->SetFocus();
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return {};
    return selected;
}
