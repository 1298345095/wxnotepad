#pragma once

#include <wx/event.h>
#include <wx/timer.h>
#include <optional>

class EditorFrame;
class wxWindowDestroyEvent;

// Opt-in observation of the actual application; owns no alternate editor.
class ProcessObserver final : public wxEvtHandler {
public:
    ProcessObserver(EditorFrame& frame, wxString directory);
    ~ProcessObserver() override;

private:
    struct Command {
        wxString sequence;
        wxString operation;
        wxString source;
    };
    EditorFrame* frame_;
    wxString observation_;
    wxString sequence_ = "0";
    wxString result_ = "ok";
    wxTimer observer_;
    bool lastDirty_ = false;
    bool lastActive_ = false;
    bool publishPending_ = false;
    bool commandPending_ = false;
    void Publish();
    void CommandFailure(const char* stage, unsigned long error);
    std::optional<Command> ConsumeCommand(const wxString& path);
    void Observe();
    void OnFrameDestroyed(wxWindowDestroyEvent& event);
};
