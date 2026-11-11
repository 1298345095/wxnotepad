#include "app/dialogs/auto_completion_settings_dialog.h"
#include "settings/original_settings.h"
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <array>

bool ShowAutoCompletionSettingsDialog(wxWindow* parent, OriginalSettings& preferences) {
    wxDialog dialog(parent, wxID_ANY, "Auto Completion Settings");
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* insertion = new wxStaticBoxSizer(wxVERTICAL, &dialog, "Automatically insert");
    constexpr std::array options = {
        std::pair{"Parentheses ()", AutoInsertMask_Parenthesis},
        std::pair{"Braces {}", AutoInsertMask_Brace},
        std::pair{"Square brackets []", AutoInsertMask_SquareBracket},
        std::pair{"Angle brackets <>", AutoInsertMask_AngleBracket},
        std::pair{"Double quotes", AutoInsertMask_DoubleQuote},
        std::pair{"Single quotes", AutoInsertMask_SingleQuote},
        std::pair{"Backticks", AutoInsertMask_Backtick},
        std::pair{"Space after comma", AutoInsertMask_SpaceAfterComma},
        std::pair{"Space after comment", AutoInsertMask_SpaceAfterComment},
    };
    std::array<wxCheckBox*, options.size()> controls;
    for (size_t index = 0; index < options.size(); ++index) {
        const auto& [label, bit] = options[index];
        auto* control = new wxCheckBox(&dialog, wxID_ANY, label);
        control->SetName(label);
        control->SetValue((preferences.autoInsertMask & bit) != 0);
        controls[index] = control;
        insertion->Add(control, 0, wxALL, dialog.FromDIP(3));
    }
    layout->Add(insertion, 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    auto* indent = new wxCheckBox(&dialog, wxID_ANY, "Auto Indent");
    indent->SetName("Auto Indent");
    indent->SetValue(preferences.autoIndent);
    layout->Add(indent, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(10));
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(layout);
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return false;
    // Commit only the controls delivered here, retaining every other original bit.
    for (size_t index = 0; index < options.size(); ++index) {
        const auto bit = options[index].second;
        preferences.autoInsertMask =
            controls[index]->GetValue() ? preferences.autoInsertMask | bit : preferences.autoInsertMask & ~bit;
    }
    preferences.autoIndent = indent->GetValue();
    return true;
}
