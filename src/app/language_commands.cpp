#include "app/editor_frame.h"
#include "app/localization.h"
#include "settings/settings_store.h"
#include "ui_language_data.h"
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/translation.h>

void EditorFrame::AppendUILanguageCommands(wxMenu* settings) {
    if (!settings_) return;
    auto* languages = new wxMenu;
    const auto append = [this, languages](uint16_t preference, const wxString& caption) {
        const int id = wxWindow::NewControlId();
        languages->AppendCheckItem(id, caption);
        Bind(wxEVT_MENU, [this, preference](wxCommandEvent&) { ChooseUILanguage(preference); }, id);
        Bind(
            wxEVT_UPDATE_UI,
            [this, preference](wxUpdateUIEvent& event) {
                event.Check(AppLocalization::MenuPreference(settings_->values.uiLanguage) == preference);
            },
            id);
    };
    append(UILanguage::System, _("System Language"));
    for (const auto& language : AppLocalization::LanguageDefinitions)
        append(language.langId, wxString::FromUTF8(language.nativeName.data(), language.nativeName.size()));
    settings->AppendSubMenu(languages, _("&Language"));
}

void EditorFrame::ChooseUILanguage(uint16_t language) {
    if (settings_->values.uiLanguage == language) return;
    // Notepad4 89b8b36 IDS_CHANGE_LANG_RESTART.
    const int choice = wxMessageBox(_("Changing the UI language requires a restart of wxnotepad, restart now?"),
                                    "wxnotepad", wxYES_NO | wxCANCEL | wxICON_QUESTION, this);
    if (choice == wxCANCEL) return;
    if (!settings_->SaveUILanguage(language)) {
        wxMessageBox(wxString::Format(_("Could not write %s"), settings_->Path()), _("Could not save settings"),
                     wxOK | wxICON_ERROR, this);
        return;
    }
    UpdateWindowUI(wxUPDATE_UI_RECURSE);
    if (choice == wxYES) Restart();
}
