#include "app/dialogs/toolbar_customization_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/localization.h"
#include <algorithm>
#include <memory>
#include <utility>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/stattext.h>
#include <wx/translation.h>

namespace {
class ToolbarCustomizationDialog final : public wxDialog {
public:
    ToolbarCustomizationDialog(wxWindow* parent, std::span<const ToolbarButtonChoice> choices,
                               std::vector<int>& buttons, std::span<const int> defaults,
                               const std::function<void()>& apply)
        : wxDialog(parent, wxID_ANY, _("Customize Toolbar"), wxDefaultPosition, wxDefaultSize,
                   DialogLayout::Style(DialogLayout::Resize::Both)),
          choices_(choices), buttons_(buttons), defaults_(defaults), apply_(apply) {
        SetName("Customize toolbar");
        auto contents = CreateContents();
        BindActions();
        RefreshChoices(0);
        DialogLayout::Fit(*this, std::move(contents), DialogLayout::Resize::Both);
        available_->SetFocus();
    }

private:
    static constexpr int SeparatorIdentity = 0;
    static constexpr int ListWidthDip = 210;
    static constexpr int ListHeightDip = 250;

    std::unique_ptr<wxBoxSizer> CreateContents() {
        const int margin = FromDIP(DialogLayout::MarginDip);
        const int gap = FromDIP(DialogLayout::GapDip);
        auto contents = std::make_unique<wxBoxSizer>(wxVERTICAL);
        auto* columns = new wxBoxSizer(wxHORIZONTAL);
        contents->Add(columns, 1, wxEXPAND);
        const auto list = [this, columns, margin, gap](const wxString& title, const char* name) {
            auto* column = new wxBoxSizer(wxVERTICAL);
            columns->Add(column, 1, wxEXPAND | wxALL, margin);
            column->Add(new wxStaticText(this, wxID_ANY, title), 0, wxBOTTOM, gap);
            auto* control =
                new wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(ListWidthDip, ListHeightDip)));
            control->SetName(name);
            AppLocalization::SetAccessibleName(*control, wxStripMenuCodes(title, wxStrip_Mnemonics));
            column->Add(control, 1, wxEXPAND);
            return control;
        };
        available_ = list(_("&Available toolbar buttons:"), "Available toolbar buttons");
        auto* actions = new wxBoxSizer(wxVERTICAL);
        columns->Add(actions, 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, margin);
        current_ = list(_("&Current toolbar buttons:"), "Current toolbar buttons");
        const auto button = [this, actions, gap](const wxString& label, const char* name) {
            auto* control = new wxButton(this, wxID_ANY, label);
            control->SetName(name);
            actions->Add(control, 0, wxEXPAND | wxBOTTOM, gap);
            return control;
        };
        add_ = button(_("&Add ->"), "Add toolbar button");
        remove_ = button(_("<- &Remove"), "Remove toolbar button");
        up_ = button(_("Move &Up"), "Move toolbar button up");
        down_ = button(_("Move &Down"), "Move toolbar button down");
        reset_ = button(_("&Reset"), "Reset toolbar buttons");
        contents->Add(CreateSeparatedButtonSizer(wxCLOSE), 0, wxEXPAND | wxALL, margin);
        return contents;
    }

    void BindActions() {
        for (auto* control : {add_, remove_, up_, down_})
            control->Bind(wxEVT_UPDATE_UI, &ToolbarCustomizationDialog::UpdateAction, this);
        add_->Bind(wxEVT_BUTTON, &ToolbarCustomizationDialog::AddButton, this);
        remove_->Bind(wxEVT_BUTTON, &ToolbarCustomizationDialog::RemoveButton, this);
        reset_->Bind(wxEVT_BUTTON, &ToolbarCustomizationDialog::ResetButtons, this);
        for (const auto& [control, offset] : {std::pair{up_, -1}, std::pair{down_, 1}})
            control->Bind(wxEVT_BUTTON, [this, offset](wxCommandEvent&) { MoveButton(offset); });
    }

    void RefreshChoices(int selection) {
        const auto previous = available_->GetStringSelection();
        available_->Clear();
        availableButtons_.clear();
        for (const auto& choice : choices_)
            if (choice.identity == SeparatorIdentity ||
                std::ranges::find(buttons_, choice.identity) == buttons_.end()) {
                availableButtons_.push_back(choice.identity);
                available_->Append(choice.label);
            }
        if (!available_->SetStringSelection(previous)) available_->SetSelection(0);
        current_->Clear();
        for (const int identity : buttons_)
            current_->Append(std::ranges::find(choices_, identity, &ToolbarButtonChoice::identity)->label);
        if (!buttons_.empty()) current_->SetSelection(std::clamp(selection, 0, static_cast<int>(buttons_.size()) - 1));
    }

    void UpdateAction(wxUpdateUIEvent& event) {
        const int selection = current_->GetSelection();
        if (event.GetId() == add_->GetId())
            event.Enable(available_->GetSelection() != wxNOT_FOUND);
        else if (event.GetId() == remove_->GetId())
            event.Enable(selection != wxNOT_FOUND);
        else if (event.GetId() == up_->GetId())
            event.Enable(selection > 0);
        else
            event.Enable(selection != wxNOT_FOUND && selection + 1 < static_cast<int>(buttons_.size()));
    }

    void ApplyLayout(int selection) {
        apply_();
        RefreshChoices(selection);
    }

    void AddButton(wxCommandEvent&) {
        const int selected = available_->GetSelection();
        if (selected == wxNOT_FOUND) return;
        int position = current_->GetSelection();
        if (position == wxNOT_FOUND) position = static_cast<int>(buttons_.size());
        buttons_.insert(buttons_.begin() + position, availableButtons_[static_cast<size_t>(selected)]);
        ApplyLayout(position);
    }

    void RemoveButton(wxCommandEvent&) {
        const int position = current_->GetSelection();
        if (position == wxNOT_FOUND) return;
        buttons_.erase(buttons_.begin() + position);
        ApplyLayout(position);
    }

    void MoveButton(int offset) {
        const int position = current_->GetSelection();
        const int destination = position + offset;
        if (position == wxNOT_FOUND || destination < 0 || destination >= static_cast<int>(buttons_.size())) return;
        std::swap(buttons_[static_cast<size_t>(position)], buttons_[static_cast<size_t>(destination)]);
        ApplyLayout(destination);
    }

    void ResetButtons(wxCommandEvent&) {
        buttons_.assign(defaults_.begin(), defaults_.end());
        ApplyLayout(0);
    }

    std::span<const ToolbarButtonChoice> choices_;
    std::vector<int>& buttons_;
    std::span<const int> defaults_;
    // The native event handlers keep this type-erased callback for the dialog's lifetime.
    std::function<void()> apply_;
    std::vector<int> availableButtons_;
    wxListBox* available_ = nullptr;
    wxListBox* current_ = nullptr;
    wxButton* add_ = nullptr;
    wxButton* remove_ = nullptr;
    wxButton* up_ = nullptr;
    wxButton* down_ = nullptr;
    wxButton* reset_ = nullptr;
};
} // namespace

void ShowToolbarCustomizationDialog(wxWindow* parent, std::span<const ToolbarButtonChoice> choices,
                                    std::vector<int>& buttons, std::span<const int> defaults,
                                    const std::function<void()>& apply) {
    ToolbarCustomizationDialog dialog(parent, choices, buttons, defaults, apply);
    dialog.ShowModal();
}
