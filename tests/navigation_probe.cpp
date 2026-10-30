// Native input probe: the external desktop driver sends real OS input and
// observes only the editor's public document/selection/viewport interface.
#include "editor_control.h"
#include <wx/wx.h>
#include <fstream>
#include <iomanip>

class NavigationProbe final : public wxApp {
public:
    bool OnInit() override {
        if (argc < 2 || argc > 3) return false;
        report_ = argv[1].ToStdString();
        frame_ = new wxFrame(nullptr, wxID_ANY, "wxnotepad #11 native probe", wxDefaultPosition, wxSize(640, 360));
        editor_ = new EditorControl(frame_);
        editor_->Send(Scintilla::Message::SetText, 0, reinterpret_cast<intptr_t>("A中文\r\n  B\r\nlast"));
        if (argc == 3 && argv[2] == "--scroll") LoadScrollSample();
        editor_->Send(Scintilla::Message::GotoPos, 0);
        editor_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
            if (event.GetKeyCode() == WXK_F6) {
                LoadScrollSample();
            } else event.Skip();
        });
        editor_->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& event) {
            std::ofstream wheel(report_ + ".wheel");
            wheel << event.GetWheelRotation() << ' ' << event.GetWheelDelta() << ' ' << event.GetLinesPerAction()
                << ' ' << event.GetWheelAxis() << '\n';
            event.Skip();
        });
        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Report(); });
        timer_.Start(100);
        frame_->Show();
        editor_->SetFocus();
        return true;
    }
private:
    wxFrame* frame_ = nullptr;
    EditorControl* editor_ = nullptr;
    wxTimer timer_;
    std::string report_;
    int ticks_ = 0;
    void LoadScrollSample() {
        std::string sample;
        for (int line = 0; line < 100; ++line) sample += std::string(240, 'x') + "中文\r\n";
        editor_->Send(Scintilla::Message::SetText, 0, reinterpret_cast<intptr_t>(sample.c_str()));
        editor_->Send(Scintilla::Message::GotoPos, 0);
    }
    void Report() {
        using Scintilla::Message;
        if (++ticks_ > (argc == 3 ? 3000 : 600)) { frame_->Close(); return; }
        std::ofstream output(report_, std::ios::trunc);
        const auto origin = editor_->ClientToScreen(wxPoint(0, 0));
        output << origin.x << ' ' << origin.y << ' ' << editor_->GetClientSize().x << ' ' << editor_->GetClientSize().y;
        for (auto message : {Message::GetCurrentPos, Message::GetAnchor, Message::GetFirstVisibleLine, Message::GetXOffset})
            output << ' ' << editor_->Send(message);
        for (int position : {1, 12})
            output << ' ' << editor_->Send(Message::PointXFromPosition, 0, position)
                << ' ' << editor_->Send(Message::PointYFromPosition, 0, position);
        output << '\n';
        const auto length = editor_->Send(Message::GetLength);
        std::string text(static_cast<size_t>(length) + 1, '\0');
        editor_->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
        for (intptr_t index = 0; index < length; ++index)
            output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(static_cast<unsigned char>(text[index]));
        output << '\n';
    }
};
wxIMPLEMENT_APP(NavigationProbe);
