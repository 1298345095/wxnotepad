#include "editor/platform/core_headers.h"
#include <wx/wx.h>
#include <wx/dcmemory.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
namespace core = Scintilla::Internal;
constexpr int SourceWidth = 8;
constexpr int SourceHeight = 6;
constexpr int CanvasSize = 24;
constexpr int OriginX = 2;
constexpr int OriginY = 3;
constexpr size_t RgbaChannels = 4;
using ImageData = std::array<unsigned char, static_cast<size_t>(SourceWidth) * SourceHeight * RgbaChannels>;

class ImageFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

wxImage Render(wxWindow& window, int scale, wxSize size, const ImageData& data) {
    wxBitmap target;
#ifdef __WXMSW__
    // MSW editor coordinates are physical pixels. Use a transformed native DC
    // to check both 1:1 drawing and a doubled destination raster.
    target.Create(CanvasSize * scale, CanvasSize * scale, 24);
#else
    target.CreateWithLogicalSize(CanvasSize, CanvasSize, scale);
#endif
    {
        wxMemoryDC dc(target);
#ifdef __WXMSW__
        dc.SetUserScale(scale, scale);
#endif
        dc.SetBackground(*wxWHITE_BRUSH);
        dc.Clear();
        auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
        surface->Init(&dc, &window);
        surface->DrawRGBAImage(core::PRectangle(OriginX, OriginY, OriginX + size.x, OriginY + size.y), SourceWidth,
                               SourceHeight, data.data());
    }
    return target.ConvertToImage();
}

void DestinationBounds(wxWindow& window, int scale, wxSize size) {
    ImageData data{};
    for (size_t pixel = 0; pixel < data.size() / RgbaChannels; ++pixel) {
        data[pixel * RgbaChannels] = 255;
        data[pixel * RgbaChannels + 3] = 255;
    }
    const auto image = Render(window, scale, size, data);
    const wxRect bounds(OriginX * scale, OriginY * scale, size.x * scale, size.y * scale);
    size_t different = 0;
    for (int y = 0; y < image.GetHeight(); ++y)
        for (int x = 0; x < image.GetWidth(); ++x) {
            const unsigned char expected = bounds.Contains(x, y) ? 0 : 255;
            if (image.GetRed(x, y) != 255 || image.GetGreen(x, y) != expected || image.GetBlue(x, y) != expected)
                ++different;
        }
    std::cout << "RGBA scale=" << scale << " destination=" << size.x << 'x' << size.y
              << " different_pixels=" << different << '\n';
    if (different) throw ImageFailure("RGBA images must fill exactly the destination rectangle");
}

unsigned char AlphaForRow(int row) {
    // Opaque, transparent, and half-transparent pairs of scanlines.
    if (row < 2) return 255;
    if (row < 4) return 0;
    return 128;
}

ImageData AlternatingPixels() {
    ImageData data{};
    for (int y = 0; y < SourceHeight; ++y)
        for (int x = 0; x < SourceWidth; ++x) {
            const size_t pixel = (y * SourceWidth + x) * RgbaChannels;
            data[pixel + (x % 2 == 0 ? 0 : 2)] = 255;
            data[pixel + 3] = AlphaForRow(y);
        }
    return data;
}

void NativeDetailAndAlpha(wxWindow& window, int scale) {
    const auto data = AlternatingPixels();
    const auto image = Render(window, scale, wxSize(SourceWidth / scale, SourceHeight / scale), data);
    for (int y = 0; y < SourceHeight; ++y)
        for (int x = 0; x < SourceWidth; ++x) {
            const int background = 255 - AlphaForRow(y);
            const int px = OriginX * scale + x;
            const int py = OriginY * scale + y;
            if (std::abs(image.GetRed(px, py) - (x % 2 == 0 ? 255 : background)) > 1 ||
                std::abs(image.GetGreen(px, py) - background) > 1 ||
                std::abs(image.GetBlue(px, py) - (x % 2 == 0 ? background : 255)) > 1)
                throw ImageFailure("RGBA images must preserve native pixel detail and alpha composition");
        }
    std::cout << "PASS: RGBA scale=" << scale << " retains alternating source pixels and alpha\n";
}
} // namespace

class ImageApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad surface image regression");
        frame->Show();
        CallAfter([this, frame] {
            try {
                for (const int scale : {1, 2}) {
                    for (const wxSize size : {wxSize(8, 6), wxSize(4, 3), wxSize(4, 6)})
                        DestinationBounds(*frame, scale, size);
                    NativeDetailAndAlpha(*frame, scale);
                }
                result_ = 0;
            } catch (const ImageFailure& error) {
                std::cerr << "FAIL: " << error.what() << '\n';
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

wxIMPLEMENT_APP(ImageApp);
