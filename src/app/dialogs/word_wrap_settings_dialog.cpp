#include "app/dialogs/word_wrap_settings_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/localization.h"
#include "settings/original_settings.h"
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/translation.h>

bool ShowWordWrapSettingsDialog(wxWindow* parent, OriginalSettings& settings) {
    // Notepad4 89b8b36 Dialogs.cpp::WordWrapSettingsDlgProc option ordering and commit rules.
    // Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
    wxDialog dialog(parent, wxID_ANY, _("Word Wrap Settings"));
    auto* layout = new wxBoxSizer(wxVERTICAL);
    auto* fields = new wxFlexGridSizer(2, dialog.FromDIP(8), dialog.FromDIP(12));
    fields->AddGrowableCol(1);
    const auto choice = [&](const wxString& label, const char* name, const wxArrayString& options, int selection) {
        fields->Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        auto* control = new wxChoice(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, options);
        control->SetName(name);
        AppLocalization::SetAccessibleName(*control, wxStripMenuCodes(label));
        control->SetSelection(selection);
        fields->Add(control, 1, wxEXPAND);
        return control;
    };
    auto* indent =
        choice(_("Continuation &indent:"), "Wrap indent",
               {_("No wrap indent"), _("Wrap indent by 1 character"), _("Wrap indent by 2 characters"),
                _("Wrap indent by 1 level"), _("Wrap indent by 2 levels"), _("Wrap indent same as first subline"),
                _("Wrap indent 1 level more than first subline"), _("Wrap indent 2 levels more than first subline")},
               std::to_underlying(settings.wordWrapIndent));
    auto* before =
        choice(_("Symbol &before wrap:"), "Wrap symbol before", {_("None"), _("Near text"), _("Near borders")},
               settings.showWordWrapSymbols ? settings.wordWrapSymbols % 10 : 0);
    auto* after = choice(_("Symbol &after wrap:"), "Wrap symbol after",
                         {_("None"), _("Near text"), _("Near borders"), _("In line number margin")},
                         settings.showWordWrapSymbols ? settings.wordWrapSymbols / 10 : 0);
    auto* mode =
        choice(_("Wrap &mode:"), "Wrap mode",
               {_("No Wrap"), _("Word"), _("Character"), _("Whitespace"), _("Automatic")}, settings.wordWrapMode);
    layout->Add(fields, 0, wxEXPAND | wxALL, dialog.FromDIP(12));
    auto* select = new wxCheckBox(&dialog, wxID_ANY, _("Margin click selects visual &subline"));
    select->SetName("Wrap select subline");
    select->SetValue(settings.wordWrapSelectSubLine);
    layout->Add(select, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));
    auto* highlight = new wxCheckBox(&dialog, wxID_ANY, _("&Highlight current subline"));
    highlight->SetName("Wrap highlight subline");
    highlight->SetValue(settings.highlightCurrentLine >= 10);
    layout->Add(highlight, 0, wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(12));
    layout->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, dialog.FromDIP(12));
    DialogLayout::Fit(dialog, layout);
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return false;
    settings.wordWrapIndent = static_cast<WrapContinuationIndent>(indent->GetSelection());
    const int symbols = before->GetSelection() + 10 * after->GetSelection();
    settings.showWordWrapSymbols = symbols != 0;
    if (symbols) settings.wordWrapSymbols = symbols;
    settings.wordWrap = mode->GetSelection() != std::to_underlying(Scintilla::Wrap::None);
    if (settings.wordWrap) settings.wordWrapMode = mode->GetSelection();
    settings.wordWrapSelectSubLine = select->GetValue();
    settings.highlightCurrentLine = settings.highlightCurrentLine % 10 + (highlight->GetValue() ? 10 : 0);
    return true;
}
