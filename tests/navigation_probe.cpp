// Native input probe: the external desktop driver sends real OS input and
// observes only the editor's public document/selection/viewport interface.
#include "editor_control.h"
#include <wx/wx.h>
#include <fstream>
#include <iomanip>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

class NavigationProbe final : public wxApp {
public:
    bool OnInit() override {
        if (argc < 2 || argc > 3) return false;
        report_ = argv[1].ToStdString();
        frame_ = new wxFrame(nullptr, wxID_ANY, "wxnotepad #11 native probe", wxDefaultPosition, wxSize(640, 360));
#ifdef __WXGTK__
        if (argc == 3 && argv[2] == "--compare-csd") {
            auto* header = gtk_header_bar_new();
            gtk_header_bar_set_title(GTK_HEADER_BAR(header), "wxnotepad #11 native probe");
            gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), true);
            gtk_window_set_titlebar(GTK_WINDOW(frame_->GetHandle()), header);
        }
#endif
        editor_ = new EditorControl(frame_);
        editor_->Send(Scintilla::Message::SetText, 0, reinterpret_cast<intptr_t>("A中文\r\n  B\r\nlast"));
        if (argc == 3 && argv[2] == "--scroll") LoadScrollSample();
        if (argc == 3 && (argv[2] == "--scroll" || argv[2] == "--compare" || argv[2] == "--compare-csd")) {
            auto* layout = new wxBoxSizer(wxVERTICAL);
            auto* close = new wxButton(frame_, wxID_CLOSE, "Close probe");
            close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { frame_->Close(); });
            layout->Add(close, 0, wxALIGN_RIGHT);
            if (argv[2] != "--scroll") {
                layout->Add(new wxStaticText(frame_, wxID_ANY, "Native GTK text field above; original-core editor below"), 0, wxEXPAND);
                layout->Add(new wxTextCtrl(frame_, wxID_ANY, "Move the pointer here, then into the editor below"), 0, wxEXPAND);
            }
            layout->Add(editor_, 1, wxEXPAND);
            frame_->SetSizer(layout);
            LoadScrollSample();
        }
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
        if (argc == 3 && argv[2] == "--diagnose") {
            Diagnose();
            return false;
        }
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
    void Diagnose() {
        wxMouseEvent motion(wxEVT_MOTION);
        motion.SetPosition(wxPoint(100, 10));
        editor_->GetEventHandler()->ProcessEvent(motion);
        std::ofstream output(report_);
        output << "resize-style=" << frame_->HasFlag(wxRESIZE_BORDER)
            << " capture=" << editor_->HasCapture() << " cursor-valid=" << editor_->GetCursor().IsOk() << '\n';
#ifdef __WXGTK__
        auto* widget = GTK_WIDGET(frame_->GetHandle());
        gtk_widget_realize(widget);
        output << "gtk-resizable=" << gtk_window_get_resizable(GTK_WINDOW(widget)) << '\n';
        auto* cursor = editor_->GetCursor().GetCursor();
        auto* image = cursor ? gdk_cursor_get_image(cursor) : nullptr;
        int visible = 0;
        if (image) {
            const auto* pixels = gdk_pixbuf_get_pixels(image);
            const int channels = gdk_pixbuf_get_n_channels(image);
            const int stride = gdk_pixbuf_get_rowstride(image);
            for (int y = 0; y < gdk_pixbuf_get_height(image); ++y)
                for (int x = 0; x < gdk_pixbuf_get_width(image); ++x)
                    if (!gdk_pixbuf_get_has_alpha(image) || pixels[y * stride + x * channels + channels - 1]) ++visible;
            g_object_unref(image);
        }
        output << "cursor-nontransparent=" << visible << '\n';
        GdkWMDecoration decorations{};
        gdk_window_get_decorations(gtk_widget_get_window(widget), &decorations);
        output << "gtk-decorations=" << decorations << '\n';
#endif
    }
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
