#include "app/editor_frame.h"
#include "app/process_observer.h"
#include "diagnostics/print.h"
#include "editor/editor_control.h"
#include "file_helpers.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include <wx/ffile.h>
#include <wx/log.h>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#ifdef __WXMSW__
#include <io.h>
#else
#include <unistd.h>
#endif
#ifdef __WXGTK__
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename Predicate> void Await(Predicate ready, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do {
        wxYield();
        if (ready()) return;
        wxMilliSleep(10);
    } while (std::chrono::steady_clock::now() < deadline);
    Require(false, message);
}

class CapturedStderr final {
public:
    explicit CapturedStderr(const std::filesystem::path& path) : output_(TestFiles::Filename(path), "wb") {
        Require(output_.IsOpened(), "open the observer's private diagnostic capture");
        Require(std::fflush(stderr) == 0, "flush stderr before capturing the observer diagnostic");
        descriptor_ = Descriptor(stderr);
        saved_ = Duplicate(descriptor_);
        Require(saved_ >= 0, "retain stderr before capturing the observer diagnostic");
        if (!Redirect(Descriptor(output_.fp()), descriptor_)) {
            Close(saved_);
            saved_ = -1;
            throw std::runtime_error("redirect stderr to the observer's private diagnostic capture");
        }
    }
    ~CapturedStderr() {
        if (!Restore()) std::fputs("FAIL: restore stderr after the observer diagnostic capture\n", stderr);
    }
    CapturedStderr(const CapturedStderr&) = delete;
    CapturedStderr& operator=(const CapturedStderr&) = delete;
    CapturedStderr(CapturedStderr&&) = delete;
    CapturedStderr& operator=(CapturedStderr&&) = delete;

    bool Restore() noexcept {
        if (saved_ < 0) return true;
        const bool flushed = std::fflush(stderr) == 0;
        if (!Redirect(saved_, descriptor_)) return false;
        const bool closed = Close(saved_);
        saved_ = -1;
        std::clearerr(stderr);
        return flushed && closed;
    }

private:
    static int Descriptor(FILE* stream) noexcept {
#ifdef __WXMSW__
        return _fileno(stream);
#else
        return ::fileno(stream);
#endif
    }
    static int Duplicate(int descriptor) noexcept {
#ifdef __WXMSW__
        return _dup(descriptor);
#else
        return ::dup(descriptor);
#endif
    }
    static bool Redirect(int source, int destination) noexcept {
#ifdef __WXMSW__
        return _dup2(source, destination) == 0;
#else
        return ::dup2(source, destination) == destination;
#endif
    }
    static bool Close(int descriptor) noexcept {
#ifdef __WXMSW__
        return _close(descriptor) == 0;
#else
        return ::close(descriptor) == 0;
#endif
    }
    wxFFile output_;
    int descriptor_ = -1;
    int saved_ = -1;
};

class ObserverApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        settings_.values.selectOption &= ~SelectOption_CopyPasteBufferAsFindText;
        frame_ = new EditorFrame(&settings_);
        observer_ = std::make_unique<ProcessObserver>(*frame_, TestFiles::Filename(directory_.Path()));
        frame_->Show();
        Bind(wxEVT_IDLE, [this](wxIdleEvent& event) {
            event.Skip();
            if (!waitingForInitialState_) return;
            if (Sequence("sequence\t0\n")) {
                waitingForInitialState_ = false;
                try {
                    Run();
                } catch (const std::exception& error) {
                    DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                    Finish(1);
                }
            } else if (std::chrono::steady_clock::now() >= initialDeadline_) {
                waitingForInitialState_ = false;
                DiagnosticPrint::Print(stderr, "FAIL: initial native observation is published\n");
                Finish(1);
            } else {
                event.RequestMore();
            }
        });
        NativeKeys::AfterPresentation(frame_, [this](bool ready) {
            try {
                Require(ready, "owned observer frame is natively presented");
                // Let the observer's own CallAfter initialize publication and
                // its timer in the normal event loop before a synchronous wait.
                initialDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                waitingForInitialState_ = true;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                Finish(1);
            }
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    void Finish(int result) {
        result_ = result;
        observer_.reset();
        frame_->Destroy();
        ExitMainLoop();
    }
    bool Sequence(const char* sequence) const {
        const auto path = directory_.Path() / "state";
        return std::filesystem::exists(path) && TestFiles::Bytes(path).starts_with(sequence);
    }
    void Run() {
        auto* editor = frame_->Editor();
#ifdef __WXGTK__
        wxString focusFile;
        NativeKeys::FocusEditor(frame_, "wxnotepad", focusFile);
#else
        NativeKeys::Shortcut(frame_, 'A', true);
#endif
        Command(1);
        Await([this] { return Sequence("sequence\t1\n"); }, "warm unchanged-state acknowledgement is published");
        selection_ = editor->SelectionRevision();
        size_ = frame_->GetSize();
        active_ = frame_->IsActive();
#ifdef __WXGTK__
        auto* widget = GTK_WIDGET(frame_->GetHandle());
        const bool remap = GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget));
        // Round-trip GTK's client size so recreation retains the same CSD geometry.
        wxSize nativeSize;
        gtk_window_get_size(GTK_WINDOW(widget), &nativeSize.x, &nativeSize.y);
        if (remap) {
            frame_->Hide();
            gtk_widget_unrealize(widget);
            Require(!gtk_widget_get_window(widget), "native remapping starts without a presented GDK window");
        }
#endif
        Command(2);
        Await([this] { return !std::filesystem::exists(directory_.Path() / "command"); },
              "observer consumes the unchanged-state command");
#ifdef __WXGTK__
        if (remap) {
            Require(Sequence("sequence\t1\n"), "unpresented native frame does not publish partial observations");
            gtk_window_resize(GTK_WINDOW(widget), nativeSize.x, nativeSize.y);
            frame_->Show();
            // Return to GTK's normal loop until the new surface is mapped.
            NativeKeys::AfterPresentation(frame_, [this](bool ready) {
                try {
                    Require(ready, "remapped observer frame is natively presented");
                    Check();
                    Finish(0);
                } catch (const std::exception& error) {
                    DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                    Finish(1);
                }
            });
            return;
        }
#endif
        Check();
        Finish(0);
    }
    void Command(int sequence) {
        const auto pending = directory_.Path() / "pending";
        TestFiles::WriteBytes(pending, std::to_string(sequence) + "\nmenu\n" + std::to_string(wxID_SELECTALL) + "\n");
        std::filesystem::rename(pending, directory_.Path() / "command");
    }
    void CheckEditorState() const {
        using enum Scintilla::Message;
        auto* editor = frame_->Editor();
        Require(editor->Send(GetLength) == 0 && !editor->Send(GetModify) && !editor->Send(CanUndo) &&
                    editor->Send(GetCurrentPos) == 0 && editor->Send(GetAnchor) == 0 &&
                    editor->SelectionRevision() == selection_ && !editor->Send(GetReadOnly),
                "Select All on an empty document preserves every tracked editor state");
    }
    void CheckFailureRecovery() {
        const auto& root = directory_.Path();
        const auto previousState = TestFiles::Bytes(root / "state");
        const auto blockedPublication = root / "state.tmp";
        Require(std::filesystem::create_directory(blockedPublication), "block the next observation's temporary file");
        Command(3);
        Await([&root] { return !std::filesystem::exists(root / "command"); },
              "observer consumes the command while publication is blocked");
        Require(TestFiles::Bytes(root / "state") == previousState && Sequence("sequence\t2\n"),
                "failed temporary-file publication preserves the entire previous acknowledgement");
        Require(std::filesystem::remove(blockedPublication), "remove the owned publication obstruction");
        Await([this] { return Sequence("sequence\t3\n"); },
              "observer automatically retries the same acknowledgement after publication becomes writable");
        Require(TestFiles::Bytes(root / "state").find("result\tok\n") != std::string::npos,
                "publication recovery retains the successful native command result");

        const auto diagnostic = root / "command-failure.log";
        CapturedStderr capture(diagnostic);
        const auto malformedCommand = root / "command";
        TestFiles::WriteBytes(malformedCommand, "4\n");
        Await(
            [&diagnostic] {
                return TestFiles::Bytes(diagnostic)
                           .find("Observation command failed: stage=packet error=0 last_ack=3 ") != std::string::npos;
            },
            "observer diagnoses the actual malformed command packet");
        Require(TestFiles::Bytes(malformedCommand) == "4\n" && Sequence("sequence\t3\n"),
                "malformed command remains pending without replacing the last acknowledgement");
        Require(std::filesystem::remove(malformedCommand), "remove the observed malformed command");
        Command(4);
        Await([this] { return Sequence("sequence\t4\n"); },
              "observer accepts a valid native command after the malformed packet is removed");
        Require(TestFiles::Bytes(root / "state").find("result\tok\n") != std::string::npos,
                "command recovery publishes the successful native command result");
        Require(capture.Restore(), "restore stderr after observing command failure and recovery");
        CheckEditorState();
        std::puts("PASS: observer recovers from failed publication and malformed command packets");
    }
    void Check() {
        Await([this] { return frame_->IsActive() == active_ && frame_->GetSize() == size_; },
              "native remapping restores the unchanged active frame geometry");
        CheckEditorState();
        Await([this] { return Sequence("sequence\t2\n"); },
              "deferred unchanged-state command acknowledgement is retried after native presentation");
        Require(TestFiles::Bytes(directory_.Path() / "state").find("result\tok\n") != std::string::npos,
                "actual native command succeeds without editing the document");
        std::puts("PASS: native unchanged-state observer acknowledgement");
        CheckFailureRecovery();
    }
    TestFiles::TemporaryRoot directory_;
    SettingsStore settings_{TestFiles::Filename(directory_.Path() / "settings.ini")};
    EditorFrame* frame_ = nullptr;
    std::unique_ptr<ProcessObserver> observer_;
    uint64_t selection_ = 0;
    wxSize size_;
    bool active_ = false;
    bool waitingForInitialState_ = false;
    std::chrono::steady_clock::time_point initialDeadline_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(ObserverApp);
