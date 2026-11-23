#include "app/file_change_monitor.h"
#include "file_helpers.h"
#include "steady_stopwatch.h"
#include <wx/wx.h>
#include <cstdio>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr int WatchRefreshCount = 1024;
constexpr int DecisionDeadlineMs = 6000;
constexpr int TimerIntervalMs = 10;
constexpr int StoppedObservationMs = 100;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <typename Predicate> void Wait(Predicate predicate) {
    TestClock::StopWatch elapsed;
    while (!predicate() && elapsed.Time() < DecisionDeadlineMs) {
        wxYield();
        wxMilliSleep(TimerIntervalMs);
    }
    Require(predicate(), "the refreshed native watch must still observe changes");
}

void CheckMonitorLifetime(wxFrame& owner) {
    const auto directory = TestFiles::TemporaryDirectory("watch-lifetime");
    std::filesystem::create_directory(directory);
    const auto path = directory / "document.txt";
    const auto signal = directory / "notifications.txt";
    TestFiles::WriteBytes(path, "initial");
    FileWatchingOptions options{FileWatchingMode::AutoReload, TimerIntervalMs, TimerIntervalMs};
    int decisions = 0;
    FileChangeMonitor monitor(owner, [&] {
        ++decisions;
        return true;
    });
    std::jthread writer([&](std::stop_token stop) {
        int notification = 0;
        while (!stop.stop_requested())
            TestFiles::WriteBytes(signal, std::to_string(notification++));
    });

    // Refresh and Stop run on the UI thread while directory completions run on
    // wxMSW's worker. Repeated changes also retire earlier watches for this path.
    for (int refresh = 0; refresh < WatchRefreshCount; ++refresh) {
        monitor.Refresh(TestFiles::Filename(path), options);
    }
    TestFiles::WriteBytes(path, "after repeated refresh");
    Wait([&] { return decisions == 1; });
    Require(TestFiles::Bytes(path) == "after repeated refresh", "native notification preserves the written file");

    monitor.Refresh(TestFiles::Filename(path), options);
    TestFiles::WriteBytes(path, "stopped");
    monitor.Stop();
    TestClock::StopWatch elapsed;
    while (elapsed.Time() < StoppedObservationMs) {
        wxYield();
        wxMilliSleep(TimerIntervalMs);
    }
    Require(decisions == 1, "Stop cancels a pending decision while native writes are completing");

    monitor.Refresh(TestFiles::Filename(path), options);
    TestFiles::WriteBytes(path, "after restart");
    Wait([&] { return decisions == 2; });
    monitor.Refresh(TestFiles::Filename(path), options);
    TestFiles::WriteBytes(signal, "destroy while directory completion is pending");
    std::puts("PASS: repeated native watch refresh, pending Stop, restart and pending destruction");
}

class MonitorLifetimeApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        CallAfter([this] {
            auto* owner = new wxFrame(nullptr, wxID_ANY, "Native watch lifetime");
            try {
                CheckMonitorLifetime(*owner);
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
            }
            owner->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace

wxIMPLEMENT_APP(MonitorLifetimeApp);
