#include "editor_control.h"
#include <wx/wx.h>
#include <wx/timer.h>
#include <cstdio>

// A separate process exercises real native expose/resize events. Pixel appearance
// is checked separately using native window captures, not inferred from events.
class DisplayApp final : public wxApp {
public:
    bool OnInit() override {
        frame_ = new wxFrame(nullptr, wxID_ANY, "wxnotepad display check", wxDefaultPosition, wxSize(800, 500));
        editor_ = new EditorControl(frame_);
        editor_->Bind(wxEVT_PAINT, [this](wxPaintEvent& event) { ++paints_; event.Skip(); });
        frame_->Show();
        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, &DisplayApp::Step, this);
        timer_.Start(500, wxTIMER_ONE_SHOT);
        return true;
    }
    int OnRun() override { wxApp::OnRun(); return result_; }

private:
    wxFrame* frame_ = nullptr;
    EditorControl* editor_ = nullptr;
    wxFrame* cover_ = nullptr;
    wxTimer timer_;
    int stage_ = 0;
    int paints_ = 0;
    int before_ = 0;
    int result_ = 1;

    bool Painted(const char* operation) {
        if (paints_ <= before_) {
            std::fprintf(stderr, "FAIL: no native paint after %s\n", operation);
            frame_->Destroy();
            ExitMainLoop();
            return false;
        }
        std::printf("PASS: native paint after %s\n", operation);
        before_ = paints_;
        return true;
    }

    void Step(wxTimerEvent&) {
        switch (stage_++) {
        case 0:
            if (!Painted("show")) return;
            frame_->SetClientSize(640, 320);
            break;
        case 1:
            if (!Painted("resize")) return;
            cover_ = new wxFrame(frame_, wxID_ANY, "wxnotepad temporary cover", frame_->GetPosition(), frame_->GetSize());
            cover_->Show();
            cover_->Raise();
            break;
        case 2:
            cover_->Destroy();
            cover_ = nullptr;
            // Composited backends may restore cached pixels without an expose.
            // An explicit native refresh also verifies our paint path on those backends.
            editor_->Refresh(false);
            break;
        case 3:
            if (!Painted("uncover and refresh")) return;
            frame_->Hide();
            break;
        case 4:
            frame_->Show();
            break;
        case 5:
            if (!Painted("hide/show")) return;
            if (editor_->Send(Scintilla::Message::GetReadOnly) != 1 ||
                editor_->Send(Scintilla::Message::GetLineCount) != 4) {
                std::fputs("FAIL: repaint changed original document state\n", stderr);
            } else {
                std::puts("PASS: repaint preserved the original read-only document");
                result_ = 0;
            }
            frame_->Destroy();
            ExitMainLoop();
            return;
        }
        timer_.Start(500, wxTIMER_ONE_SHOT);
    }
};

wxIMPLEMENT_APP(DisplayApp);
