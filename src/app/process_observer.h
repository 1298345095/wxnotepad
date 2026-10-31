#pragma once

#include <wx/event.h>
#include <wx/timer.h>

class EditorFrame;
class wxWindowDestroyEvent;

// Opt-in observation of the actual application; owns no alternate editor.
class ProcessObserver final : public wxEvtHandler {
public:
    ProcessObserver(EditorFrame& frame, wxString directory);
    ~ProcessObserver() override;

private:
    EditorFrame* frame_;
    wxString observation_;
    wxString sequence_ = "0";
    wxString result_ = "ok";
    wxTimer observer_;
    bool lastDirty_ = false;
    bool lastActive_ = false;
    void Publish();
    void Observe();
    void OnFrameDestroyed(wxWindowDestroyEvent& event);
};
