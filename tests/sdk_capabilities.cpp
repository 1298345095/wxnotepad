#include <wx/wx.h>
#include <wx/bmpbndl.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/iconbndl.h>
#include <wx/imagpng.h>
#include <wx/listctrl.h>
#include <wx/mstream.h>
#include <wx/notebook.h>
#include <wx/print.h>
#include <wx/printdlg.h>
#include <wx/scopeguard.h>
#include <wx/spinctrl.h>
#include <wx/treectrl.h>
#include <wx/zstream.h>
#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
#include <print>
#include <stdexcept>
#include <string_view>

static_assert(wxUSE_PRINTING_ARCHITECTURE && wxUSE_ZLIB && wxUSE_LIBPNG && wxUSE_NANOSVG);
static_assert(wxUSE_IMAGE && wxUSE_ICO_CUR && wxUSE_XPM);
static_assert(wxUSE_CLIPBOARD && wxUSE_DRAG_AND_DROP && wxUSE_FSWATCHER);
static_assert(wxUSE_LISTCTRL && wxUSE_TREECTRL && wxUSE_NOTEBOOK && wxUSE_SPINCTRL);
#if WXNOTEPAD_VERIFY_TRIMMED_SDK
static_assert(!wxUSE_STC, "The application uses the separately pinned original core");
#ifdef __WXMSW__
static_assert(!wxUSE_GRID && !wxUSE_DATAVIEWCTRL && !wxUSE_TREELISTCTRL);
static_assert(!wxUSE_DOC_VIEW_ARCHITECTURE && !wxUSE_MDI && !wxUSE_MDI_ARCHITECTURE);
static_assert(!wxUSE_REGEX && !wxUSE_LIBJPEG && !wxUSE_LIBTIFF && !wxUSE_LIBWEBP);
static_assert(!wxUSE_GIF && !wxUSE_PCX && !wxUSE_TGA && !wxUSE_IFF && !wxUSE_PNM);
static_assert(!wxUSE_ANIMATIONCTRL && !wxUSE_CALENDARCTRL && !wxUSE_DATEPICKCTRL && !wxUSE_TIMEPICKCTRL);
static_assert(!wxUSE_FINDREPLDLG && !wxUSE_WIZARDDLG && !wxUSE_SPLASH);
static_assert(!wxUSE_ACTIVEX && !wxUSE_JOYSTICK && !wxUSE_SEARCHCTRL && !wxUSE_RICHTOOLTIP);
static_assert(!wxUSE_SVG && !wxUSE_POSTSCRIPT && !wxUSE_AFM_FOR_POSTSCRIPT && !wxUSE_POSTSCRIPT_ARCHITECTURE_IN_MSW);
static_assert(!wxUSE_ZIPSTREAM && !wxUSE_TARSTREAM && !wxUSE_ARCHIVE_STREAMS && !wxUSE_FS_ARCHIVE && !wxUSE_FS_ZIP);
#endif
#endif
#ifdef __WXMSW__
static_assert(wxUSE_ACCESSIBILITY);
#endif

namespace {
class CapabilityFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
void Require(bool condition, const char* message) {
    if (!condition) throw CapabilityFailure(message);
}
class ProbePrintout final : public wxPrintout {
public:
    ProbePrintout() : wxPrintout("Retained printing capability") {}
    bool OnPrintPage(int) override { return true; }
};
void CheckCapabilities() {
    wxFFile output;
    const auto printPath = wxFileName::CreateTempFileName("wxnotepad-standard-print-", &output);
    Require(output.IsOpened(), "standard print probe must create its output in the user temporary directory");
    wxScopeGuard removePrintFile = wxMakeGuard([&] {
        output.Close();
        wxRemoveFile(printPath);
    });
    std::print(output.fp(), "{} {}", "print", 42);
    std::println(output.fp(), " {}", "ok");
    std::println(output.fp(), "{:04x}", 42);
    std::println(output.fp(), "{}", "中文");
    Require(std::fflush(output.fp()) == 0 && std::fseek(output.fp(), 0, SEEK_SET) == 0,
            "standard print output must be readable");
    constexpr std::string_view expectedPrint = "print 42 ok\n002a\n中文\n";
    std::array<char, expectedPrint.size() + 1> printed{};
    const auto printedSize = std::fread(printed.data(), 1, printed.size(), output.fp());
    Require(std::string_view(printed.data(), printedSize) == expectedPrint,
            "std::print and std::println must retain formatted UTF-8 output bytes");

    wxFrame frame(nullptr, wxID_ANY, "Hidden SDK capability probe");
    auto grid = std::make_unique<wxGridSizer>(2);
    wxListCtrl list(&frame);
    wxTreeCtrl tree(&frame);
    wxNotebook notebook(&frame, wxID_ANY);
    wxSpinCtrl spin(&frame);
    const std::array<wxWindow*, 4> controls{&list, &tree, &notebook, &spin};
    for (auto* control : controls) {
        Require(control->GetHandle() != nullptr, "retained controls must create their native windows");
        grid->Add(control);
    }
    frame.SetSizer(grid.release());
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
    constexpr std::array xpmData{"2 2 2 1", "r c #ff0000", "g c #00ff00", "rg", "gr"};
    const wxImage xpm(xpmData.data());
    Require(xpm.IsOk() && xpm.GetSize() == wxSize(2, 2) && xpm.GetRed(0, 0) == 255 && xpm.GetGreen(1, 0) == 255,
            "XPM artwork must retain its pixels");
#ifdef __WXMSW__
    const wxIconBundle icons("APP_ICON", nullptr);
    Require(!icons.IsEmpty() && icons.GetIcon().IsOk(), "the application icon resource must load through the SDK");
#endif
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
            std::puts("PASS: retained SDK capabilities (native controls, printing, zlib, PNG/SVG/XPM and icons)");
        } catch (const std::exception& error) {
            std::cerr << "FAIL: " << error.what() << '\n';
            result = 1;
        }
        wxLog::SetActiveTarget(previousLog);
        return result;
    }
};
} // namespace
wxIMPLEMENT_APP(ProbeApp);
