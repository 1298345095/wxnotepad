#pragma once

#ifdef __WXMSW__
#include <windows.h>

namespace NativeFocus {
inline void Activate(HWND root, HWND focus) {
    // Keep the tested UI queue independent from other applications. Hosted CI
    // prepares its foreground policy without merging native focus/key states.
    BringWindowToTop(root);
    SetForegroundWindow(root);
    SetActiveWindow(root);
    SetFocus(focus);
}
} // namespace NativeFocus
#endif
