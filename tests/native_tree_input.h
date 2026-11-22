#pragma once
#include "native_key_helpers.h"
#include <wx/ffile.h>
#include <wx/treectrl.h>
#include <chrono>
#include <stdexcept>

// Real mouse gestures target the calling fixture's native modal tree. Linux
// gestures use the existing private-display driver, never the host desktop.
namespace NativeTreeInput {
template <class Predicate> void Wait(Predicate complete, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!complete() && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    if (!complete()) throw std::runtime_error(message);
}
inline void Present(wxDialog& dialog) {
#ifdef __WXGTK__
    // Wayland deliberately hides global positions. Fullscreen fixes this owned
    // surface to the private compositor output origin for pointer coordinates.
    auto* widget = GTK_WIDGET(dialog.GetHandle());
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget)))
        dialog.ShowFullScreen(true);
    else
        dialog.SetSize(0, 0, 1400, 900);
    bool presented = false;
    NativeKeys::AfterPresentation(&dialog, [&](bool ready) { presented = ready; });
    Wait([&] { return presented; }, "native tree dialog must be presented before pointer input");
#else
    NativeFocus::Activate(static_cast<HWND>(dialog.GetHandle()), static_cast<HWND>(dialog.GetHandle()));
#endif
}
inline wxPoint Point(wxTreeCtrl& tree, wxTreeItemId item, bool checkbox = false) {
    tree.EnsureVisible(item);
    wxYield();
    wxRect rect;
    if (!tree.GetBoundingRect(item, rect, true)) throw std::runtime_error("native tree item must be visible");
    const int y = rect.GetY() + rect.GetHeight() / 2;
    if (!checkbox) return tree.ClientToScreen(wxPoint(rect.GetX() + rect.GetWidth() / 2, y));
    for (int x = 0; x < rect.GetX(); ++x) {
        int flags = 0;
        if (tree.HitTest(wxPoint(x, y), flags) == item && (flags & wxTREE_HITTEST_ONITEMSTATEICON))
            return tree.ClientToScreen(wxPoint(x + 1, y));
    }
    throw std::runtime_error("native tree checkbox exposes a clickable state image");
}
#ifdef __WXGTK__
inline void Request(const wxString& command) {
    wxString path;
    if (!wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &path))
        throw std::runtime_error("tree mouse input requires the private display driver");
    {
        wxFFile output(path + ".tmp", "wb");
        if (!output.IsOpened() || !output.Write(command)) throw std::runtime_error("write native tree gesture");
    }
    if (!wxRenameFile(path + ".tmp", path, true)) throw std::runtime_error("publish native tree gesture");
    Wait(
        [&] {
            wxString pending;
            wxFFile input(path, "rb");
            return input.IsOpened() && input.ReadAll(&pending) && pending.empty();
        },
        "private driver acknowledges the native tree gesture");
}
#else
inline void Mouse(DWORD flags) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = flags;
    if (SendInput(1, &input, sizeof(INPUT)) != 1) throw std::runtime_error("send native tree pointer input");
    wxYield();
}
inline void Move(wxTreeCtrl& tree, wxPoint point) {
    DWORD process = 0;
    const auto target = WindowFromPoint(POINT{point.x, point.y});
    GetWindowThreadProcessId(target, &process);
    if (process != GetCurrentProcessId() || target != static_cast<HWND>(tree.GetHandle()))
        throw std::runtime_error("native tree pointer must target this fixture's owned tree");
    if (!SetCursorPos(point.x, point.y)) throw std::runtime_error("move native tree pointer");
    wxYield();
}
#endif
inline void Click(wxTreeCtrl& tree, wxTreeItemId item) {
    const auto point = Point(tree, item, true);
#ifdef __WXGTK__
    Request(wxString::Format("click:%d,%d,1", point.x, point.y));
#else
    NativeFocus::Activate(static_cast<HWND>(wxGetTopLevelParent(&tree)->GetHandle()),
                          static_cast<HWND>(tree.GetHandle()));
    Move(tree, point);
    Mouse(MOUSEEVENTF_LEFTDOWN);
    Mouse(MOUSEEVENTF_LEFTUP);
#endif
}
inline void Drag(wxTreeCtrl& tree, wxTreeItemId source, wxTreeItemId target) {
    const auto from = Point(tree, source);
    const auto to = Point(tree, target);
#ifdef __WXGTK__
    Request(wxString::Format("tree-drag:%d,%d,%d,%d", from.x, from.y, to.x, to.y));
#else
    NativeFocus::Activate(static_cast<HWND>(wxGetTopLevelParent(&tree)->GetHandle()),
                          static_cast<HWND>(tree.GetHandle()));
    Move(tree, from);
    Mouse(MOUSEEVENTF_LEFTDOWN);
    constexpr int MotionSteps = 8;
    for (int step = 1; step <= MotionSteps; ++step) {
        Move(tree,
             wxPoint(from.x + (to.x - from.x) * step / MotionSteps, from.y + (to.y - from.y) * step / MotionSteps));
        wxMilliSleep(30);
    }
    Mouse(MOUSEEVENTF_LEFTUP);
#endif
}
inline void Enter(wxDialog& dialog) {
#ifdef __WXGTK__
    Request("key:Return");
#else
    NativeKeys::DialogNavigation(&dialog, VK_RETURN);
#endif
    Wait([&] { return !dialog.IsModal(); }, "native Enter accepts the owned scheme dialog");
}
} // namespace NativeTreeInput
