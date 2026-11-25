#pragma once

#ifdef __WXMSW__
#include <windows.h>
#include <thread>

namespace NativeFocus {
// [DEBUG-context-activation] Receipt for the focused Windows investigation.
struct ActivationReceipt {
    HWND previousForeground = nullptr;
    DWORD previousThreadId = 0;
    DWORD uiThreadId = 0;
    DWORD attachmentThreadId = 0;
    bool usedInputAttachment = false;
    HWND foregroundAfterDetach = nullptr;
};
inline void Activate(HWND root, HWND focus, ActivationReceipt* receipt = nullptr) {
    const auto uiThread = GetCurrentThreadId();
    const auto previousForeground = GetForegroundWindow();
    const auto foregroundThread = GetWindowThreadProcessId(previousForeground, nullptr);
    DWORD attachmentThread = 0;
    bool attached = false;
    BringWindowToTop(root);
    if (foregroundThread && foregroundThread != uiThread) {
        // Keep the tested UI queue independent. A controller can attach to the
        // old foreground queue and request activation across to our UI queue.
        std::thread controller([&] {
            MSG message{};
            PeekMessage(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
            attachmentThread = GetCurrentThreadId();
            attached = AttachThreadInput(attachmentThread, foregroundThread, TRUE) != FALSE;
            SetForegroundWindow(root);
            if (attached) AttachThreadInput(attachmentThread, foregroundThread, FALSE);
        });
        controller.join();
        // Cross-queue activation is asynchronous. Dispatch its pending sent
        // notification before this UI thread sets focus or sends native input.
        MSG message{};
        PeekMessage(&message, nullptr, WM_NULL, WM_NULL, PM_NOREMOVE);
    } else {
        SetForegroundWindow(root);
    }
    SetActiveWindow(root);
    SetFocus(focus);
    if (receipt)
        *receipt = {previousForeground, foregroundThread, uiThread, attachmentThread, attached, GetForegroundWindow()};
}
} // namespace NativeFocus
#endif
