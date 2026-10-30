#include "editor_control.h"
#include "platform/core_headers.h"
#include <wx/dcclient.h>

using Scintilla::Message;
namespace core = Scintilla::Internal;

class EditorControl::Core final : public core::ScintillaBase {
public:
    explicit Core(EditorControl& window) : window_(window) {
        wMain = static_cast<wxWindow*>(&window_);
        Initialise();
        WndProc(Message::SetCodePage, 65001, 0);
        WndProc(Message::StyleSetFont, 32, reinterpret_cast<intptr_t>(core::Platform::DefaultFont()));
        WndProc(Message::StyleSetSize, 32, 12);
        WndProc(Message::StyleClearAll, 0, 0);
        WndProc(Message::SetCaretStyle, 0, 0);
        WndProc(Message::SetMarginWidthN, 1, 0);
        static constexpr char sample[] = "Original Notepad4 core + wxWidgets\nASCII: read-only document\n中文：原版内核中的只读文本\n";
        WndProc(Message::SetText, 0, reinterpret_cast<intptr_t>(sample));
        WndProc(Message::EmptyUndoBuffer, 0, 0);
        WndProc(Message::SetSavePoint, 0, 0);
        WndProc(Message::SetReadOnly, 1, 0);
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
    void NotifyParent(Scintilla::NotificationData&) const noexcept override {}
    std::string CaseMapString(const std::string& value, CaseMapping mapping) const override {
        wxString text = wxString::FromUTF8(value);
        if (mapping == CaseMapping::upper) text.MakeUpper();
        else if (mapping == CaseMapping::lower) text.MakeLower();
        return text.ToStdString(wxConvUTF8);
    }
    void StartDrag() override {}
    bool FineTickerRunning(TickReason) const noexcept override { return false; }
    void FineTickerStart(TickReason, int, int) noexcept override {}
    void FineTickerCancel(TickReason) noexcept override {}
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
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    core_ = std::make_unique<Core>(*this);
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
