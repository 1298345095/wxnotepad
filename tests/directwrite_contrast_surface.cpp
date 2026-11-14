// Test-only OS query and native-target observation; the production adapter is unchanged.
#include "original_directwrite.inc"
#include <wx/wx.h>
namespace {
constexpr UINT InitialClearTypeContrast = 1000;
UINT contrast = InitialClearTypeContrast;
Scintilla::Internal::DCRenderTarget observedTarget;
} // namespace
void SetControlledFontSmoothingContrast(UINT value) {
    contrast = value;
}
ID2D1RenderTarget* ObservedDirectWriteTarget() {
    return observedTarget.Get();
}
BOOL WINAPI ControlledSystemParametersInfoW(UINT action, UINT parameter, PVOID value, UINT flags) {
    if (action == SPI_GETFONTSMOOTHINGCONTRAST) {
        *static_cast<UINT*>(value) = contrast;
        return TRUE;
    }
    return ::SystemParametersInfoW(action, parameter, value, flags);
}
namespace Scintilla::Internal {
HRESULT ObserveDCRenderTarget(const D2D1_RENDER_TARGET_PROPERTIES* properties, DCRenderTarget& target) noexcept {
    const auto result = CreateDCRenderTarget(properties, target);
    observedTarget = target;
    return result;
}
} // namespace Scintilla::Internal
#define SystemParametersInfoW ControlledSystemParametersInfoW
#define CreateDCRenderTarget ObserveDCRenderTarget
#include "editor/platform/directwrite_surface.cpp"
