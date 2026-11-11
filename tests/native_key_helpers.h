#pragma once

#include "native_search_helpers.h"
#include "native_focus_helpers.h"
#include "native_trace_helpers.h"
#include <wx/eventfilter.h>
#include <wx/ffile.h>
#include <wx/timer.h>
#include <chrono>
#include <algorithm>
#include <functional>
#include <memory>
#ifdef __WXGTK__
#include <gdk/gdkwayland.h>
#include <wayland-client-core.h>
#endif

// Native shortcuts target the caller's editor on the existing private display.
namespace NativeKeys {
inline int UnshiftedPunctuation(int key) {
    switch (key) {
    case '{':
        return '[';
    case '}':
        return ']';
    case '<':
        return ',';
    case '>':
        return '.';
    default:
        return key;
    }
}
inline void AfterPresentation(wxTopLevelWindow* window, std::function<void(bool)> callback) {
#ifdef __WXGTK__
    auto* widget = GTK_WIDGET(window->GetHandle());
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget))) {
        // Keep the normal main loop running until Weston sees this exact view.
        // Entering a synchronous shortcut wait before the first commit can leave
        // GTK awaiting a frame callback for an empty, unmapped surface.
        class Preparation final : public wxTimer {
        public:
            Preparation(wxTopLevelWindow* window, std::function<void(bool)> callback)
                : callback_(std::move(callback)),
                  deadline_(std::chrono::steady_clock::now() + std::chrono::seconds(3)) {
                auto* widget = GTK_WIDGET(window->GetHandle());
                auto* surface = gdk_wayland_window_get_wl_surface(gtk_widget_get_window(widget));
                if (surface && wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile_)) {
                    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
                                               deadline_ - std::chrono::steady_clock::now())
                                               .count();
                    wxFFile file(focusFile_, "wb");
                    file.Write(wxString::Format("prepare:%lu:%u:%lld", wxGetProcessId(),
                                                wl_proxy_get_id(reinterpret_cast<wl_proxy*>(surface)),
                                                static_cast<long long>(g_get_monotonic_time() + remaining)));
                }
            }
            void StartWaiting(std::shared_ptr<Preparation> pending) {
                pending_ = std::move(pending);
                Start(10);
            }
            void Notify() override {
                bool ready = false;
                if (!focusFile_.empty()) {
                    wxString pending;
                    wxFFile file(focusFile_, "rb");
                    ready = file.IsOpened() && file.ReadAll(&pending) && pending.empty();
                }
                if (!ready && std::chrono::steady_clock::now() < deadline_) return;
                Stop();
                // Retain the timer through Notify and the callback, then release
                // the pending operation when this invocation returns.
                auto completed = std::move(pending_);
                auto callback = std::move(callback_);
                callback(ready);
            }

        private:
            std::function<void(bool)> callback_;
            std::chrono::steady_clock::time_point deadline_;
            wxString focusFile_;
            std::shared_ptr<Preparation> pending_;
        };
        auto preparation = std::make_shared<Preparation>(window, std::move(callback));
        preparation->StartWaiting(preparation);
        return;
    }
#endif
    wxTheApp->CallAfter([callback = std::move(callback)] { callback(true); });
}

class KeyRelease final : public wxEventFilter {
public:
    KeyRelease(int key, bool& received, bool* signal, wxWindow* intended = nullptr)
        : key_(key), received_(received), signal_(signal), intended_(intended) {
        wxEvtHandler::AddFilter(this);
    }
    ~KeyRelease() override { wxEvtHandler::RemoveFilter(this); }
    int FilterEvent(wxEvent& event) override {
        if (event.GetEventType() == wxEVT_KEY_UP &&
            UnshiftedPunctuation(static_cast<wxKeyEvent&>(event).GetKeyCode()) == key_ &&
            (!intended_ || event.GetEventObject() == intended_)) {
            received_ = true;
            if (signal_) *signal_ = true;
        }
        return Event_Skip;
    }

private:
    int key_;
    bool& received_;
    bool* signal_;
    wxWindow* intended_;
};
#ifdef __WXMSW__
inline void DialogNavigation(wxDialog* dialog, int key) {
    BYTE previous[256];
    if (!GetKeyboardState(previous)) throw std::runtime_error("read test-thread keyboard state for dialog navigation");
    struct RestoreKeyboardState {
        BYTE* previous;
        ~RestoreKeyboardState() { SetKeyboardState(previous); }
    } restore{previous};
    BYTE released[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(released));
    for (const int modifier : {VK_CONTROL, VK_MENU, VK_SHIFT})
        released[modifier] = 0;
    if (!SetKeyboardState(released)) throw std::runtime_error("release shortcut modifiers for dialog navigation");
    MSG message{};
    auto* focused = wxWindow::FindFocus();
    message.hwnd = static_cast<HWND>((focused ? focused : dialog)->GetHandle());
    message.message = WM_KEYDOWN;
    message.wParam = key;
    // Native dialog navigation has a separate path from accelerator translation.
    if (!dialog->MSWProcessMessage(&message)) {
        if (key == VK_ESCAPE) {
            // wx's Escape handler is a WH_KEYBOARD hook, which preprocessing,
            // posted messages and Translate/DispatchMessage don't invoke.
            const auto owned = static_cast<HWND>(dialog->GetHandle());
            DWORD process = 0;
            GetWindowThreadProcessId(owned, &process);
            if (process != GetCurrentProcessId() || !dialog->IsModal())
                throw std::runtime_error("native Escape must target the recognized owned modal dialog");
            const auto inputDesktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
            wchar_t inputName[256]{}, threadName[256]{};
            DWORD needed = 0;
            const bool inputIsCurrent =
                inputDesktop &&
                GetUserObjectInformationW(inputDesktop, UOI_NAME, inputName, sizeof(inputName), &needed) &&
                GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, threadName,
                                          sizeof(threadName), &needed) &&
                std::wstring(inputName) == threadName;
            if (inputDesktop) CloseDesktop(inputDesktop);
            if (!inputIsCurrent)
                throw std::runtime_error("physical native Escape requires the current Windows input desktop");
            if (message.hwnd != owned && !IsChild(owned, message.hwnd))
                throw std::runtime_error("native Escape focus must belong to the owned dialog");
            NativeFocus::Activate(owned, message.hwnd);
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            const auto ownsInput = [&] {
                const auto focus = GetFocus();
                return dialog->IsShown() && GetForegroundWindow() == owned && (focus == owned || IsChild(owned, focus));
            };
            while (!ownsInput() && std::chrono::steady_clock::now() < until) {
                wxYield();
                wxMilliSleep(10);
            }
            if (!ownsInput()) throw std::runtime_error("native Escape requires the owned dialog on the input desktop");
            for (const int modifier : {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN, VK_ESCAPE})
                if (GetAsyncKeyState(modifier) & 0x8000)
                    throw std::runtime_error("native Escape requires released physical shortcut modifiers");
            INPUT input[2]{};
            for (auto& event : input) {
                event.type = INPUT_KEYBOARD;
                event.ki.wVk = VK_ESCAPE;
            }
            input[1].ki.dwFlags = KEYEVENTF_KEYUP;
            if (!ownsInput() || SendInput(2, input, sizeof(INPUT)) != 2)
                throw std::runtime_error("native Escape input must reach the verified owned dialog");
            while (dialog->IsShown() && std::chrono::steady_clock::now() < until) {
                if (!ownsInput()) throw std::runtime_error("owned dialog lost focus before native Escape completed");
                wxYield();
                wxMilliSleep(10);
            }
            if (dialog->IsShown()) throw std::runtime_error("native Escape must close the owned dialog");
        } else {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}
#endif
#ifndef __WXMSW__
inline void FocusEditor(EditorFrame* frame, const wxString& focusName, wxString& focusFile) {
    using Clock = std::chrono::steady_clock;
    auto* editor = frame->Editor();
    editor->SetFocus();
#ifdef __WXGTK__
    auto* widget = GTK_WIDGET(frame->GetHandle());
    const bool wayland = GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget));
#endif
    wxYield();
    if (!wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile))
        throw std::runtime_error("native shortcuts require a private display");
    const auto focusDeadline = Clock::now() + std::chrono::seconds(3);
    wxString intent = wxString(frame->IsActive() ? "focus:" : "front:") + focusName;
#ifdef __WXGTK__
    if (!frame->IsActive() && wayland) {
        auto* surface = gdk_wayland_window_get_wl_surface(gtk_widget_get_window(widget));
        if (!surface) throw std::runtime_error("native shortcuts require the owned Wayland surface");
        const auto remaining =
            std::chrono::duration_cast<std::chrono::microseconds>(focusDeadline - Clock::now()).count();
        intent = wxString::Format("activate:%lu:%u:%lld", wxGetProcessId(),
                                  wl_proxy_get_id(reinterpret_cast<wl_proxy*>(surface)),
                                  static_cast<long long>(g_get_monotonic_time() + remaining));
    }
#endif
    {
        wxFFile file(focusFile, "wb");
        file.Write(NativeTrace::Request(intent));
    }
    bool acknowledged = false;
    while (Clock::now() < focusDeadline) {
        wxString pending;
        {
            wxFFile file(focusFile, "rb");
            file.ReadAll(&pending);
        }
        acknowledged = pending.empty();
        if (acknowledged && frame->IsActive() && editor->HasFocus()) break;
        wxYield();
        wxMilliSleep(10);
    }
    if (!acknowledged || !frame->IsActive() || !editor->HasFocus())
        throw std::runtime_error("private native input must activate the owned editor");
}
inline wxString KeyName(int key) {
    if (key >= WXK_F1 && key <= WXK_F24) return wxString::Format("F%d", key - WXK_F1 + 1);
    switch (key) {
    case ' ':
        return "space";
    case '/':
        return "slash";
    case WXK_NUMPAD_DIVIDE:
        return "KP_Divide";
    case '[':
        return "bracketleft";
    case ']':
        return "bracketright";
    case ',':
        return "comma";
    case '.':
        return "period";
    default:
        const auto letter = static_cast<char>(key >= 'A' && key <= 'Z' ? key + 'a' - 'A' : key);
        return wxString::Format("%c", letter);
    }
}
#endif
inline void Shortcut(EditorFrame* frame, int key, bool control, bool alt = false, bool shift = false,
                     bool* releaseSignal = nullptr, [[maybe_unused]] const wxString& focusName = "wxnotepad") {
    const NativeTrace::Gesture trace(*frame->Editor(), key,
                                     (control ? wxMOD_CONTROL : 0) | (alt ? wxMOD_ALT : 0) | (shift ? wxMOD_SHIFT : 0));
#ifdef __WXMSW__
    int nativeKey = key >= WXK_F1 && key <= WXK_F24 ? VK_F1 + key - WXK_F1 : key;
    // Match wxMSW's layout lookup and OEM fallback for punctuation accelerators.
    if (key == '[' || key == ']' || key == ',' || key == '.') {
        const SHORT mapped = VkKeyScanW(static_cast<wchar_t>(key));
        if (mapped != -1)
            nativeKey = LOBYTE(mapped);
        else
            nativeKey = key == '[' ? VK_OEM_4 : key == ']' ? VK_OEM_6 : key == ',' ? VK_OEM_COMMA : VK_OEM_PERIOD;
    }
    NativeSearch::WindowsShortcut(frame->Editor(), nativeKey,
                                  {{VK_CONTROL, static_cast<BYTE>(control ? 0x80 : 0)},
                                   {VK_MENU, static_cast<BYTE>(alt ? 0x80 : 0)},
                                   {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)}});
    if (key == '[' || key == ']' || key == ',' || key == '.') {
        bool received = false;
        KeyRelease observer(key, received, releaseSignal, frame->Editor());
        const LPARAM flags = (LPARAM{KF_UP | KF_REPEAT | (alt ? KF_ALTDOWN : 0)} << 16) | 1;
        SendMessageW(static_cast<HWND>(frame->Editor()->GetHandle()), alt ? WM_SYSKEYUP : WM_KEYUP, nativeKey, flags);
        if (!received) throw std::runtime_error("native punctuation release must reach the intended editor");
    }
#else
    using Clock = std::chrono::steady_clock;
    auto* editor = frame->Editor();
    wxString focusFile;
    FocusEditor(frame, focusName, focusFile);
    bool received = false;
    const bool punctuation = key == '[' || key == ']' || key == ',' || key == '.';
    KeyRelease observer(key, received, releaseSignal, punctuation ? editor : nullptr);
    {
        wxFFile file(focusFile, "wb");
        const auto name = KeyName(key);
        file.Write(NativeTrace::Request(wxString("key:") + (control ? "ctrl+" : "") + (alt ? "alt+" : "") +
                                        (shift ? "shift+" : "") + name));
    }
    const auto keyDeadline = Clock::now() + std::chrono::seconds(3);
    while (!received && Clock::now() < keyDeadline) {
        wxYield();
        wxMilliSleep(10);
    }
    if (!received) throw std::runtime_error("native shortcut release must reach the owned editor or its dialog");
#endif
    wxYield();
}
} // namespace NativeKeys
