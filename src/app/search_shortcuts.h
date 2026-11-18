#pragma once

#include <wx/accel.h>
#include <array>
#include <cstdint>
#include <utility>

enum class F3SearchAction : std::uint8_t { Next, Previous, SaveFindText, SelectionNext, SelectionPrevious, Ignore };

namespace SearchShortcuts {
inline constexpr std::array F3Bindings{
    std::pair{wxMOD_NONE, wxACCEL_NORMAL},
    std::pair{wxMOD_SHIFT, wxACCEL_SHIFT},
    std::pair{wxMOD_ALT, wxACCEL_ALT},
    std::pair{wxMOD_CONTROL, wxACCEL_CTRL},
    std::pair{static_cast<wxKeyModifier>(wxMOD_CONTROL | wxMOD_SHIFT),
              static_cast<wxAcceleratorEntryFlags>(wxACCEL_CTRL | wxACCEL_SHIFT)},
};

inline F3SearchAction ClassifyF3(int modifiers) {
    for (size_t index = 0; index < F3Bindings.size(); ++index) {
        if (F3Bindings[index].first == modifiers) return static_cast<F3SearchAction>(index);
    }
    return F3SearchAction::Ignore;
}

inline wxAcceleratorEntry F3Accelerator(F3SearchAction action, int command) {
    return {F3Bindings[std::to_underlying(action)].second, WXK_F3, command};
}
} // namespace SearchShortcuts
