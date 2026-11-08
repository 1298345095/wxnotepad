#include "app/editor_frame.h"
#include "app/new_window.h"
#include "editor/editor_control.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <wx/menu.h>
#include <wx/accel.h>
#include <wx/filename.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <memory>
#ifdef __WXMSW__
#include <windows.h>
#endif

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::filesystem::path Directory() {
    wxString path;
    Require(wxGetEnv("WXNOTEPAD_NEW_WINDOW_TEST", &path), "test directory must be configured");
#ifdef __WXMSW__
    return std::filesystem::path(path.ToStdWstring());
#else
    return std::filesystem::path(path.ToStdString(wxConvUTF8));
#endif
}
wxString Name(const std::filesystem::path& path) {
    return TestFiles::Filename(path);
}
template <class Ready> void Wait(Ready ready, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready() && std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(Directory() / "failure.txt"))
            throw std::runtime_error(TestFiles::Bytes(Directory() / "failure.txt"));
        wxYield();
        wxMilliSleep(10);
    }
    Require(ready(), message);
}
wxString Scenario() {
    wxString value;
    wxGetEnv("WXNOTEPAD_NEW_WINDOW_CASE", &value);
    return value;
}
void Child(EditorFrame* frame) {
    const auto scenario = Scenario();
    const bool maximized = scenario == "maximized";
    Wait([&] { return frame->IsMaximized() == maximized; }, "child must inherit maximized state");
    Require(frame->DocumentPath().empty(), "child must have no document identity");
    Require(frame->GetTitle() == "Untitled - wxnotepad", "child must be Untitled");
    Require(TestEditor::Text(frame).empty() && !frame->Editor()->Send(Message::GetModify), "child must start blank");
    Require(frame->DocumentEncoding() == FileEncoding::Utf8, "child must use startup encoding");
    Require(frame->Editor()->Send(Message::GetWrapMode) == 0, "child must load persisted settings");
    Require(wxGetCwd() == Name(Directory() / u8"startup working 中文"),
            "child must use the original startup working directory");
#ifdef __WXMSW__
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    Require(GetWindowPlacement(static_cast<HWND>(frame->GetHandle()), &placement), "child placement must be available");
    const auto& normal = placement.rcNormalPosition;
    Require(normal.right - normal.left == 640 && normal.bottom - normal.top == 480,
            "child must inherit normal window size");
    if (scenario == "edge") {
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        Require(GetMonitorInfo(MonitorFromRect(&normal, MONITOR_DEFAULTTONEAREST), &monitor),
                "child monitor must be available");
        Require(normal.left == monitor.rcMonitor.left && normal.top == monitor.rcMonitor.top,
                "a child crossing the work-area edge must wrap to the monitor origin");
    } else
        Require(normal.left == 110 && normal.top == 110, "child must use the original +10/+10 offset");
#else
    if (maximized) {
        frame->Restore();
        Wait([&] { return !frame->IsMaximized(); }, "child must restore independently");
    }
    Require(frame->GetSize() == wxSize(640, 480), "child must inherit normal window size");
#endif
    TestEditor::Append(frame, "independent child");
    Require(frame->SaveAs(Name(Directory() / (scenario.ToStdString() + "-child.txt"))).status == SaveStatus::Saved,
            "child must save independently");
    std::ofstream(Directory() / (scenario.ToStdString() + "-ready")) << wxGetProcessId() << '\n';
    frame->Close();
}
void Parent(EditorFrame* frame) {
    Require(frame->Open(Name(Directory() / "parent.txt")).status == OpenStatus::Opened, "parent must open fixture");
    TestEditor::Append(frame, "!");
    frame->Editor()->Send(Message::SetSel, 2, 5);
    // Unsaved preferences stay in this frame; the child loads the saved defaults.
    frame->Editor()->ToggleWordWrap();
    const auto before = TestEditor::Text(frame);
    const auto path = frame->DocumentPath();
    const auto encoding = frame->DocumentEncoding();
    const auto bom = frame->DocumentHasBom();
    const int item = frame->GetMenuBar()->FindMenuItem("File", "New Window");
    Require(item != wxNOT_FOUND, "File > New Window must exist");
    const auto* entry = frame->GetMenuBar()->GetMenu(0)->FindItemByPosition(1);
    Require(entry && entry->GetId() == item, "New Window must follow New in the native File menu");
    const std::unique_ptr<wxAcceleratorEntry> accelerator(entry->GetAccel());
    Require(accelerator && accelerator->GetFlags() == wxACCEL_ALT && accelerator->GetKeyCode() == 'N',
            "New Window must expose the native Alt+N accelerator");
    Require(wxSetWorkingDirectory(Name(Directory() / "changed working")),
            "test must change the parent's current directory");
    for (const wxString scenario : {"normal", "edge", "maximized"}) {
#ifndef __WXMSW__
        if (scenario == "edge") continue; // Linux leaves top-level placement to the compositor.
#endif
        wxSetEnv("WXNOTEPAD_NEW_WINDOW_CASE", scenario);
        frame->SetSize(100, 100, 640, 480);
#ifdef __WXMSW__
        if (scenario == "edge") {
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            Require(GetMonitorInfo(MonitorFromWindow(static_cast<HWND>(frame->GetHandle()), MONITOR_DEFAULTTONEAREST),
                                   &monitor),
                    "parent monitor must be available");
            frame->SetSize(monitor.rcWork.right - 640, monitor.rcWork.bottom - 480, 640, 480);
        }
#endif
        if (scenario == "maximized") {
            frame->Maximize();
            Wait([&] { return frame->IsMaximized(); }, "parent must maximize before relaunch");
        }
        wxYield();
        if (scenario == "maximized")
            NativeKeys::Shortcut(frame, 'N', false, true);
        else
            NativeSearch::Command(frame, item);
        const auto marker = Directory() / (scenario.ToStdString() + "-ready");
        Wait([&] { return std::filesystem::exists(marker); }, "a real blank child must finish its independent save");
        long childPid = 0;
        std::ifstream(marker) >> childPid;
        Require(childPid != wxGetProcessId(), "child must be a separate process");
        Wait([&] { return !wxProcess::Exists(static_cast<int>(childPid)); },
             "child must close while parent remains running");
    }
    Require(TestEditor::Text(frame) == before && frame->DocumentPath() == path &&
                frame->DocumentEncoding() == encoding && frame->DocumentHasBom() == bom &&
                frame->Editor()->Send(Message::GetModify),
            "parent document and dirty state must survive");
    TestEditor::RequireSelection(frame->Editor(), 2, 5, "parent selection must survive");
    const auto executable = wxStandardPaths::Get().GetExecutablePath();
#ifdef __WXMSW__
    const auto moved = executable + ".moved";
    Require(wxRenameFile(executable, moved), "test must temporarily move its copied executable");
    const bool launched = frame->NewWindow();
    const bool restored = wxRenameFile(moved, executable);
#else
    const auto binary = std::filesystem::path(executable.ToStdString(wxConvUTF8));
    const auto permissions = std::filesystem::status(binary).permissions();
    std::filesystem::permissions(binary, std::filesystem::perms::none);
    const bool launched = frame->NewWindow();
    std::filesystem::permissions(binary, permissions);
    const bool restored = true;
#endif
    Require(restored && !launched, "an unavailable executable must return launch failure");
    Require(TestEditor::Text(frame) == before && frame->DocumentPath() == path &&
                frame->DocumentEncoding() == encoding && frame->DocumentHasBom() == bom &&
                frame->Editor()->Send(Message::GetModify),
            "failed launch must retain the parent document");
    TestEditor::RequireSelection(frame->Editor(), 2, 5, "failed launch must retain parent selection");
    frame->Editor()->Send(Message::Undo);
    Require(!frame->Editor()->Send(Message::GetModify), "parent undo/save point must survive");
    frame->Close();
    std::puts("PASS: New Window launches a blank independent process and preserves the dirty parent");
}
} // namespace

class NewWindowTest final : public wxApp {
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 0;

public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        auto* frame = new EditorFrame(settings_.get());
        const bool child = argc > 1 && wxString(argv[1]) == "--window-position";
        if (child && (argc != 3 || !ApplyInitialWindowPosition(*frame, argv[2]))) return false;
        frame->Show();
        CallAfter([this, frame, child] {
            try {
                if (child) {
                    Require(argc == 3, "child must receive only its requested window position");
                    Child(frame);
                } else
                    Parent(frame);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                std::ofstream(Directory() / "failure.txt") << error.what() << '\n';
                result_ = 1;
                frame->Destroy();
            }
        });
        return true;
    }
    int OnExit() override { return result_; }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
};
wxIMPLEMENT_APP(NewWindowTest);
