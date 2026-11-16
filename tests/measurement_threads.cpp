#include "editor/platform/core_headers.h"
#include "measurement_workers.h"
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <array>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace core = Scintilla::Internal;
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

struct FontCase {
    std::shared_ptr<core::Font> font;
    std::vector<Measurement> expected;
};

struct SurfaceCase {
    int context;
    bool utf8;
    std::vector<std::string_view> samples;
    std::vector<FontCase> fonts;
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

void Equal(const Measurement& actual, const Measurement& expected, const SurfaceCase& test, int worker, size_t font,
           size_t sample, int iteration) {
    if (actual.positions != expected.positions || actual.width != expected.width) {
        size_t byte = 0;
        while (byte < actual.positions.size() && byte < expected.positions.size() &&
               actual.positions[byte] == expected.positions[byte])
            ++byte;
        const bool positionMismatch = byte < actual.positions.size() || byte < expected.positions.size();
        std::fprintf(stderr,
                     "context=%d utf8=%d worker=%d font=%zu sample=%zu iteration=%d actual-width=%.17g "
                     "expected-width=%.17g first-byte=%td actual-position=%.17g expected-position=%.17g "
                     "actual-bytes=%zu expected-bytes=%zu\n",
                     test.context, test.utf8, worker, font, sample, iteration, actual.width, expected.width,
                     positionMismatch ? static_cast<std::ptrdiff_t>(byte) : -1,
                     byte < actual.positions.size() ? actual.positions[byte] : -1,
                     byte < expected.positions.size() ? expected.positions[byte] : -1, actual.positions.size(),
                     expected.positions.size());
    }
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

std::unique_ptr<wxDC> DrawingContext(wxFrame& frame, int context, wxBitmap& bitmap) {
    if (context) {
        bitmap.CreateWithLogicalSize(BitmapWidth, BitmapHeight, context);
        return std::make_unique<wxMemoryDC>(bitmap);
    }
    return std::make_unique<wxClientDC>(&frame);
}

void PrintMeasurement(const SurfaceCase& test, size_t font, size_t sample, const Measurement& measured) {
    std::printf("context=%d utf8=%d font=%zu sample=%zu width=%g positions=", test.context, test.utf8, font, sample,
                measured.width);
    for (const auto position : measured.positions)
        std::printf("%g,", position);
    std::putchar('\n');
}

void BuildFontReference(wxFrame& frame, core::Surface& surface, SurfaceCase& test, int size, int style,
                        bool serialOnly) {
    const bool bold = style == 1;
    const bool italic = style == 2;
    const auto weight = bold ? Scintilla::FontWeight::Bold : Scintilla::FontWeight::Normal;
    test.fonts.push_back({core::Font::Allocate(core::FontParameters(core::Platform::DefaultFont(), size, weight,
                                                                    Scintilla::FontStretch::Normal, italic)),
                          {}});
    auto& font = test.fonts.back();
    // Each font's independent reference DC is used only on the GUI thread.
    wxBitmap referenceBitmap;
    auto reference = DrawingContext(frame, test.context, referenceBitmap);
    reference->SetFont(wxFont(wxFontInfo(size)
                                  .FaceName(core::Platform::DefaultFont())
                                  .Weight(bold ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL)
                                  .Italic(italic)));
    for (size_t sample = 0; sample < test.samples.size(); ++sample) {
        const auto measured = Measure(surface, *font.font, test.samples[sample], false);
        NativeReference(*reference, test.samples[sample], test.utf8, sample, measured);
        if (test.utf8)
            Equal(Measure(surface, *font.font, test.samples[sample], true), measured, test, -1, test.fonts.size() - 1,
                  sample, -1);
        font.expected.push_back(measured);
        if (serialOnly) PrintMeasurement(test, test.fonts.size() - 1, sample, measured);
    }
}

void CheckWorker(core::Surface& surface, const SurfaceCase& test, int worker) {
    for (int iteration = 0; iteration < Iterations; ++iteration) {
        const auto font = (static_cast<size_t>(worker) * 3 + iteration) % test.fonts.size();
        const auto& reference = test.fonts[font];
        for (size_t sample = 0; sample < test.samples.size(); ++sample)
            Equal(Measure(surface, *reference.font, test.samples[sample], test.utf8 && (iteration + worker) % 2),
                  reference.expected[sample], test, worker, font, sample, iteration);
    }
}

void CheckWorkers(core::Surface& surface, const SurfaceCase& test) {
    MeasurementWorkers::Run([&surface, &test](int worker) { CheckWorker(surface, test, worker); });
}

void CheckSurface(wxFrame& frame, int context, bool utf8, bool serialOnly) {
    wxBitmap bitmap;
    auto dc = DrawingContext(frame, context, bitmap);
    auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
    surface->Init(dc.get(), &frame);
    surface->SetMode(core::SurfaceMode{utf8 ? Scintilla::CpUtf8 : 0});
    SurfaceCase test{context,
                     utf8,
                     utf8 ? std::vector(Utf8Samples.begin(), Utf8Samples.end())
                          : std::vector(Latin1Samples.begin(), Latin1Samples.end()),
                     {}};
    for (const auto size : PointSizes)
        for (int style = 0; style < 3; ++style)
            BuildFontReference(frame, *surface, test, size, style, serialOnly);
    if (!serialOnly) CheckWorkers(*surface, test);
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
