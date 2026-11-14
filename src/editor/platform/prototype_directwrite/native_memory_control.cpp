// PROTOTYPE: original ScintillaWin factory/target sequence on a plain Win32 HWND.
#include "original_directwrite_diagnostic.inc"
#include "diagnostic_metrics.h"

HANDLE g_hDefaultHeap = GetProcessHeap();
namespace {
constexpr int ClientWidth = 1020;
constexpr int ClientHeight = 600;
void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::runtime_error(operation);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    namespace core = Scintilla::Internal;
    HWND window = nullptr;
    if (!DWriteDiagnosis::Initialize()) return 1;
    try {
        DWriteDiagnosis::Memory("native-before-window");
        WNDCLASSW type{};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = instance;
        type.lpszClassName = L"WxNotepadOriginalTargetControl";
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (!RegisterClassW(&type)) throw std::runtime_error("register native control class");
        window = CreateWindowExW(0, type.lpszClassName, L"PROTOTYPE - original native target memory control",
                                 WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, ClientWidth, ClientHeight, nullptr,
                                 nullptr, instance, nullptr);
        if (!window) throw std::runtime_error("create native control window");
        RECT dimensions{0, 0, ClientWidth, ClientHeight};
        AdjustWindowRectExForDpi(&dimensions, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(window));
        SetWindowPos(window, nullptr, 0, 0, dimensions.right - dimensions.left, dimensions.bottom - dimensions.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        ShowWindow(window, SW_SHOWNORMAL);
        UpdateWindow(window);
        if (!core::LoadD2D()) throw std::runtime_error("load original native D2D factories");
        RECT client{};
        GetClientRect(window, &client);
        std::printf("NATIVE_CONTROL client=%ldx%ld dpi=%u\n", client.right, client.bottom, GetDpiForWindow(window));
        const auto properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
            core::dpiDefault, core::dpiDefault);
        const auto host = D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(client.right, client.bottom),
                                                           D2D1_PRESENT_OPTIONS_NONE);
        ComPtr<ID2D1HwndRenderTarget> target;
        DWriteDiagnosis::Memory("native-before-hwnd-target");
        Check(core::pD2DFactory->CreateHwndRenderTarget(properties, host, &target),
              "create original native HWND target");
        DWriteDiagnosis::Memory("native-after-hwnd-target");
        const core::FontParameters parameters("Consolas", 12.0 * GetDpiForWindow(window) / core::pointsPerInch,
                                              Scintilla::FontWeight::Normal, Scintilla::FontStretch::Normal, false,
                                              Scintilla::FontQuality::QualityDefault,
                                              Scintilla::Technology::DirectWrite);
        auto font = std::make_shared<core::FontWin>(parameters);
        auto surface = std::make_unique<SurfaceD2D>();
        surface->Init(target.Get(), window);
        surface->SetMode({Scintilla::CpUtf8});
        target->BeginDraw();
        const core::PRectangle area(0, 0, client.right, client.bottom);
        surface->FillRectangle(area, core::Fill(core::ColourRGBA(255, 255, 255)));
        surface->DrawTextTransparentUTF8(area, font.get(), 40, "Original DirectWrite control: 中文 😀",
                                         core::ColourRGBA(0, 0, 0));
        surface->Release();
        Check(target->EndDraw(), "present original native target");
        Sleep(1000);
        DWriteDiagnosis::Memory("native-after-first-paint");
        surface.reset();
        font.reset();
        target.Reset();
        DestroyWindow(window);
        window = nullptr;
        Sleep(1000);
        DWriteDiagnosis::Memory("native-after-target-destroy");
        DWriteDiagnosis::Finish();
        std::puts("PASS: original native target memory control; no wx window or layout scheduler");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        if (window) DestroyWindow(window);
        DWriteDiagnosis::Finish();
        return 1;
    }
}
