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

    wxString marginGroup;
    if (units.metric) {
        // TRANSLATORS: IDD_PAGESETUP margin group in a metric measurement locale.
        marginGroup = _("Margins (millimeters)");
    } else {
        // TRANSLATORS: IDD_PAGESETUP margin group in a U.S. measurement locale.
        marginGroup = _("Margins (inches)");
    }
    auto* marginBox = new wxStaticBoxSizer(wxVERTICAL, &dialog, marginGroup);
    auto* marginFields = new wxFlexGridSizer(4, dialog.FromDIP(5), padding);
    const std::array marginLabels = {
        // TRANSLATORS: Left paper margin in IDD_PAGESETUP.
        _("Left:"),
        // TRANSLATORS: Top paper margin in IDD_PAGESETUP.
        _("Top:"),
        // TRANSLATORS: Right paper margin in IDD_PAGESETUP.
        _("Right:"),
        // TRANSLATORS: Bottom paper margin in IDD_PAGESETUP.
        _("Bottom:"),
    };
    const std::array marginNames = {
        // TRANSLATORS: Accessible name for the left paper margin input in IDD_PAGESETUP.
        _("Left margin"),
        // TRANSLATORS: Accessible name for the top paper margin input in IDD_PAGESETUP.
        _("Top margin"),
        // TRANSLATORS: Accessible name for the right paper margin input in IDD_PAGESETUP.
        _("Right margin"),
        // TRANSLATORS: Accessible name for the bottom paper margin input in IDD_PAGESETUP.
        _("Bottom margin"),
    };
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
    // TRANSLATORS: IDS_PRINT_HEADER and IDS_PRINT_FOOTER choice to omit the header or footer.
    const auto blank = _("Leave blank");
    const auto* const header =
        AddChoices(dialog, *fields,
                   // TRANSLATORS: Caption for IDC_PAGESETUP_HEADER_LIST.
                   _("Header:"),
                   // TRANSLATORS: Accessible name for IDC_PAGESETUP_HEADER_LIST.
                   _("Print header"),
                   {// TRANSLATORS: IDS_PRINT_HEADER choice: show the filename and the current date and time.
                    _("Filename, Current Date and Time"),
                    // TRANSLATORS: IDS_PRINT_HEADER choice: show the filename and current date without the time.
                    _("Filename, Current Date"),
                    // TRANSLATORS: IDS_PRINT_HEADER choice: show only the filename.
                    _("Filename"), blank},
                   std::to_underlying(options.header));
    const auto* const footer =
        AddChoices(dialog, *fields,
                   // TRANSLATORS: Caption for IDC_PAGESETUP_FOOTER_LIST.
                   _("Footer:"),
                   // TRANSLATORS: Accessible name for IDC_PAGESETUP_FOOTER_LIST.
                   _("Print footer"),
                   {// TRANSLATORS: IDS_PRINT_FOOTER choice: show the page number at the bottom of each page.
                    _("Page Number"), blank},
                   std::to_underlying(options.footer));
    auto* const colour =
        AddChoices(dialog, *fields,
                   // TRANSLATORS: Caption for IDC_PAGESETUP_COLOR_MODE_LIST.
                   _("Print color mode:"),
                   // TRANSLATORS: Accessible name for IDC_PAGESETUP_COLOR_MODE_LIST.
                   _("Print color mode"),
                   {
                       // TRANSLATORS: IDS_PRINT_COLOR choice: normal print color mapping.
                       _("Normal"),
                       // TRANSLATORS: IDS_PRINT_COLOR choice: invert lightness when printing a dark theme.
                       _("Invert light (dark background)"),
                       // TRANSLATORS: IDS_PRINT_COLOR choice: print black text on white paper.
                       _("Black on white"),
                       // TRANSLATORS: IDS_PRINT_COLOR choice: print colored text on white paper.
                       _("Color on white"),
                       // TRANSLATORS: IDS_PRINT_COLOR choice: use white backgrounds except in the line number margin.
                       _("Color on white (except line numbers)"),
                       // TRANSLATORS: IDS_PRINT_COLOR choice: print with the current editor theme colors.
                       _("Current screen color (WYSIWYG)"),
                   },
                   std::to_underlying(options.colour));
    colour->SetName("Print color mode");
    const auto zoomLabel = wxString::Format(
        // TRANSLATORS: IDC_PAGESETUP_ZOOMLEVEL label; the numbers are minimum and maximum zoom percentages.
        _("Print zoom (%d%% to %d%%):"), Scintilla::MinZoomLevel, Scintilla::MaxZoomLevel);
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
