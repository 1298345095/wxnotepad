#include "app/dialogs/tab_settings_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include <utility>
#include "app/localization.h"
#include <wx/translation.h>
#include <wx/wx.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>

namespace {
struct TabControls {
    wxSpinCtrl* tab;
    wxSpinCtrl* indent;
    wxCheckBox* spaces;
    OriginalTabs Read() const { return {tab->GetValue(), indent->GetValue(), spaces->GetValue(), false}; }
    void Set(const OriginalTabs& value, bool enabled) const {
        tab->SetValue(value.tabWidth);
        indent->SetValue(value.indentWidth);
        spaces->SetValue(value.tabsAsSpaces);
        tab->Enable(enabled);
        indent->Enable(enabled);
        spaces->Enable(enabled);
    }
};
} // namespace

bool ShowTabSettingsDialog(wxWindow* parent, OriginalSettings& preferences, SyntaxLanguage language,
                           IndentationSettings& indentation) {
    wxDialog dialog(parent, wxID_ANY, _("Tab Settings"));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* groups = new wxBoxSizer(wxVERTICAL);
    const auto addGroup = [groups, &dialog](const char* name, const OriginalTabs& value, wxCheckBox** inherit,
                                            const wxString& inheritLabel) {
        const auto label = wxGetTranslation(name);
        auto* box = new wxStaticBoxSizer(wxVERTICAL, &dialog, label);
        auto* owner = box->GetStaticBox();
        auto* tab = new wxSpinCtrl(owner, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1,
                                   OriginalSettingLimits::MaxTabWidth);
        tab->SetName(wxString(name) + " Tab Width");
        AppLocalization::SetAccessibleName(*tab, wxString::Format(_("%s tab width"), label));
        auto* indent = new wxSpinCtrl(owner, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0,
                                      OriginalSettingLimits::MaxTabWidth);
        indent->SetName(wxString(name) + " Indentation Width");
        AppLocalization::SetAccessibleName(*indent, wxString::Format(_("%s indentation width"), label));
        auto* spaces = new wxCheckBox(owner, wxID_ANY, _("Tabs as Spaces"));
        spaces->SetName(wxString(name) + " Tabs as Spaces");
        AppLocalization::SetAccessibleName(*spaces, wxString::Format(_("%s tabs as spaces"), label));
        const int gap = dialog.FromDIP(DialogLayout::GapDip);
        auto* fields = new wxFlexGridSizer(3, gap, gap);
        fields->Add(new wxStaticText(owner, wxID_ANY, _("Tab Width (1–256):")), 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(tab, 0, wxEXPAND);
        fields->Add(spaces, 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(new wxStaticText(owner, wxID_ANY, _("Indentation Width (0–256):")), 0, wxALIGN_CENTER_VERTICAL);
        fields->Add(indent, 0, wxEXPAND);
        if (inherit) {
            *inherit = new wxCheckBox(owner, wxID_ANY, inheritLabel);
            fields->Add(*inherit, 0, wxALIGN_CENTER_VERTICAL);
        } else {
            fields->AddSpacer(0);
        }
        box->Add(fields, 0, wxEXPAND | wxALL, gap);
        groups->Add(box, 0, wxEXPAND | wxALL, gap);
        TabControls controls{tab, indent, spaces};
        controls.Set(value, true);
        return controls;
    };
    auto& scheme = preferences.schemeTabs[static_cast<size_t>(std::to_underlying(SyntaxSettingsLanguage(language)))];
    wxCheckBox* useGlobal;
    wxCheckBox* useScheme;
    const auto global = addGroup(wxTRANSLATE("Global"), preferences.globalTabs, nullptr, {});
    const auto schemeControls = addGroup(wxTRANSLATE("Scheme"), scheme, &useGlobal, _("Use Global Tab Settings"));
    const auto initialFile = indentation.Effective(language);
    const auto initialScheme = scheme;
    const auto file = addGroup(wxTRANSLATE("File"), initialFile, &useScheme, _("Use Scheme Tab Settings"));
    useGlobal->SetValue(scheme.useGlobal);
    useScheme->SetValue(!indentation.HasFileSettings());
    bool synchronizing = false;
    const auto sync = [&synchronizing, useGlobal, useScheme, schemeControls, global, initialScheme, file,
                       initialFile](bool fromGlobal) {
        if (synchronizing) return;
        synchronizing = true;
        if (fromGlobal)
            schemeControls.Set(useGlobal->GetValue() ? global.Read() : initialScheme, !useGlobal->GetValue());
        file.Set(useScheme->GetValue() ? schemeControls.Read() : initialFile, !useScheme->GetValue());
        synchronizing = false;
    };
    const auto changed = [global, schemeControls, useGlobal, useScheme, sync](int id) {
        if (id == global.tab->GetId() || id == global.indent->GetId() || id == global.spaces->GetId() ||
            id == useGlobal->GetId())
            sync(true);
        else if (id == schemeControls.tab->GetId() || id == schemeControls.indent->GetId() ||
                 id == schemeControls.spaces->GetId() || id == useScheme->GetId())
            sync(false);
    };
    dialog.Bind(wxEVT_CHECKBOX, [changed](const wxCommandEvent& event) { changed(event.GetId()); });
    dialog.Bind(wxEVT_SPINCTRL, [changed](const wxSpinEvent& event) { changed(event.GetId()); });
    dialog.Bind(wxEVT_TEXT, [changed](const wxCommandEvent& event) { changed(event.GetId()); });
    sync(true);
    layout->Add(groups, 0, wxEXPAND);
    auto* tabIndents = new wxCheckBox(&dialog, wxID_ANY, _("Tab Indents"));
    tabIndents->SetValue(indentation.TabIndents());
    auto* backspace = new wxCheckBox(&dialog, wxID_ANY, _("Backspace Unindents"));
    backspace->SetValue((preferences.backspaceUnindents & 1) != 0);
    auto* smart = new wxCheckBox(&dialog, wxID_ANY, _("Backspace at Indentation Deletes up to Tab Stop"));
    smart->SetValue((preferences.backspaceUnindents & 2) != 0);
    auto* detect = new wxCheckBox(&dialog, wxID_ANY, _("Detect Indentation"));
    detect->SetValue(preferences.detectIndentation);
    for (auto* control : {tabIndents, backspace, smart, detect})
        layout->Add(control, 0, wxALL, 5);
    layout->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 5);
    DialogLayout::Fit(dialog, layout);
    if (dialog.ShowModal() != wxID_OK) return false;
    preferences.globalTabs = global.Read();
    scheme = schemeControls.Read();
    scheme.useGlobal = useGlobal->GetValue();
    indentation.SetFileSettings(useScheme->GetValue() ? std::nullopt : std::optional<OriginalTabs>(file.Read()));
    indentation.SetTabIndents(tabIndents->GetValue());
    preferences.backspaceUnindents = (backspace->GetValue() ? 1 : 0) | (smart->GetValue() ? 2 : 0);
    preferences.detectIndentation = detect->GetValue();
    return true;
}
