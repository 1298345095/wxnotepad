#pragma once

#ifdef __WXMSW__
#include <windows.h>

namespace NativeFocus {
inline void Activate(HWND root, HWND focus) {
    const auto thread = GetCurrentThreadId();
    const auto foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const bool attached =
        foregroundThread && foregroundThread != thread && AttachThreadInput(thread, foregroundThread, TRUE);
    BringWindowToTop(root);
    SetForegroundWindow(root);
    SetActiveWindow(root);
    SetFocus(focus);
    if (attached) AttachThreadInput(thread, foregroundThread, FALSE);
}
} // namespace NativeFocus
#endif
