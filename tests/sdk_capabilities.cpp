#include <wx/wx.h>
#include <wx/bmpbndl.h>
#include <wx/imagpng.h>
#include <wx/listctrl.h>
#include <wx/mstream.h>
#include <wx/notebook.h>
#include <wx/print.h>
#include <wx/printdlg.h>
#include <wx/spinctrl.h>
#include <wx/treectrl.h>
#include <wx/zstream.h>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string_view>

static_assert(wxUSE_PRINTING_ARCHITECTURE && wxUSE_ZLIB && wxUSE_LIBPNG && wxUSE_NANOSVG);
static_assert(wxUSE_LISTCTRL && wxUSE_TREECTRL && wxUSE_NOTEBOOK && wxUSE_SPINCTRL);
#if WXNOTEPAD_VERIFY_TRIMMED_SDK
static_assert(!wxUSE_STC, "The application uses the separately pinned original core");
#ifdef __WXMSW__
static_assert(!wxUSE_GRID && !wxUSE_DATAVIEWCTRL && !wxUSE_TREELISTCTRL);
static_assert(!wxUSE_DOC_VIEW_ARCHITECTURE && !wxUSE_MDI && !wxUSE_MDI_ARCHITECTURE);
static_assert(!wxUSE_REGEX && !wxUSE_LIBJPEG && !wxUSE_LIBTIFF && !wxUSE_LIBWEBP);
#endif
#endif
#ifdef __WXMSW__
static_assert(wxUSE_ACCESSIBILITY);
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class ProbePrintout final : public wxPrintout {
public:
    ProbePrintout() : wxPrintout("Retained printing capability") {}
    bool OnPrintPage(int) override { return true; }
};
void CheckCapabilities() {
    wxFrame frame(nullptr, wxID_ANY, "Hidden SDK capability probe");
    auto* grid = new wxGridSizer(2);
    const std::array<wxWindow*, 4> controls{new wxListCtrl(&frame), new wxTreeCtrl(&frame),
                                            new wxNotebook(&frame, wxID_ANY), new wxSpinCtrl(&frame)};
    for (auto* control : controls) {
        Require(control->GetHandle() != nullptr, "retained controls must create their native windows");
        grid->Add(control);
    }
    frame.SetSizer(grid);
    frame.Layout();
    wxPrintData printData;
    printData.SetPaperId(wxPAPER_A4);
    wxPageSetupDialogData pageSetup(printData);
    constexpr int printMarginMillimetres = 20;
    pageSetup.SetMarginTopLeft(wxPoint(printMarginMillimetres, printMarginMillimetres));
    wxPrintDialogData dialogData(printData);
    wxPrinter printer(&dialogData);
    ProbePrintout printout;
    Require(pageSetup.GetPrintData().GetPaperId() == wxPAPER_A4 &&
                pageSetup.GetMarginTopLeft() == wxPoint(printMarginMillimetres, printMarginMillimetres) &&
                printout.GetTitle() == "Retained printing capability",
            "printing and page setup objects must retain their data without sending a print job");

    constexpr std::string_view payload = "retained SDK zlib capability";
    wxMemoryOutputStream compressed;
    {
        wxZlibOutputStream stream(compressed);
        stream.Write(payload.data(), payload.size());
    }
    wxMemoryInputStream encoded(compressed);
    wxZlibInputStream restored(encoded);
    std::array<char, payload.size()> bytes{};
    restored.Read(bytes.data(), bytes.size());
    Require(restored.LastRead() == bytes.size() && std::string_view(bytes.data(), bytes.size()) == payload,
            "zlib must round-trip actual bytes");

    wxPNGHandler pngHandler;
    wxImage image(2, 2);
    image.SetRGB(wxRect(0, 0, 2, 2), 31, 63, 127);
    wxMemoryOutputStream png;
    Require(pngHandler.SaveFile(&image, png), "PNG encoding must remain available");
    wxMemoryInputStream pngInput(png);
    wxImage decoded;
    Require(pngHandler.LoadFile(&decoded, pngInput) && decoded.IsOk() && decoded.GetSize() == image.GetSize() &&
                decoded.GetRed(0, 0) == 31,
            "PNG decoding must retain pixels");
    constexpr int iconPixels = 8;
    const auto svg = wxBitmapBundle::FromSVG("<svg xmlns='http://www.w3.org/2000/svg' width='8' height='8'>"
                                             "<rect width='8' height='8' fill='red'/></svg>",
                                             wxSize(iconPixels, iconPixels));
    Require(svg.IsOk() && svg.GetBitmap(wxSize(iconPixels, iconPixels)).IsOk(), "SVG art must still rasterize");
}
class ProbeApp final : public wxApp {
public:
    bool OnInit() override { return true; }
    int OnRun() override {
        wxLogStderr log;
        auto* previousLog = wxLog::SetActiveTarget(&log);
        int result = 0;
        try {
            CheckCapabilities();
            std::puts("PASS: retained SDK capabilities (native controls, printing, zlib, PNG and SVG)");
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: %s\n", error.what());
            result = 1;
        }
        wxLog::SetActiveTarget(previousLog);
        return result;
    }
};
} // namespace
wxIMPLEMENT_APP(ProbeApp);
