#pragma once

#ifdef __WXMSW__
#include <windows.h>

namespace NativeFocus {
// [DEBUG-context-activation] Receipt for the focused Windows investigation.
struct ActivationReceipt {
    HWND previousForeground = nullptr;
    DWORD previousThreadId = 0;
    DWORD uiThreadId = 0;
    DWORD attachmentThreadId = 0;
    bool usedInputAttachment = false;
    bool foregroundAccepted = false;
    HWND foregroundInsideDriver = nullptr;
    HWND foregroundAfterDetach = nullptr;
};
inline void Activate(HWND root, HWND focus, ActivationReceipt* receipt = nullptr) {
    const auto uiThread = GetCurrentThreadId();
    const auto previousForeground = GetForegroundWindow();
    const auto foregroundThread = GetWindowThreadProcessId(previousForeground, nullptr);
    BringWindowToTop(root);
    const bool accepted = SetForegroundWindow(root) != FALSE;
    SetActiveWindow(root);
    SetFocus(focus);
    if (receipt)
        *receipt = {previousForeground, foregroundThread, uiThread, 0, false, accepted, nullptr, GetForegroundWindow()};
}
} // namespace NativeFocus
#endif
