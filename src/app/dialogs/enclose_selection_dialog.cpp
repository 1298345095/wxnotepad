#include "app/dialogs/enclose_selection_dialog.h"
#include "editor/editor_control.h"
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
wxString WinEditText(wxTextCtrl* input) {
    // wx multiline controls expose LF on every platform. Reconstruct the native
    // WinEdit line separators consumed by the original enclosure operation.
    auto text = input->GetValue();
    text.Replace("\n", "\r\n");
    return text;
}
} // namespace

void ShowEncloseSelectionDialog(wxWindow* parent, EditorControl& editor, wxString& before, wxString& after) {
    // Original Edit.cpp::EditEncloseSelectionDlgProc and IDD_ENCLOSESELECTION.
    wxDialog dialog(parent, wxID_ANY, "Enclose Selection", wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* contents = new wxBoxSizer(wxVERTICAL);
    const auto addInput = [contents, &dialog](const wxString& label, const wxString& name, const wxString& value) {
        contents->Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
        auto display = value;
        display.Replace("\r\n", "\n");
        auto* input = new wxTextCtrl(&dialog, wxID_ANY, display, wxDefaultPosition, dialog.FromDIP(wxSize(400, 65)),
                                     wxTE_MULTILINE | wxTE_PROCESS_ENTER);
        input->SetName(name);
        contents->Add(input, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
        return input;
    };
    auto* prefix = addInput("Insert &before selection:", "Insert before selection", before);
    auto* suffix = addInput("Insert &after selection:", "Insert after selection", after);
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    dialog.SetMinSize(dialog.GetSize());
    prefix->SetFocus();
    dialog.CentreOnParent();
    dialog.Bind(
        wxEVT_BUTTON,
        [prefix, suffix, &before, &after, &editor, &dialog](wxCommandEvent&) {
            before = WinEditText(prefix);
            after = WinEditText(suffix);
            if (!editor.EncloseSelection(before, after))
                wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                             wxOK | wxICON_WARNING, &dialog);
            dialog.EndModal(wxID_OK);
        },
        wxID_OK);
    dialog.ShowModal();
    editor.SetFocus();
}
