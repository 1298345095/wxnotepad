#pragma once

#include <ScintillaMessages.h>
#include <wx/control.h>
#include <wx/timer.h>
#include <cstdint>
#include <memory>

wxDECLARE_EVENT(wxEVT_EDITOR_STATE_CHANGED, wxCommandEvent);

// A wx window backed by exactly one original Notepad4 Scintilla document.
// Commands use the pinned core's public message API; no mirrored wx text exists.
class EditorControl final : public wxControl {
public:
    explicit EditorControl(wxWindow* parent);
    ~EditorControl() override;
    intptr_t Send(Scintilla::Message message, uintptr_t wParam = 0, intptr_t lParam = 0);

private:
    class Core;
    wxTimer caretTimer_;
    wxTimer scrollTimer_;
    wxTimer widenTimer_;
    std::unique_ptr<Core> core_;
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
};
