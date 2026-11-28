#pragma once

#include "native_search_helpers.h"
#include <print>
#include "native_focus_helpers.h"
#include "native_trace_helpers.h"
#include <wx/eventfilter.h>
#include <wx/ffile.h>
#include <wx/timer.h>
#include <wx/weakref.h>
#include <chrono>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>
#ifdef __WXGTK__
#include <gdk/gdkwayland.h>
#include <gdk/gdkx.h>
#include <X11/extensions/XInput2.h>
#undef BadAlloc
#undef None
#undef Status
#include <wayland-client-core.h>
#endif

// Native shortcuts target the caller's editor on the existing private display.
namespace NativeKeys {
inline constexpr auto FocusAcknowledgementTimeout = std::chrono::seconds(3);
inline void DispatchFocusEvents([[maybe_unused]] wxWindow* window) {
    NativeSearch::DispatchEvents(window);
}
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
    case '"':
        return '\'';
    default:
        return key;
    }
}
inline void AfterPresentation(wxTopLevelWindow* window, std::function<void(bool)> callback) {
#ifdef __WXGTK__
    auto* widget = GTK_WIDGET(window->GetHandle());
    if (GDK_IS_X11_DISPLAY(gtk_widget_get_display(widget))) {
        class Preparation final : public wxTimer {
        public:
            Preparation(wxTopLevelWindow* window, std::function<void(bool)> callback)
                : window_(window), callback_(std::move(callback)),
                  deadline_(std::chrono::steady_clock::now() + std::chrono::seconds(3)) {
                auto* widget = GTK_WIDGET(window->GetHandle());
                g_weak_ref_init(&native_, G_OBJECT(widget));
                eventHandler_ = g_signal_connect(widget, "event-after",
                                                 G_CALLBACK(+[](GtkWidget* widget, GdkEvent* event, gpointer data) {
                                                     if (event->any.window != gtk_widget_get_window(widget) ||
                                                         (event->type != GDK_MAP && event->type != GDK_CONFIGURE))
                                                         return;
                                                     auto& preparation = *static_cast<Preparation*>(data);
                                                     preparation.paintedFrame_ = -1;
                                                     preparation.ObserveClock(widget);
                                                     gtk_widget_queue_draw(widget);
                                                 }),
                                                 this);
                ObserveClock(widget);
                gtk_widget_queue_draw(widget);
            }
            ~Preparation() override {
                if (auto* widget = g_weak_ref_get(&native_)) {
                    g_signal_handler_disconnect(widget, eventHandler_);
                    g_object_unref(widget);
                }
                g_weak_ref_clear(&native_);
                if (clock_) {
                    g_signal_handler_disconnect(clock_, paintHandler_);
                    g_object_unref(clock_);
                }
            }
            void StartWaiting(std::shared_ptr<Preparation> pending) {
                pending_ = std::move(pending);
                Start(10);
            }
            void Notify() override {
                const bool ready = Presented();
                if (!ready && window_ && std::chrono::steady_clock::now() < deadline_) return;
                Stop();
                auto completed = std::move(pending_);
                auto callback = std::move(callback_);
                callback(ready);
            }

        private:
            void ObserveClock(GtkWidget* widget) {
                auto* window = gtk_widget_get_window(widget);
                if (clock_ || !window) return;
                clock_ = gdk_window_get_frame_clock(window);
                if (!clock_) return;
                g_object_ref(clock_);
                paintHandler_ =
                    g_signal_connect(clock_, "after-paint", G_CALLBACK(+[](GdkFrameClock* clock, gpointer data) {
                                         static_cast<Preparation*>(data)->paintedFrame_ =
                                             gdk_frame_clock_get_frame_counter(clock);
                                     }),
                                     this);
            }
            bool Presented() {
                if (!window_ || paintedFrame_ < 0) return false;
                auto* object = g_weak_ref_get(&native_);
                if (!object) return false;
                auto* widget = GTK_WIDGET(object);
                auto* native = gtk_widget_get_window(widget);
                bool ready = native && gtk_widget_get_mapped(widget) &&
                             !(gdk_window_get_state(native) & GDK_WINDOW_STATE_WITHDRAWN);
                if (ready) {
                    GdkRectangle outer{};
                    gdk_window_get_frame_extents(native, &outer);
                    // Map and wx's shown flag precede the final GTK allocation.
                    // Frames must also agree with their wx geometry. Stock native
                    // dialogs expose the GTK widget without maintaining that rectangle.
                    const bool frameReady = !window_->IsKindOf(wxCLASSINFO(wxFrame)) ||
                                            window_->GetRect() == wxRect(outer.x, outer.y, outer.width, outer.height);
                    ready = frameReady && gtk_widget_get_allocated_width(widget) == gdk_window_get_width(native) &&
                            gtk_widget_get_allocated_height(widget) == gdk_window_get_height(native);
                }
                g_object_unref(object);
                return ready;
            }
            wxWeakRef<wxTopLevelWindow> window_;
            std::function<void(bool)> callback_;
            std::chrono::steady_clock::time_point deadline_;
            GWeakRef native_{};
            GdkFrameClock* clock_ = nullptr;
            gulong eventHandler_ = 0;
            gulong paintHandler_ = 0;
            gint64 paintedFrame_ = -1;
            std::shared_ptr<Preparation> pending_;
        };
        auto preparation = std::make_shared<Preparation>(window, std::move(callback));
        preparation->StartWaiting(preparation);
        return;
    }
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

#ifdef __WXGTK__
// A shown flag precedes X11 MapNotify. Keep each shortcut's newly shown owned
// windows observed until GTK and wx have both finished handling that event.
class ShortcutPresentation final : public wxEventFilter {
public:
    explicit ShortcutPresentation(EditorFrame* frame)
        : owner_(frame), enabled_(GDK_IS_X11_DISPLAY(gtk_widget_get_display(GTK_WIDGET(frame->GetHandle())))) {
        if (!enabled_) return;
        for (auto* window : wxTopLevelWindows)
            Watch(window, window->IsShown());
        wxEvtHandler::AddFilter(this);
    }
    ~ShortcutPresentation() override {
        if (enabled_) wxEvtHandler::RemoveFilter(this);
    }
    bool Complete() const {
        return std::all_of(windows_.begin(), windows_.end(), [](const auto& entry) { return entry->Complete(); });
    }
    int FilterEvent(wxEvent& event) override {
        if (event.GetEventType() == wxEVT_CREATE || event.GetEventType() == wxEVT_SHOW) {
            auto* window = wxDynamicCast(event.GetEventObject(), wxWindow);
            if (auto* entry = Watch(window); entry && event.GetEventType() == wxEVT_SHOW)
                entry->Shown(static_cast<wxShowEvent&>(event).IsShown());
        }
        return Event_Skip;
    }

private:
    struct Window {
        Window(wxWindow* value, bool initiallyShown) : window(value), acknowledgedEpoch(initiallyShown ? 1 : 0) {
            auto* widget = GTK_WIDGET(value->GetHandle());
            g_weak_ref_init(&native, G_OBJECT(widget));
            // event-after runs after the widget's map handlers, including wx's
            // GTKHandleMapped and GTK's late-map handling.
            handler = g_signal_connect(widget, "event-after", G_CALLBACK(AfterEvent), this);
        }
        ~Window() {
            if (auto* widget = g_weak_ref_get(&native)) {
                g_signal_handler_disconnect(widget, handler);
                g_object_unref(widget);
            }
            g_weak_ref_clear(&native);
        }
        void Shown(bool shown) {
            if (shown)
                ++showEpoch;
            else
                acknowledgedEpoch = showEpoch;
        }
        bool Complete() const { return showEpoch == acknowledgedEpoch || !window || !window->IsShown(); }
        static void AfterEvent(GtkWidget* widget, GdkEvent* event, gpointer data) {
            if (event->type == GDK_MAP && event->any.window == gtk_widget_get_window(widget)) {
                auto& entry = *static_cast<Window*>(data);
                entry.acknowledgedEpoch = entry.showEpoch;
            }
        }
        wxWeakRef<wxWindow> window;
        GWeakRef native{};
        gulong handler = 0;
        // Deferred X11 Show sets IsShown before it sends wxEVT_SHOW. A window
        // first seen hidden or newly created must still await that first map.
        std::size_t showEpoch = 1;
        std::size_t acknowledgedEpoch = 0;
    };
    Window* Watch(wxWindow* window, bool initiallyShown = false) {
        if (!window || !window->IsTopLevel() || !window->GetHandle()) return nullptr;
        bool owned = false;
        for (auto* parent = window; parent; parent = parent->GetParent())
            if (parent == owner_.get()) {
                owned = true;
                break;
            }
        if (!owned) return nullptr;
        for (const auto& entry : windows_)
            if (entry->window.get() == window) return entry.get();
        windows_.push_back(std::make_unique<Window>(window, initiallyShown));
        return windows_.back().get();
    }
    wxWeakRef<wxWindow> owner_;
    bool enabled_;
    std::vector<std::unique_ptr<Window>> windows_;
};
#endif

class KeyReleaseObserver final : public wxEventFilter {
public:
    struct Observation {
        const char* phase = "none";
        int key = WXK_NONE;
        int modifiers = wxMOD_NONE;
        wxObject* target = nullptr;
    };

    KeyReleaseObserver(int key, bool& received, bool* signal, wxWindow* intended = nullptr)
        : key_(key), received_(received), signal_(signal), intended_(intended) {
        wxEvtHandler::AddFilter(this);
    }
    ~KeyReleaseObserver() override { wxEvtHandler::RemoveFilter(this); }
    const Observation& LastKeyEvent() const { return lastKeyEvent_; }
    int FilterEvent(wxEvent& event) override {
        if (event.GetEventType() == wxEVT_KEY_DOWN || event.GetEventType() == wxEVT_KEY_UP) {
            const auto& key = static_cast<wxKeyEvent&>(event);
            lastKeyEvent_ = {event.GetEventType() == wxEVT_KEY_UP ? "up" : "down", key.GetKeyCode(), key.GetModifiers(),
                             event.GetEventObject()};
        }
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
    Observation lastKeyEvent_;
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
            HWND observedFocus = nullptr, observedForeground = nullptr;
            bool observedShown = false, observedFocusOwned = false;
            const auto ownsInput = [&] {
                observedFocus = GetFocus();
                observedShown = dialog->IsShown();
                observedForeground = GetForegroundWindow();
                observedFocusOwned = observedFocus == owned || IsChild(owned, observedFocus);
                return observedShown && observedForeground == owned && observedFocusOwned;
            };
            while (!ownsInput() && std::chrono::steady_clock::now() < until) {
                wxYield();
                wxMilliSleep(10);
            }
            if (!ownsInput()) {
                DWORD foregroundProcess = 0;
                const auto foregroundThread = GetWindowThreadProcessId(observedForeground, &foregroundProcess);
                GUITHREADINFO foregroundInfo{sizeof(GUITHREADINFO)};
                const bool foregroundInfoRead = GetGUIThreadInfo(foregroundThread, &foregroundInfo);
                std::fprintf(stderr,
                             "Native Escape ownership: shown=%d foregroundOwned=%d focusOwned=%d owned=%p "
                             "target=%p foreground=%p focus=%p active=%p visible=%d enabled=%d process=%lu "
                             "thread=%lu foregroundProcess=%lu foregroundThread=%lu foregroundInfoRead=%d "
                             "foregroundActive=%p foregroundFocus=%p\n",
                             observedShown, observedForeground == owned, observedFocusOwned, static_cast<void*>(owned),
                             static_cast<void*>(message.hwnd), static_cast<void*>(observedForeground),
                             static_cast<void*>(observedFocus), static_cast<void*>(GetActiveWindow()),
                             IsWindowVisible(owned), IsWindowEnabled(owned), GetCurrentProcessId(),
                             GetCurrentThreadId(), foregroundProcess, foregroundThread, foregroundInfoRead,
                             static_cast<void*>(foregroundInfo.hwndActive),
                             static_cast<void*>(foregroundInfo.hwndFocus));
                std::fflush(stderr);
                throw std::runtime_error("native Escape requires the owned dialog on the input desktop");
            }
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
#ifdef __WXGTK__
inline void ObserveShortcutReleaseFailure(EditorFrame* frame, const wxString& focusFile, const wxString& request,
                                          int expectedKey, bool received,
                                          const KeyReleaseObserver::Observation& lastEvent) {
    const auto failedAt = g_get_monotonic_time();
    wxString pending;
    wxFFile observed(focusFile, "rb");
    const bool read = observed.IsOpened() && observed.ReadAll(&pending);
    const auto snapshot = wxString::Format(
        "mono_us=%lld\tevent=driver.key-release-failure\townedPid=%lu\trequest_kind=%s"
        "\tcached_pending_source=last-write\tcached_pending_kind=%s\texpected_key=%d\trelease_received=%d"
        "\tfresh_pending_read=%d\tfresh_pending_empty=%d\tfresh_pending_kind=%s\tpending_read_mono_us=%lld"
        "\tlast_key_phase=%s\tlast_key_code=%d\tlast_modifiers=%d\tlast_target=%p\tcurrent_focus=%p"
        "\towned_editor=%p\tframeActive=%d\teditorFocus=%d\n",
        static_cast<long long>(failedAt), wxGetProcessId(), request.BeforeFirst(':'), request.BeforeFirst(':'),
        expectedKey, received, read, pending.empty(), pending.BeforeFirst(':'),
        static_cast<long long>(g_get_monotonic_time()), lastEvent.phase, lastEvent.key, lastEvent.modifiers,
        static_cast<void*>(lastEvent.target), static_cast<void*>(wxWindow::FindFocus()),
        static_cast<void*>(frame->Editor()), frame->IsActive(), frame->Editor()->HasFocus());
    std::print(stderr, "Native shortcut release failure: {}", snapshot.utf8_str().data());
    std::fflush(stderr);
    bool written = false;
    {
        wxFFile marker(focusFile + ".failure.tmp", "wb");
        written = marker.IsOpened() && marker.Write(snapshot);
    }
    if (!written || !wxRenameFile(focusFile + ".failure.tmp", focusFile + ".failure", true))
        std::fputs("Could not publish native shortcut release failure snapshot\n", stderr);
}

inline void ObserveFocusFailure(EditorFrame* frame, GtkWidget* widget, const wxString& focusFile,
                                const wxString& intent, const wxString& lastPending, bool acknowledged,
                                unsigned int surfaceId, long long deadline) {
    const auto failedAt = g_get_monotonic_time();
    auto* editor = frame->Editor();
    if (!surfaceId && GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget))) {
        auto* surface = gdk_wayland_window_get_wl_surface(gtk_widget_get_window(widget));
        if (surface) surfaceId = wl_proxy_get_id(reinterpret_cast<wl_proxy*>(surface));
    }
    std::println(stderr,
                 "Native activation failure: request={} acknowledged={:d} frameActive={:d} editorFocus={:d} "
                 "ownedPid={:d} ownedSurface={:d}",
                 intent.BeforeFirst(':').utf8_str().data(), acknowledged, frame->IsActive(), editor->HasFocus(),
                 wxGetProcessId(), surfaceId);
    std::fflush(stderr);
    unsigned long long gesture = 0;
#ifdef WXNOTEPAD_DIAGNOSTICS
    gesture = DiagnosticTrace::Gesture();
#endif
    wxString failurePending;
    wxFFile observed(focusFile, "rb");
    const bool failureRead = observed.IsOpened() && observed.ReadAll(&failurePending);
    const auto snapshot = wxString::Format(
        "mono_us=%lld\tevent=driver.wait-failure\tgesture=%llu\trequest_kind=%s\tpending_kind=%s"
        "\tacknowledged=%d\tframeActive=%d\teditorFocus=%d\townedPid=%lu\townedSurface=%u"
        "\tfailure_pending_read=%d\tfailure_pending_empty=%d\tfailure_pending_kind=%s"
        "\tpending_read_mono_us=%lld\tdeadline_mono_us=%lld\tintent=%s\tlast_pending=%s\tfailure_pending=%s"
        "\tframe_title=%s\n",
        static_cast<long long>(failedAt), gesture, intent.BeforeFirst(':'), lastPending.BeforeFirst(':'), acknowledged,
        frame->IsActive(), editor->HasFocus(), wxGetProcessId(), surfaceId, failureRead, failurePending.empty(),
        failurePending.BeforeFirst(':'), static_cast<long long>(g_get_monotonic_time()), deadline, intent, lastPending,
        failurePending, frame->GetTitle());
    bool written = false;
    {
        wxFFile marker(focusFile + ".failure.tmp", "wb");
        written = marker.IsOpened() && marker.Write(snapshot);
    }
    if (!written || !wxRenameFile(focusFile + ".failure.tmp", focusFile + ".failure", true))
        std::fputs("Could not publish native focus failure snapshot\n", stderr);
}
#endif
inline void FocusEditor(EditorFrame* frame, const wxString& focusName, wxString& focusFile,
                        const std::function<void(const wxString&)>& requestPublished = {}) {
    using Clock = std::chrono::steady_clock;
    auto* editor = frame->Editor();
    editor->SetFocus();
#ifdef __WXGTK__
    auto* widget = GTK_WIDGET(frame->GetHandle());
    auto* display = gtk_widget_get_display(widget);
    const bool wayland = GDK_IS_WAYLAND_DISPLAY(display);
    auto* nativeDisplay = wayland ? nullptr : gdk_x11_display_get_xdisplay(display);
    int inputOpcode = 0;
    if (!wayland) {
        int firstEvent = 0;
        int firstError = 0;
        XQueryExtension(nativeDisplay, "XInputExtension", &inputOpcode, &firstEvent, &firstError);
    }
#endif
    DispatchFocusEvents(frame);
    if (!wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile))
        throw std::runtime_error("native shortcuts require a private display");
    const auto focusDeadline = Clock::now() + FocusAcknowledgementTimeout;
    const bool active = frame->IsActive();
    wxString intent = wxString(active ? "focus:" : "front:") + focusName;
#ifdef __WXGTK__
    unsigned int surfaceId = 0;
    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(focusDeadline - Clock::now()).count();
    const auto expiry = static_cast<long long>(g_get_monotonic_time() + remaining);
    if (active) {
        intent = wxString::Format("focus-until:%lld:", expiry) + focusName;
    } else if (wayland) {
        auto* surface = gdk_wayland_window_get_wl_surface(gtk_widget_get_window(widget));
        if (!surface) throw std::runtime_error("native shortcuts require the owned Wayland surface");
        surfaceId = wl_proxy_get_id(reinterpret_cast<wl_proxy*>(surface));
        intent = wxString::Format("activate:%lu:%u:%lld", wxGetProcessId(), surfaceId, expiry);
    } else {
        intent = wxString::Format("front-until:%lld:", expiry) + focusName;
    }
#endif
    {
        wxFFile file(focusFile, "wb");
        file.Write(NativeTrace::Request(intent));
    }
    if (requestPublished) requestPublished(intent);
    bool acknowledged = false;
    wxString lastPending;
    const auto observeAcknowledgement = [&] {
        wxString pending;
        {
            wxFFile file(focusFile, "rb");
            file.ReadAll(&pending);
        }
        lastPending = pending;
        acknowledged = pending.empty();
    };
    bool nativeFocusSettled = true;
#ifdef __WXGTK__
    bool nativeFocusSynchronized = wayland;
    nativeFocusSettled = wayland;
    const auto settleNativeFocus = [&] {
        if (!acknowledged || nativeFocusSettled) return;
        if (!nativeFocusSynchronized) {
            // The driver ACK covers its X11 connection. Round-trip this GUI
            // connection before trusting focus flags from before the request.
            gdk_display_sync(gtk_widget_get_display(widget));
            nativeFocusSynchronized = true;
            // Flush deferred wx focus changes in one finite dispatch, also
            // when the final ACK refresh follows an overlong GUI callback.
            DispatchFocusEvents(frame);
        }
        const auto pendingFocus = [&] {
            using Event = std::unique_ptr<GdkEvent, decltype(&gdk_event_free)>;
            std::vector<Event> events;
            bool focusPending = false;
            // Only this GUI thread consumes these queues. Peek does not fetch
            // native input; while its event exists, get_event cannot fetch it
            // either. Thus this loop only removes the existing GDK prefix.
            while (auto* peek = gdk_display_peek_event(display)) {
                gdk_event_free(peek);
                auto* event = gdk_display_get_event(display);
                events.emplace_back(event, gdk_event_free);
                focusPending |= event->type == GDK_FOCUS_CHANGE;
            }
            for (const auto& event : events)
                gdk_display_put_event(display, event.get());
            struct NativePrefix {
                int remaining;
                int inputOpcode;
                bool focusPending = false;
            } prefix{XEventsQueued(nativeDisplay, QueuedAlready), inputOpcode};
            if (prefix.remaining) {
                XEvent peek{};
                // The Nth event is already queued. Match it regardless of
                // focus, so this read-only peek cannot wait for newer input.
                XPeekIfEvent(
                    nativeDisplay, &peek,
                    +[](Display*, XEvent* event, XPointer data) -> Bool {
                        auto* prefix = reinterpret_cast<NativePrefix*>(data);
                        // Inspect headers only; normal Xlib/GDK dispatch retains cookie ownership.
                        prefix->focusPending |=
                            event->type == FocusIn || event->type == FocusOut ||
                            (event->type == GenericEvent && event->xcookie.extension == prefix->inputOpcode &&
                             (event->xcookie.evtype == XI_FocusIn || event->xcookie.evtype == XI_FocusOut));
                        return --prefix->remaining == 0;
                    },
                    reinterpret_cast<XPointer>(&prefix));
            }
            return focusPending || prefix.focusPending;
        };
        nativeFocusSettled = !pendingFocus();
        while (!nativeFocusSettled && Clock::now() < focusDeadline) {
            DispatchFocusEvents(frame);
            nativeFocusSettled = !pendingFocus();
        }
    };
#endif
    while (Clock::now() < focusDeadline) {
        observeAcknowledgement();
#ifdef __WXGTK__
        settleNativeFocus();
#endif
        if (acknowledged && nativeFocusSettled && frame->IsActive() && editor->HasFocus()) break;
        DispatchFocusEvents(frame);
        wxMilliSleep(10);
    }
#ifdef __WXGTK__
    // A GUI callback can outlast polling even when the driver claimed on time.
    // These requests are deadline-gated by the driver, so a fresh read cannot
    // turn a claim after the original deadline into a successful wait.
    if (!acknowledged) observeAcknowledgement();
    settleNativeFocus();
#endif
    if (!acknowledged || !nativeFocusSettled || !frame->IsActive() || !editor->HasFocus()) {
#ifdef __WXGTK__
        ObserveFocusFailure(frame, widget, focusFile, intent, lastPending, acknowledged, surfaceId, expiry);
#endif
        throw std::runtime_error("private native input must activate the owned editor");
    }
}
inline wxString KeyName(int key) {
    if (key >= WXK_F1 && key <= WXK_F24) return wxString::Format("F%d", key - WXK_F1 + 1);
    switch (key) {
    case ' ':
        return "space";
    case WXK_TAB:
        return "Tab";
    case WXK_ESCAPE:
        return "Escape";
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
    case '\'':
        return "apostrophe";
    default:
        const auto letter = static_cast<char>(key >= 'A' && key <= 'Z' ? key + 'a' - 'A' : key);
        return wxString::Format("%c", letter);
    }
}
#endif
inline void Shortcut(EditorFrame* frame, int key, bool control, bool alt = false, bool shift = false,
                     bool* releaseSignal = nullptr, [[maybe_unused]] const wxString& focusName = "wxnotepad",
                     [[maybe_unused]] std::chrono::steady_clock::time_point* releaseDeadline = nullptr) {
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
        KeyReleaseObserver observer(key, received, releaseSignal, frame->Editor());
        const LPARAM flags = (LPARAM{KF_UP | KF_REPEAT | (alt ? KF_ALTDOWN : 0)} << 16) | 1;
        SendMessageW(static_cast<HWND>(frame->Editor()->GetHandle()), alt ? WM_SYSKEYUP : WM_KEYUP, nativeKey, flags);
        if (!received) throw std::runtime_error("native punctuation release must reach the intended editor");
    }
#else
    using Clock = std::chrono::steady_clock;
#ifdef __WXGTK__
    const bool x11 = GDK_IS_X11_DISPLAY(gtk_widget_get_display(GTK_WIDGET(frame->GetHandle())));
    ShortcutPresentation presentation(frame);
#endif
    auto* editor = frame->Editor();
    wxString focusFile;
    FocusEditor(frame, focusName, focusFile);
    bool received = false;
    const bool punctuation = key == '[' || key == ']' || key == ',' || key == '.';
    KeyReleaseObserver observer(key, received, releaseSignal, punctuation ? editor : nullptr);
    wxString request;
    {
        wxFFile file(focusFile, "wb");
        const auto name = KeyName(key);
        request = wxString("key:") + (control ? "ctrl+" : "") + (alt ? "alt+" : "") + (shift ? "shift+" : "") + name;
        wxString delay;
        if (wxGetEnv("WXNOTEPAD_NATIVE_KEY_DELAY_MS", &delay)) request = "key-delay:" + delay + ":" + request.Mid(4);
        file.Write(NativeTrace::Request(request));
    }
    const auto keyDeadline = Clock::now() + std::chrono::seconds(3);
    if (releaseDeadline) *releaseDeadline = keyDeadline;
#ifdef __WXGTK__
    NativeSearch::OpeningShortcut openingShortcut(received, keyDeadline);
#endif
    while (!received && Clock::now() < keyDeadline) {
        // Let native priorities deliver the release before forcing idle work.
#ifdef __WXGTK__
        if (x11)
            NativeSearch::DispatchX11Iteration();
        else
#endif
            NativeSearch::DispatchEvents(frame);
        wxMilliSleep(10);
    }
    if (!received) {
#ifdef __WXGTK__
        ObserveShortcutReleaseFailure(frame, focusFile, request, key, received, observer.LastKeyEvent());
#endif
        throw std::runtime_error("native shortcut release must reach the owned editor or its dialog");
    }
#endif
    NativeSearch::DispatchEvents(frame);
#ifdef __WXGTK__
    while (!presentation.Complete() && Clock::now() < keyDeadline) {
        NativeSearch::DispatchEvents(frame);
        wxMilliSleep(10);
    }
    if (!presentation.Complete())
        throw std::runtime_error("native shortcut must finish mapping its owned shown windows");
#endif
}
} // namespace NativeKeys
