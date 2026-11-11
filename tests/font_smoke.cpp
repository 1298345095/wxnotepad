#include "editor/platform/core_headers.h"
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace core = Scintilla::Internal;

#ifdef __WXGTK__
namespace {
void BufferedTextRaster(wxWindow& window, const core::Font& font, double scale) {
    constexpr int Width = 400;
    constexpr int Height = 80;
    constexpr int Baseline = 30;
    constexpr size_t Channels = 3;
    const core::PRectangle rectangle(0, 0, Width, Height);
    constexpr std::string_view Text = "Hg 0123456789 | ASCII | 中文";
    wxBitmap directBitmap, copiedBitmap;
    directBitmap.CreateWithLogicalSize(Width, Height, scale);
    copiedBitmap.CreateWithLogicalSize(Width, Height, scale);
    {
        wxMemoryDC directDC(directBitmap), copiedDC(copiedBitmap);
        auto direct = core::Surface::Allocate(Scintilla::Technology::Default);
        auto copied = core::Surface::Allocate(Scintilla::Technology::Default);
        direct->Init(&directDC, &window);
        copied->Init(&copiedDC, &window);
        auto buffered = direct->AllocatePixMap(Width, Height);
        direct->FillRectangle(rectangle, core::Fill(core::ColourRGBA(255, 255, 255)));
        buffered->FillRectangle(rectangle, core::Fill(core::ColourRGBA(255, 255, 255)));
        direct->DrawTextTransparent(rectangle, &font, Baseline, Text, core::ColourRGBA(0, 0, 0));
        buffered->DrawTextTransparent(rectangle, &font, Baseline, Text, core::ColourRGBA(0, 0, 0));
        copied->Copy(rectangle, core::Point(0, 0), *buffered);
    }
    const auto direct = directBitmap.ConvertToImage();
    const auto copied = copiedBitmap.ConvertToImage();
    const auto pixels = static_cast<size_t>(direct.GetWidth()) * direct.GetHeight();
    size_t ink = 0, different = 0;
    for (size_t pixel = 0; pixel < pixels; ++pixel) {
        const auto* expected = direct.GetData() + pixel * Channels;
        const auto* actual = copied.GetData() + pixel * Channels;
        ink += expected[0] != 255 || expected[1] != 255 || expected[2] != 255;
        different += expected[0] != actual[0] || expected[1] != actual[1] || expected[2] != actual[2];
    }
    std::cout << "scale=" << scale << " ink_pixels=" << ink << " different_pixels=" << different << '\n';
    if (!ink || different)
        throw std::runtime_error("Buffered editor text must retain the native raster resolution at the target scale");
}
} // namespace
#endif

class FontApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad font DPI regression");
        frame->Show();
        CallAfter([this, frame] {
            try {
                std::cout << "window_content_scale=" << frame->GetContentScaleFactor() << '\n';
                wxClientDC reference(frame);
                reference.SetFont(wxFont(wxFontInfo(12).FaceName(core::Platform::DefaultFont())));
                const auto expected = reference.GetTextExtent("Hg 0123456789");
                auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
                surface->Init(frame);
                auto pixmap = surface->AllocatePixMap(400, 100);
                const auto font = core::Font::Allocate(core::FontParameters(core::Platform::DefaultFont(), 12));
                const auto direct = surface->WidthText(font.get(), "Hg 0123456789");
                const auto buffered = pixmap->WidthText(font.get(), "Hg 0123456789");
                const auto directHeight = surface->Metrics(font.get()).height;
                const auto bufferedHeight = pixmap->Metrics(font.get()).height;
                std::printf(
                    "dpi=%d reference_width=%d direct_width=%g buffered_width=%g direct_height=%g buffered_height=%g\n",
                    frame->GetDPI().y, expected.x, direct, buffered, directHeight, bufferedHeight);
                if (direct != expected.x || buffered != expected.x || directHeight != bufferedHeight ||
                    surface->LogPixelsY() != pixmap->LogPixelsY())
                    throw std::runtime_error(
                        "Buffered and direct editor text must use the same native point size at the window DPI");
                std::puts("PASS: buffered and direct font metrics match native window DPI");
#ifdef __WXGTK__
                BufferedTextRaster(*frame, *font, 1.0);
                BufferedTextRaster(*frame, *font, 2.0);
                std::puts("PASS: buffered text pixels match direct native rendering at 100% and 200%");
#endif
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};

wxIMPLEMENT_APP(FontApp);
