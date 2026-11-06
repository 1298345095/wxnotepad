#pragma once

#include "diagnostics/trace.h"
#include <wx/string.h>

namespace NativeTrace {
// A trace scope adds an identity to the existing native request protocol. The
// driver still acknowledges and injects input at its original boundaries.
class Gesture {
public:
    Gesture([[maybe_unused]] void* target, [[maybe_unused]] int key, [[maybe_unused]] int modifiers) {
#ifdef WXNOTEPAD_DIAGNOSTICS
        if (DiagnosticTrace::Enabled()) {
            DiagnosticTrace::BeginGesture();
            WXNOTE_TRACE("driver.request", "\ttarget=%p\tkey=%d\tmodifiers=%d", target, key, modifiers);
        }
#endif
    }
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
