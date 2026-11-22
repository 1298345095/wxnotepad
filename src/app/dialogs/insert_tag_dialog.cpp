#include "app/dialogs/insert_tag_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/dialogs/win_edit_input.h"
#include <wx/translation.h>
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
    wxDialog dialog(parent, wxID_ANY, _("Insert HTML/XML Tag"), wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* contents = new wxBoxSizer(wxVERTICAL);
    auto* fields = new wxFlexGridSizer(1);
    fields->AddGrowableCol(0);
    // Original ResizeDlg_InitY2 gives the opening tag 75% of added height.
    fields->AddGrowableRow(1, 3);
    fields->AddGrowableRow(3, 1);
    auto* opening = AddWinEditInput(dialog, *fields, _("&Opening tag (with attributes):"), "Opening tag", "<tag>");
    auto* closing = AddWinEditInput(dialog, *fields, _("&Closing tag (can be edited):"), "Closing tag", "</tag>");
    contents->Add(fields, 1, wxEXPAND);
    opening->Bind(wxEVT_TEXT, [opening, closing](wxCommandEvent&) {
        // A new opening edit overwrites even an explicitly edited closing tag.
        closing->ChangeValue(ClosingTag(opening->GetValue()));
    });
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    DialogLayout::Fit(dialog, contents, DialogLayout::Resize::Both);
    const auto accept = [opening, closing, &editor, &dialog] {
        auto before = opening->GetValue();
        auto after = closing->GetValue();
        before.Replace("\n", "\r\n");
        after.Replace("\n", "\r\n");
        if (!editor.EncloseSelection(before, after))
            wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                         wxOK | wxICON_WARNING, &dialog);
        dialog.EndModal(wxID_OK);
    };
    dialog.Bind(wxEVT_BUTTON, [accept](wxCommandEvent&) { accept(); }, wxID_OK);
    for (auto* input : {opening, closing}) {
        input->Bind(wxEVT_KEY_DOWN, [accept](wxKeyEvent& event) {
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
