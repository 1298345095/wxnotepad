#pragma once

#include "native_key_helpers.h"
#include <wx/modalhook.h>
#include <wx/menu.h>

namespace TestSyntax {
// Pinned Notepad4.rc has a CSV File... command because selection opens options.
// The catalog/status/filter caption remains CSV File, without that UI suffix.
inline int SchemeCommand(wxMenuBar& bar, const wxString& name) {
    const auto label = name == wxGetTranslation("CSV File") ? name + "..." : name;
    return bar.FindMenuItem("Scheme", label);
}

// Generic all-scheme regressions still select CSV through its actual menu and
// present its options dialog. Accept the current values without editing them.
class SchemeOptions final : public wxModalDialogHook {
public:
    SchemeOptions() { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == wxGetTranslation("CSV Options")) {
            ++shown;
            wxTheApp->CallAfter([this, dialog] {
                NativeKeys::AfterPresentation(dialog, [this, dialog](bool ready) {
                    try {
                        if (!ready) throw TestEditor::Failure("CSV options must be natively presented");
                        auto* button = dialog->FindWindow(wxID_OK);
                        if (!button || !button->IsShown())
                            throw TestEditor::Failure("CSV options must offer its native OK button");
                        NativeSearch::Click(dialog, wxID_OK);
                    } catch (...) {
                        failure = std::current_exception();
                        dialog->EndModal(wxID_CANCEL);
                    }
                });
            });
        }
        return wxID_NONE;
    }
    unsigned shown = 0;
    std::exception_ptr failure;
};

template <class Action> void WithSchemeOptions(const wxString& name, Action action) {
    SchemeOptions options;
    action();
    if (options.failure) std::rethrow_exception(options.failure);
    const auto expected = name == wxGetTranslation("CSV File") ? 1U : 0U;
    if (options.shown != expected)
        throw TestEditor::Failure("language selection presents exactly its supported native options dialog");
}
} // namespace TestSyntax
