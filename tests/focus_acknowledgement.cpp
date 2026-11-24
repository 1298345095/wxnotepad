#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include <print>
#include <wx/log.h>
#include <thread>
#include <array>
#include <algorithm>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto PollInterval = std::chrono::milliseconds(1);
constexpr auto GuiReturnMargin = std::chrono::milliseconds(300);

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
wxString Pending(const wxString& path) {
    wxString contents;
    wxFFile file(path, "rb");
    Require(file.IsOpened() && file.ReadAll(&contents), "read the owned native focus request");
    return contents;
}
struct BusyObservation {
    bool entered = false;
    bool completed = false;
    bool acknowledgedBeforeExpiry = false;
};
void HoldGui(const std::shared_ptr<BusyObservation>& observed, const wxString& path, long long expiry) {
    observed->entered = true;
    while (!Pending(path).empty() && g_get_monotonic_time() < expiry)
        std::this_thread::sleep_for(PollInterval);
    observed->acknowledgedBeforeExpiry = Pending(path).empty() && g_get_monotonic_time() < expiry;
    const auto margin = std::chrono::duration_cast<std::chrono::microseconds>(GuiReturnMargin).count();
    while (g_get_monotonic_time() < expiry + margin)
        std::this_thread::sleep_for(PollInterval);
    observed->completed = true;
    std::println(
        stderr, "Busy GUI callback: entered={} completed={} acknowledged_before_expiry={} return_after_expiry_us={}",
        observed->entered, observed->completed, observed->acknowledgedBeforeExpiry, g_get_monotonic_time() - expiry);
    std::fflush(stderr);
}
void QueuedFocusAcknowledgement(EditorFrame* frame) {
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(GTK_WIDGET(frame->GetHandle())))) return;
    auto* editor = frame->Editor();
    int focusLosses = 0;
    wxEvtHandler focusEvents;
    focusEvents.Bind(wxEVT_KILL_FOCUS, [&focusLosses](wxFocusEvent& event) {
        ++focusLosses;
        event.Skip();
    });
    editor->PushEventHandler(&focusEvents);
    struct RemoveHandler {
        EditorControl* editor;
        ~RemoveHandler() { editor->PopEventHandler(false); }
    } removeHandler{editor};
    wxString focus;
    NativeKeys::FocusEditor(frame, frame->GetTitle(), focus, [&focus](const wxString& request) {
        Require(request.StartsWith("focus-until:"), "the presented editor begins with native focus");
        const auto expiry = std::stoll(request.AfterFirst(':').ToStdString());
        // Let the real driver finish while this GUI callback leaves its native
        // focus events queued. No GTK event is consumed before the ACK exists.
        while (!Pending(focus).empty() && g_get_monotonic_time() < expiry)
            std::this_thread::yield();
        Require(Pending(focus).empty() && g_get_monotonic_time() < expiry,
                "the actual native focus operation completes within its original deadline");
    });
    const auto lossesAtReturn = focusLosses;
    Require(frame->IsActive() && editor->HasFocus() && editor->Send(Scintilla::Message::GetFocus),
            "the acknowledged native focus operation returns with the editor focused");
    NativePaint::Complete(editor);
    Require(focusLosses == lossesAtReturn,
            "the acknowledged native focus operation cannot deliver a delayed focus loss after return");
    Require(frame->IsActive() && editor->HasFocus() && editor->Send(Scintilla::Message::GetFocus),
            "the editor retains native and core focus through subsequent real paints");
}
void EarlyAcknowledgement(EditorFrame* frame) {
    wxString focus;
    NativeKeys::FocusEditor(frame, frame->GetTitle(), focus);
    // Finish initial presentation/activation before the separate timed overlap.
    wxYield();
    Require(frame->IsActive() && frame->Editor()->HasFocus(), "warmup settles actual native activation and focus");
    const auto observed = std::make_shared<BusyObservation>();
    NativeKeys::FocusEditor(frame, frame->GetTitle(), focus, [observed, &focus](const wxString& request) {
        Require(request.StartsWith("focus-until:"), "the already-active editor publishes its original focus deadline");
        const auto expiry = std::stoll(request.AfterFirst(':').ToStdString());
        // Queue and enter the real GUI callback after publication, while the
        // private driver can acknowledge independently. It must run before a
        // fast acknowledgement can let FocusEditor return without exercising it.
        wxTheApp->CallAfter([observed, focus, expiry] { HoldGui(observed, focus, expiry); });
        wxTheApp->ProcessPendingEvents();
    });
    Require(observed->entered && observed->completed && observed->acknowledgedBeforeExpiry,
            "the GUI callback outlasts the wait after a driver acknowledgement within its original deadline");
    Require(frame->IsActive() && frame->Editor()->HasFocus(), "the owned editor retains actual native focus");
}
void PendingNativeAcknowledgement(EditorFrame* frame) {
    auto* widget = GTK_WIDGET(frame->GetHandle());
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget))) return;
    auto* window = gtk_widget_get_window(widget);
    const auto events = gdk_window_get_events(window);
    struct Notifications {
        std::array<GdkAtom, 2> properties{gdk_atom_intern_static_string("_WXNOTEPAD_FOCUS_ACK_FIRST"),
                                          gdk_atom_intern_static_string("_WXNOTEPAD_FOCUS_ACK_SECOND")};
        std::vector<GdkAtom> delivered;
    } notifications;
    gdk_window_set_events(window, static_cast<GdkEventMask>(events | GDK_PROPERTY_CHANGE_MASK));
    const auto handler = g_signal_connect(
        widget, "property-notify-event",
        G_CALLBACK(+[](GtkWidget*, GdkEventProperty* event, gpointer data) -> gboolean {
            auto* notifications = static_cast<Notifications*>(data);
            if (std::ranges::find(notifications->properties, event->atom) != notifications->properties.end())
                notifications->delivered.push_back(event->atom);
            return FALSE;
        }),
        &notifications);
    struct RestoreProperties {
        GtkWidget* widget;
        GdkWindow* window;
        gulong handler;
        Notifications& notifications;
        GdkEventMask events;
        ~RestoreProperties() {
            g_signal_handler_disconnect(widget, handler);
            for (const auto property : notifications.properties)
                gdk_property_delete(window, property);
            gdk_window_set_events(window, events);
        }
    } restore{widget, window, handler, notifications, events};
    wxString focus;
    const auto observed = std::make_shared<BusyObservation>();
    NativeKeys::FocusEditor(
        frame, frame->GetTitle(), focus, [observed, &focus, window, &notifications](const wxString& request) {
            Require(request.StartsWith("focus-until:"), "native backlog preserves the active editor request");
            const auto expiry = std::stoll(request.AfterFirst(':').ToStdString());
            HoldGui(observed, focus, expiry);
            // Ask the real server for two unrelated notifications after the GUI
            // callback outlasts the original deadline. Focus itself is unchanged.
            constexpr int PropertyFormatBits = 8;
            constexpr unsigned char marker[] = "owned focus notification";
            for (const auto property : notifications.properties)
                gdk_property_change(window, property, GDK_SELECTION_TYPE_STRING, PropertyFormatBits,
                                    GDK_PROP_MODE_REPLACE, marker, sizeof(marker));
        });
    Require(observed->entered && observed->completed && observed->acknowledgedBeforeExpiry,
            "native backlog retains the original on-time ACK and late GUI callback");
    Require(frame->IsActive() && frame->Editor()->HasFocus() && frame->Editor()->Send(Scintilla::Message::GetFocus),
            "unrelated ready native notifications cannot reject acknowledged editor focus");
    NativePaint::Complete(frame->Editor());
    Require(notifications.delivered ==
                std::vector<GdkAtom>(notifications.properties.begin(), notifications.properties.end()),
            "owned native notifications retain their original delivery order");
}
void ExpiredAcknowledgement(EditorFrame* frame) {
    wxString focus;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focus), "use the private input driver");
    const auto identity = wxString::Format("%lu", wxGetProcessId());
    const auto request = "trace:" + identity + ":focus-until:0:" + frame->GetTitle();
    {
        wxFFile file(focus, "wb");
        Require(file.IsOpened() && file.Write(request) && file.Close(), "publish an expired owned focus request");
    }
    const auto observed = [&] {
        return wxFileExists(focus + ".diagnostics") &&
               Pending(focus + ".diagnostics").Contains("\tevent=driver.expired\tgesture=" + identity + "\t");
    };
    const auto until = Clock::now() + NativeKeys::FocusAcknowledgementTimeout;
    while (!observed() && Clock::now() < until) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(observed(), "the actual driver observes and rejects the expired request");
    Require(Pending(focus) == request, "an expired focus request must remain unacknowledged");
}
class FocusApp final : public wxApp {
    int result_ = 1;

public:
    bool OnInit() override {
        SetExitOnFrameDelete(false);
        wxLog::SetActiveTarget(new wxLogStderr);
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "the private display presents the actual editor frame");
                QueuedFocusAcknowledgement(frame);
                PendingNativeAcknowledgement(frame);
                EarlyAcknowledgement(frame);
                ExpiredAcknowledgement(frame);
                result_ = 0;
                std::puts("PASS: early focus acknowledgement survives a busy GUI; expired requests stay rejected");
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
};
} // namespace
wxIMPLEMENT_APP(FocusApp);
