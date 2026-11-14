// wx window/DC integration for the pinned original DirectWrite surface.
#pragma once
#include "editor/platform/core_headers.h"
#include <wx/font.h>

class wxWindow;

namespace Scintilla::Internal {
bool DirectWriteAvailable() noexcept;
void DropDirectWriteTarget(wxWindow& window);
bool RefreshDirectWriteRenderingParams(wxWindow& window, bool force);
std::shared_ptr<Font> AllocateDirectWriteFont(const FontParameters& parameters);
std::unique_ptr<Surface> AllocateDirectWriteSurface();
wxFont DirectWriteListFont(const Font& font, const wxWindow& window);
bool FinishDirectWriteDrawing(Surface& surface);
// Direct original-backend oracle; takes native HWND/ID2D1RenderTarget identifiers.
std::unique_ptr<Surface> OriginalDirectWriteSurface();
} // namespace Scintilla::Internal
