#include "app/dialogs/print_page_setup_dialog.h"
#include "app/editor_printing.h"
#include "app/localization.h"
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/intl.h>
#include <wx/printdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace {
wxChoice* AddChoices(wxDialog& dialog, wxSizer& fields, const wxString& label, const wxString& accessibleName,
                     const wxArrayString& choices, int selected) {
    fields.Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto* choice = new wxChoice(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
    choice->SetSelection(selected);
    AppLocalization::SetAccessibleName(*choice, accessibleName);
    fields.Add(choice, 1, wxEXPAND);
    return choice;
}

int ParseZoom(const wxComboBox& input) {
    const auto text = input.GetValue().ToStdString(wxConvUTF8);
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    return end != text.c_str() && value >= Scintilla::MinZoomLevel && value <= Scintilla::MaxZoomLevel
               ? static_cast<int>(value)
               : PrintOptions{}.zoom;
}
} // namespace

std::optional<wxPrintData> ShowPrintPageSetupDialog(wxWindow& parent, PrintOptions& options, const wxPrintData& driver,
                                                    PrintMarginDisplay units) {
    // TRANSLATORS: Title of the printing configuration dialog (IDD_PAGESETUP).
    wxDialog dialog(&parent, wxID_ANY, _("Page Setup"));
    auto* content = new wxBoxSizer(wxVERTICAL);
    const auto padding = dialog.FromDIP(10);
    // TRANSLATORS: Opens native paper and orientation controls from IDD_PAGESETUP.
    auto* paperButton = new wxButton(&dialog, wxID_ANY, _("Paper and &Orientation..."));
    content->Add(paperButton, 0, wxEXPAND | wxALL, padding);
    std::optional<wxPrintData> draft;
    paperButton->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        wxPageSetupDialogData data(EditorPrinting::CopyPrinterData(draft ? *draft : driver));
        data.EnableMargins(false);
        wxPageSetupDialog paper(&dialog, &data);
        if (paper.ShowModal() == wxID_OK) draft = paper.GetPageSetupDialogData().GetPrintData();
    });

    // TRANSLATORS: IDD_PAGESETUP margin group with the current unit made explicit.
    const auto marginGroup = units.metric ? _("Margins (millimeters)") : _("Margins (inches)");
    auto* marginBox = new wxStaticBoxSizer(wxVERTICAL, &dialog, marginGroup);
    auto* marginFields = new wxFlexGridSizer(4, dialog.FromDIP(5), padding);
    // TRANSLATORS: Paper edge labels in IDD_PAGESETUP, in left/top/right/bottom order.
    const std::array marginLabels = {_("Left:"), _("Top:"), _("Right:"), _("Bottom:")};
    // TRANSLATORS: Screen reader names for the four IDC_PAGESETUP_MARGIN_* inputs.
    const std::array marginNames = {_("Left margin"), _("Top margin"), _("Right margin"), _("Bottom margin")};
    constexpr double maximumMargin = std::numeric_limits<int>::max();
    std::array<wxSpinCtrlDouble*, PrintMarginKeys.size()> marginInputs{};
    for (size_t index = 0; index < marginInputs.size(); ++index) {
        auto* owner = marginBox->GetStaticBox();
        marginFields->Add(new wxStaticText(owner, wxID_ANY, marginLabels[index]), 0, wxALIGN_CENTER_VERTICAL);
        marginInputs[index] = new wxSpinCtrlDouble(
            owner, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0,
            maximumMargin / units.unitsPerDisplayUnit,
            static_cast<double>(options.margins[index]) / units.unitsPerDisplayUnit, 1.0 / units.unitsPerDisplayUnit);
        marginInputs[index]->SetDigits(units.decimalPlaces);
        AppLocalization::SetAccessibleName(*marginInputs[index], marginNames[index]);
        marginFields->Add(marginInputs[index], 1, wxEXPAND);
    }
    marginBox->Add(marginFields, 1, wxEXPAND | wxALL, padding);
    content->Add(marginBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, padding);

    auto* fields = new wxFlexGridSizer(2, dialog.FromDIP(5), padding);
    fields->AddGrowableCol(1);
    const auto* const header = AddChoices(
        // TRANSLATORS: Caption and screen reader name for IDC_PAGESETUP_HEADER_LIST.
        dialog, *fields, _("Header:"), _("Print header"),
        // TRANSLATORS: IDS_PRINT_HEADER choices for text at the top of each printed page.
        {_("Filename, Current Date and Time"), _("Filename, Current Date"), _("Filename"), _("Leave blank")},
        std::to_underlying(options.header));
    const auto* const footer = AddChoices(
        // TRANSLATORS: Caption and screen reader name for IDC_PAGESETUP_FOOTER_LIST.
        dialog, *fields, _("Footer:"), _("Print footer"),
        // TRANSLATORS: IDS_PRINT_FOOTER choices for text at the bottom of each printed page.
        {_("Page Number"), _("Leave blank")}, std::to_underlying(options.footer));
    auto* const colour = AddChoices(
        // TRANSLATORS: Caption and screen reader name for IDC_PAGESETUP_COLOR_MODE_LIST.
        dialog, *fields, _("Print color mode:"), _("Print color mode"),
        // TRANSLATORS: IDS_PRINT_COLOR choices for mapping editor colors to print.
        {_("Normal"), _("Invert light (dark background)"), _("Black on white"), _("Color on white"),
         _("Color on white (except line numbers)"), _("Current screen color (WYSIWYG)")},
        std::to_underlying(options.colour));
    colour->SetName("Print color mode");
    // TRANSLATORS: IDC_PAGESETUP_ZOOMLEVEL label; the numbers are minimum and maximum zoom percentages.
    const auto zoomLabel =
        wxString::Format(_("Print zoom (%d%% to %d%%):"), Scintilla::MinZoomLevel, Scintilla::MaxZoomLevel);
    fields->Add(new wxStaticText(&dialog, wxID_ANY, zoomLabel), 0, wxALIGN_CENTER_VERTICAL);
    auto* zoom = new wxComboBox(&dialog, wxID_ANY, wxString::Format("%d%%", options.zoom));
    zoom->SetName("Print zoom");
    // TRANSLATORS: Screen reader name for the IDC_PAGESETUP_ZOOMLEVEL percentage field.
    AppLocalization::SetAccessibleName(*zoom, _("Print zoom"));
    zoom->SetMaxLength(8);
    for (int level : {500, 450, 400, 350, 300, 250, 200, 175, 150, 125, 100, 75, 50, 25})
        zoom->Append(wxString::Format("%d%%", level));
    fields->Add(zoom, 1, wxEXPAND);
    content->Add(fields, 0, wxEXPAND | wxALL, padding);
    content->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, padding);
    dialog.SetSizerAndFit(content);
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;

    options.header = static_cast<PrintHeader>(header->GetSelection());
    options.footer = static_cast<PrintFooter>(footer->GetSelection());
    options.colour = static_cast<Scintilla::PrintOption>(colour->GetSelection());
    options.zoom = ParseZoom(*zoom);
    for (size_t index = 0; index < options.margins.size(); ++index)
        options.margins[index] =
            static_cast<int>(std::lround(marginInputs[index]->GetValue() * units.unitsPerDisplayUnit));
    return draft;
}
