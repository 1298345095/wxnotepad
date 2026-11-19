#include "app/dialogs/auto_save_settings_dialog.h"
#include "app/localization.h"
#include "settings/auto_save_options.h"
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>
#include <wx/filename.h>
#include <cmath>
#include <cwchar>
#include <limits>

// Shared with the manual and periodic backup writer.
#include "document/document_session.h"

bool ShowAutoSaveSettingsDialog(wxWindow* parent, AutoSaveOptions& options) {
    // TRANSLATORS: IDD_AUTOSAVE title; this dialog configures periodic backup copies.
    wxDialog dialog(parent, wxID_ANY, _("AutoSave Settings"));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    // TRANSLATORS: IDD_AUTOSAVE IDC_AUTOSAVE_ENABLE checkbox; enable timed backup copies.
    auto* enabled = new wxCheckBox(&dialog, wxID_ANY, _("Enable &periodic backup."));
    enabled->SetName("Periodic backup");
    enabled->SetValue(options.flags & AutoSaveOption::Periodic);
    layout->Add(enabled, 0, wxALL, dialog.FromDIP(10));
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    // TRANSLATORS: IDD_AUTOSAVE IDC_AUTOSAVE_PERIOD label; the numeric interval is measured in seconds.
    row->Add(new wxStaticText(&dialog, wxID_ANY, _("Backup period (in &seconds):")), 0,
             wxALIGN_CENTER_VERTICAL | wxRIGHT, dialog.FromDIP(10));
    const auto period = options.periodMilliseconds;
    const auto text = period % 1000 ? wxString::Format("%u.%03u", period / 1000, period % 1000)
                                    : wxString::Format("%u.0", period / 1000);
    auto* input = new wxTextCtrl(&dialog, wxID_ANY, text);
    input->SetName("AutoSave period");
    // TRANSLATORS: IDD_AUTOSAVE IDC_AUTOSAVE_PERIOD accessible input name; units are seconds.
    AppLocalization::SetAccessibleName(*input, _("Backup period in seconds"));
    input->SetMaxLength(31);
    row->Add(input, 1);
    layout->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(10));
    // TRANSLATORS: IDD_AUTOSAVE IDC_AUTOSAVE_MANUALLYDELETE checkbox; retain old backups for manual deletion.
    auto* retain = new wxCheckBox(&dialog, wxID_ANY, _("&Manually delete backups."));
    retain->SetName("Manually delete backups");
    retain->SetValue(options.flags & AutoSaveOption::ManuallyDelete);
    layout->Add(retain, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(10));
    // TRANSLATORS: IDD_AUTOSAVE IDC_AUTOSAVE_OPENFOLDER button; open the backup directory in the file manager.
    auto* folder = new wxButton(&dialog, wxID_ANY, _("Open &Folder"));
    folder->SetName("Open backup folder");
    folder->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {
        const auto path = DocumentSession::BackupDirectory();
        if (wxDirExists(path) || wxFileName::Mkdir(path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL))
            wxLaunchDefaultApplication(path);
    });
    layout->Add(folder, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(10));
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(layout);
    dialog.CentreOnParent();
    dialog.Bind(
        wxEVT_BUTTON,
        [&dialog, enabled, retain, input, &options](wxCommandEvent&) {
            // Original StrToFloat/wcstof accepts a numeric prefix and converts using
            // float precision before truncating milliseconds to a signed int.
            const float seconds = std::wcstof(input->GetValue().wc_str(), nullptr);
            const float milliseconds = seconds * 1000;
            if (!std::isfinite(milliseconds) ||
                static_cast<double>(milliseconds) < std::numeric_limits<int32_t>::min() ||
                static_cast<double>(milliseconds) > std::numeric_limits<int32_t>::max()) {
                input->SetFocus();
                input->SelectAll();
                return;
            }
            options.periodMilliseconds = static_cast<uint32_t>(static_cast<int32_t>(milliseconds));
            options.flags &= ~(AutoSaveOption::Periodic | AutoSaveOption::ManuallyDelete);
            if (enabled->GetValue()) options.flags |= AutoSaveOption::Periodic;
            if (retain->GetValue()) options.flags |= AutoSaveOption::ManuallyDelete;
            dialog.EndModal(wxID_OK);
        },
        wxID_OK);
    return dialog.ShowModal() == wxID_OK;
}
