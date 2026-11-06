#pragma once

#include "native_search_helpers.h"
#include <wx/eventfilter.h>
#include <wx/ffile.h>
#include <chrono>

// Native shortcuts target the caller's editor on the existing private display.
namespace NativeKeys {
#ifndef __WXMSW__
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
                     bool* releaseSignal = nullptr) {
#ifdef __WXMSW__
    NativeSearch::WindowsShortcut(frame->Editor(), key,
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
        file.Write(frame->IsActive() ? "focus:wxnotepad" : "front:wxnotepad");
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
        file.Write(wxString("key:") + (control ? "ctrl+" : "") + (alt ? "alt+" : "") + (shift ? "shift+" : "") +
                   wxString::Format("%c", letter));
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
