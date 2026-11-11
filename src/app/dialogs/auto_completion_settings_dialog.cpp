#include "app/dialogs/auto_completion_settings_dialog.h"
#include "settings/original_settings.h"
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/valtext.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <vector>

bool ShowAutoCompletionSettingsDialog(wxWindow* parent, OriginalSettings& preferences) {
    wxDialog dialog(parent, wxID_ANY, "Auto Completion Settings");
    const int gap = dialog.FromDIP(3);
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* columns = new wxBoxSizer(wxHORIZONTAL);
    auto* words = new wxStaticBoxSizer(wxVERTICAL, &dialog, "Word completion");
    auto* insertion = new wxStaticBoxSizer(wxVERTICAL, &dialog, "Automatically insert");
    const auto check = [&](wxSizer* sizer, const char* label, bool value) {
        auto* control = new wxCheckBox(&dialog, wxID_ANY, label);
        control->SetName(label);
        control->SetValue(value);
        sizer->Add(control, 0, wxALL, gap);
        return control;
    };
    using Flag = std::pair<wxCheckBox*, int>;
    const auto addFlags = [&](wxSizer* sizer, auto options, int mask) {
        std::vector<Flag> controls;
        for (const auto& [label, bit] : options)
            controls.emplace_back(check(sizer, label, (mask & bit) != 0), bit);
        return controls;
    };
    constexpr std::array wordOptions = {
        std::pair{"Complete words", WordCompletionOption::CompleteWord},
        std::pair{"Scan words in document", WordCompletionOption::ScanWordsInDocument},
        std::pair{"Only words in document", WordCompletionOption::OnlyWordsInDocument},
        std::pair{"English IME mode only", WordCompletionOption::EnglishIMEModeOnly},
    };
    auto& completion = preferences.completion;
    const auto wordControls = addFlags(words, wordOptions, completion.completeOption);
    auto* fields = new wxFlexGridSizer(2, gap, gap);
    fields->AddGrowableCol(1);
    const auto field = [&](const char* label, const wxString& value, bool numeric) {
        fields->Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        auto* control = new wxTextCtrl(&dialog, wxID_ANY, value, wxDefaultPosition, dialog.FromDIP(wxSize(95, -1)));
        control->SetName(label);
        if (numeric) {
            control->SetMaxLength(8); // Original EM_LIMITTEXT.
            control->SetValidator(wxTextValidator(wxFILTER_DIGITS));
        }
        fields->Add(control, 1, wxEXPAND);
        return control;
    };
    auto* rows = field("Visible rows", wxString::Format("%d", completion.visibleItemCount), true);
    auto* wordLength = field("Minimum word length", wxString::Format("%d", completion.minWordLength), true);
    auto* numberLength = field("Minimum number length", wxString::Format("%d", completion.minNumberLength), true);
    auto* timeout = field("Scan timeout", wxString::Format("%d ms", completion.scanWordsTimeout), false);
    words->Add(fields, 0, wxEXPAND | wxALL, gap);
    constexpr std::array insertOptions = {
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
    auto* insertGrid = new wxGridSizer(2);
    const auto insertControls = addFlags(insertGrid, insertOptions, preferences.autoInsertMask);
    insertion->Add(insertGrid, 0, wxEXPAND);
    auto* tags = check(insertion, "Auto close HTML and XML tags",
                       (completion.completeOption & WordCompletionOption::CloseTags) != 0);
    auto* indent = check(insertion, "Auto Indent", preferences.autoIndent);
    columns->Add(words, 1, wxEXPAND | wxALL, gap);
    columns->Add(insertion, 1, wxEXPAND | wxALL, gap);
    layout->Add(columns, 0, wxEXPAND);
    auto* scopes = new wxBoxSizer(wxHORIZONTAL);
    auto* completeScope = new wxStaticBoxSizer(wxVERTICAL, &dialog, "Complete words inside");
    auto* scanScope = new wxStaticBoxSizer(wxVERTICAL, &dialog, "Scan document words inside");
    constexpr std::array completeOptions = {std::pair{"Complete inside comments", WordCompletionScope::Comment},
                                            std::pair{"Complete inside strings", WordCompletionScope::String},
                                            std::pair{"Complete inside plain text", WordCompletionScope::PlainText}};
    constexpr std::array scanOptions = {std::pair{"Scan inside comments", WordCompletionScope::Comment},
                                        std::pair{"Scan inside strings", WordCompletionScope::String},
                                        std::pair{"Scan inside plain text", WordCompletionScope::PlainText}};
    auto* completeRow = new wxBoxSizer(wxHORIZONTAL);
    auto* scanRow = new wxBoxSizer(wxHORIZONTAL);
    const auto completeControls = addFlags(completeRow, completeOptions, completion.completeScope);
    const auto scanControls = addFlags(scanRow, scanOptions, completion.scanWordScope);
    constexpr std::array scopeLabels = {"Comments", "Strings", "Plain Text"};
    for (size_t index = 0; index < scopeLabels.size(); ++index) {
        completeControls[index].first->SetLabel(scopeLabels[index]);
        scanControls[index].first->SetLabel(scopeLabels[index]);
    }
    completeScope->Add(completeRow);
    scanScope->Add(scanRow);
    scopes->Add(completeScope, 1, wxEXPAND | wxALL, gap);
    scopes->Add(scanScope, 1, wxEXPAND | wxALL, gap);
    layout->Add(scopes, 0, wxEXPAND);
    auto* confirmation = new wxStaticBoxSizer(wxVERTICAL, &dialog, "Finish completion (Esc cancels)");
    auto* keys = new wxBoxSizer(wxHORIZONTAL);
    constexpr std::array keyOptions = {std::pair{"Complete with Enter", CompletionFillUp::Enter},
                                       std::pair{"Complete with Tab", CompletionFillUp::Tab},
                                       std::pair{"Complete with Space", CompletionFillUp::Space},
                                       std::pair{"Complete with punctuation", CompletionFillUp::Punctuation}};
    const auto keyControls = addFlags(keys, keyOptions, completion.fillUpMask);
    constexpr std::array keyLabels = {"Enter", "Tab", "Space", "Punctuation"};
    for (size_t index = 0; index < keyLabels.size(); ++index)
        keyControls[index].first->SetLabel(keyLabels[index]);
    confirmation->Add(keys, 0, wxEXPAND);
    auto* punctuation = new wxTextCtrl(&dialog, wxID_ANY, completion.fillUpPunctuation);
    punctuation->SetName("Fill-up punctuation");
    punctuation->SetMaxLength(WordCompletionLimits::MaxPunctuationCharacters);
    confirmation->Add(punctuation, 0, wxEXPAND | wxALL, gap);
    layout->Add(confirmation, 0, wxEXPAND | wxALL, gap);
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, gap * 2);
    dialog.SetSizerAndFit(layout);
    dialog.CentreOnParent();
    // Numeric controls use the original edit's input restriction, not wx's
    // modal validation: invalid programmatic text is normalized only on OK.
    dialog.Bind(wxEVT_BUTTON, [&dialog](wxCommandEvent&) { dialog.EndModal(wxID_OK); }, wxID_OK);
    if (dialog.ShowModal() != wxID_OK) return false;
    const auto commitFlags = [](const std::vector<Flag>& controls, int mask) {
        for (const auto& [control, bit] : controls)
            mask = control->GetValue() ? mask | bit : mask & ~bit;
        return mask;
    };
    preferences.autoInsertMask = commitFlags(insertControls, preferences.autoInsertMask);
    completion.completeOption = commitFlags(wordControls, completion.completeOption);
    completion.completeOption = tags->GetValue() ? completion.completeOption | WordCompletionOption::CloseTags
                                                 : completion.completeOption & ~WordCompletionOption::CloseTags;
    preferences.autoIndent = indent->GetValue();
    const auto unsignedValue = [](wxTextCtrl* control, int minimum) {
        // Original GetDlgItemInt(FALSE) accepts a decimal prefix; invalid input
        // yields zero. Values are subsequently assigned to an int and clamped.
        const auto text = control->GetValue().ToStdString(wxConvUTF8);
        char* end;
        const unsigned long value = std::strtoul(text.c_str(), &end, 10);
        const int number = end == text.c_str() || value > std::numeric_limits<unsigned>::max()
                               ? 0
                               : static_cast<int>(static_cast<unsigned>(value));
        return std::max(number, minimum);
    };
    completion.visibleItemCount = unsignedValue(rows, WordCompletionLimits::MinVisibleRows);
    completion.minWordLength = unsignedValue(wordLength, WordCompletionLimits::MinWordLength);
    completion.minNumberLength = unsignedValue(numberLength, WordCompletionLimits::MinNumberLength);
    const auto timeoutText = timeout->GetValue().Left(31).ToStdString(wxConvUTF8);
    char* end;
    const long parsed = std::strtol(timeoutText.c_str(), &end, 10);
    if (end != timeoutText.c_str())
        completion.scanWordsTimeout =
            std::max(static_cast<int>(std::clamp(parsed, static_cast<long>(std::numeric_limits<int>::min()),
                                                 static_cast<long>(std::numeric_limits<int>::max()))),
                     WordCompletionLimits::MinScanTimeoutMs);
    completion.completeScope = commitFlags(completeControls, WordCompletionScope::Other);
    completion.scanWordScope = commitFlags(scanControls, WordCompletionScope::Other);
    completion.fillUpMask = commitFlags(keyControls, 0);
    completion.fillUpPunctuation.clear();
    for (const wxUniChar ch : punctuation->GetValue().Left(WordCompletionLimits::MaxPunctuationCharacters))
        if (ch.GetValue() < 128 && std::ispunct(static_cast<unsigned char>(ch.GetValue())))
            completion.fillUpPunctuation += ch;
    return true;
}
