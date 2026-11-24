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
    const auto thread = GetCurrentThreadId();
    const auto previousForeground = GetForegroundWindow();
    const auto foregroundThread = GetWindowThreadProcessId(previousForeground, nullptr);
    const bool attached =
        foregroundThread && foregroundThread != thread && AttachThreadInput(thread, foregroundThread, TRUE);
    BringWindowToTop(root);
    SetForegroundWindow(root);
    SetActiveWindow(root);
    SetFocus(focus);
    if (attached) AttachThreadInput(thread, foregroundThread, FALSE);
    if (receipt) *receipt = {previousForeground, foregroundThread, attached, GetForegroundWindow()};
}
} // namespace NativeFocus
#endif
