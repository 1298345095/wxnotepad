// Independent stock-widget application: it does not link the editor or core.
#include <wx/wx.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/timer.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>

class PeerApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        if (argc != 2) return false;
        directory_ = argv[1];
        frame_ = new wxFrame(nullptr, wxID_ANY, "wxnotepad clipboard test peer");
        text_ = new wxTextCtrl(frame_, wxID_ANY, {}, wxDefaultPosition, wxSize(400, 180), wxTE_MULTILINE);
        frame_->Show();
        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Poll(); });
        timer_.Start(20);
        started_ = wxGetUTCTimeMillis();
        return true;
    }

private:
    wxString directory_;
    wxFrame* frame_ = nullptr;
    wxTextCtrl* text_ = nullptr;
    wxTimer timer_;
    wxLongLong started_;
    bool pendingPaste_ = false;
    wxString pendingCommand_;
    wxLongLong activationStarted_;
    void Respond(const wxString& value) {
        {
            wxFFile file(directory_ + "/response.tmp", "wb");
            file.Write(value, wxConvUTF8);
        }
        wxRenameFile(directory_ + "/response.tmp", directory_ + "/response", true);
    }
    void Poll() {
        if (wxGetUTCTimeMillis() - started_ > 25000) {
            frame_->Destroy();
            ExitMainLoop();
            return;
        }
        if (!pendingCommand_.empty()) {
            if (!frame_->IsActive() || !text_->HasFocus()) {
                if (wxGetUTCTimeMillis() - activationStarted_ > 3000) {
                    pendingCommand_.clear();
                    Respond("native focus unavailable");
                }
                return;
            }
            const auto command = pendingCommand_;
            pendingCommand_.clear();
            wxString focusFile;
            if (wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile)) {
                wxFFile file(focusFile, "wb");
            }
            frame_->SetTitle("wxnotepad clipboard test peer");
            Perform(command);
            return;
        }
        if (pendingPaste_) {
            // GTK delivers the native clipboard request asynchronously.
            if (!text_->GetValue().empty()) {
                pendingPaste_ = false;
                Respond(text_->GetValue());
            }
            return;
        }
        const auto request = directory_ + "/request";
        if (!wxFileExists(request)) return;
        wxString command;
        {
            wxFFile file(request, "rb");
            file.ReadAll(&command, wxConvUTF8);
        }
        wxRemoveFile(request);
        if (command == "copy" || command == "wxcopy" || command == "nontext" || command == "paste") {
            frame_->SetTitle(wxString::Format("wxnotepad clipboard focus: peer [%lu]", wxGetProcessId()));
            wxString focusFile;
            if (wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile)) {
                wxFFile file(focusFile, "wb");
                file.Write("peer");
            }
            frame_->Raise();
            text_->SetFocus();
            activationStarted_ = wxGetUTCTimeMillis();
            pendingCommand_ = command;
            return;
        }
        Perform(command);
    }
    void Perform(const wxString& command) {
        if (command == "ready") {
            Respond("ready");
        } else if (command == "wxcopy") {
            wxClipboardLocker lock;
            Respond(!(!lock) && wxTheClipboard->SetData(
                                    new wxTextDataObject(wxString::FromUTF8("peer English 中文 😀\n第二行\nlast")))
                        ? "ready"
                        : "failed");
        } else if (command == "nontext") {
            wxClipboardLocker lock;
            auto* object = new wxCustomDataObject(wxDataFormat("application/x-wxnotepad-test"));
            object->SetData(5, "owned");
            Respond(!(!lock) && wxTheClipboard->SetData(object) ? "ready" : "failed");
        } else if (command == "copy") {
            text_->SetValue(wxString::FromUTF8("peer English 中文 😀\n第二行\nlast"));
            text_->SelectAll();
            text_->Copy();
            Respond("ready");
        } else if (command == "paste") {
            text_->Clear();
            pendingPaste_ = true;
            text_->Paste();
#ifdef __WXMSW__
        } else if (command == "hold") {
            Respond(::OpenClipboard(static_cast<HWND>(frame_->GetHandle())) ? "held" : "failed");
        } else if (command == "release") {
            ::CloseClipboard();
            Respond("released");
#endif
        } else if (command == "quit") {
            Respond("bye");
            frame_->Destroy();
            ExitMainLoop();
        }
    }
};
wxIMPLEMENT_APP(PeerApp);
