#pragma once

#include <stdexcept>

namespace Scintilla::Internal {
// Native drawing and measurement failures without a platform error code.
// Keep the runtime_error base used by the editor's existing recovery boundaries.
class NativeDrawingError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
} // namespace Scintilla::Internal
