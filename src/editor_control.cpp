#include "editor_control.h"
#include "platform/core_headers.h"
#include <wx/dcclient.h>

using Scintilla::Message;
namespace core = Scintilla::Internal;
wxDEFINE_EVENT(wxEVT_EDITOR_STATE_CHANGED, wxCommandEvent);

class EditorControl::Core final : public core::ScintillaBase {
public:
    explicit Core(EditorControl& window) : window_(window) {
        wMain = static_cast<wxWindow*>(&window_);
        Initialise();
        WndProc(Message::SetCodePage, 65001, 0);
        // Match unconfigured Notepad4 on every host; saving preserves actual EOLs.
        WndProc(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::CrLf), 0);
        WndProc(Message::StyleSetFont, 32, reinterpret_cast<intptr_t>(core::Platform::DefaultFont()));
        WndProc(Message::StyleSetSize, 32, 12);
        WndProc(Message::StyleClearAll, 0, 0);
        WndProc(Message::SetCaretStyle, 1, 0);
        WndProc(Message::SetMarginWidthN, 1, 0);
        WndProc(Message::EmptyUndoBuffer, 0, 0);
        WndProc(Message::SetSavePoint, 0, 0);
    }
    ~Core() override { Finalise(); }

    void Draw(wxDC& dc) {
        auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
        surface->Init(&dc, &window_);
        surface->SetMode({65001});
        const auto size = window_.GetClientSize();
        rcPaint = core::PRectangle(0, 0, size.x, size.y);
        paintState = PaintState::painting;
        paintingAllText = true;
        Paint(surface.get(), rcPaint);
        paintState = PaintState::notPainting;
    }
    void Resize() { ChangeSize(); }
    void Character(std::string_view text) { InsertCharacter(text, Scintilla::CharacterSource::DirectInput); }
    void TickCaret() { TickFor(TickReason::caret); }

private:
    EditorControl& window_;
    void NotifyCaretMove() const noexcept override {}
    void UpdateSystemCaret() override {}
    void SetHorizontalScrollPos() override {}
    bool ModifyScrollBars(Sci::Line, Sci::Line) override { return false; }
    void SetVerticalScrollPos() override {}
    void Copy(bool) const override {}
    void CopyToClipboard(const core::SelectionText&) const override {}
    void Paste(bool) override {}
    void NotifyChange() const noexcept override {}
    void NotifyParent(Scintilla::NotificationData& notification) const noexcept override {
        if (notification.nmhdr.code == Scintilla::Notification::SavePointReached ||
            notification.nmhdr.code == Scintilla::Notification::SavePointLeft) {
            auto* event = new wxCommandEvent(wxEVT_EDITOR_STATE_CHANGED, window_.GetId());
            event->SetEventObject(&window_);
            wxQueueEvent(&window_, event);
        }
    }
    std::string CaseMapString(const std::string& value, CaseMapping mapping) const override {
        wxString text = wxString::FromUTF8(value);
        if (mapping == CaseMapping::upper) text.MakeUpper();
        else if (mapping == CaseMapping::lower) text.MakeLower();
        return text.ToStdString(wxConvUTF8);
    }
    void StartDrag() override {}
    bool FineTickerRunning(TickReason reason) const noexcept override {
        return reason == TickReason::caret && window_.caretTimer_.IsRunning();
    }
    void FineTickerStart(TickReason reason, int millis, int) noexcept override {
        if (reason == TickReason::caret) window_.caretTimer_.Start(millis);
    }
    void FineTickerCancel(TickReason reason) noexcept override {
        if (reason == TickReason::caret) window_.caretTimer_.Stop();
    }
    bool SetIdle(bool) noexcept override { return false; }
    void SetMouseCapture(bool on) noexcept override {
        if (on && !window_.HasCapture()) window_.CaptureMouse();
        else if (!on && window_.HasCapture()) window_.ReleaseMouse();
    }
    bool HaveMouseCapture() const noexcept override { return window_.HasCapture(); }
    void UpdateBaseElements() noexcept override {}
    bool ValidCodePage(int codepage) const noexcept override { return codepage == 65001; }
    std::string UTF8FromEncoded(std::string_view text) const override { return std::string(text); }
    std::string EncodedFromUTF8(std::string_view text) const override { return std::string(text); }
    intptr_t DefWndProc(Message, uintptr_t, intptr_t) override { return 0; }
    void SCICALL CreateCallTipWindow(core::PRectangle) noexcept override {}
};

EditorControl::EditorControl(wxWindow* parent)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxWANTS_CHARS),
      caretTimer_(this) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    core_ = std::make_unique<Core>(*this);
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) { Send(Message::SetFocus, 1); Refresh(false); event.Skip(); });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { Send(Message::SetFocus, 0); Refresh(false); event.Skip(); });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { core_->TickCaret(); }, caretTimer_.GetId());
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        Message command{};
        if (event.ControlDown() && !event.AltDown()) {
            if (event.GetKeyCode() == 'Z') command = event.ShiftDown() ? Message::Redo : Message::Undo;
            else if (event.GetKeyCode() == 'Y') command = Message::Redo;
        } else if (!event.AltDown() && !event.ShiftDown()) {
            switch (event.GetKeyCode()) {
            case WXK_RETURN: case WXK_NUMPAD_ENTER: command = Message::NewLine; break;
            case WXK_TAB: command = Message::Tab; break;
            case WXK_BACK: command = Message::DeleteBack; break;
            case WXK_DELETE: command = Message::Clear; break;
            default: break;
            }
        }
        if (command != Message{}) Send(command);
        else event.Skip();
    });
    Bind(wxEVT_CHAR, [this](wxKeyEvent& event) {
        const int character = event.GetUnicodeKey();
        if (character >= 32 && character != WXK_DELETE && !event.ControlDown() && !event.AltDown()) {
            // Keep the conversion owner alive: ToUTF8().data() on a temporary
            // would leave InsertCharacter holding a dangling view.
            const wxString text(wxUniChar(static_cast<unsigned int>(character)));
            const auto utf8 = text.ToUTF8();
            core_->Character(std::string_view(utf8.data(), utf8.length()));
        } else {
            event.Skip();
        }
    });
    Bind(wxEVT_PAINT, &EditorControl::OnPaint, this);
    Bind(wxEVT_SIZE, &EditorControl::OnSize, this);
}

EditorControl::~EditorControl() = default;

intptr_t EditorControl::Send(Message message, uintptr_t wParam, intptr_t lParam) {
    return core_->WndProc(message, wParam, lParam);
}

void EditorControl::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    core_->Draw(dc);
}

void EditorControl::OnSize(wxSizeEvent& event) {
    core_->Resize();
    Refresh(false);
    event.Skip();
}
