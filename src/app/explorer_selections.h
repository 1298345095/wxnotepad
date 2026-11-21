#pragma once

#include <memory>
#include <wx/timer.h>
class wxWindow;

// Frame-owned requests stay on the GUI STA. Destroying the frame cancels its
// timer and releases the native interfaces; no callback owns the frame.
class ExplorerSelections final : public wxEvtHandler {
public:
    explicit ExplorerSelections(wxWindow& owner);
    ~ExplorerSelections() override;
    // True means accepted, not yet selected. Later failure uses the native
    // association fallback and, if that fails, reports to the surviving owner.
    bool Start(const wxString& path);
    void Close();

private:
    struct State;
    std::unique_ptr<State> state_;
    wxWindow& owner_;
    wxTimer timer_{this};
    bool closing_ = false;
    bool polling_ = false;
    void Poll(wxTimerEvent& event);
    void Cancel();
    void QueueFallback(const wxString& directory);
};
