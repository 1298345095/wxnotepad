#pragma once

#include "native_search_helpers.h"
#include "native_trace_helpers.h"
#include <wx/eventfilter.h>
#include <wx/ffile.h>
#include <chrono>
#include <algorithm>

// Native shortcuts target the caller's editor on the existing private display.
namespace NativeKeys {
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
            const auto thread = GetCurrentThreadId();
            const auto foregroundThread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
            const bool attached =
                foregroundThread && foregroundThread != thread && AttachThreadInput(thread, foregroundThread, TRUE);
            BringWindowToTop(owned);
            SetForegroundWindow(owned);
            SetActiveWindow(owned);
            SetFocus(message.hwnd);
            if (attached) AttachThreadInput(thread, foregroundThread, FALSE);
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
#else
class KeyRelease final : public wxEventFilter {
public:
    KeyRelease(int key, bool& received, bool* signal) : key_(key), received_(received), signal_(signal) {
        wxEvtHandler::AddFilter(this);
    }
    ~KeyRelease() override { wxEvtHandler::RemoveFilter(this); }
    int FilterEvent(wxEvent& event) override {
        if (event.GetEventType() == wxEVT_KEY_UP && static_cast<wxKeyEvent&>(event).GetKeyCode() == key_) {
            received_ = true;
            if (signal_) *signal_ = true;
        }
        return Event_Skip;
    }

private:
    int key_;
    bool& received_;
    bool* signal_;
};
#endif
inline void Shortcut(EditorFrame* frame, int key, bool control, bool alt = false, bool shift = false,
                     bool* releaseSignal = nullptr, [[maybe_unused]] const wxString& focusName = "wxnotepad") {
    const NativeTrace::Gesture trace(frame->Editor(), key,
                                     (control ? wxMOD_CONTROL : 0) | (alt ? wxMOD_ALT : 0) | (shift ? wxMOD_SHIFT : 0));
#ifdef __WXMSW__
    const int nativeKey = key >= WXK_F1 && key <= WXK_F24 ? VK_F1 + key - WXK_F1 : key;
    NativeSearch::WindowsShortcut(frame->Editor(), nativeKey,
                                  {{VK_CONTROL, static_cast<BYTE>(control ? 0x80 : 0)},
                                   {VK_MENU, static_cast<BYTE>(alt ? 0x80 : 0)},
                                   {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)}});
#else
    using Clock = std::chrono::steady_clock;
    auto* editor = frame->Editor();
    editor->SetFocus();
    wxYield();
    wxString focusFile;
    if (!wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile))
        throw std::runtime_error("native shortcuts require a private display");
    {
        wxFFile file(focusFile, "wb");
        file.Write(NativeTrace::Request(wxString(frame->IsActive() ? "focus:" : "front:") + focusName));
    }
    bool acknowledged = false;
    const auto focusDeadline = Clock::now() + std::chrono::seconds(3);
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
    bool received = false;
    KeyRelease observer(key, received, releaseSignal);
    {
        wxFFile file(focusFile, "wb");
        const auto letter = static_cast<char>(key >= 'A' && key <= 'Z' ? key + 'a' - 'A' : key);
        const wxString name = key >= WXK_F1 && key <= WXK_F24 ? wxString::Format("F%d", key - WXK_F1 + 1)
                                                              : wxString::Format("%c", letter);
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
