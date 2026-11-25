#pragma once

#ifdef __WXMSW__
#include <windows.h>

namespace NativeFocus {
inline bool GrantTo(HWND sender, DWORD recipient) {
    // The foreground test process grants only its cooperating peer permission
    // to request activation; callers still verify the actual native focus.
    return recipient != 0 && recipient != ASFW_ANY && GetForegroundWindow() == sender &&
           AllowSetForegroundWindow(recipient) != FALSE;
}
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
