#include "editor/editor_control.h"
#include <array>
#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <wx/timer.h>
#include "file_command_driver.h"
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace {
void TracePrefix(const char* transition) {
    static const auto start = std::chrono::steady_clock::now();
    static unsigned sequence = 0;
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::fprintf(stderr, "CLOSE-LIFECYCLE %u +%lldms %s", ++sequence, static_cast<long long>(elapsed.count()),
                 transition);
}
void Trace(const char* transition) {
    TracePrefix(transition);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}
void TraceFrame(const char* transition, const EditorFrame* frame) {
    TracePrefix(transition);
    std::fprintf(stderr, " frame=%p\n", static_cast<const void*>(frame));
    std::fflush(stderr);
}
class FrameLifecycle {
public:
    EditorFrame* NewFrame() {
        // Each caller has finished its assertions and stopped its dialog driver.
        // Process deferred destruction before the next independent scenario; the
        // original Cancel scenario's still-live frame remains available.
        wxYield();
        if (completedDestructions_ != scheduledDestructions_)
            throw std::runtime_error("completed scenarios must destroy their deferred frames before the next one");
        auto* frame = new EditorFrame;
        frame->SetFileWatchingOptions({.mode = FileWatchingMode::None});
        TraceFrame("created", frame);
        frame->Bind(wxEVT_DESTROY, [this, frame](wxWindowDestroyEvent& event) {
            if (event.GetEventObject() == frame) {
                ++completedDestructions_;
                TraceFrame("destroy event", frame);
            }
            event.Skip();
        });
        return frame;
    }
    bool CloseFrame(EditorFrame* frame) {
        TraceFrame("close begin", frame);
        const bool closed = frame->Close();
        if (closed) ++scheduledDestructions_;
        TraceFrame(closed ? "close returned true" : "close returned false", frame);
        return closed;
    }
    void DestroyFrame(EditorFrame* frame) {
        TraceFrame("destroy requested", frame);
        ++scheduledDestructions_;
        frame->Destroy();
    }

private:
    unsigned scheduledDestructions_ = 0;
    unsigned completedDestructions_ = 0;
};
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
using TestEditor::Text;
using TestFiles::Bytes;
using TestFiles::Filename;
void Add(EditorFrame* frame, const std::string& text) {
    frame->Editor()->Send(Scintilla::Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
}
using NativeDialogs::FileCommandDriver;
using NativeDialogs::FileDialogProfile;

struct Sample {
    const char* name;
    std::string bytes;
    bool prompt;
    bool named = false;
};
void CheckWhitespaceTransition(FrameLifecycle& frames, const std::filesystem::path& directory,
                               const std::filesystem::path& source, const Sample& sample, const char* action) {
    auto* candidate = frames.NewFrame();
    if (sample.named)
        Require(candidate->SaveFirst(Filename(directory / "named-whitespace.txt")).status == SaveStatus::Saved,
                "named whitespace fixture must save");
    if (sample.bytes.empty()) {
        Add(candidate, "x");
        candidate->Editor()->Send(Scintilla::Message::ClearAll);
    } else
        Add(candidate, sample.bytes);
    const auto path = candidate->DocumentPath();
    Require(candidate->Editor()->Send(Scintilla::Message::GetModify), "sample must be modified");
    FileCommandDriver driver(FileDialogProfile::Save, wxID_CANCEL, {}, Trace);
    const bool closing = std::string_view(action) == "Close";
    const bool opening = std::string_view(action) == "Open";
    const bool completed = closing   ? frames.CloseFrame(candidate)
                           : opening ? candidate->Open(Filename(source)).status == OpenStatus::Opened
                                     : candidate->NewDocument();
    driver.Stop();
    if (completed == sample.prompt || driver.prompts != (sample.prompt ? 1 : 0))
        throw std::runtime_error(std::string(action) + " prompt mismatch: " + sample.name);
    if (sample.prompt) {
        Require(Text(candidate) == sample.bytes && candidate->DocumentPath() == path &&
                    candidate->Editor()->Send(Scintilla::Message::GetModify) &&
                    candidate->Editor()->Send(Scintilla::Message::CanUndo),
                "Cancel must preserve bytes, path, dirty state and history");
    } else if (!closing) {
        Require(Text(candidate) == (opening ? "opened" : "") &&
                    !candidate->Editor()->Send(Scintilla::Message::GetModify),
                "successful replacement must establish the new document");
    }
    if (!closing || sample.prompt) frames.DestroyFrame(candidate);
}
void CheckWhitespacePrompts(FrameLifecycle& frames, const std::filesystem::path& directory) {
    const auto source = directory / "open-whitespace.txt";
    std::ofstream(source, std::ios::binary) << "opened";
    const std::array<Sample, 13> samples = {{
        {"2047 spaces", std::string(2047, ' '), false},
        {"2048 spaces", std::string(2048, ' '), true},
        {"tabs", "\t\t", false},
        {"line endings", "\r\n\n\r", false},
        {"vertical tab and form feed", "\v\f", false},
        {"mixed ASCII whitespace", " \t\r\n\v\f", false},
        {"empty after editing", "", false},
        {"non-whitespace", " \tx\r\n", true},
        {"embedded NUL", std::string(" \0 ", 3), true},
        {"non-breaking space", "\x{c2}\x{a0}", true},
        {"Unicode em space", "\x{e2}\x{80}\x{83}", true},
        {"named whitespace", " \t\r\n", true, true},
        {"named empty after editing", "", true, true},
    }};
    for (const auto& sample : samples) {
        for (const auto* action : {"New", "Open", "Close"}) {
            CheckWhitespaceTransition(frames, directory, source, sample, action);
        }
        std::printf("PASS: New/Open/Close whitespace rule: %s\n", sample.name);
    }

    auto* explicitSave = frames.NewFrame();
    const std::string whitespace = " \t\r\n\v\f";
    Add(explicitSave, whitespace);
    Require(explicitSave->Open(std::nullopt).status == OpenStatus::Cancelled &&
                explicitSave->Open(Filename(directory / "missing.txt")).status == OpenStatus::Failed &&
                Text(explicitSave) == whitespace && explicitSave->DocumentPath().empty() &&
                explicitSave->Editor()->Send(Scintilla::Message::GetModify),
            "cancelled and failed Open must preserve short whitespace-only Untitled");
    FileCommandDriver cancelSave(FileDialogProfile::Save, wxID_CANCEL, {}, Trace);
    Require(explicitSave->Save().status == SaveStatus::Cancelled, "explicit Save must still open its chooser");
    cancelSave.Stop();
    Require(cancelSave.choosers == 1 && cancelSave.prompts == 0 && Text(explicitSave) == whitespace &&
                explicitSave->DocumentPath().empty() && explicitSave->Editor()->Send(Scintilla::Message::GetModify),
            "cancelled explicit Save must retain whitespace and dirty state");
    Require(explicitSave->SaveFirst(Filename(directory / "missing" / "failure.txt")).status == SaveStatus::Failed &&
                Text(explicitSave) == whitespace && explicitSave->DocumentPath().empty() &&
                explicitSave->Editor()->Send(Scintilla::Message::GetModify),
            "failed explicit Save must retain whitespace and dirty state");
    const auto firstPath = directory / "explicit-whitespace.txt";
    FileCommandDriver saveDriver(FileDialogProfile::Save, wxID_YES, Filename(firstPath), Trace);
    Require(explicitSave->Save().status == SaveStatus::Saved, "explicit Save must write short whitespace Untitled");
    saveDriver.Stop();
    Require(saveDriver.choosers == 1 && Bytes(firstPath) == whitespace,
            "explicit Save must preserve exact whitespace bytes on disk");
    const auto secondPath = directory / "explicit-whitespace-copy.txt";
    FileCommandDriver saveAsDriver(FileDialogProfile::Save, wxID_YES, Filename(secondPath), Trace);
    Require(explicitSave->SaveAs().status == SaveStatus::Saved, "explicit Save As must write short whitespace");
    saveAsDriver.Stop();
    Require(saveAsDriver.choosers == 1 && Bytes(secondPath) == whitespace &&
                !explicitSave->Editor()->Send(Scintilla::Message::GetModify),
            "explicit Save As must preserve exact whitespace bytes and establish its save point");
    frames.DestroyFrame(explicitSave);
    std::puts("PASS: explicit whitespace Save/Save As and cancelled/failed operation state");
}

class CloseApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        Trace("OnInit");
        SetExitOnFrameDelete(false);
        auto* frame = frames_.NewFrame();
        CallAfter([this, frame] {
            try {
                Add(frame, "Unsaved 中文\r\n");
                FileCommandDriver driver(FileDialogProfile::Save, wxID_CANCEL, {}, Trace);
                Require(!frames_.CloseFrame(frame), "Cancel must veto the actual close event");
                driver.Stop();
                Require(driver.prompts == 1, "dirty close must show the native confirmation");
                Require(!frame->IsBeingDeleted(), "Cancel must retain the window");
                Require(Text(frame) == "Unsaved 中文\r\n" && frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Cancel must retain text and dirty state");
                Require(frame->DocumentPath().empty(), "Cancel must retain unnamed identity");
                Add(frame, "continued");
                Require(Text(frame) == "Unsaved 中文\r\ncontinued", "Cancel must allow continued editing");
                std::puts("PASS: close Cancel retains document and window");
                const auto directory = TestFiles::TemporaryDirectory("wxnotepad-close");
                std::filesystem::create_directory(directory);
                CheckWhitespacePrompts(frames_, directory);
                auto* saved = frames_.NewFrame();
                const auto path = directory / "known.txt";
                Add(saved, "first");
                Require(saved->SaveFirst(Filename(path)).status == SaveStatus::Saved, "named-close setup must save");
                Add(saved, " 中文\r\n");
                FileCommandDriver saveDriver(FileDialogProfile::Save, wxID_YES, {}, Trace);
                Require(frames_.CloseFrame(saved), "successful Save must allow closing");
                saveDriver.Stop();
                Require(saveDriver.prompts == 1, "named dirty document must show native confirmation");
                Require(Bytes(path) == "first \x{e4}\x{b8}\x{ad}\x{e6}\x{96}\x{87}\r\n",
                        "close Save must write changes to the known path");
                std::puts("PASS: close Save writes changes to known path");
                auto* clean = frames_.NewFrame();
                FileCommandDriver cleanDriver(FileDialogProfile::Save, wxID_CANCEL, {}, Trace);
                Require(frames_.CloseFrame(clean), "unmodified document must close directly");
                cleanDriver.Stop();
                Require(cleanDriver.prompts == 0, "unmodified close must not show a confirmation");
                std::puts("PASS: unmodified document closes directly");
                auto* discard = frames_.NewFrame();
                const auto discardPath = directory / "discard.txt";
                Add(discard, "original bytes");
                Require(discard->SaveFirst(Filename(discardPath)).status == SaveStatus::Saved,
                        "discard setup must save");
                Add(discard, " changed");
                FileCommandDriver discardDriver(FileDialogProfile::Save, wxID_NO, {}, Trace);
                Require(frames_.CloseFrame(discard), "Discard must allow closing");
                discardDriver.Stop();
                Require(discardDriver.prompts == 1 && Bytes(discardPath) == "original bytes",
                        "Discard must prompt and leave disk unchanged");
                std::puts("PASS: close Discard does not write changes");
                auto* cancelledSave = frames_.NewFrame();
                Add(cancelledSave, "pending");
                FileCommandDriver cancelledDriver(FileDialogProfile::Save, wxID_YES, {}, Trace);
                Require(!frames_.CloseFrame(cancelledSave), "cancelling the Save chooser must veto closing");
                cancelledDriver.Stop();
                Require(cancelledDriver.prompts == 1 && cancelledDriver.choosers == 1,
                        "close Save must show native confirmation and chooser");
                Require(Text(cancelledSave) == "pending" && cancelledSave->DocumentPath().empty() &&
                            cancelledSave->Editor()->Send(Scintilla::Message::GetModify),
                        "cancelled Save must retain text, identity and dirty state");
                Add(cancelledSave, " continued");
                Require(Text(cancelledSave) == "pending continued", "cancelled Save must allow continued editing");
                frames_.DestroyFrame(cancelledSave);
                std::puts("PASS: cancelled close Save retains the editable document");
                auto* firstSave = frames_.NewFrame();
                const auto firstPath = directory / "first-close.txt";
                Add(firstSave, "First 中文😀\r\n");
                FileCommandDriver firstDriver(FileDialogProfile::Save, wxID_YES, Filename(firstPath), Trace);
                Require(frames_.CloseFrame(firstSave), "successful first Save must allow closing");
                firstDriver.Stop();
                Require(firstDriver.prompts == 1 && firstDriver.choosers == 1,
                        "first close Save must use the native confirmation and path chooser");
                Require(Bytes(firstPath) == "First \x{e4}\x{b8}\x{ad}\x{e6}\x{96}\x{87}\x{f0}\x{9f}\x{98}\x{80}\r\n",
                        "first close Save must preserve exact UTF-8 bytes without BOM");
                std::puts("PASS: first close Save uses the native path chooser");
                auto* failedSave = frames_.NewFrame();
                const auto failedPath = directory / "failure.txt";
                Add(failedSave, "original");
                Require(failedSave->SaveFirst(Filename(failedPath)).status == SaveStatus::Saved,
                        "failed-close setup must save");
                Add(failedSave, " changed");
                std::filesystem::remove(failedPath);
                std::filesystem::create_directory(failedPath);
                {
                    std::ofstream(failedPath / "sentinel", std::ios::binary) << "keep";
                }
                FileCommandDriver failedDriver(FileDialogProfile::Save, wxID_YES, {}, Trace);
                Require(!frames_.CloseFrame(failedSave), "failed Save must veto closing");
                failedDriver.Stop();
                Require(failedDriver.prompts == 1 && failedDriver.errors == 1 && failedDriver.choosers == 0,
                        "failed known-path Save must show its error without another path chooser");
                Require(Text(failedSave) == "original changed" && failedSave->DocumentPath() == Filename(failedPath) &&
                            failedSave->Editor()->Send(Scintilla::Message::GetModify),
                        "failed Save must retain text, identity and dirty state");
                Require(Bytes(failedPath / "sentinel") == "keep", "failed close Save must preserve the target");
                failedSave->Editor()->Send(Scintilla::Message::Undo);
                Require(Text(failedSave) == "original" && !failedSave->Editor()->Send(Scintilla::Message::GetModify),
                        "failed close Save must retain history and the previous save point");
                frames_.DestroyFrame(failedSave);
                std::filesystem::remove_all(directory);
                std::puts("PASS: failed close Save retains document, identity and history");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            if (!frame->IsBeingDeleted()) frames_.DestroyFrame(frame);
            Trace("ExitMainLoop requested");
            ExitMainLoop();
        });
        return true;
    }
    int OnExit() override {
        Trace("OnExit");
        return result_;
    }
    int OnRun() override {
        Trace("OnRun enter");
        wxApp::OnRun();
        Trace("OnRun returned");
        return result_;
    }
    ~CloseApp() override { Trace("app destructor"); }

private:
    FrameLifecycle frames_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(CloseApp);
