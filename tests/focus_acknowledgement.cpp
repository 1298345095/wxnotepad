#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include <print>
#include <wx/log.h>
#include <thread>

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
