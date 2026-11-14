// Reuse pinned Notepad4 drawing and measurement; adapt wx host and paint lifetime.
#include "original_directwrite.inc"
#include "directwrite_surface.h"
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/fontutil.h>
#include <system_error>

namespace Scintilla::Internal {
namespace {
void CheckResult(HRESULT result, const char* operation) {
    if (FAILED(result)) throw std::system_error(static_cast<int>(result), std::system_category(), operation);
}

struct WindowTarget {
    ComPtr<ID2D1HwndRenderTarget> target;
};

// GUI-only targets survive individual paints, as in ScintillaWin. Native pixmaps
// cached by EditView must stay in the same D2D resource domain between paints.
auto& WindowTargets() {
    static std::map<wxWindow*, std::shared_ptr<WindowTarget>> windowTargets;
    return windowTargets;
}
std::shared_ptr<WindowTarget> TargetForExisting(wxWindow& window) {
    const auto& targets = WindowTargets();
    const auto found = targets.find(&window);
    return found != targets.end() ? found->second : nullptr;
}
std::shared_ptr<WindowTarget> TargetFor(wxWindow& window) {
    auto& windowTargets = WindowTargets();
    const auto found = windowTargets.find(&window);
    if (found != windowTargets.end() && found->second->target) return found->second;
    auto target = found != windowTargets.end() ? found->second : std::make_shared<WindowTarget>();
    const auto size = window.GetClientSize();
    const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                         D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE),
                                                         dpiDefault, dpiDefault);
    const auto nativeWindow = static_cast<HWND>(window.GetHandle());
    const auto windowProperties =
        D2D1::HwndRenderTargetProperties(nativeWindow, D2D1::SizeU(std::max(1, size.x), std::max(1, size.y)));
    CheckResult(pD2DFactory->CreateHwndRenderTarget(properties, windowProperties, &target->target),
                "CreateHwndRenderTarget");
    if (found == windowTargets.end()) {
        windowTargets.emplace(&window, target);
        window.Bind(wxEVT_DESTROY, [host = &window](wxWindowDestroyEvent& event) {
            if (event.GetEventObject() == host) WindowTargets().erase(host);
            event.Skip();
        });
    }
    return target;
}

class WxDirectWriteSurface final : public SurfaceD2D {
    wxWindow* window_ = nullptr;
    std::unique_ptr<wxClientDC> ownedDC_;
    std::shared_ptr<WindowTarget> windowTarget_;
    DCRenderTarget dcTarget_;
    ID2D1RenderTarget* drawing_ = nullptr;
    HRESULT status_ = S_OK;

public:
    ~WxDirectWriteSurface() noexcept override { Release(); }
    void Init(WindowID id) noexcept override {
        window_ = static_cast<wxWindow*>(id);
        SurfaceD2D::Init(window_->GetHandle());
    }
    void Init(SurfaceID id, WindowID window, bool printing) noexcept override {
        window_ = static_cast<wxWindow*>(window);
        auto* dc = static_cast<wxDC*>(id);
        // Paint directly to HWND. Offscreen drawing uses a DC target with the
        // same original text renderer; no GDI text-measurement fallback.
        if (dc->IsKindOf(wxCLASSINFO(wxPaintDC))) {
            try {
                windowTarget_ = TargetFor(*window_);
            } catch (const std::system_error& error) {
                status_ = static_cast<HRESULT>(error.code().value());
                return;
            } catch (const std::bad_alloc&) {
                status_ = E_OUTOFMEMORY;
                return;
            }
            const auto size = window_->GetClientSize();
            const auto desired = D2D1::SizeU(std::max(1, size.x), std::max(1, size.y));
            const auto previous = windowTarget_->target->GetPixelSize();
            if (previous.width != desired.width || previous.height != desired.height)
                status_ = windowTarget_->target->Resize(desired);
            drawing_ = windowTarget_->target.Get();
        } else {
            const auto properties = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                dpiDefault, dpiDefault);
            status_ = CreateDCRenderTarget(&properties, dcTarget_);
            if (SUCCEEDED(status_)) {
                const auto size = dc->GetSize();
                const RECT area{0, 0, std::max(1, size.x), std::max(1, size.y)};
                status_ = dcTarget_->BindDC(static_cast<HDC>(dc->GetHDC()), &area);
            }
            drawing_ = dcTarget_.Get();
        }
        if (FAILED(status_)) {
            drawing_ = nullptr;
            return;
        }
        SurfaceD2D::Init(drawing_, window_->GetHandle(), printing);
        drawing_->BeginDraw();
    }
    std::unique_ptr<Surface> AllocatePixMap(int width, int height) override {
        if (!drawing_) {
            // A measurement-only surface may later need an offscreen drawing target.
            ownedDC_ = std::make_unique<wxClientDC>(window_);
            Init(ownedDC_.get(), window_, false);
        }
        CheckResult(status_, "Initialize Direct2D drawing target");
        return SurfaceD2D::AllocatePixMap(width, height);
    }
    bool Finish() {
        if (drawing_) {
            // ScintillaWin releases brushes/clips before ending its native draw.
            SurfaceD2D::Release();
            status_ = drawing_->EndDraw();
            drawing_ = nullptr;
        }
        if (status_ == D2DERR_RECREATE_TARGET && windowTarget_) {
            windowTarget_->target.Reset();
            return false;
        }
        CheckResult(status_, "Finish Direct2D drawing");
        return true;
    }
    void Release() noexcept override {
        if (drawing_) {
            const auto result = drawing_->EndDraw();
            if (FAILED(result)) std::fprintf(stderr, "Direct2D EndDraw failed: 0x%08lx\n", result);
            drawing_ = nullptr;
        }
        SurfaceD2D::Release();
        dcTarget_.Reset();
        windowTarget_.reset();
        ownedDC_.reset();
    }
};

void EnsureDirectWrite() {
    // Factory/font/surface setup runs on the GUI thread before worker use.
    if (!LoadD2D()) throw std::runtime_error("Original Direct2D/DirectWrite initialization failed");
}
} // namespace

bool DirectWriteAvailable() noexcept {
    return LoadD2D();
}
void DropDirectWriteTarget(wxWindow& window) {
    // Discarded together with the core's compatible pixmaps on technology change.
    const auto target = TargetForExisting(window);
    if (target) target->target.Reset();
}

std::shared_ptr<Font> AllocateDirectWriteFont(const FontParameters& parameters) {
    EnsureDirectWrite();
    auto native = parameters;
    native.technology = Technology::DirectWrite;
    auto font = std::make_shared<FontWin>(native);
    if (!font->pTextFormat) throw std::runtime_error("Original DirectWrite font creation failed");
    return font;
}
std::unique_ptr<Surface> AllocateDirectWriteSurface() {
    EnsureDirectWrite();
    return std::make_unique<WxDirectWriteSurface>();
}
std::unique_ptr<Surface> OriginalDirectWriteSurface() {
    EnsureDirectWrite();
    return std::make_unique<SurfaceD2D>();
}
wxFont DirectWriteListFont(const Font& font) {
    wxNativeFontInfo info;
    info.lf = static_cast<const FontWin&>(font).lf;
    return wxFont(info);
}
bool FinishDirectWriteDrawing(Surface& surface) {
    return static_cast<WxDirectWriteSurface&>(surface).Finish();
}
} // namespace Scintilla::Internal
