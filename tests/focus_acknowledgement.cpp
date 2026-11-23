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
}
void EarlyAcknowledgement(EditorFrame* frame) {
    wxString focus;
    NativeKeys::FocusEditor(frame, frame->GetTitle(), focus);
    const auto observed = std::make_shared<BusyObservation>();
    std::jthread worker([observed, focus](std::stop_token stop) {
        while (!stop.stop_requested()) {
            const auto request = Pending(focus);
            if (request.StartsWith("focus:") || request.StartsWith("focus-until:")) {
                const auto expiry =
                    request.StartsWith("focus-until:")
                        ? std::stoll(request.AfterFirst(':').ToStdString())
                        : g_get_monotonic_time() + std::chrono::duration_cast<std::chrono::microseconds>(
                                                       NativeKeys::FocusAcknowledgementTimeout)
                                                       .count();
                wxTheApp->CallAfter([observed, focus, expiry] { HoldGui(observed, focus, expiry); });
                return;
            }
            std::this_thread::sleep_for(PollInterval);
        }
    });
    NativeKeys::FocusEditor(frame, frame->GetTitle(), focus);
    worker.request_stop();
    worker.join();
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
