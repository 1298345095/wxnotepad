#include "app/editor_printing.h"
#include "app/dialogs/print_page_setup_dialog.h"
#include <algorithm>
#include <cstring>
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
    PrintMarginDisplay display;
    int unitsPerInch;
    int defaultMargin;
};

MarginUnits GetMarginUnits() {
    bool metric = true;
#ifdef __WXMSW__
    constexpr DWORD metricMeasurementSystem = 0;
    DWORD system = 0;
    GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IMEASURE | LOCALE_RETURN_NUMBER, reinterpret_cast<LPWSTR>(&system),
                    sizeof(system) / sizeof(wchar_t));
    metric = system == metricMeasurementSystem;
#else
    constexpr char usMeasurementSystem = 2;
    if (auto locale = newlocale(LC_MEASUREMENT_MASK, "", nullptr)) {
        metric = *nl_langinfo_l(_NL_MEASUREMENT_MEASUREMENT, locale) != usMeasurementSystem;
        freelocale(locale);
    }
#endif
    constexpr int hundredthsPerMillimeter = 100;
    constexpr int hundredthsOfMillimeterPerInch = 2540;
    constexpr int thousandthsPerInch = 1000;
    constexpr int defaultMarginMillimeters = 20;
    constexpr int defaultMarginInches = 1;
    return metric ? MarginUnits{{hundredthsPerMillimeter, 2, true},
                                hundredthsOfMillimeterPerInch,
                                defaultMarginMillimeters * hundredthsPerMillimeter}
                  : MarginUnits{
                        {thousandthsPerInch, 3, false}, thousandthsPerInch, defaultMarginInches * thousandthsPerInch};
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

void PageSetup(wxWindow& parent, PrintOptions& options, wxPrintData& driver) {
    const auto units = GetMarginUnits();
    InitMargins(options, units);
    if (const auto draft = ShowPrintPageSetupDialog(parent, options, driver, units.display)) driver = *draft;
}
} // namespace EditorPrinting
