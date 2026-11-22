#include "app/dialogs/toolbar_customization_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/localization.h"
#include <algorithm>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/stattext.h>
#include <wx/translation.h>

void ShowToolbarCustomizationDialog(wxWindow* parent, std::span<const ToolbarButtonChoice> choices,
                                    std::vector<int>& buttons, std::span<const int> defaults,
                                    const std::function<void()>& apply) {
    constexpr int SeparatorIdentity = 0;
    constexpr int ListWidthDip = 210;
    constexpr int ListHeightDip = 250;
    wxDialog dialog(parent, wxID_ANY, _("Customize Toolbar"), wxDefaultPosition, wxDefaultSize,
                    DialogLayout::Style(DialogLayout::Resize::Both));
    dialog.SetName("Customize toolbar");
    const int margin = dialog.FromDIP(DialogLayout::MarginDip);
    const int gap = dialog.FromDIP(DialogLayout::GapDip);
    auto contents = std::make_unique<wxBoxSizer>(wxVERTICAL);
    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    const auto list = [&](const wxString& title, const char* name) {
        auto* column = new wxBoxSizer(wxVERTICAL);
        column->Add(new wxStaticText(&dialog, wxID_ANY, title), 0, wxBOTTOM, gap);
        auto* control =
            new wxListBox(&dialog, wxID_ANY, wxDefaultPosition, dialog.FromDIP(wxSize(ListWidthDip, ListHeightDip)));
        control->SetName(name);
        AppLocalization::SetAccessibleName(*control, wxStripMenuCodes(title, wxStrip_Mnemonics));
        column->Add(control, 1, wxEXPAND);
        columns->Add(column, 1, wxEXPAND | wxALL, margin);
        return control;
    };
    auto* available = list(_("&Available toolbar buttons:"), "Available toolbar buttons");
    auto* actions = new wxBoxSizer(wxVERTICAL);
    columns->Add(actions, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, margin);
    auto* current = list(_("&Current toolbar buttons:"), "Current toolbar buttons");
    const auto button = [&](const wxString& label, const char* name) {
        auto* control = new wxButton(&dialog, wxID_ANY, label);
        control->SetName(name);
        actions->Add(control, 0, wxEXPAND | wxBOTTOM, gap);
        return control;
    };
    auto* add = button(_("&Add →"), "Add toolbar button");
    auto* remove = button(_("← &Remove"), "Remove toolbar button");
    auto* up = button(_("Move &Up"), "Move toolbar button up");
    auto* down = button(_("Move &Down"), "Move toolbar button down");
    auto* reset = button(_("&Reset"), "Reset toolbar buttons");
    contents->Add(columns, 1, wxEXPAND);
    contents->Add(dialog.CreateSeparatedButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, margin);
    std::vector<int> availableButtons;
    const auto label = [&](int identity) {
        return std::ranges::find(choices, identity, &ToolbarButtonChoice::identity)->label;
    };
    const auto refresh = [&](int selection) {
        const auto previous = available->GetStringSelection();
        available->Clear();
        availableButtons.clear();
        for (const auto& choice : choices)
            if (choice.identity == SeparatorIdentity || std::ranges::find(buttons, choice.identity) == buttons.end()) {
                availableButtons.push_back(choice.identity);
                available->Append(choice.label);
            }
        if (!available->SetStringSelection(previous)) available->SetSelection(0);
        current->Clear();
        for (const int identity : buttons)
            current->Append(label(identity));
        if (!buttons.empty()) current->SetSelection(std::clamp(selection, 0, static_cast<int>(buttons.size()) - 1));
    };
    const auto update = [&](wxUpdateUIEvent& event) {
        const int selection = current->GetSelection();
        if (event.GetId() == add->GetId())
            event.Enable(available->GetSelection() != wxNOT_FOUND);
        else if (event.GetId() == remove->GetId())
            event.Enable(selection != wxNOT_FOUND);
        else if (event.GetId() == up->GetId())
            event.Enable(selection > 0);
        else
            event.Enable(selection != wxNOT_FOUND && selection + 1 < static_cast<int>(buttons.size()));
    };
    for (auto* control : {add, remove, up, down})
        control->Bind(wxEVT_UPDATE_UI, update);
    add->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        const int selected = available->GetSelection();
        if (selected == wxNOT_FOUND) return;
        int position = current->GetSelection();
        if (position == wxNOT_FOUND) position = static_cast<int>(buttons.size());
        buttons.insert(buttons.begin() + position, availableButtons[static_cast<size_t>(selected)]);
        apply();
        refresh(position);
    });
    remove->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        const int position = current->GetSelection();
        if (position == wxNOT_FOUND) return;
        buttons.erase(buttons.begin() + position);
        apply();
        refresh(position);
    });
    for (const auto& [control, offset] : {std::pair{up, -1}, std::pair{down, 1}})
        control->Bind(wxEVT_BUTTON, [&, offset](wxCommandEvent&) {
            const int position = current->GetSelection();
            const int destination = position + offset;
            if (position == wxNOT_FOUND || destination < 0 || destination >= static_cast<int>(buttons.size())) return;
            std::swap(buttons[static_cast<size_t>(position)], buttons[static_cast<size_t>(destination)]);
            apply();
            refresh(destination);
        });
    reset->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        buttons.assign(defaults.begin(), defaults.end());
        apply();
        refresh(0);
    });
    refresh(0);
    DialogLayout::Fit(dialog, std::move(contents), DialogLayout::Resize::Both);
    available->SetFocus();
    dialog.ShowModal();
}
