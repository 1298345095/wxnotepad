#pragma once

#ifdef __WXMSW__
#include <windows.h>

namespace NativeFocus {
// [DEBUG-context-activation] Receipt for the focused Windows investigation.
struct ActivationReceipt {
    HWND previousForeground = nullptr;
    DWORD previousThreadId = 0;
    bool usedInputAttachment = false;
    HWND foregroundAfterDetach = nullptr;
};
inline void Activate(HWND root, HWND focus, ActivationReceipt* receipt = nullptr) {
    const auto previousForeground = GetForegroundWindow();
    const auto foregroundThread = GetWindowThreadProcessId(previousForeground, nullptr);
    BringWindowToTop(root);
    SetForegroundWindow(root);
    SetActiveWindow(root);
    SetFocus(focus);
    if (receipt) *receipt = {previousForeground, foregroundThread, false, GetForegroundWindow()};
}
} // namespace NativeFocus
#endif
