#include "app/dialogs/print_page_setup_dialog.h"
#include "app/dialogs/dialog_layout.h"
#include "app/dialogs/print_page_preview.h"
#include "app/dialogs/zoom_input.h"
#include "app/editor_printing.h"
#include "app/localization.h"
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/dcclient.h>
#include <wx/panel.h>
#include <wx/intl.h>
#include <wx/printdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <cmath>
#include <array>
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
} // namespace

std::optional<wxPrintData> ShowPrintPageSetupDialog(wxWindow& parent, PrintOptions& options, const wxPrintData& driver,
                                                    PrintMarginDisplay units) {
    // TRANSLATORS: Title of the printing configuration dialog (IDD_PAGESETUP).
    wxDialog dialog(&parent, wxID_ANY, _("Page Setup"));
    auto content = std::make_unique<wxBoxSizer>(wxVERTICAL);
    const auto padding = dialog.FromDIP(10);
    auto columns = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
    auto settings = std::make_unique<wxBoxSizer>(wxVERTICAL);
    auto previewColumn = std::make_unique<wxBoxSizer>(wxVERTICAL);
    // Keep the original left-hand paper/margins/header/color groups and the
    // right-hand preview/zoom column. Paper selection stays a native dialog.
    auto paperBox = std::make_unique<wxStaticBoxSizer>(wxVERTICAL, &dialog, _("Paper"));
    // TRANSLATORS: Opens native paper and orientation controls from IDD_PAGESETUP.
    auto* paperButton = new wxButton(&dialog, wxID_ANY, _("Paper and &Orientation..."));
    paperBox->Add(paperButton, 0, wxEXPAND | wxALL, padding);
    settings->Add(paperBox.release(), 0, wxEXPAND | wxALL, padding);
    wxPrintData draft = EditorPrinting::CopyPrinterData(driver);
    bool paperChanged = false;
    auto previewBox = std::make_unique<wxStaticBoxSizer>(wxVERTICAL, &dialog, _("Preview"));
    auto* preview = new wxPanel(&dialog, wxID_ANY, wxDefaultPosition, dialog.FromDIP(wxSize(150, 220)));
    preview->SetName("Print page preview");
    AppLocalization::SetAccessibleName(*preview, _("Preview"));
    previewBox->Add(preview, 1, wxEXPAND | wxALL, padding);
    previewColumn->Add(previewBox.release(), 1, wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM, padding);
    paperButton->Bind(wxEVT_BUTTON, [&dialog, &draft, &paperChanged, preview](wxCommandEvent&) {
        wxPageSetupDialogData data(EditorPrinting::CopyPrinterData(draft));
        data.EnableMargins(false);
        if (wxPageSetupDialog paper(&dialog, &data); paper.ShowModal() == wxID_OK) {
            draft = paper.GetPageSetupDialogData().GetPrintData();
            paperChanged = true;
        }
        preview->Refresh();
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
    struct MarginInput {
        wxSpinCtrlDouble& control;
        int& option;
    };
    auto* const marginOwner = marginBox->GetStaticBox();
    const auto addMargin = [marginOwner, marginFields, units](const wxString& label, const wxString& accessibleName,
                                                              int& option) -> MarginInput {
        constexpr double maximumMargin = std::numeric_limits<int>::max();
        marginFields->Add(new wxStaticText(marginOwner, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
        auto* input = new wxSpinCtrlDouble(marginOwner, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS,
                                           0, maximumMargin / units.unitsPerDisplayUnit,
                                           static_cast<double>(option) / units.unitsPerDisplayUnit,
                                           1.0 / units.unitsPerDisplayUnit);
        input->SetDigits(units.decimalPlaces);
        AppLocalization::SetAccessibleName(*input, accessibleName);
        marginFields->Add(input, 1, wxEXPAND);
        return {*input, option};
    };
    auto& [left, top, right, bottom] = options.margins;
    const std::array marginInputs = {
        addMargin(
            // TRANSLATORS: Left paper margin in IDD_PAGESETUP.
            _("Left:"),
            // TRANSLATORS: Accessible name for the left paper margin input in IDD_PAGESETUP.
            _("Left margin"), left),
        addMargin(
            // TRANSLATORS: Top paper margin in IDD_PAGESETUP.
            _("Top:"),
            // TRANSLATORS: Accessible name for the top paper margin input in IDD_PAGESETUP.
            _("Top margin"), top),
        addMargin(
            // TRANSLATORS: Right paper margin in IDD_PAGESETUP.
            _("Right:"),
            // TRANSLATORS: Accessible name for the right paper margin input in IDD_PAGESETUP.
            _("Right margin"), right),
        addMargin(
            // TRANSLATORS: Bottom paper margin in IDD_PAGESETUP.
            _("Bottom:"),
            // TRANSLATORS: Accessible name for the bottom paper margin input in IDD_PAGESETUP.
            _("Bottom margin"), bottom),
    };
    marginBox->Add(marginFields, 1, wxEXPAND | wxALL, padding);
    settings->Add(marginBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, padding);

    preview->Bind(wxEVT_PAINT, [preview, &draft, marginInputs, units](wxPaintEvent&) {
        wxPaintDC dc(preview);
        dc.SetBackground(wxBrush(preview->GetBackgroundColour()));
        dc.Clear();
        std::array<double, 4> displayMargins{};
        for (size_t index = 0; index < displayMargins.size(); ++index)
            displayMargins[index] = marginInputs[index].control.GetValue();
        const auto bounds = preview->GetClientRect().Deflate(preview->FromDIP(8));
        DrawPrintPagePreview(dc, bounds, draft, displayMargins, units);
    });
    for (const auto& input : marginInputs)
        input.control.Bind(wxEVT_TEXT, [preview](wxCommandEvent& event) {
            preview->Refresh();
            event.Skip();
        });

    auto headerBox = std::make_unique<wxStaticBoxSizer>(wxVERTICAL, &dialog, _("Headers and Footers"));
    auto fields = std::make_unique<wxFlexGridSizer>(2, dialog.FromDIP(5), padding);
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
    headerBox->Add(fields.release(), 0, wxEXPAND | wxALL, padding);
    settings->Add(headerBox.release(), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, padding);
    auto colorBox = std::make_unique<wxStaticBoxSizer>(wxVERTICAL, &dialog, _("Print Colors"));
    auto colorFields = std::make_unique<wxFlexGridSizer>(2, dialog.FromDIP(5), padding);
    colorFields->AddGrowableCol(1);
    auto* const colour =
        AddChoices(dialog, *colorFields,
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
    colorBox->Add(colorFields.release(), 0, wxEXPAND | wxALL, padding);
    settings->Add(colorBox.release(), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, padding);
    const auto zoomLabel = wxString::Format(
        // TRANSLATORS: IDC_PAGESETUP_ZOOMLEVEL label; the numbers are minimum and maximum zoom percentages.
        _("Print zoom (%d%% to %d%%):"), Scintilla::MinZoomLevel, Scintilla::MaxZoomLevel);
    auto zoomBox = std::make_unique<wxStaticBoxSizer>(wxVERTICAL, &dialog, _("Zoom"));
    zoomBox->Add(new wxStaticText(&dialog, wxID_ANY, zoomLabel), 0, wxALL, padding);
    auto* zoom = CreateZoomInput(&dialog, options.zoom);
    zoom->SetName("Print zoom");
    // TRANSLATORS: Screen reader name for the IDC_PAGESETUP_ZOOMLEVEL percentage field.
    AppLocalization::SetAccessibleName(*zoom, _("Print zoom"));
    zoomBox->Add(zoom, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, padding);
    previewColumn->Add(zoomBox.release(), 0, wxEXPAND | wxRIGHT | wxBOTTOM, padding);
    columns->Add(settings.release(), 1, wxEXPAND);
    columns->Add(previewColumn.release(), 0, wxEXPAND);
    content->Add(columns.release(), 0, wxEXPAND);
    content->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, padding);
    DialogLayout::Fit(dialog, std::move(content));
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;

    options.header = static_cast<PrintHeader>(header->GetSelection());
    options.footer = static_cast<PrintFooter>(footer->GetSelection());
    options.colour = static_cast<Scintilla::PrintOption>(colour->GetSelection());
    options.zoom = ReadZoomLevel(*zoom).value_or(PrintOptions{}.zoom);
    for (const auto& [control, option] : marginInputs)
        option = static_cast<int>(std::lround(control.GetValue() * units.unitsPerDisplayUnit));
    if (paperChanged) return draft;
    return std::nullopt;
}
