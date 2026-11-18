#include "app/editor_printing.h"
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/intl.h>
#include <wx/printdlg.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/button.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <wx/msw/printdlg.h>
#else
#include <langinfo.h>
#include <locale.h>
#include <gtk/gtk.h>
#include <wx/gtk/print.h>
#endif

namespace EditorPrinting {
namespace {
struct MarginUnits {
    int unitsPerInch;
    int unitsPerDisplayUnit;
    int defaultMargin;
    unsigned decimalPlaces;
    wxString label;
};

MarginUnits GetMarginUnits() {
    bool metric = true;
#ifdef __WXMSW__
    DWORD system = 0;
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&system),
                    sizeof(system) / sizeof(wchar_t));
    metric = system == 0;
#else
    if (auto locale = newlocale(LC_MEASUREMENT_MASK, "", nullptr)) {
        metric = *nl_langinfo_l(_NL_MEASUREMENT_MEASUREMENT, locale) != 2;
        freelocale(locale);
    }
#endif
    return metric ? MarginUnits{2540, 100, 2000, 2, _("Margins (millimeters)")}
                  : MarginUnits{1000, 1000, 1000, 3, _("Margins (inches)")};
}

void InitMargins(PrintOptions& options, const MarginUnits& units) {
    if (std::ranges::any_of(options.margins, [](int margin) { return margin < 0; }))
        options.margins.fill(units.defaultMargin);
}

} // namespace

int InitializeMargins(PrintOptions& options) {
    const auto units = GetMarginUnits();
    InitMargins(options, units);
    return units.unitsPerInch;
}

wxPrintData CopyPrinterData(const wxPrintData& original) {
    wxPrintData draft;
    draft.SetPrinterName(original.GetPrinterName());
    draft.SetOrientation(original.GetOrientation());
    draft.SetOrientationReversed(original.IsOrientationReversed());
    draft.SetPaperId(original.GetPaperId());
    draft.SetPaperSize(original.GetPaperSize());
    draft.SetBin(original.GetBin());
    draft.SetMedia(original.GetMedia());
    draft.SetQuality(original.GetQuality());
    draft.SetColour(original.GetColour());
    draft.SetDuplex(original.GetDuplex());
    draft.SetNoCopies(original.GetNoCopies());
    draft.SetCollate(original.GetCollate());
#ifdef __WXMSW__
    const auto* from = static_cast<const wxWindowsPrintNativeData*>(original.GetNativeData());
    auto* into = static_cast<wxWindowsPrintNativeData*>(draft.GetNativeData());
    auto clone = [](void* source) -> HGLOBAL {
        if (!source) return nullptr;
        const auto length = GlobalSize(source);
        const auto target = GlobalAlloc(GMEM_MOVEABLE, length);
        if (!target) return nullptr;
        const auto* input = GlobalLock(source);
        auto* output = GlobalLock(target);
        if (input && output) std::memcpy(output, input, length);
        if (input) GlobalUnlock(source);
        if (output) GlobalUnlock(target);
        if (input && output) return target;
        GlobalFree(target);
        return nullptr;
    };
    into->SetDevMode(clone(from->GetDevMode()));
    into->SetDevNames(clone(from->GetDevNames()));
#else
    auto* from = static_cast<wxGtkPrintNativeData*>(original.GetNativeData());
    auto* into = static_cast<wxGtkPrintNativeData*>(draft.GetNativeData());
    auto* config = gtk_print_settings_copy(from->GetPrintConfig());
    into->SetPrintConfig(config);
    g_object_unref(config);
#endif
    return draft;
}

namespace {
wxChoice* AddChoices(wxDialog& dialog, wxSizer& fields, const wxString& label, const wxArrayString& choices,
                     int selected) {
    fields.Add(new wxStaticText(&dialog, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
    auto* choice = new wxChoice(&dialog, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
    choice->SetSelection(selected);
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

void PageSetup(wxWindow& parent, PrintOptions& options, wxPrintData& driver) {
    const auto units = GetMarginUnits();
    InitMargins(options, units);

    wxDialog dialog(&parent, wxID_ANY, _("Page Setup"));
    auto* content = new wxBoxSizer(wxVERTICAL);
    const auto padding = dialog.FromDIP(10);
    auto* paperButton = new wxButton(&dialog, wxID_ANY, _("Paper and &Orientation..."));
    content->Add(paperButton, 0, wxEXPAND | wxALL, padding);
    auto draft = CopyPrinterData(driver);
    bool paperChanged = false;
    paperButton->Bind(wxEVT_BUTTON, [&](wxCommandEvent&) {
        wxPageSetupDialogData data(CopyPrinterData(draft));
        data.EnableMargins(false);
        wxPageSetupDialog paper(&dialog, &data);
        if (paper.ShowModal() == wxID_OK) {
            draft = paper.GetPageSetupDialogData().GetPrintData();
            paperChanged = true;
        }
    });

    auto* marginBox = new wxStaticBoxSizer(wxVERTICAL, &dialog, units.label);
    auto* marginFields = new wxFlexGridSizer(4, dialog.FromDIP(5), padding);
    const std::array marginLabels = {_("Left:"), _("Top:"), _("Right:"), _("Bottom:")};
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
        marginFields->Add(marginInputs[index], 1, wxEXPAND);
    }
    marginBox->Add(marginFields, 1, wxEXPAND | wxALL, padding);
    content->Add(marginBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, padding);

    auto* fields = new wxFlexGridSizer(2, dialog.FromDIP(5), padding);
    fields->AddGrowableCol(1);
    const auto* const header =
        AddChoices(dialog, *fields, _("Header:"),
                   {_("Filename, Current Date and Time"), _("Filename, Current Date"), _("Filename"), _("Leave blank")},
                   std::to_underlying(options.header));
    const auto* const footer = AddChoices(dialog, *fields, _("Footer:"), {_("Page Number"), _("Leave blank")},
                                          std::to_underlying(options.footer));
    auto* const colour =
        AddChoices(dialog, *fields, _("Print color mode:"),
                   {_("Normal"), _("Invert light (dark background)"), _("Black on white"), _("Color on white"),
                    _("Color on white (except line numbers)"), _("Current screen color (WYSIWYG)")},
                   std::to_underlying(options.colour));
    colour->SetName("Print color mode");
    fields->Add(new wxStaticText(&dialog, wxID_ANY,
                                 wxString::Format(_("Print zoom (%d%% to %d%%):"), Scintilla::MinZoomLevel,
                                                  Scintilla::MaxZoomLevel)),
                0, wxALIGN_CENTER_VERTICAL);
    auto* zoom = new wxComboBox(&dialog, wxID_ANY, wxString::Format("%d%%", options.zoom));
    zoom->SetName("Print zoom");
    zoom->SetMaxLength(8);
    for (int level : {500, 450, 400, 350, 300, 250, 200, 175, 150, 125, 100, 75, 50, 25})
        zoom->Append(wxString::Format("%d%%", level));
    fields->Add(zoom, 1, wxEXPAND);
    content->Add(fields, 0, wxEXPAND | wxALL, padding);
    content->Add(dialog.CreateSeparatedButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, padding);
    dialog.SetSizerAndFit(content);
    dialog.CentreOnParent();
    if (dialog.ShowModal() != wxID_OK) return;

    options.header = static_cast<PrintHeader>(header->GetSelection());
    options.footer = static_cast<PrintFooter>(footer->GetSelection());
    options.colour = static_cast<Scintilla::PrintOption>(colour->GetSelection());
    options.zoom = ParseZoom(*zoom);
    for (size_t index = 0; index < options.margins.size(); ++index)
        options.margins[index] =
            static_cast<int>(std::lround(marginInputs[index]->GetValue() * units.unitsPerDisplayUnit));
    if (paperChanged) driver = draft;
}
} // namespace EditorPrinting
