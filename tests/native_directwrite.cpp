// Compare the production wx adapter with the pinned original DirectWrite backend.
#include "editor/platform/directwrite_surface.h"
#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include <wx/wx.h>
#include <wx/dcmemory.h>
#include "PlatWin.h"
#include "SurfaceD2D.h"
#include <array>
#include <barrier>
#include <filesystem>
#include <thread>

#ifdef WXNOTEPAD_TEST_CLEARTYPE_CONTRAST
BOOL WINAPI ControlledSystemParametersInfoW(UINT action, UINT parameter, PVOID value, UINT flags);
void SetControlledFontSmoothingContrast(UINT value);
ID2D1RenderTarget* ObservedDirectWriteTarget();
#define SystemParametersInfoW ControlledSystemParametersInfoW
#endif

namespace {
namespace core = Scintilla::Internal;
constexpr int Workers = 4;
constexpr int Iterations = 64;
constexpr std::array PointSizes{8, 12, 22};
constexpr std::array Dpis{96, 144, 192};
enum class FontStyle : std::uint8_t { Normal, Bold, Italic };
constexpr std::array FontStyles{FontStyle::Normal, FontStyle::Bold, FontStyle::Italic};
enum class RasterCase : std::uint8_t { OriginalDirect, AdaptedDirect, OriginalBuffered, AdaptedBuffered };
constexpr std::array RasterCases{RasterCase::OriginalDirect, RasterCase::AdaptedDirect, RasterCase::OriginalBuffered,
                                 RasterCase::AdaptedBuffered};
constexpr std::array Utf8Samples{std::string_view("Wim 0123456789"), std::string_view("A中😀Z"),
                                 std::string_view("Café"),           std::string_view("👩‍💻"),
                                 std::string_view("AΩ中😀א"),        std::string_view("office ffi"),
                                 std::string_view("a\tb"),           std::string_view("a\0b", 3)};
constexpr std::array ByteSamples{std::string_view("Wim 0123456789"), std::string_view("A\xc0\xe9\xffZ"),
                                 std::string_view("a\tb"), std::string_view("a\0b", 3)};
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Measurement {
    std::vector<double> positions;
    double width;
    bool operator==(const Measurement&) const = default;
};
Measurement Measure(core::Surface& surface, const core::Font& font, std::string_view text, bool utf8) {
    Measurement result{std::vector<double>(text.size(), -1), 0};
    if (utf8) {
        surface.MeasureWidthsUTF8(&font, text, result.positions.data());
        result.width = surface.WidthTextUTF8(&font, text);
    } else {
        surface.MeasureWidths(&font, text, result.positions.data());
        result.width = surface.WidthText(&font, text);
    }
    return result;
}
void Metrics(wxWindow& window) {
    auto adapted = core::AllocateDirectWriteSurface();
    adapted->Init(&window);
    auto original = core::OriginalDirectWriteSurface();
    original->Init(window.GetHandle());
    Require(adapted->LogPixelsY() == original->LogPixelsY() && adapted->LogPixelsY() == window.GetDPI().y,
            "wx host conversion preserves original window DPI");
    size_t records = 0;
    for (const auto dpi : Dpis) {
        for (const bool utf8 : {true, false}) {
            adapted->SetMode({utf8 ? Scintilla::CpUtf8 : 0});
            original->SetMode({utf8 ? Scintilla::CpUtf8 : 0});
            const auto samples = utf8 ? std::vector(Utf8Samples.begin(), Utf8Samples.end())
                                      : std::vector(ByteSamples.begin(), ByteSamples.end());
            std::vector<std::shared_ptr<core::Font>> fonts;
            std::vector<std::vector<Measurement>> expected;
            fonts.reserve(PointSizes.size() * FontStyles.size());
            expected.reserve(PointSizes.size() * FontStyles.size());
            for (const auto points : PointSizes) {
                for (const auto style : FontStyles) {
                    const auto weight =
                        style == FontStyle::Bold ? Scintilla::FontWeight::Bold : Scintilla::FontWeight::Normal;
                    // Original DWrite FontParameters take device pixels, not points.
                    fonts.push_back(core::Font::Allocate(core::FontParameters(
                        core::Platform::DefaultFont(), points * dpi / static_cast<double>(core::pointsPerInch), weight,
                        Scintilla::FontStretch::Normal, style == FontStyle::Italic,
                        Scintilla::FontQuality::QualityDefault, Scintilla::Technology::DirectWrite)));
                    expected.emplace_back();
                    for (const auto sample : samples) {
                        const auto reference = Measure(*original, *fonts.back(), sample, utf8);
                        Require(Measure(*adapted, *fonts.back(), sample, utf8) == reference,
                                "adapter preserves original DirectWrite widths and every byte position");
                        expected.back().push_back(reference);
                        ++records;
                    }
                }
            }
            std::array<std::exception_ptr, Workers> errors{};
            std::barrier start(Workers);
            std::vector<std::thread> threads;
            threads.reserve(Workers);
            for (int worker = 0; worker < Workers; ++worker) {
                threads.emplace_back([&, worker] {
                    start.arrive_and_wait();
                    try {
                        for (int iteration = 0; iteration < Iterations; ++iteration) {
                            const auto font =
                                (static_cast<size_t>(worker) * FontStyles.size() + iteration) % fonts.size();
                            for (size_t sample = 0; sample < samples.size(); ++sample)
                                Require(Measure(*adapted, *fonts[font], samples[sample], utf8) ==
                                            expected[font][sample],
                                        "parallel DirectWrite preserves original serial byte positions exactly");
                        }
                    } catch (...) {
                        errors[worker] = std::current_exception();
                    }
                });
            }
            for (auto& thread : threads)
                thread.join();
            for (const auto& error : errors)
                if (error) std::rethrow_exception(error);
        }
    }
    std::printf("PASS: %zu original DWrite metric records; %d workers x %d iterations; 96/144/192-DPI em sizes\n",
                records, Workers, Iterations);
}

constexpr int RasterWidth = 800;
constexpr int RasterHeight = 180;
void Draw(core::Surface& surface, const core::Font& font) {
    const core::PRectangle area(0, 0, RasterWidth, RasterHeight);
    surface.SetMode({Scintilla::CpUtf8});
    surface.FillRectangle(area, core::Fill(core::ColourRGBA(255, 255, 255)));
    surface.DrawTextTransparentUTF8(area, &font, 40, "DirectWrite: English 中文 😀 office ffi Café",
                                    core::ColourRGBA(20, 20, 20));
    surface.DrawTextClippedUTF8(core::PRectangle(20, 55, 500, 100), &font, 85,
                                "// comment: same font size and baseline", core::ColourRGBA(40, 120, 40),
                                core::ColourRGBA(255, 255, 255));
}
// Independent original-editor setup, from ScintillaWin::UpdateRenderingParams.
// Read the real monitor and SPI contrast without changing desktop settings.
struct OriginalRenderingParams {
    core::WriteRenderingParams monitor;
    core::WriteRenderingParams clearType;
    explicit OriginalRenderingParams(wxWindow& window) {
        const auto parent = ::GetParent(static_cast<HWND>(window.GetHandle()));
        const auto display = ::MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
        ComPtr<IDWriteRenderingParams> parameters;
        auto result = core::pIDWriteFactory->CreateMonitorRenderingParams(display, parameters.GetAddressOf());
        if (SUCCEEDED(result)) result = parameters.As(&monitor);
        Require(SUCCEEDED(result) && monitor, "original monitor rendering parameters are available");
        UINT contrast = 0;
        if (::SystemParametersInfo(SPI_GETFONTSMOOTHINGCONTRAST, 0, &contrast, 0)) {
            constexpr UINT MinimumContrast = 1000;
            constexpr UINT MaximumContrast = 2200;
            if (contrast >= MinimumContrast && contrast <= MaximumContrast)
                Require(SUCCEEDED(core::pIDWriteFactory->CreateCustomRenderingParams(
                            static_cast<FLOAT>(contrast) / 1000.0F, monitor->GetEnhancedContrast(),
                            monitor->GetGrayscaleEnhancedContrast(), monitor->GetClearTypeLevel(),
                            monitor->GetPixelGeometry(), monitor->GetRenderingMode(), clearType.GetAddressOf())),
                        "original SPI ClearType gamma rendering parameters are available");
        }
        std::printf("original monitor gamma=%g custom ClearType gamma=%g SPI contrast=%u\n", monitor->GetGamma(),
                    clearType ? clearType->GetGamma() : monitor->GetGamma(), contrast);
    }
};
void Raster(wxWindow& window, const std::filesystem::path& evidence, Scintilla::FontQuality quality) {
    const OriginalRenderingParams parameters(window);
    std::printf("native rendering mode=%u pixel geometry=%u clearType=%g quality=%u\n",
                static_cast<unsigned>(parameters.monitor->GetRenderingMode()),
                static_cast<unsigned>(parameters.monitor->GetPixelGeometry()), parameters.monitor->GetClearTypeLevel(),
                static_cast<unsigned>(quality));
    const auto font = core::Font::Allocate(core::FontParameters(
        core::Platform::DefaultFont(), 12.0 * window.GetDPI().y / core::pointsPerInch, Scintilla::FontWeight::Normal,
        Scintilla::FontStretch::Normal, false, quality, Scintilla::Technology::DirectWrite));
    std::array<wxBitmap, RasterCases.size()> bitmaps{
        wxBitmap(RasterWidth, RasterHeight), wxBitmap(RasterWidth, RasterHeight), wxBitmap(RasterWidth, RasterHeight),
        wxBitmap(RasterWidth, RasterHeight)};
    ComPtr<IDWriteRenderingParams> originalApplied;
    for (const auto rasterCase : RasterCases) {
        const auto version = static_cast<size_t>(rasterCase);
        wxMemoryDC dc(bitmaps[version]);
        if (rasterCase == RasterCase::OriginalDirect || rasterCase == RasterCase::OriginalBuffered) {
            core::DCRenderTarget target;
            const auto properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                core::dpiDefault, core::dpiDefault);
            Require(SUCCEEDED(core::CreateDCRenderTarget(&properties, target)), "original native DC render target");
            const RECT rectangle{0, 0, RasterWidth, RasterHeight};
            Require(SUCCEEDED(target->BindDC(static_cast<HDC>(dc.GetHDC()), &rectangle)), "bind original raster DC");
            auto surface = core::OriginalDirectWriteSurface();
            surface->Init(target.Get(), window.GetHandle());
            surface->SetRenderingParams(parameters.monitor.Get(), parameters.clearType.Get());
            target->BeginDraw();
            if (rasterCase == RasterCase::OriginalDirect) {
                Draw(*surface, *font);
                target->GetTextRenderingParams(originalApplied.GetAddressOf());
                auto* expected = quality == Scintilla::FontQuality::QualityLcdOptimized && parameters.clearType
                                     ? parameters.clearType.Get()
                                     : parameters.monitor.Get();
                Require(originalApplied && originalApplied->GetGamma() == expected->GetGamma(),
                        "original native drawing target applies the independently configured rendering gamma");
                std::printf("original native target gamma=%g antialias=%u\n", originalApplied->GetGamma(),
                            static_cast<unsigned>(target->GetTextAntialiasMode()));
            } else {
                auto buffered = surface->AllocatePixMap(RasterWidth, RasterHeight);
                Draw(*buffered, *font);
                buffered->FlushDrawing();
                surface->Copy(core::PRectangle(0, 0, RasterWidth, RasterHeight), core::Point(0, 0), *buffered);
            }
            Require(SUCCEEDED(target->EndDraw()), "original raster EndDraw succeeds");
        } else {
            auto surface = core::AllocateDirectWriteSurface();
            surface->Init(&dc, &window);
            Require(surface->Initialised(), "adapted native DC render target");
            if (rasterCase == RasterCase::AdaptedDirect)
                Draw(*surface, *font);
            else {
                auto buffered = surface->AllocatePixMap(RasterWidth, RasterHeight);
                Draw(*buffered, *font);
                buffered->FlushDrawing();
                surface->Copy(core::PRectangle(0, 0, RasterWidth, RasterHeight), core::Point(0, 0), *buffered);
            }
#ifdef WXNOTEPAD_TEST_CLEARTYPE_CONTRAST
            if (rasterCase == RasterCase::AdaptedDirect) {
                ComPtr<IDWriteRenderingParams> applied;
                ObservedDirectWriteTarget()->GetTextRenderingParams(applied.GetAddressOf());
                auto* expected = originalApplied.Get();
                Require(applied && applied->GetGamma() == expected->GetGamma() &&
                            applied->GetEnhancedContrast() == expected->GetEnhancedContrast() &&
                            applied->GetClearTypeLevel() == expected->GetClearTypeLevel() &&
                            applied->GetPixelGeometry() == expected->GetPixelGeometry() &&
                            applied->GetRenderingMode() == expected->GetRenderingMode(),
                        "production drawing target applies the original monitor or SPI ClearType parameters");
            }
#endif
            Require(core::FinishDirectWriteDrawing(*surface), "adapted raster EndDraw succeeds");
        }
    }
    const auto original = bitmaps[0].ConvertToImage();
    constexpr size_t RasterBytes = static_cast<size_t>(RasterWidth) * RasterHeight * 3;
    Require(std::any_of(original.GetData(), original.GetData() + RasterBytes,
                        [](unsigned char channel) { return channel != 255; }),
            "native raster contains text ink");
    constexpr std::array Names{"original-direct.png", "adapted-direct.png", "original-buffered.png",
                               "adapted-buffered.png"};
    for (const auto rasterCase : RasterCases) {
        const auto version = static_cast<size_t>(rasterCase);
        const auto image = bitmaps[version].ConvertToImage();
        if (!evidence.empty())
            Require(image.SaveFile(wxString((evidence / Names[version]).wstring()), wxBITMAP_TYPE_PNG),
                    "save native raster evidence");
        size_t changed = 0, ink = 0;
        for (size_t channel = 0; channel < RasterBytes; ++channel) {
            changed += original.GetData()[channel] != image.GetData()[channel];
            ink += image.GetData()[channel] != 255;
        }
        std::printf("raster=%zu changed_channels_from_direct=%zu ink_channels=%zu\n", version, changed, ink);
        if (rasterCase == RasterCase::AdaptedDirect || rasterCase == RasterCase::AdaptedBuffered) {
            const auto originalCase =
                rasterCase == RasterCase::AdaptedDirect ? RasterCase::OriginalDirect : RasterCase::OriginalBuffered;
            const auto reference = bitmaps[static_cast<size_t>(originalCase)].ConvertToImage();
            Require(std::equal(reference.GetData(), reference.GetData() + RasterBytes, image.GetData()),
                    "wx-adapted direct/buffered raster matches the corresponding original DirectWrite path exactly");
        }
    }
    std::puts("PASS: adapted direct and buffered rasters each exactly match their original DirectWrite counterpart");
}
class OracleApp final : public wxApp {
    int result_ = 1;

public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        const auto evidence =
            argc == 2 ? std::filesystem::path(wxString(argv[1]).ToStdWstring()) : std::filesystem::path{};
        auto* frame = new EditorFrame;
        frame->SetTitle("Native DirectWrite correctness");
        frame->Editor()->Send(Scintilla::Message::SetTechnology,
                              static_cast<uintptr_t>(Scintilla::Technology::DirectWrite));
        frame->Show();
        CallAfter([this, frame, evidence] {
            try {
                Require(frame->Editor()->Send(Scintilla::Message::GetTechnology) ==
                            static_cast<intptr_t>(Scintilla::Technology::DirectWrite),
                        "actual editor uses the original DirectWrite technology");
                Require(frame->Editor()->Send(Scintilla::Message::GetBufferedDraw) == 0,
                        "original DirectWrite technology disables the extra Scintilla line pixmap buffer");
                Metrics(*frame);
                Raster(*frame->Editor(), evidence, Scintilla::FontQuality::QualityDefault);
                Raster(*frame->Editor(), {}, Scintilla::FontQuality::QualityLcdOptimized);
#ifdef WXNOTEPAD_TEST_CLEARTYPE_CONTRAST
                constexpr UINT UpdatedClearTypeContrast = 2200;
                SetControlledFontSmoothingContrast(UpdatedClearTypeContrast);
#endif
                // Exercise the real settings refresh after compatible surfaces have been used.
                SendMessageW(static_cast<HWND>(frame->Editor()->GetHandle()), WM_SETTINGCHANGE, 0, 0);
                Raster(*frame->Editor(), {}, Scintilla::FontQuality::QualityLcdOptimized);
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
};
} // namespace
wxIMPLEMENT_APP(OracleApp);
