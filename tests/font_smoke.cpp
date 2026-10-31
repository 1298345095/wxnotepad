#include "platform/core_headers.h"
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <cstdio>

namespace core = Scintilla::Internal;

class FontApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad font DPI regression");
        frame->Show();
        CallAfter([this, frame] {
            try {
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
