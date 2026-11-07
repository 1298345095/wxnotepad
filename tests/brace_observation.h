#pragma once

#include "editor/editor_core.h"
#include <array>

// Test-owned observation of the actual decoration consumed by Scintilla's paint
// path. Brace indicators are overlays, so IndicatorValueAt cannot observe them.
struct EditorCoreObservation {
    static std::array<intptr_t, 3> Braces(EditorControl& editor) {
        return {editor.core_->braces[0], editor.core_->braces[1], editor.core_->bracesMatchStyle};
    }
};
