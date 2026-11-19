#pragma once

#include <memory>

class wxDC;

namespace Scintilla::Internal {
class Surface;
class TextMeasurement;

struct PrintSurface {
    wxDC& dc;
    const TextMeasurement& measurement;
};

std::unique_ptr<Surface> AllocatePrintSurface(const PrintSurface& printer);
} // namespace Scintilla::Internal
