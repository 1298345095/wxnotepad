#pragma once
#include "native_search_helpers.h"
#include <wx/spinctrl.h>
#include <wx/timer.h>
#include <functional>

namespace TabDialog {
inline void Toggle(wxCheckBox* option, bool checked) {
    if (option->GetValue() != checked) NativeSearch::Toggle(option, checked);
}
inline wxSpinCtrl* Width(wxWindow* dialog, const wxString& name) {
    auto* control = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxSpinCtrl);
    if (!control) throw std::runtime_error("Tab Settings width control must exist");
    return control;
}
inline wxCheckBox* Option(wxWindow* dialog, const wxString& name) {
    auto* control = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxCheckBox);
    if (!control) control = wxDynamicCast(wxWindow::FindWindowByLabel(name, dialog), wxCheckBox);
    if (!control) throw std::runtime_error("Tab Settings checkbox must exist");
    return control;
}
inline void Show(EditorFrame* frame, const std::function<void(wxDialog*)>& edit, bool accept = true) {
    wxTimer timer;
    std::exception_ptr failure;
    timer.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || dialog->GetTitle() != "Tab Settings") continue;
            timer.Stop();
            try {
                edit(dialog);
            } catch (...) {
                failure = std::current_exception();
            }
            dialog->EndModal(accept && !failure ? wxID_OK : wxID_CANCEL);
            return;
        }
    });
    timer.Start(10);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Tab Settings..."));
    if (failure) std::rethrow_exception(failure);
}
inline void File(EditorFrame* frame, int tab, int indent, bool spaces) {
    Show(frame, [=](wxDialog* dialog) {
        Toggle(Option(dialog, "Use Scheme Tab Settings"), false);
        Width(dialog, "File Tab Width")->SetValue(tab);
        Width(dialog, "File Indentation Width")->SetValue(indent);
        Toggle(Option(dialog, "File Tabs as Spaces"), spaces);
    });
}
} // namespace TabDialog
