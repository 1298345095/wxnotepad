#include "app/dialogs/sort_lines_dialog.h"
#include "editor/sort_lines.h"
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/sizer.h>
#include <array>

bool ShowSortLinesDialog(wxWindow* parent, unsigned& options, bool rectangular) {
    wxDialog dialog(parent, wxID_ANY, "Sort Lines");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    const std::array<const char*, 4> labels{"Don't &sort.", "Sort &ascending.", "Sort &descending.", "Shu&ffle lines."};
    const std::array<unsigned, 4> modes{SortOption::DontSort, SortOption::Ascending, SortOption::Descending,
                                        SortOption::Shuffle};
    std::array<wxRadioButton*, 4> radios{};
    for (size_t i = 0; i < radios.size(); ++i) {
        radios[i] =
            new wxRadioButton(&dialog, wxID_ANY, labels[i], wxDefaultPosition, wxDefaultSize, i == 0 ? wxRB_GROUP : 0);
        radios[i]->SetName(wxControl::GetLabelText(labels[i]));
        contents->Add(radios[i], 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(8));
    }
    const size_t selected = options & SortOption::Shuffle    ? 3
                            : options & SortOption::DontSort ? 0
                                                             : 1 + (options & SortOption::Descending);
    radios[selected]->SetValue(true);
    const std::array<const char*, 7> checks{"&Merge duplicate lines.",
                                            "&Remove duplicate lines.",
                                            "Remove &unique lines.",
                                            "Case &insensitive.",
                                            "Logical &number comparison.",
                                            "Group by file &type.",
                                            "&Column sort (rectangular selection)."};
    const std::array<unsigned, 7> flags{
        SortOption::MergeDuplicate, SortOption::RemoveDuplicate, SortOption::RemoveUnique, SortOption::IgnoreCase,
        SortOption::LogicalNumber,  SortOption::FileType,        SortOption::Column};
    std::array<wxCheckBox*, 7> boxes{};
    for (size_t i = 0; i < boxes.size(); ++i) {
        boxes[i] = new wxCheckBox(&dialog, wxID_ANY, checks[i]);
        boxes[i]->SetName(wxControl::GetLabelText(checks[i]));
        boxes[i]->SetValue(i == 6 ? rectangular : (options & flags[i]) != 0);
        contents->Add(boxes[i], 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(i == 0 || i == 3 || i == 6 ? 14 : 8));
    }
    boxes[6]->Enable(rectangular);
    const auto updateMode = [&] {
        const bool enabled = !radios[3]->GetValue();
        for (size_t i = 0; i < 6; ++i)
            boxes[i]->Enable(enabled);
        boxes[0]->Enable(enabled && !boxes[2]->GetValue());
    };
    if (options & SortOption::Shuffle) updateMode();
    if (options & SortOption::RemoveDuplicate) boxes[0]->Enable(false);
    for (auto* radio : radios)
        radio->Bind(wxEVT_RADIOBUTTON, [&](wxCommandEvent&) { updateMode(); });
    boxes[1]->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent&) { boxes[0]->Enable(!boxes[1]->GetValue()); });
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    dialog.CentreOnParent();
    radios[selected]->SetFocus();
    if (dialog.ShowModal() != wxID_OK) return false;
    options = 0;
    for (size_t i = 0; i < radios.size(); ++i)
        if (radios[i]->GetValue()) options |= modes[i];
    for (size_t i = 0; i < boxes.size(); ++i)
        if (boxes[i]->GetValue()) options |= flags[i];
    return true;
}
