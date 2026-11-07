#include "app/dialogs/insert_tag_dialog.h"
#include "editor/editor_control.h"
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
namespace {
bool TagCharacter(wxUniChar ch) {
    const auto value = ch.GetValue();
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
           value == ':' || value == '_' || value == '-' || value == '.';
}
wxString ClosingTag(const wxString& text) {
    // Original EditInsertTagDlgProc scans the first '<' and first following '>'.
    if (text.length() < 3) return {};
    const auto start = text.find('<');
    if (start == wxString::npos) return {};
    auto current = start + 1;
    while (current < text.length() && TagCharacter(text[current]))
        ++current;
    const auto name = text.Mid(start + 1, current - start - 1);
    const auto end = text.find('>', current);
    if (name.empty() || end == wxString::npos || text[end - 1] == '/') return {};
    // All currently delivered language contexts take the original non-XML path.
    // The one-letter <p> deliberately bypasses the void-element check.
    const wxString voidTags = " area base basefont br col command embed frame hr img input isindex keygen link meta p "
                              "param source track wbr ";
    if (name.length() > 1 && name.length() < 13 && voidTags.Contains(" " + name.Lower() + " ")) return {};
    return "</" + name + ">";
}
} // namespace

void ShowInsertTagDialog(wxWindow* parent, EditorControl& editor) {
    // Original Notepad4 89b8b36 Edit.cpp::EditInsertTagDlgProc and IDD_INSERTTAG.
    wxDialog dialog(parent, wxID_ANY, "Insert HTML/XML Tag", wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* contents = new wxBoxSizer(wxVERTICAL);
    const auto field = [&](const char* label, const char* name, const char* initial) {
        contents->Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
        auto* input = new wxTextCtrl(&dialog, wxID_ANY, initial, wxDefaultPosition, dialog.FromDIP(wxSize(400, 65)),
                                     wxTE_MULTILINE | wxTE_PROCESS_ENTER);
        input->SetName(name);
        contents->Add(input, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
        return input;
    };
    auto* opening = field("&Opening tag (with attributes):", "Opening tag", "<tag>");
    auto* closing = field("&Closing tag (can be edited):", "Closing tag", "</tag>");
    opening->Bind(wxEVT_TEXT, [opening, closing](wxCommandEvent&) {
        // A new opening edit overwrites even an explicitly edited closing tag.
        closing->ChangeValue(ClosingTag(opening->GetValue()));
    });
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    dialog.SetMinSize(dialog.GetSize());
    const auto accept = [&] {
        auto before = opening->GetValue();
        auto after = closing->GetValue();
        before.Replace("\n", "\r\n");
        after.Replace("\n", "\r\n");
        if (!editor.EncloseSelection(before, after))
            wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                         wxOK | wxICON_WARNING, &dialog);
        dialog.EndModal(wxID_OK);
    };
    dialog.Bind(wxEVT_BUTTON, [&](wxCommandEvent&) { accept(); }, wxID_OK);
    for (auto* input : {opening, closing}) {
        input->Bind(wxEVT_KEY_DOWN, [&](wxKeyEvent& event) {
            // Original MultilineEditProc accepts Enter; Shift+Enter inserts a line break.
            if (event.GetKeyCode() == WXK_RETURN && !event.ShiftDown())
                accept();
            else
                event.Skip();
        });
    }
    opening->SetFocus();
    opening->SetSelection(1, 4);
    dialog.CentreOnParent();
    dialog.ShowModal();
    editor.SetFocus();
}
