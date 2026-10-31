#include "app/process_observer.h"
#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include <wx/ffile.h>
#include <wx/textfile.h>
#include <utility>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

ProcessObserver::ProcessObserver(EditorFrame& frame, wxString directory)
    : frame_(&frame), observation_(std::move(directory)), observer_(this) {
    frame_->Bind(wxEVT_DESTROY, &ProcessObserver::OnFrameDestroyed, this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Observe(); });
    CallAfter([this] {
        if (frame_) {
            Publish();
            observer_.Start(10);
        }
    });
}

ProcessObserver::~ProcessObserver() {
    observer_.Stop();
    if (frame_) frame_->Unbind(wxEVT_DESTROY, &ProcessObserver::OnFrameDestroyed, this);
}

void ProcessObserver::OnFrameDestroyed(wxWindowDestroyEvent& event) {
    if (event.GetEventObject() == frame_) {
        observer_.Stop();
        frame_ = nullptr;
    }
    event.Skip();
}

// Opt-in external observation of this actual, shown application. No alternate
// editor host or hidden operation path: Open/Save are the frame's commands.
void ProcessObserver::Publish() {
    if (!frame_) return;
    frame_->Update();
    frame_->Editor()->Update();
    lastDirty_ = frame_->Editor()->Send(Scintilla::Message::GetModify) != 0;
    lastActive_ = frame_->IsActive();
    wxString backend = "Windows";
#ifdef __WXGTK__
    backend = G_OBJECT_TYPE_NAME(gdk_display_get_default());
#endif
    wxFFile state(observation_ + "/state.tmp", "wb");
    state.Write(wxString::Format(
        "sequence\t%s\nresult\t%s\nlength\t%lld\ndirty\t%d\nshown\t%d\nfocus\t%d\nactive\t%d\nbackend\t%s\neditor_"
        "handle\t%llu\n",
        sequence_, result_, static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetLength)), lastDirty_,
        frame_->IsShown() && !frame_->IsIconized(), wxWindow::FindFocus() == frame_->Editor(), lastActive_, backend,
        static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frame_->Editor()->GetHandle()))));
    state.Close();
    wxRenameFile(observation_ + "/state.tmp", observation_ + "/state", true);
}

void ProcessObserver::Observe() {
    if (!frame_) return;
    const wxString path = observation_ + "/command";
    if (wxFileExists(path)) {
        wxTextFile command(path);
        if (!command.Open() || command.GetLineCount() < 2) return;
        sequence_ = command[0];
        const wxString operation = command[1];
        const wxString source = command.GetLineCount() > 2 ? command[2] : wxString();
        command.Close();
        wxRemoveFile(path);
        result_ = "failed";
        if (operation == "open" && !source.empty())
            result_ = frame_->Open(source).status == OpenStatus::Opened ? "ok" : "failed";
        if (operation == "save") result_ = frame_->Save().status == SaveStatus::Saved ? "ok" : "failed";
        Publish();
    } else if ((frame_->Editor()->Send(Scintilla::Message::GetModify) != 0) != lastDirty_ ||
               frame_->IsActive() != lastActive_) {
        Publish();
    }
}
