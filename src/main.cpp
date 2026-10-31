#include "editor_frame.h"
#include "editor_control.h"
#include <wx/wx.h>
#include <wx/ffile.h>
#include <wx/textfile.h>
#include <wx/timer.h>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

class NotepadApp final : public wxApp {
    EditorFrame* frame_ = nullptr;
    wxString observation_;
    wxString sequence_ = "0";
    wxString result_ = "ok";
    wxTimer observer_;
    bool lastDirty_ = false;
    bool lastActive_ = false;

    // Opt-in external observation of this actual, shown application. No alternate
    // editor host or hidden operation path: Open/Save are the frame's commands.
    void Publish() {
        frame_->Update();
        frame_->Editor()->Update();
        lastDirty_ = frame_->Editor()->Send(Scintilla::Message::GetModify) != 0;
        lastActive_ = frame_->IsActive();
        wxString backend = "Windows";
#ifdef __WXGTK__
        backend = G_OBJECT_TYPE_NAME(gdk_display_get_default());
#endif
        wxFFile state(observation_ + "/state.tmp", "wb");
        state.Write(wxString::Format("sequence\t%s\nresult\t%s\nlength\t%lld\ndirty\t%d\nshown\t%d\nfocus\t%d\nactive\t%d\nbackend\t%s\neditor_handle\t%llu\n",
            sequence_, result_, static_cast<long long>(frame_->Editor()->Send(Scintilla::Message::GetLength)),
            lastDirty_, frame_->IsShown() && !frame_->IsIconized(),
            wxWindow::FindFocus() == frame_->Editor(), lastActive_, backend,
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(frame_->Editor()->GetHandle()))));
        state.Close();
        wxRenameFile(observation_ + "/state.tmp", observation_ + "/state", true);
    }

    void Observe() {
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
            if (operation == "save")
                result_ = frame_->Save().status == SaveStatus::Saved ? "ok" : "failed";
            Publish();
        } else if ((frame_->Editor()->Send(Scintilla::Message::GetModify) != 0) != lastDirty_ ||
                   frame_->IsActive() != lastActive_) {
            Publish();
        }
    }

public:
    bool OnInit() override {
        wxString startup;
        for (int i = 1; i < argc; ++i) {
            const wxString argument(argv[i]);
            if (argument == "--observe" && i + 1 < argc) observation_ = argv[++i];
            else if (argument == "--startup-file" && i + 1 < argc) startup = argv[++i];
            else return false;
        }
        frame_ = new EditorFrame;
        if (!startup.empty() && frame_->Open(startup).status != OpenStatus::Opened) return false;
        frame_->Show();
        frame_->Editor()->SetFocus();
        if (!observation_.empty()) {
            observer_.SetOwner(this);
            Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Observe(); });
            CallAfter([this] { Publish(); observer_.Start(10); });
        }
        return true;
    }
};

wxIMPLEMENT_APP(NotepadApp);
