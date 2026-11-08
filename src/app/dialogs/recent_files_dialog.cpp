#include "app/dialogs/recent_files_dialog.h"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valtext.h>
#include <wx/filefn.h>
#include <wx/msgdlg.h>
#include <algorithm>
#include <limits>

RecentFilesDialog::RecentFilesDialog(wxWindow* parent, OriginalSettings& preferences,
                                     std::function<void()> clearHistory, std::function<void(size_t)> removeHistory)
    : wxDialog(parent, wxID_ANY, "Recent Files", wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      preferences_(preferences), removeHistory_(std::move(removeHistory)) {
    auto* layout = new wxBoxSizer(wxVERTICAL);
    files_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(620, 280)),
                            wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    files_->SetName("Recent files");
    files_->InsertColumn(0, "File");
    layout->Add(files_, 1, wxEXPAND | wxALL, FromDIP(10));
    remember_ = new wxCheckBox(this, wxID_ANY, "&Remember recent files on exit");
    remember_->SetName("Remember recent files");
    remember_->SetValue(preferences_.saveRecentFiles);
    layout->Add(remember_, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    auto* clear = new wxButton(this, wxID_CLEAR, "Clear History");
    layout->Add(clear, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    clear->Bind(wxEVT_BUTTON, [this, clearHistory = std::move(clearHistory)](wxCommandEvent&) {
        clearHistory();
        RefreshFiles();
    });
    auto* count = new wxBoxSizer(wxHORIZONTAL);
    count->Add(new wxStaticText(this, wxID_ANY, "Maximum recent &files:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
               FromDIP(8));
    capacity_ = new wxTextCtrl(this, wxID_ANY, wxString::Format("%d", preferences_.maxRecentFiles), wxDefaultPosition,
                               FromDIP(wxSize(90, -1)), 0, wxTextValidator(wxFILTER_DIGITS));
    capacity_->SetName("Recent file capacity");
    count->Add(capacity_);
    layout->Add(count, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(10));
    layout->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(10));
    SetSizerAndFit(layout);
    SetMinSize(GetSize());
    RefreshFiles();
    files_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) { FindWindow(wxID_OK)->Enable(true); });
    files_->Bind(wxEVT_LIST_ITEM_DESELECTED,
                 [this](wxListEvent&) { FindWindow(wxID_OK)->Enable(files_->GetSelectedItemCount() != 0); });
    files_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { Accept(); });
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Accept(); }, wxID_OK);
    CentreOnParent();
}

void RecentFilesDialog::RefreshFiles() {
    files_->DeleteAllItems();
    for (size_t index = 0; index < preferences_.recentFiles.size(); ++index)
        files_->InsertItem(static_cast<long>(index), preferences_.recentFiles[index]);
    files_->SetColumnWidth(0, wxLIST_AUTOSIZE);
    if (files_->GetItemCount()) files_->SetItemState(0, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
    FindWindow(wxID_OK)->Enable(false);
}

void RecentFilesDialog::Accept() {
    const long selected = files_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selected < 0) return;
    if (wxFileExists(files_->GetItemText(selected))) {
        EndModal(wxID_OK);
        return;
    }
    wxMessageDialog question(this, "No access to the selected file!\nWould you like to remove it from the list?",
                             "Remove recent file?", wxYES_NO | wxICON_WARNING);
    if (question.ShowModal() == wxID_YES) {
        removeHistory_(static_cast<size_t>(selected));
        RefreshFiles();
    }
}

std::optional<wxString> RecentFilesDialog::Choose() {
    const int choice = ShowModal();
    // Pinned FileMRUDlgProc applies options on destruction, even after Cancel.
    preferences_.saveRecentFiles = remember_->GetValue();
    long capacity = 0;
    if (!capacity_->GetValue().ToLong(&capacity) || capacity > std::numeric_limits<int>::max()) capacity = 0;
    preferences_.maxRecentFiles = std::max(static_cast<int>(capacity), 32);
    if (choice != wxID_OK) return std::nullopt;
    const long selected = files_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    return selected < 0 ? std::nullopt : std::optional<wxString>(files_->GetItemText(selected));
}
