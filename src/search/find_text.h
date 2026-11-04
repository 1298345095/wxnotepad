#pragma once

#include <wx/string.h>

struct EscapedFindText {
    wxString text;
    bool controls = false;
};

// Adapted from pinned Notepad4 89b8b36 Helpers.cpp::AddBackslashW.
// See licenses/Notepad4.txt. Plain backslashes are doubled only if a control
// character also needs escaping; regex punctuation is deliberately unchanged.
inline EscapedFindText EscapeFindText(const wxString& input) {
    const auto text = input.BeforeFirst(wxUniChar(0));
    EscapedFindText result;
    for (const auto value : text) {
        const auto ch = value.GetValue();
        if (ch >= '\a' && ch <= '\r') {
            result.text += '\\';
            result.text += "abtnvfr"[ch - '\a'];
            result.controls = true;
        } else if (ch == 0x1b) {
            result.text += "\\e";
            result.controls = true;
        } else if (ch < ' ' || ch == 0x7f) {
            result.text += wxString::Format("\\x%02X", ch);
            result.controls = true;
        } else {
            result.text += value;
            if (ch == '\\') result.text += value;
        }
    }
    if (!result.controls) result.text = text;
    return result;
}
