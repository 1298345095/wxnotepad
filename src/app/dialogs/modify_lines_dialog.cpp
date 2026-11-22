#include "app/dialogs/modify_lines_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/dialogs/win_edit_input.h"
#include <wx/translation.h>
#include "editor/editor_control.h"
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

void ShowModifyLinesDialog(wxWindow* parent, EditorControl& editor, wxString& before, wxString& after,
                           bool& skipEmpty) {
    // Original Edit.cpp::EditModifyLinesDlgProc and IDD_MODIFYLINES.
    wxDialog dialog(parent, wxID_ANY, _("Modify Lines"), wxDefaultPosition, wxDefaultSize,
                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    auto* contents = new wxBoxSizer(wxVERTICAL);
    auto* prefix = AddWinEditInput(dialog, *contents, _("&Prefix text to lines:"), "Prefix text to lines", before);
    auto* suffix = AddWinEditInput(dialog, *contents, _("&Append text to lines:"), "Append text to lines", after);
    auto* focused = prefix;
    for (auto* input : {prefix, suffix}) {
        input->Bind(wxEVT_KEY_DOWN, [&dialog](wxKeyEvent& event) {
            // Original Helpers.cpp::MultilineEditProc accepts Return unless Shift is down.
            if ((event.GetKeyCode() == WXK_RETURN || event.GetKeyCode() == WXK_NUMPAD_ENTER) && !event.ShiftDown()) {
                wxCommandEvent accept(wxEVT_BUTTON, wxID_OK);
                dialog.GetEventHandler()->ProcessEvent(accept);
            } else {
                event.Skip();
            }
        });
        input->Bind(wxEVT_SET_FOCUS, [&focused, input](wxFocusEvent& event) {
            focused = input;
            event.Skip();
        });
    }
    struct Substitution {
        const char* token;
        const char* zeroToken;
        const char* description;
    };
    for (const auto& row : {Substitution{"$(L)", "$(0L)", wxTRANSLATE("Document line number.")},
                            Substitution{"$(N)", "$(0N)", wxTRANSLATE("Continuous number.")},
                            Substitution{"$(I)", "$(0I)", wxTRANSLATE("Continuous number (zero-based).")}}) {
        auto* controls = new wxBoxSizer(wxHORIZONTAL);
        for (const char* token : {row.token, row.zeroToken}) {
            auto* link = new wxHyperlinkCtrl;
            // The original SysLink controls are outside the TAB traversal chain.
            // Set this before Create so wxMSW also omits the native WS_TABSTOP style.
            link->DisableFocusFromKeyboard();
            link->Create(&dialog, wxID_ANY, token, token, wxDefaultPosition, wxDefaultSize, wxHL_ALIGN_LEFT);
            link->SetName(token);
            controls->Add(link, 0, wxRIGHT, dialog.FromDIP(10));
            link->Bind(wxEVT_HYPERLINK, [&focused, &dialog, token](wxHyperlinkEvent&) {
                focused->SetSelection(-1, -1);
                focused->WriteText(token);
                dialog.CallAfter([field = focused] { field->SetFocus(); });
            });
        }
        controls->Add(new wxStaticText(&dialog, wxID_ANY, wxGetTranslation(row.description)));
        contents->Add(controls, 0, wxLEFT | wxRIGHT | wxTOP, dialog.FromDIP(10));
    }
    auto* skip = new wxCheckBox(&dialog, wxID_ANY, _("&Skip empty lines."));
    skip->SetName("Skip empty lines");
    skip->SetValue(skipEmpty);
    auto* footer = new wxBoxSizer(wxHORIZONTAL);
    footer->Add(skip, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, dialog.FromDIP(10));
    footer->AddStretchSpacer();
    footer->Add(dialog.CreateStdDialogButtonSizer(wxOK | wxCANCEL));
    contents->Add(footer, 0, wxEXPAND | wxALL, dialog.FromDIP(10));
    DialogLayout::Fit(dialog, contents, DialogLayout::Resize::Both);
    prefix->SetFocus();
    dialog.CentreOnParent();
    dialog.Bind(
        wxEVT_BUTTON,
        [prefix, suffix, skip, &before, &after, &skipEmpty, &editor, &dialog](wxCommandEvent&) {
            before = ReadWinEditInput(*prefix);
            after = ReadWinEditInput(*suffix);
            skipEmpty = skip->GetValue();
            if (!editor.ModifyLines(before, after, skipEmpty))
                wxMessageBox(_("This operation can't be performed within a rectangular selection."), "wxnotepad",
                             wxOK | wxICON_WARNING, &dialog);
            dialog.EndModal(wxID_OK);
        },
        wxID_OK);
    dialog.ShowModal();
    editor.SetFocus();
}
