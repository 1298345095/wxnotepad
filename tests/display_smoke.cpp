#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <wx/timer.h>
#include <cstdio>

// A separate process exercises real native expose/resize events. Pixel appearance
// is checked separately using native window captures, not inferred from events.
class DisplayApp final : public wxApp {
public:
    bool OnInit() override {
        frame_ = new EditorFrame;
        editor_ = frame_->Editor();
        editor_->Bind(wxEVT_PAINT, [this](wxPaintEvent& event) { ++paints_; event.Skip(); });
        frame_->Show();
        editor_->SetFocus();
        editor_->Send(Scintilla::Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("ASCII input\r\n中文输入\tcaret"));
        caret_ = editor_->Send(Scintilla::Message::GetCurrentPos);
        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, &DisplayApp::Step, this);
        timer_.Start(500, wxTIMER_ONE_SHOT);
        return true;
    }
    int OnRun() override { wxApp::OnRun(); return result_; }

private:
    EditorFrame* frame_ = nullptr;
    EditorControl* editor_ = nullptr;
    wxFrame* cover_ = nullptr;
    wxButton* alternateFocus_ = nullptr;
    wxTimer timer_;
    int stage_ = 0;
    int paints_ = 0;
    int before_ = 0;
    int result_ = 1;
    intptr_t caret_ = 0;
    bool focusAvailable_ = false;

    bool Focused(bool expected) {
        if ((editor_->Send(Scintilla::Message::GetFocus) != 0) == expected) return true;
        std::fputs("FAIL: native focus was not reflected by the core\n", stderr);
        frame_->Destroy();
        ExitMainLoop();
        return false;
    }

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
            focusAvailable_ = editor_->Send(Scintilla::Message::GetFocus) != 0;
            if (!focusAvailable_)
                std::puts("SKIP: native focus assertions; desktop did not activate the test window");
            alternateFocus_ = new wxButton(frame_, wxID_ANY, "temporary focus target", wxPoint(400, 50));
            alternateFocus_->SetFocus();
            break;
        case 1:
            if (focusAvailable_ && !Focused(false)) return;
            alternateFocus_->Destroy();
            alternateFocus_ = nullptr;
            editor_->SetFocus();
            break;
        case 2:
            if (focusAvailable_ && !Focused(true)) return;
            frame_->SetClientSize(640, 320);
            break;
        case 3:
            if (!Painted("resize")) return;
            cover_ = new wxFrame(frame_, wxID_ANY, "wxnotepad temporary cover", frame_->GetPosition(), frame_->GetSize());
            cover_->Show();
            cover_->Raise();
            break;
        case 4:
            cover_->Destroy();
            cover_ = nullptr;
            frame_->Raise();
            editor_->SetFocus();
            // Composited backends may restore cached pixels without an expose.
            // An explicit native refresh also verifies our paint path on those backends.
            editor_->Refresh(false);
            break;
        case 5:
            if (!Painted("uncover and refresh")) return;
            frame_->Hide();
            break;
        case 6:
            frame_->Show();
            break;
        case 7:
            if (!Painted("hide/show")) return;
            if (editor_->Send(Scintilla::Message::GetLineCount) != 2 ||
                editor_->Send(Scintilla::Message::GetCurrentPos) != caret_ ||
                editor_->Send(Scintilla::Message::GetModify) != 1) {
                std::fputs("FAIL: repaint changed original document state\n", stderr);
            } else {
                std::puts(focusAvailable_ ? "PASS: native focus, caret and modified document survive repaint"
                    : "PASS: caret and modified document survive repaint; native focus assertions skipped");
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
