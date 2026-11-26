#include "app/dialogs/csv_options_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/localization.h"
#include "syntax/csv_options.h"
#include <wx/checkbox.h>
#include <wx/gbsizer.h>
#include <wx/radiobut.h>
#include <wx/statbox.h>
#include <wx/textctrl.h>
#include <array>
#include <algorithm>

bool ShowCsvOptionsDialog(wxWindow* parent, uint32_t& options) {
    // Pinned IDD_CSV_OPTIONS / Styles.cpp::SelectCSVOptionsDlgProc: fixed layout,
    // one visible OK button, close/Escape cancel, and no persisted format state.
    wxDialog dialog(parent, wxID_ANY, _("CSV Options"));
    dialog.SetEscapeId(wxID_CANCEL);
    const int margin = dialog.FromDIP(DialogLayout::MarginDip);
    const int gap = dialog.FromDIP(DialogLayout::GapDip);
    auto* contents = new wxBoxSizer(wxVERTICAL);
    auto* delimiterGroup = new wxStaticBoxSizer(wxVERTICAL, &dialog, _("Delimiter"));
    auto* grid = new wxGridBagSizer(gap, gap);
    constexpr auto delimiterLabels =
        std::to_array<const char*>({wxTRANSLATE("Comma (,)"), wxTRANSLATE("Tab"), wxTRANSLATE("Pipe (|)"),
                                    wxTRANSLATE("Semicolon (;)"), wxTRANSLATE("Space"), wxTRANSLATE("Other:")});
    constexpr auto delimiterCharacters = std::to_array<unsigned char>({',', '\t', '|', ';', ' ', 0});
    std::array<wxRadioButton*, delimiterLabels.size()> delimiters{};
    const auto delimiter = static_cast<unsigned char>(options);
    const auto found = std::find(delimiterCharacters.begin(), delimiterCharacters.end() - 1, delimiter);
    const auto selected = static_cast<size_t>(found - delimiterCharacters.begin());
    for (size_t index = 0; index < delimiters.size(); ++index) {
        const auto label = index == 1   ? wxGETTEXT_IN_CONTEXT("CSV delimiter", "Tab")
                           : index == 4 ? wxGETTEXT_IN_CONTEXT("CSV delimiter", "Space")
                                        : wxGetTranslation(delimiterLabels[index]);
        auto* radio = new wxRadioButton(delimiterGroup->GetStaticBox(), wxID_ANY, label, wxDefaultPosition,
                                        wxDefaultSize, index == 0 ? wxRB_GROUP : 0);
        radio->SetName(delimiterLabels[index]);
        AppLocalization::SetAccessibleName(*radio, label);
        grid->Add(radio, wxGBPosition(static_cast<int>(index / 3), static_cast<int>(index % 3)));
        delimiters[index] = radio;
    }
    delimiters[selected]->SetValue(true);
    auto* custom =
        new wxTextCtrl(delimiterGroup->GetStaticBox(), wxID_ANY,
                       selected == delimiters.size() - 1 ? wxString(static_cast<wxChar>(delimiter)) : wxString{});
    custom->SetName("Custom delimiter");
    AppLocalization::SetAccessibleName(*custom, _("Custom delimiter"));
    custom->Enable(selected == delimiters.size() - 1);
    auto* merge = new wxCheckBox(delimiterGroup->GetStaticBox(), wxID_ANY, _("Treat consecutive delimiters as one."));
    merge->SetName("Merge delimiters");
    merge->SetValue((options & CsvOption::MergeDelimiter) != 0);
    grid->Add(merge, wxGBPosition(2, 0), wxGBSpan(1, 2), wxALIGN_CENTER_VERTICAL);
    grid->Add(custom, wxGBPosition(2, 2), wxDefaultSpan, wxEXPAND);
    delimiterGroup->Add(grid, 1, wxEXPAND | wxALL, gap);
    contents->Add(delimiterGroup, 0, wxEXPAND | wxALL, margin);

    auto* qualifierGroup = new wxStaticBoxSizer(wxHORIZONTAL, &dialog, _("Text qualifier"));
    constexpr auto qualifierLabels =
        std::to_array<const char*>({wxTRANSLATE("Double quote"), wxTRANSLATE("Single quote"), wxTRANSLATE("None")});
    constexpr auto qualifierCharacters = std::to_array<unsigned char>({'"', '\'', 0});
    std::array<wxRadioButton*, qualifierLabels.size()> qualifiers{};
    const auto qualifier = (options >> 8U) & 0x7fU;
    for (size_t index = 0; index < qualifiers.size(); ++index) {
        auto* radio =
            new wxRadioButton(qualifierGroup->GetStaticBox(), wxID_ANY, wxGetTranslation(qualifierLabels[index]),
                              wxDefaultPosition, wxDefaultSize, index == 0 ? wxRB_GROUP : 0);
        radio->SetName(qualifierLabels[index]);
        AppLocalization::SetAccessibleName(*radio, wxGetTranslation(qualifierLabels[index]));
        qualifierGroup->Add(radio, 1, wxALL, gap);
        qualifiers[index] = radio;
        radio->SetValue(qualifierCharacters[index] == qualifier);
    }
    contents->Add(qualifierGroup, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, margin);
    auto* bottom = new wxBoxSizer(wxHORIZONTAL);
    auto* escape = new wxCheckBox(&dialog, wxID_ANY, _("Escape with backslash."));
    escape->SetName("Backslash escape");
    escape->SetValue(qualifier == 0 || (options & CsvOption::BackslashEscape) != 0);
    bottom->Add(escape, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    bottom->AddStretchSpacer();
    auto* ok = new wxButton(&dialog, wxID_OK);
    ok->SetDefault();
    bottom->Add(ok);
    contents->Add(bottom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, margin);
    for (auto* radio : delimiters)
        radio->Bind(wxEVT_RADIOBUTTON,
                    [custom, other = delimiters.back()](wxCommandEvent&) { custom->Enable(other->GetValue()); });
    qualifiers.back()->Bind(wxEVT_RADIOBUTTON, [escape](wxCommandEvent&) { escape->SetValue(true); });
    dialog.Bind(wxEVT_CHAR_HOOK, [&dialog](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE)
            dialog.EndModal(wxID_CANCEL);
        else
            event.Skip();
    });
    DialogLayout::Fit(dialog, contents);
    dialog.CentreOnParent();
    delimiters[selected]->SetFocus();
    if (dialog.ShowModal() != wxID_OK) return false;

    uint32_t updated = ',';
    for (size_t index = 0; index + 1 < delimiters.size(); ++index)
        if (delimiters[index]->GetValue()) updated = delimiterCharacters[index];
    if (delimiters.back()->GetValue() && !custom->GetValue().empty()) {
        const auto ch = custom->GetValue()[0].GetValue();
        if (ch > ' ' && ch < 0x7f) updated = ch;
    }
    uint32_t quote = 0;
    for (size_t index = 0; index < qualifiers.size(); ++index)
        if (qualifiers[index]->GetValue()) quote = qualifierCharacters[index];
    updated |= quote << 8U;
    if (quote == 0 || escape->GetValue()) updated |= CsvOption::BackslashEscape;
    if (merge->GetValue()) updated |= CsvOption::MergeDelimiter;
    const bool changed = updated != options;
    options = updated;
    return changed;
}
