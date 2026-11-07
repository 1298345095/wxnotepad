#pragma once

#include "diagnostics/trace.h"
#include <wx/string.h>

class wxWindow;

namespace NativeTrace {
// A trace scope adds an identity to the existing native request protocol. The
// driver still acknowledges and injects input at its original boundaries.
class Gesture {
public:
    Gesture([[maybe_unused]] wxWindow& target, [[maybe_unused]] int key, [[maybe_unused]] int modifiers) {
#ifdef WXNOTEPAD_DIAGNOSTICS
        if (DiagnosticTrace::Enabled()) {
            DiagnosticTrace::BeginGesture();
            WXNOTE_TRACE("driver.request", "\ttarget=%p\tkey=%d\tmodifiers=%d", static_cast<void*>(&target), key,
                         modifiers);
        }
#endif
    }
    // The lexical, thread-bound owner ends its gesture exactly once.
    Gesture(const Gesture&) = delete;
    Gesture& operator=(const Gesture&) = delete;
    Gesture(Gesture&&) = delete;
    Gesture& operator=(Gesture&&) = delete;
    ~Gesture() {
#ifdef WXNOTEPAD_DIAGNOSTICS
        if (DiagnosticTrace::Enabled()) {
            WXNOTE_TRACE("driver.complete");
            DiagnosticTrace::EndGesture();
        }
#endif
    }
};

inline wxString Request(const wxString& request) {
#ifdef WXNOTEPAD_DIAGNOSTICS
    if (DiagnosticTrace::Enabled() && DiagnosticTrace::Gesture())
        return wxString::Format("trace:%llu:", static_cast<unsigned long long>(DiagnosticTrace::Gesture())) + request;
#endif
    return request;
}
} // namespace NativeTrace
