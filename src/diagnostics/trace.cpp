#include "diagnostics/trace.h"
#include <wx/eventfilter.h>
#include <wx/window.h>
#include <wx/version.h>
#include <wx/utils.h>
#include <wx/wxcrt.h>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#ifdef __WXMSW__
#include <windows.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace DiagnosticTrace {
namespace {
using Clock = std::chrono::steady_clock;
// Native callbacks share an identity without modifying their event payloads.
// Only Begin/EndGesture write the current thread's callback-chain identity.
thread_local std::uint64_t gesture = 0;
// Concurrent driver threads must receive distinct process-local sequences.
std::atomic<std::uint64_t> nextGesture{0};

unsigned long Process() noexcept {
#ifdef __WXMSW__
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}
unsigned long Thread() noexcept {
#ifdef __WXMSW__
    return GetCurrentThreadId();
#else
    return static_cast<unsigned long>(syscall(SYS_gettid));
#endif
}

class Trace final : public wxEventFilter {
public:
    Trace() noexcept {
        wxString enabled;
        if (!wxGetEnv("WXNOTEPAD_DIAGNOSTICS", &enabled) || enabled != "1") return;
        wxString destination;
        if (!wxGetEnv("WXNOTEPAD_DIAGNOSTIC_LOG", &destination) || destination.empty()) return;
        // These existing wrappers retain native Unicode paths and C FILE output.
        if (destination == "stderr")
            file_ = stderr;
        else {
            file_ = wxFopen(destination, "wb");
            if (!file_) {
                std::fputs("Could not open native diagnostic log\n", stderr);
                return;
            }
        }
#ifdef __WXGTK__
        const char* backend =
            gdk_display_get_default() ? G_OBJECT_TYPE_NAME(gdk_display_get_default()) : "GTK-no-display";
#else
        const char* backend = "Windows";
#endif
        Record("header", "\tbuild=%s " __DATE__ " " __TIME__ "\tbackend=%s\twx=%s\tcore=%s\tcompiler=%s",
               WXNOTEPAD_DIAGNOSTIC_BUILD, backend, wxVERSION_NUM_DOT_STRING, WXNOTEPAD_DIAGNOSTIC_CORE,
               WXNOTEPAD_DIAGNOSTIC_COMPILER);
#ifdef __WXGTK__
        Record("dependencies", "\tgtk=%u.%u.%u\tglib=%u.%u.%u", gtk_get_major_version(), gtk_get_minor_version(),
               gtk_get_micro_version(), glib_major_version, glib_minor_version, glib_micro_version);
#endif
        wxEvtHandler::AddFilter(this);
    }
    ~Trace() override {
        if (!file_) return;
        wxEvtHandler::RemoveFilter(this);
        Record("process.exit", "");
        if (file_ != stderr) std::fclose(file_);
    }
    bool Enabled() const noexcept { return file_ != nullptr; }
    // Source-controlled literal formats and matching scalar/pointer conversions
    // write directly to FILE* without an intermediate formatted record string.
    void Record(const char* event, const char* format, ...) noexcept {
        va_list arguments;
        va_start(arguments, format);
        RecordArguments(event, format, arguments);
        va_end(arguments);
    }
    // Keep the event/printf-format order shared by both variadic entry points.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void RecordArguments(const char* event, const char* format, va_list arguments) noexcept {
        if (!file_) return;
        const std::lock_guard lock(mutex_);
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started_).count();
        std::fprintf(file_, "us=%lld\tpid=%lu\ttid=%lu\tseq=%llu\tevent=%s\tgesture=%llu",
                     static_cast<long long>(elapsed), Process(), Thread(), ++sequence_, event,
                     static_cast<unsigned long long>(gesture));
        // Forward the same trusted format/arguments; no text payload supplies a format.
        std::vfprintf(file_, format, arguments);
        std::fputc('\n', file_);
        // Each completed transition reaches the OS before returning to native
        // delivery. A later abnormal exit cannot strand a userspace buffer.
        std::fflush(file_);
    }
    int FilterEvent(wxEvent& event) override {
        const auto type = event.GetEventType();
        if (type == wxEVT_KEY_DOWN || type == wxEVT_KEY_UP) {
            const auto& key = static_cast<wxKeyEvent&>(event);
            Record(type == wxEVT_KEY_DOWN ? "application.key-down" : "application.key-up",
                   "\ttarget=%p\tfocus=%p\tkey=%d\tmodifiers=%d", static_cast<void*>(event.GetEventObject()),
                   static_cast<void*>(wxWindow::FindFocus()), key.GetKeyCode(), key.GetModifiers());
        } else if (type == wxEVT_SET_FOCUS || type == wxEVT_KILL_FOCUS) {
            Record(type == wxEVT_SET_FOCUS ? "application.focus-in" : "application.focus-out", "\ttarget=%p\tfocus=%p",
                   static_cast<void*>(event.GetEventObject()), static_cast<void*>(wxWindow::FindFocus()));
        } else if (type == wxEVT_MENU) {
            Record("application.command", "\ttarget=%p\tcommand=%d", static_cast<void*>(event.GetEventObject()),
                   event.GetId());
        }
        return Event_Skip;
    }

private:
    FILE* file_ = nullptr;
    Clock::time_point started_ = Clock::now();
    std::mutex mutex_;
    unsigned long long sequence_ = 0;
};

Trace& Current() noexcept {
    static Trace trace;
    return trace;
}
} // namespace

bool Enabled() noexcept {
    return Current().Enabled();
}
void Write(const char* event, const char* format, ...) noexcept {
    va_list arguments;
    va_start(arguments, format);
    Current().RecordArguments(event, format, arguments);
    va_end(arguments);
}
std::uint64_t BeginGesture() noexcept {
    gesture = (static_cast<std::uint64_t>(Process()) << 32) | ++nextGesture;
    return gesture;
}
void EndGesture() noexcept {
    gesture = 0;
}
std::uint64_t Gesture() noexcept {
    return gesture;
}
} // namespace DiagnosticTrace
