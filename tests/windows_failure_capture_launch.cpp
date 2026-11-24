#include "file_helpers.h"
#include "windows_failure_capture.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

class CaptureLaunchApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        try {
            Require(argc == 2, "capture regression receives the original Python executable path");
            Require(wxFileName(wxString::FromUTF8(NATIVE_FAILURE_PYTHON)).SameAs(wxFileName(argv[1])),
                    "compiled capture Python path preserves the configured executable");
            wxFrame frame(nullptr, wxID_ANY, "owned failure capture regression");
            const auto directory = TestFiles::TemporaryDirectory("failure-capture-launch");
            std::filesystem::create_directory(directory);
            const auto previous = wxGetCwd();
            Require(wxSetWorkingDirectory(TestFiles::Filename(directory)), "capture owns a fresh evidence directory");
            NativeFailure::Capture("launch-regression", {static_cast<long>(wxGetProcessId())});
            Require(wxSetWorkingDirectory(previous), "restore the native capture working directory");
            const auto output = directory / "Testing/Temporary/failure-evidence" /
                                ("launch-regression-" + std::to_string(wxGetProcessId())) / "windows.json";
            Require(!TestFiles::Bytes(output).empty(),
                    "real Python capture helper writes owned native window evidence");
            std::puts("PASS: configured Python path launches the real native failure capture helper");
            result_ = 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: %s\n", error.what());
        }
        return true;
    }
    int OnRun() override { return result_; }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(CaptureLaunchApp);
