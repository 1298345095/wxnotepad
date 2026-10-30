#include "editor_control.h"
#include "platform/core_headers.h"
#include <wx/dcclient.h>
#include <chrono>

using Scintilla::Message;
namespace core = Scintilla::Internal;
wxDEFINE_EVENT(wxEVT_EDITOR_STATE_CHANGED, wxCommandEvent);

namespace {
unsigned MouseTime() {
    return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
Scintilla::KeyMod MouseModifiers(const wxMouseEvent& event) {
    // This host supports one stream selection; Alt rectangular selection is deferred.
    return event.ShiftDown() ? Scintilla::KeyMod::Shift : Scintilla::KeyMod::Norm;
}
}

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
        WndProc(Message::SetScrollWidthTracking, 1, 0);
        dragDropEnabled = false;
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
    bool Navigation(int key, bool control, bool shift) {
        Scintilla::Keys coreKey;
        switch (key) {
        case WXK_LEFT: coreKey = Scintilla::Keys::Left; break;
        case WXK_RIGHT: coreKey = Scintilla::Keys::Right; break;
        case WXK_UP: coreKey = Scintilla::Keys::Up; break;
        case WXK_DOWN: coreKey = Scintilla::Keys::Down; break;
        case WXK_HOME: coreKey = Scintilla::Keys::Home; break;
        case WXK_END: coreKey = Scintilla::Keys::End; break;
        case WXK_PAGEUP: coreKey = Scintilla::Keys::Prior; break;
        case WXK_PAGEDOWN: coreKey = Scintilla::Keys::Next; break;
        default: return false;
        }
        bool consumed = false;
        KeyDownWithModifiers(coreKey, static_cast<Scintilla::KeyMod>((control ? 2 : 0) | (shift ? 1 : 0)), &consumed);
        return consumed;
    }
    void TickCaret() { TickFor(TickReason::caret); }
    void TickScroll() { TickFor(TickReason::scroll); }
    void TickWiden() { TickFor(TickReason::widen); }
    void Scroll(wxScrollWinEvent& event) {
        const bool vertical = event.GetOrientation() == wxVERTICAL;
        int position = event.GetPosition();
        const int current = vertical ? static_cast<int>(topLine) : xOffset;
        const int line = vertical ? 1 : std::max(1, static_cast<int>(vs.spaceWidth));
        const int page = vertical ? static_cast<int>(LinesOnScreen()) : window_.GetClientSize().x;
        const auto type = event.GetEventType();
        if (type == wxEVT_SCROLLWIN_LINEUP) position = current - line;
        else if (type == wxEVT_SCROLLWIN_LINEDOWN) position = current + line;
        else if (type == wxEVT_SCROLLWIN_PAGEUP) position = current - page;
        else if (type == wxEVT_SCROLLWIN_PAGEDOWN) position = current + page;
        else if (type == wxEVT_SCROLLWIN_TOP) position = 0;
        else if (type == wxEVT_SCROLLWIN_BOTTOM) position = vertical ? static_cast<int>(MaxScrollPos()) : scrollWidth;
        if (vertical) ScrollTo(position);
        else HorizontalScrollTo(std::clamp(position, 0, std::max(0, scrollWidth - window_.GetClientSize().x)));
    }
    void Wheel(wxMouseEvent& event) {
        const bool horizontal = event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL || event.ShiftDown();
        int& remainder = horizontal ? horizontalWheel_ : verticalWheel_;
        remainder += event.GetWheelRotation();
        const int delta = event.GetWheelDelta();
        if (!delta) return;
        const int steps = remainder / delta;
        remainder %= delta;
        if (horizontal) {
            const int direction = event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL ? 1 : -1;
            const int distance = std::max(1, static_cast<int>(vs.spaceWidth)) * event.GetColumnsPerAction();
            HorizontalScrollTo(std::clamp(xOffset + direction * steps * distance, 0,
                std::max(0, scrollWidth - window_.GetClientSize().x)));
        } else {
            const auto distance = event.IsPageScroll() ? LinesOnScreen() : event.GetLinesPerAction();
            ScrollTo(topLine - steps * distance);
        }
    }
    void Mouse(wxMouseEvent& event) {
        const core::Point point(event.GetX(), event.GetY());
        if (event.GetEventType() == wxEVT_LEFT_DOWN) {
            window_.SetFocus();
            ButtonDownWithModifiers(point, MouseTime(), MouseModifiers(event));
        } else if (event.GetEventType() == wxEVT_LEFT_UP) {
            ButtonUpWithModifiers(point, MouseTime(), MouseModifiers(event));
        } else {
            ButtonMoveWithModifiers(point, MouseTime(), MouseModifiers(event));
        }
    }
    void CaptureLost() { ChangeMouseCapture(false); }

private:
    EditorControl& window_;
    int verticalWheel_ = 0;
    int horizontalWheel_ = 0;
    void NotifyCaretMove() const noexcept override {}
    void UpdateSystemCaret() override {}
    void SetHorizontalScrollPos() override {
        window_.SetScrollPos(wxHORIZONTAL, xOffset);
        UpdateSystemCaret();
    }
    bool ModifyScrollBars(Sci::Line maximum, Sci::Line page) override {
        const auto before = window_.GetClientSize();
        window_.SetScrollbar(wxVERTICAL, static_cast<int>(topLine), static_cast<int>(page), static_cast<int>(maximum + 1));
        window_.SetScrollbar(wxHORIZONTAL, xOffset, window_.GetClientSize().x, scrollWidth);
        return before != window_.GetClientSize();
    }
    void SetVerticalScrollPos() override {
        window_.SetScrollPos(wxVERTICAL, static_cast<int>(topLine));
        UpdateSystemCaret();
    }
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
        if (reason == TickReason::scroll) return window_.scrollTimer_.IsRunning();
        if (reason == TickReason::widen) return window_.widenTimer_.IsRunning();
        return reason == TickReason::caret && window_.caretTimer_.IsRunning();
    }
    void FineTickerStart(TickReason reason, int millis, int) noexcept override {
        if (reason == TickReason::caret) window_.caretTimer_.Start(millis);
        else if (reason == TickReason::scroll) window_.scrollTimer_.Start(millis);
        else if (reason == TickReason::widen) window_.widenTimer_.Start(millis);
    }
    void FineTickerCancel(TickReason reason) noexcept override {
        if (reason == TickReason::caret) window_.caretTimer_.Stop();
        else if (reason == TickReason::scroll) window_.scrollTimer_.Stop();
        else if (reason == TickReason::widen) window_.widenTimer_.Stop();
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
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE | wxWANTS_CHARS | wxVSCROLL | wxHSCROLL),
      caretTimer_(this), scrollTimer_(this), widenTimer_(this) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    core_ = std::make_unique<Core>(*this);
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) { Send(Message::SetFocus, 1); Refresh(false); event.Skip(); });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) { Send(Message::SetFocus, 0); Refresh(false); event.Skip(); });
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { core_->TickCaret(); }, caretTimer_.GetId());
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { core_->TickScroll(); }, scrollTimer_.GetId());
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { core_->TickWiden(); }, widenTimer_.GetId());
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) { core_->Mouse(event); });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& event) { core_->Mouse(event); });
    Bind(wxEVT_MOTION, [this](wxMouseEvent& event) { core_->Mouse(event); });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { core_->CaptureLost(); });
    Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& event) { core_->Wheel(event); });
    const auto scroll = [this](wxScrollWinEvent& event) { core_->Scroll(event); };
    Bind(wxEVT_SCROLLWIN_TOP, scroll);
    Bind(wxEVT_SCROLLWIN_BOTTOM, scroll);
    Bind(wxEVT_SCROLLWIN_LINEUP, scroll);
    Bind(wxEVT_SCROLLWIN_LINEDOWN, scroll);
    Bind(wxEVT_SCROLLWIN_PAGEUP, scroll);
    Bind(wxEVT_SCROLLWIN_PAGEDOWN, scroll);
    Bind(wxEVT_SCROLLWIN_THUMBTRACK, scroll);
    Bind(wxEVT_SCROLLWIN_THUMBRELEASE, scroll);
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (!event.AltDown() && core_->Navigation(event.GetKeyCode(), event.ControlDown(), event.ShiftDown())) return;
        Message command{};
        if (event.ControlDown() && !event.AltDown()) {
            if (event.GetKeyCode() == 'Z') command = event.ShiftDown() ? Message::Redo : Message::Undo;
            else if (event.GetKeyCode() == 'Y') command = Message::Redo;
            else if (event.GetKeyCode() == 'A') command = Message::SelectAll;
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
