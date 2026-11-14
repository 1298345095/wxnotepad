#include "editor/platform/core_headers.h"
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <array>
#include <barrier>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
namespace core = Scintilla::Internal;
constexpr int WorkerCount = 4;
constexpr int Iterations = 64;
constexpr int BitmapWidth = 640;
constexpr int BitmapHeight = 120;
constexpr std::array PointSizes{8, 12, 22};
constexpr std::array Utf8Samples{std::string_view("Wim 0123456789"), std::string_view("A中😀Z"),
                                 std::string_view("Café"),           std::string_view("👩‍💻"),
                                 std::string_view("AΩ中😀א"),        std::string_view("office ffi"),
                                 std::string_view("a\tb"),           std::string_view("a\0b", 3)};
constexpr std::array Latin1Samples{std::string_view("Wim 0123456789"), std::string_view("A\xc0\xe9\xffZ"),
                                   std::string_view("a\tb"), std::string_view("a\0b", 3)};

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Measurement {
    std::vector<double> positions;
    double width;
};

Measurement Measure(core::Surface& surface, const core::Font& font, std::string_view text, bool explicitUtf8) {
    Measurement result{std::vector<double>(text.size(), -1), 0};
    if (explicitUtf8)
        surface.MeasureWidthsUTF8(&font, text, result.positions.data());
    else
        surface.MeasureWidths(&font, text, result.positions.data());
    result.width = surface.WidthText(&font, text);
    return result;
}

void Equal(const Measurement& actual, const Measurement& expected) {
    Require(actual.positions == expected.positions && actual.width == expected.width,
            "concurrent measurement must preserve every serial byte position and text width");
}

void NativeReference(wxDC& dc, std::string_view text, bool utf8, size_t sample, const Measurement& actual) {
    const auto characters =
        utf8 ? wxString::FromUTF8(text.data(), text.size()) : wxString::From8BitData(text.data(), text.size());
    const auto expectedWidth = dc.GetTextExtent(characters).x;
    if (actual.width != expectedWidth)
        std::fprintf(stderr, "sample=%zu utf8=%d native-width=%d measured-width=%g\n", sample, utf8, expectedWidth,
                     actual.width);
    Require(actual.width == expectedWidth, "text width must match the native drawing DC");
    // These independent scalar cases have known byte-to-native-unit offsets.
    // Complex shaping retains the pre-change serial numeric oracle instead.
    if (utf8 && sample > 1) return;
    wxArrayInt extents;
    dc.GetPartialTextExtents(characters, extents);
    if (utf8 && sample == 1) {
        const size_t emojiEnd = sizeof(wchar_t) == 2 ? 3 : 2;
        const std::array<size_t, 9> offsets{0, 1, 1, 1, emojiEnd, emojiEnd, emojiEnd, emojiEnd, emojiEnd + 1};
        Require(actual.positions.size() == offsets.size(), "the scalar fixture has known UTF-8 byte offsets");
        for (size_t byte = 0; byte < offsets.size(); ++byte)
            Require(offsets[byte] < extents.size() && actual.positions[byte] == extents[offsets[byte]],
                    "UTF-8 byte positions must match native scalar extents");
    } else {
        Require(actual.positions.size() == extents.size(), "single-byte native extents cover every input byte");
        for (size_t byte = 0; byte < text.size(); ++byte)
            Require(actual.positions[byte] == extents[byte], "single-byte positions must match native drawing");
    }
}

void CheckSurface(wxFrame& frame, int context, bool utf8, bool serialOnly) {
    const double scale = context;
    wxBitmap bitmap;
    std::unique_ptr<wxDC> dc;
    if (context) {
        bitmap.CreateWithLogicalSize(BitmapWidth, BitmapHeight, scale);
        dc = std::make_unique<wxMemoryDC>(bitmap);
    } else {
        dc = std::make_unique<wxClientDC>(&frame);
    }
    auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
    surface->Init(dc.get(), &frame);
    surface->SetMode(core::SurfaceMode{utf8 ? Scintilla::CpUtf8 : 0});
    std::vector<std::shared_ptr<core::Font>> fonts;
    std::vector<std::vector<Measurement>> expected;
    const auto samples = utf8 ? std::vector(Utf8Samples.begin(), Utf8Samples.end())
                              : std::vector(Latin1Samples.begin(), Latin1Samples.end());
    for (const auto size : PointSizes) {
        for (int style = 0; style < 3; ++style) {
            const bool bold = style == 1;
            const bool italic = style == 2;
            const auto weight = bold ? Scintilla::FontWeight::Bold : Scintilla::FontWeight::Normal;
            fonts.push_back(core::Font::Allocate(core::FontParameters(core::Platform::DefaultFont(), size, weight,
                                                                      Scintilla::FontStretch::Normal, italic)));
            // Each font's independent reference DC is used only on the GUI thread.
            wxBitmap referenceBitmap;
            std::unique_ptr<wxDC> reference;
            if (context) {
                referenceBitmap.CreateWithLogicalSize(BitmapWidth, BitmapHeight, scale);
                reference = std::make_unique<wxMemoryDC>(referenceBitmap);
            } else {
                reference = std::make_unique<wxClientDC>(&frame);
            }
            reference->SetFont(wxFont(wxFontInfo(size)
                                          .FaceName(core::Platform::DefaultFont())
                                          .Weight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL)
                                          .Italic(italic)));
            expected.emplace_back();
            for (size_t sample = 0; sample < samples.size(); ++sample) {
                const auto measured = Measure(*surface, *fonts.back(), samples[sample], false);
                NativeReference(*reference, samples[sample], utf8, sample, measured);
                if (utf8) Equal(Measure(*surface, *fonts.back(), samples[sample], true), measured);
                expected.back().push_back(measured);
                if (serialOnly) {
                    std::printf("context=%d utf8=%d font=%zu sample=%zu width=%g positions=", context, utf8,
                                fonts.size() - 1, sample, measured.width);
                    for (const auto position : measured.positions)
                        std::printf("%g,", position);
                    std::putchar('\n');
                }
            }
        }
    }
    if (serialOnly) return;
    std::array<std::exception_ptr, WorkerCount> failures{};
    std::barrier start(WorkerCount);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < WorkerCount; ++worker) {
        workers.emplace_back([&, worker] {
            start.arrive_and_wait();
            try {
                for (int iteration = 0; iteration < Iterations; ++iteration) {
                    const auto font = (static_cast<size_t>(worker) * 3 + iteration) % fonts.size();
                    for (size_t sample = 0; sample < samples.size(); ++sample)
                        Equal(Measure(*surface, *fonts[font], samples[sample], utf8 && (iteration + worker) % 2),
                              expected[font][sample]);
                }
            } catch (...) {
                failures[worker] = std::current_exception();
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    for (const auto& failure : failures)
        if (failure) std::rethrow_exception(failure);
}

class MeasurementApp final : public wxApp {
public:
    bool OnInit() override {
        bool serialOnly = false;
        for (int argument = 1; argument < argc; ++argument)
            if (wxString(argv[argument]) == "--serial") serialOnly = true;
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad parallel measurement regression");
        frame->Show();
        CallAfter([this, frame, serialOnly] {
            try {
                for (int context = 0; context <= 2; ++context)
                    for (const bool utf8 : {true, false})
                        CheckSurface(*frame, context, utf8, serialOnly);
                if (!serialOnly)
                    std::puts("PASS: concurrent native text metrics preserve fonts, DPI, Unicode and byte positions");
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
} // namespace
wxIMPLEMENT_APP(MeasurementApp);
