#pragma once
#include "diagnostics/longline_probe.h"

#include "editor/editor_control.h"
#include "editor_test_helpers.h"
#include "app/editor_frame.h"
#include "native_dialog_helpers.h"
#include "diagnostics/trace.h"
#include <wx/wx.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/modalhook.h>
#include <wx/timer.h>
#include <wx/weakref.h>
#include <chrono>
#include <functional>
#include <algorithm>
#include <cstdio>
#include <print>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#ifdef __WXGTK__
#include <gdk/gdkwayland.h>
#endif

// Native operations target only widgets owned by each independent search test.
// Test cases and expected document bytes remain in their own GUI processes.
namespace NativeSearch {
inline void DispatchEvents([[maybe_unused]] wxWindow* window) {
#ifdef __WXGTK__
    // Wayland operations also settle compositor surface transitions. Preserve
    // their existing drain while X11 returns between native iterations.
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(GTK_WIDGET(window->GetHandle())))) {
        wxYield();
        return;
    }
    gtk_main_iteration_do(FALSE);
#if wxUSE_EXCEPTIONS
    // Native callbacks store exceptions until control returns to C++.
    wxTheApp->RethrowStoredException();
#endif
    wxTheApp->ProcessPendingEvents();
    wxTheApp->ProcessIdle();
#else
    wxYield();
#endif
}
inline void Click(wxWindow* dialog, int id);
#ifdef __WXGTK__
// The modal response observes the opening shortcut's real key-up and deadline
// only while that stack scope is alive.
class OpeningShortcut final : public wxTrackable {
public:
    using Clock = std::chrono::steady_clock;
    OpeningShortcut(const bool& released, Clock::time_point deadline)
        : released_(released), deadline_(deadline), previous_(current_) {
        current_ = this;
    }
    ~OpeningShortcut() { current_ = previous_; }
    OpeningShortcut(const OpeningShortcut&) = delete;
    OpeningShortcut& operator=(const OpeningShortcut&) = delete;
    static OpeningShortcut* Current() { return current_; }
    bool Released() const { return released_; }
    Clock::time_point Deadline() const { return deadline_; }

private:
    const bool& released_;
    Clock::time_point deadline_;
    OpeningShortcut* previous_;
    inline static thread_local OpeningShortcut* current_ = nullptr;
};

class FeedbackResponse final : public wxEvtHandler {
public:
    FeedbackResponse() { Bind(wxEVT_TIMER, &FeedbackResponse::OnTimer, this, timer_.GetId()); }
    ~FeedbackResponse() override { timer_.Stop(); }
    void Respond(wxDialog* dialog, int response, std::function<void()> beforeClick = {}) {
        timer_.Stop();
        dialog_ = dialog;
        response_ = response;
        opening_ = OpeningShortcut::Current();
        hadOpening_ = opening_.get() != nullptr;
        beforeClick_ = std::move(beforeClick);
        Schedule();
    }
    void Cancel(wxDialog* dialog) {
        if (dialog_.get() != dialog) return;
        timer_.Stop();
        dialog_.Release();
        opening_.Release();
        hadOpening_ = false;
        beforeClick_ = {};
    }

private:
    void Schedule() {
        // Native timers can run inside ShowModal even while paint keeps wx's
        // lower-priority pending-callback idle source from running.
        if (!timer_.StartOnce(10)) {
            Cancel(dialog_.get());
            throw std::runtime_error("schedule the native search feedback response");
        }
        LonglineProbe::Event(LonglineProbe::Kind::FeedbackQueued, 0,
            reinterpret_cast<std::intptr_t>(dialog_.get()), 1,
            !opening_ || opening_->Released());
    }
    void OnTimer(wxTimerEvent&) {
        auto* dialog = dialog_.get();
        LonglineProbe::Event(LonglineProbe::Kind::FeedbackFired, 0,
            reinterpret_cast<std::intptr_t>(dialog), !opening_ || opening_->Released(),
            dialog && dialog->IsModal(), opening_ ?
                std::chrono::duration_cast<std::chrono::microseconds>(
                    opening_->Deadline() - OpeningShortcut::Clock::now()).count() : -1);
        // A later modal hook can preempt ShowModal before its exit guard exists.
        if (!dialog || !dialog->IsModal() || (hadOpening_ && !opening_)) {
            Cancel(dialog);
            return;
        }
        if (opening_ && !opening_->Released()) {
            if (OpeningShortcut::Clock::now() >= opening_->Deadline())
                throw std::runtime_error("search feedback must receive its opening shortcut's native release");
            Schedule();
            return;
        }
        const int response = response_;
        auto beforeClick = std::move(beforeClick_);
        Cancel(dialog);
        if (beforeClick) beforeClick();
        Click(dialog, response);
    }
    wxTimer timer_{this};
    wxWeakRef<wxDialog> dialog_;
    int response_ = wxID_OK;
    wxWeakRef<OpeningShortcut> opening_;
    bool hadOpening_ = false;
    std::function<void()> beforeClick_;
};
#endif
// Document-result fixtures accept feedback at the native modal boundary. The
// search_wrap fixture separately drives the real choices and suppression state.
class AcceptFeedback final : public wxModalDialogHook {
public:
    AcceptFeedback() { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetName() == "Search feedback") {
            LonglineProbe::Event(LonglineProbe::Kind::FeedbackEnter, 0,
                reinterpret_cast<std::intptr_t>(dialog), dialog->IsShown(), dialog->IsModal());
#ifdef __WXGTK__
            response_.Respond(dialog, wxID_OK);
#else
            wxTheApp->CallAfter([dialog] {
                LonglineProbe::Event(LonglineProbe::Kind::FeedbackFired, 0,
                    reinterpret_cast<std::intptr_t>(dialog), -1, dialog->IsModal(), -1);
                Click(dialog, wxID_OK);
            });
            LonglineProbe::Event(LonglineProbe::Kind::FeedbackQueued, 0,
                reinterpret_cast<std::intptr_t>(dialog), 0);
#endif
        }
        return wxID_NONE;
    }
#ifdef __WXGTK__
    void Exit(wxDialog* dialog) override {
        LonglineProbe::Event(LonglineProbe::Kind::FeedbackExit, 0,
            reinterpret_cast<std::intptr_t>(dialog));
        response_.Cancel(dialog);
    }

private:
    FeedbackResponse response_;
#endif
};
#ifdef __WXMSW__
inline bool WindowsShortcut(wxWindow* window, int key, std::initializer_list<std::pair<int, BYTE>> modifiers) {
    BYTE previous[256];
    if (!GetKeyboardState(previous)) throw std::runtime_error("must read this test thread's keyboard state");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    for (const auto& [modifier, state] : modifiers)
        modified[modifier] = state;
    if (!SetKeyboardState(modified)) throw std::runtime_error("must set this test thread's shortcut modifiers");
    WXNOTE_TRACE("driver.ack", "\ttarget=%p\tkey=%d", static_cast<void*>(window), key);
    MSG message{};
    message.hwnd = static_cast<HWND>(window->GetHandle());
    const bool alt = (modified[VK_MENU] & 0x80) != 0;
    message.message = alt ? WM_SYSKEYDOWN : WM_KEYDOWN;
    message.wParam = key;
    message.lParam = alt ? (LPARAM{KF_ALTDOWN} << 16) : 0;
    const bool handled = wxGetTopLevelParent(window)->MSWTranslateMessage(&message);
    WXNOTE_TRACE("driver.accelerator-result", "\ttarget=%p\tkey=%d\thandled=%d", static_cast<void*>(window), key,
                 handled);
    SetKeyboardState(previous);
    return handled;
}
#endif
inline void ActivateCommand(EditorFrame* frame, int id) {
    auto* item = frame->GetMenuBar()->FindItem(id);
    if (!item) throw std::runtime_error("native menu command must exist");
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(frame->GetHandle()), WM_COMMAND, MAKEWPARAM(id, 0), 0);
#elif defined(__WXGTK__)
    gtk_menu_item_activate(GTK_MENU_ITEM(item->GetMenuItem()));
#else
    wxCommandEvent command(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(command);
#endif
}
inline void Command(EditorFrame* frame, int id) {
    // Opening a native menu refreshes command enablement before dispatch. Do
    // the same when sending a command directly to this test's owned frame.
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    ActivateCommand(frame, id);
    wxYield();
}
inline void RevealAllFolds(EditorFrame* frame) {
    using enum Scintilla::Message;
    const auto* bar = frame->GetMenuBar();
    const int id = bar->FindMenuItem("View", "All Levels");
    const auto allExpanded = [editor = frame->Editor()] {
        for (intptr_t line = 0; line < editor->Send(GetLineCount); ++line)
            if (!editor->Send(GetFoldExpanded, line) || !editor->Send(GetLineVisible, line)) return false;
        return true;
    };
    Command(frame, id);
    if (!allExpanded()) Command(frame, id);
    if (!allExpanded()) throw std::runtime_error("native All Levels must reveal all folds within two toggles");
}
inline void Toggle(wxCheckBox* option, bool checked) {
    if (option->GetValue() == checked) return;
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(option->GetHandle());
    SendMessageW(handle, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(static_cast<HWND>(option->GetParent()->GetHandle()), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(handle), BN_CLICKED), reinterpret_cast<LPARAM>(handle));
#elif defined(__WXGTK__)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(option->GetHandle()), checked);
#endif
    wxYield();
}
inline void Click(wxWindow* dialog, int id) {
    const LonglineProbe::Scope clickTiming(LonglineProbe::Kind::FeedbackClickBegin,
        LonglineProbe::Kind::FeedbackClickEnd, reinterpret_cast<std::intptr_t>(dialog),
        id, dialog->IsShown(), dialog->GetName() == "Search feedback");
    auto* button = wxDynamicCast(dialog->FindWindow(id), wxButton);
    if (!button) throw std::runtime_error("native dialog button must exist");
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(button->GetHandle());
    // Notify the owned dialog directly: BM_CLICK requires activating an inactive
    // dialog, which would steal desktop focus.
    SendMessageW(static_cast<HWND>(dialog->GetHandle()), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(handle), BN_CLICKED),
                 reinterpret_cast<LPARAM>(handle));
#elif defined(__WXGTK__)
    gtk_button_clicked(GTK_BUTTON(button->GetHandle()));
#endif
    DispatchEvents(dialog);
}
inline void Selection(EditorControl* editor, intptr_t start, intptr_t end, const char* message) {
    const auto actualStart = editor->Send(Scintilla::Message::GetSelectionStart);
    const auto actualEnd = editor->Send(Scintilla::Message::GetSelectionEnd);
    if (actualStart != start || actualEnd != end)
        std::println(stderr, "Range: {:d}..{:d}; expected {:d}..{:d}", static_cast<long long>(actualStart),
                     static_cast<long long>(actualEnd), static_cast<long long>(start), static_cast<long long>(end));
    if (actualStart != start || actualEnd != end) throw std::runtime_error(message);
}
using TestEditor::Text;
inline void NativeF3(wxWindow* window) {
#ifdef __WXMSW__
    PostMessageW(static_cast<HWND>(window->GetHandle()), WM_KEYDOWN, VK_F3, 0);
    PostMessageW(static_cast<HWND>(window->GetHandle()), WM_KEYUP, VK_F3, 0);
#elif defined(__WXGTK__)
    auto* widget = window->GetConnectWidget();
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_F3;
    event->key.state = static_cast<GdkModifierType>(0);
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
} // namespace NativeSearch
