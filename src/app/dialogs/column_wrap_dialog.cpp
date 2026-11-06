#include "app/dialogs/column_wrap_dialog.h"
#include "app/dialogs/column_wrap_digits.h"
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <algorithm>
#include <charconv>
#include <cstdint>

namespace {
int DigitValue(char32_t character) {
    // Captured from the original dialog's Windows GetDlgItemInt conversion.
    for (const auto& range : ColumnWrapDigitRanges)
        if (character >= range.first && character <= range.last) return static_cast<int>(character - range.zero);
    return -1;
}
} // namespace

bool ShowColumnWrapDialog(wxWindow* parent, int& column, int boundary) {
    wxDialog dialog(parent, wxID_ANY, "Column Wrap");
    auto* contents = new wxBoxSizer(wxVERTICAL);
    contents->Add(new wxStaticText(&dialog, wxID_ANY, "&Boundary:"), 0, wxALL, dialog.FromDIP(10));
    auto* input = new wxTextCtrl(&dialog, wxID_ANY, wxString::Format("%d", column ? column : boundary));
    input->SetName("Boundary");
    input->SetMaxLength(15);
    input->Bind(wxEVT_CHAR, [](wxKeyEvent& event) {
        const wxUniChar ch(event.GetUnicodeKey());
        if (ch.GetValue() < 32 || DigitValue(ch.GetValue()) >= 0 || event.ControlDown() || event.AltDown())
            event.Skip();
    });
    contents->Add(input, 0, wxEXPAND | wxLEFT | wxRIGHT, dialog.FromDIP(10));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    input->SetFocus();
    input->SelectAll();
    dialog.CentreOnParent();
    dialog.Bind(
        wxEVT_BUTTON,
        [&](wxCommandEvent&) {
            // Original unsigned GetDlgItemInt accepts leading spaces, requires a
            // complete decimal value, then stores its UINT result in a signed int.
            std::string value;
            bool valid = true;
            const auto text = input->GetValue();
            for (auto position = text.begin(); position != text.end(); ++position) {
                auto character = static_cast<char32_t>(wxUniChar(*position).GetValue());
                // wxString iterates UTF-16 units in the default Windows build.
                // GetDlgItemInt accepts supplementary digits as complete pairs.
                if (character >= 0xD800 && character <= 0xDBFF) {
                    if (++position == text.end()) {
                        valid = false;
                        break;
                    }
                    const auto low = wxUniChar(*position).GetValue();
                    if (low < 0xDC00 || low > 0xDFFF) {
                        valid = false;
                        break;
                    }
                    character = 0x10000 + ((character - 0xD800) << 10) + low - 0xDC00;
                }
                if (character == ' ' && value.empty()) continue;
                const auto digit = DigitValue(character);
                if (digit < 0) {
                    valid = false;
                    break;
                }
                value += static_cast<char>('0' + digit);
            }
            uint32_t number = 0;
            valid &= !value.empty();
            if (valid) {
                const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
                valid = result.ec == std::errc{} && result.ptr == value.data() + value.size();
            }
            if (!valid) {
                input->SetFocus();
                input->SelectAll();
                return;
            }
            column = std::clamp(static_cast<int32_t>(number), 1, 512);
            dialog.EndModal(wxID_OK);
        },
        wxID_OK);
    return dialog.ShowModal() == wxID_OK;
}
