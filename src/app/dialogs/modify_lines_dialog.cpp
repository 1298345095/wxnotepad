#include "app/dialogs/modify_lines_dialog.h"
#include "editor/editor_control.h"
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace {
wxString WinEditText(wxTextCtrl* input) {
    auto text = input->GetValue();
    text.Replace("\n", "\r\n");
    return text;
}
} // namespace

void ShowModifyLinesDialog(wxWindow* parent, EditorControl& editor, wxString& before, wxString& after,
                           bool& skipEmpty) {
    // Original Edit.cpp::EditModifyLinesDlgProc and IDD_MODIFYLINES.
    wxDialog dialog(parent, wxID_ANY, "Modify Lines", wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* contents = new wxBoxSizer(wxVERTICAL);
    const auto addInput = [&](const wxString& label, const wxString& name, const wxString& value) {
        contents->Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
        auto display = value;
        display.Replace("\r\n", "\n");
        auto* input = new wxTextCtrl(&dialog, wxID_ANY, display, wxDefaultPosition, dialog.FromDIP(wxSize(400, 65)),
                                     wxTE_MULTILINE | wxTE_PROCESS_ENTER);
        input->SetName(name);
        contents->Add(input, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
        return input;
    };
    auto* prefix = addInput("&Prefix text to lines:", "Prefix text to lines", before);
    auto* suffix = addInput("&Append text to lines:", "Append text to lines", after);
    auto* focused = prefix;
    for (auto* input : {prefix, suffix}) {
        input->Bind(wxEVT_SET_FOCUS, [&, input](wxFocusEvent& event) {
            focused = input;
            event.Skip();
        });
    }
    struct Substitution {
        const char* token;
        const char* zeroToken;
        const char* description;
    };
    for (const auto& row :
         {Substitution{"$(L)", "$(0L)", "Document line number."}, Substitution{"$(N)", "$(0N)", "Continuous number."},
          Substitution{"$(I)", "$(0I)", "Continuous number (zero-based)."}}) {
        auto* controls = new wxBoxSizer(wxHORIZONTAL);
        for (const char* token : {row.token, row.zeroToken}) {
            auto* link =
                new wxHyperlinkCtrl(&dialog, wxID_ANY, token, token, wxDefaultPosition, wxDefaultSize, wxHL_ALIGN_LEFT);
            link->SetName(token);
            // The original SysLink controls are outside the TAB traversal chain.
            link->DisableFocusFromKeyboard();
            controls->Add(link, 0, wxRIGHT, dialog.FromDIP(10));
            link->Bind(wxEVT_HYPERLINK, [&, token](wxHyperlinkEvent&) {
                focused->SetSelection(-1, -1);
                focused->WriteText(token);
                dialog.CallAfter([field = focused] { field->SetFocus(); });
            });
        }
        controls->Add(new wxStaticText(&dialog, wxID_ANY, row.description));
        contents->Add(controls, 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
    }
    auto* skip = new wxCheckBox(&dialog, wxID_ANY, "&Skip empty lines.");
    skip->SetName("Skip empty lines");
    skip->SetValue(skipEmpty);
    contents->Add(skip, 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
    contents->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    dialog.SetSizerAndFit(contents);
    dialog.SetMinSize(dialog.GetSize());
    prefix->SetFocus();
    dialog.CentreOnParent();
    dialog.Bind(
        wxEVT_BUTTON,
        [&](wxCommandEvent&) {
            before = WinEditText(prefix);
            after = WinEditText(suffix);
            skipEmpty = skip->GetValue();
            if (!editor.ModifyLines(before, after, skipEmpty))
                wxMessageBox("This operation can't be performed within a rectangular selection.", "wxnotepad",
                             wxOK | wxICON_WARNING, &dialog);
            dialog.EndModal(wxID_OK);
        },
        wxID_OK);
    dialog.ShowModal();
    editor.SetFocus();
}
