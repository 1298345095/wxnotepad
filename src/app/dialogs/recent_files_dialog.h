#pragma once

#include "settings/original_settings.h"
#include <wx/dialog.h>
#include <optional>
#include <functional>

class wxCheckBox;
class wxListCtrl;
class wxTextCtrl;

class RecentFilesDialog final : public wxDialog {
public:
    RecentFilesDialog(wxWindow* parent, OriginalSettings& preferences, std::function<void()> clearHistory,
                      std::function<void(size_t)> removeHistory);
    std::optional<wxString> Choose();

private:
    OriginalSettings& preferences_;
    wxListCtrl* files_;
    wxCheckBox* remember_;
    wxTextCtrl* capacity_;
    std::function<void(size_t)> removeHistory_;
    void Accept();
    void RefreshFiles();
};
