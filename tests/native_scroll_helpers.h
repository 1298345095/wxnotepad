#pragma once

#include "editor/editor_control.h"
#include <wx/utils.h>
#include <chrono>

namespace NativeScroll {
inline bool WaitForHorizontalRange(EditorControl* editor, bool overflow, int minimumOffset = 1) {
    const auto ready = [=] {
        const int extent = editor->GetScrollRange(wxHORIZONTAL) - editor->GetScrollThumb(wxHORIZONTAL);
        return overflow ? extent >= minimumOffset : extent <= 0;
    };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!ready() && std::chrono::steady_clock::now() < deadline) {
        editor->Update();
        wxYield();
        wxMilliSleep(5);
    }
    return ready();
}
} // namespace NativeScroll
