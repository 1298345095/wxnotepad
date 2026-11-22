#include "app/dialogs/enclose_selection_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/dialogs/win_edit_input.h"
#include <wx/translation.h>
#include "editor/editor_control.h"
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

void ShowEncloseSelectionDialog(wxWindow* parent, EditorControl& editor, wxString& before, wxString& after) {
    // Original Edit.cpp::EditEncloseSelectionDlgProc and IDD_ENCLOSESELECTION.
    wxDialog dialog(parent, wxID_ANY, _("Enclose Selection"), wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* contents = new wxBoxSizer(wxVERTICAL);
    auto* prefix =
        AddWinEditInput(dialog, *contents, _("Insert &before selection:"), "Insert before selection", before);
    auto* suffix = AddWinEditInput(dialog, *contents, _("Insert &after selection:"), "Insert after selection", after);
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    DialogLayout::Fit(dialog, contents, DialogLayout::Resize::Both);
    prefix->SetFocus();
    dialog.CentreOnParent();
    dialog.Bind(
        wxEVT_BUTTON,
        [prefix, suffix, &before, &after, &editor, &dialog](wxCommandEvent&) {
            before = ReadWinEditInput(*prefix);
            after = ReadWinEditInput(*suffix);
            if (!editor.EncloseSelection(before, after))
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, &dialog);
            dialog.EndModal(wxID_OK);
        },
        wxID_OK);
    dialog.ShowModal();
    editor.SetFocus();
}
