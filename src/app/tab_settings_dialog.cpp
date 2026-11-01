#include "app/tab_settings_dialog.h"
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
    wxDialog dialog(parent, wxID_ANY, "Tab Settings");
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* groups = new wxBoxSizer(wxHORIZONTAL);
    const auto addGroup = [&](const wxString& name, const OriginalTabs& value, wxCheckBox** inherit,
                              const wxString& inheritLabel) {
        auto* box = new wxStaticBoxSizer(wxVERTICAL, &dialog, name);
        auto* owner = box->GetStaticBox();
        auto* tab = new wxSpinCtrl(owner, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 256);
        tab->SetName(name + " Tab Width");
        auto* indent = new wxSpinCtrl(owner, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 256);
        indent->SetName(name + " Indentation Width");
        auto* spaces = new wxCheckBox(owner, wxID_ANY, "Tabs as Spaces");
        spaces->SetName(name + " Tabs as Spaces");
        box->Add(new wxStaticText(owner, wxID_ANY, "Tab Width (1–256):"), 0, wxALL, 4);
        box->Add(tab, 0, wxEXPAND | wxALL, 4);
        box->Add(new wxStaticText(owner, wxID_ANY, "Indentation Width (0–256):"), 0, wxALL, 4);
        box->Add(indent, 0, wxEXPAND | wxALL, 4);
        box->Add(spaces, 0, wxALL, 4);
        if (inherit) {
            *inherit = new wxCheckBox(owner, wxID_ANY, inheritLabel);
            box->Add(*inherit, 0, wxALL, 4);
        }
        groups->Add(box, 1, wxEXPAND | wxALL, 5);
        TabControls controls{tab, indent, spaces};
        controls.Set(value, true);
        return controls;
    };
    auto& scheme = preferences.schemeTabs[static_cast<size_t>(language)];
    wxCheckBox* useGlobal;
    wxCheckBox* useScheme;
    const auto global = addGroup("Global", preferences.globalTabs, nullptr, {});
    const auto schemeControls = addGroup("Scheme", scheme, &useGlobal, "Use Global Tab Settings");
    const auto initialFile = indentation.Effective(language);
    const auto initialScheme = scheme;
    const auto file = addGroup("File", initialFile, &useScheme, "Use Scheme Tab Settings");
    useGlobal->SetValue(scheme.useGlobal);
    useScheme->SetValue(!indentation.HasFileSettings());
    bool synchronizing = false;
    const auto sync = [&](bool fromGlobal) {
        if (synchronizing) return;
        synchronizing = true;
        if (fromGlobal)
            schemeControls.Set(useGlobal->GetValue() ? global.Read() : initialScheme, !useGlobal->GetValue());
        file.Set(useScheme->GetValue() ? schemeControls.Read() : initialFile, !useScheme->GetValue());
        synchronizing = false;
    };
    const auto changed = [&](int id) {
        if (id == global.tab->GetId() || id == global.indent->GetId() || id == global.spaces->GetId() ||
            id == useGlobal->GetId())
            sync(true);
        else if (id == schemeControls.tab->GetId() || id == schemeControls.indent->GetId() ||
                 id == schemeControls.spaces->GetId() || id == useScheme->GetId())
            sync(false);
    };
    dialog.Bind(wxEVT_CHECKBOX, [&](wxCommandEvent& event) { changed(event.GetId()); });
    dialog.Bind(wxEVT_SPINCTRL, [&](wxSpinEvent& event) { changed(event.GetId()); });
    dialog.Bind(wxEVT_TEXT, [&](wxCommandEvent& event) { changed(event.GetId()); });
    sync(true);
    layout->Add(groups, 0, wxEXPAND);
    auto* tabIndents = new wxCheckBox(&dialog, wxID_ANY, "Tab Indents");
    tabIndents->SetValue(preferences.tabIndents);
    auto* backspace = new wxCheckBox(&dialog, wxID_ANY, "Backspace Unindents");
    backspace->SetValue((preferences.backspaceUnindents & 1) != 0);
    auto* smart = new wxCheckBox(&dialog, wxID_ANY, "Backspace at Indentation Deletes up to Tab Stop");
    smart->SetValue((preferences.backspaceUnindents & 2) != 0);
    for (auto* control : {tabIndents, backspace, smart})
        layout->Add(control, 0, wxALL, 5);
    layout->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 5);
    dialog.SetSizerAndFit(layout);
    if (dialog.ShowModal() != wxID_OK) return false;
    preferences.globalTabs = global.Read();
    scheme = schemeControls.Read();
    scheme.useGlobal = useGlobal->GetValue();
    indentation.SetFileSettings(useScheme->GetValue() ? std::nullopt : std::optional<OriginalTabs>(file.Read()));
    preferences.tabIndents = tabIndents->GetValue();
    preferences.backspaceUnindents = (backspace->GetValue() ? 1 : 0) | (smart->GetValue() ? 2 : 0);
    return true;
}
