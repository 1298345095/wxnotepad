#include "editor/editor_control.h"
#include "editor/platform/core_headers.h"
#include <wx/dcclient.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <chrono>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif
#ifdef __WXMSW__
#include <wx/caret.h>
#include <imm.h>
#include <cwchar>
#endif

using Scintilla::Message;
namespace core = Scintilla::Internal;
wxDEFINE_EVENT(wxEVT_EDITOR_STATE_CHANGED, wxCommandEvent);

namespace {
unsigned MouseTime() {
    return static_cast<unsigned>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
Scintilla::KeyMod MouseModifiers(const wxMouseEvent& event) {
    // This host supports one stream selection; Alt rectangular selection is deferred.
    return event.ShiftDown() ? Scintilla::KeyMod::Shift : Scintilla::KeyMod::Norm;
}
} // namespace

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
        WndProc(Message::StyleSetBack, 32, 0xFFFFFF);
        WndProc(Message::StyleClearAll, 0, 0);
        WndProc(Message::SetCaretStyle, 1, 0);
        WndProc(Message::SetMarginWidthN, 1, 0);
        WndProc(Message::SetScrollWidthTracking, 1, 0);
        dragDropEnabled = false;
        WndProc(Message::EmptyUndoBuffer, 0, 0);
        WndProc(Message::SetSavePoint, 0, 0);
    }
    ~Core() override { Finalise(); }

    uint64_t SelectionRevision() const noexcept {
        const auto anchor = sel.MainAnchor();
        const auto caret = sel.MainCaret();
        if (anchor != observedAnchor_ || caret != observedCaret_) {
            observedAnchor_ = anchor;
            observedCaret_ = caret;
            ++selectionRevision_;
        }
        return selectionRevision_;
    }

    intptr_t SelectionCharacterCount() {
        if (WndProc(Message::GetSelectionMode, 0, 0) != static_cast<intptr_t>(Scintilla::SelectionMode::Stream) ||
            WndProc(Message::GetSelections, 0, 0) != 1)
            return 0;
        const auto start = WndProc(Message::GetSelectionStart, 0, 0);
        const auto end = WndProc(Message::GetSelectionEnd, 0, 0);
        if (start == end) return 0;
        if (selectionCountDirty_ || start != countedStart_ || end != countedEnd_) {
            countedStart_ = start;
            countedEnd_ = end;
            selectedCharacters_ = WndProc(Message::CountCharacters, start, end);
            selectionCountDirty_ = false;
        }
        return selectedCharacters_;
    }

    void Draw(wxDC& dc) {
        window_.UpdateLineNumberMargin();
        auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
        surface->Init(&dc, &window_);
        surface->SetMode({65001});
        const auto size = window_.GetClientSize();
        rcPaint = core::PRectangle(0, 0, size.x, size.y);
        paintState = PaintState::painting;
        paintingAllText = true;
        Paint(surface.get(), rcPaint);
        paintState = PaintState::notPainting;
        UpdateSystemCaret();
    }
    void Resize() {
        ChangeSize();
        if (Wrapping()) UpdateWrapLayout();
    }
    void UpdateWrapLayout() {
        WrapLines(WrapScope::wsAll);
        EnsureCaretVisible();
    }
    void DpiChanged() {
        if (WndProc(Message::GetMarginWidthN, 1, 0) > 0) {
            WndProc(Message::SetMarginWidthN, 1, window_.FromDIP(16));
            for (int marker = 25; marker <= 31; ++marker)
                WndProc(Message::MarkerSetStrokeWidth, marker, window_.FromDIP(100));
        }
        InvalidateStyleRedraw();
        window_.lineNumberWidthDirty_ = true;
        window_.UpdateLineNumberMargin();
        ChangeSize();
    }
    void Character(std::string_view text) { InsertCharacter(text, Scintilla::CharacterSource::DirectInput); }
    bool EditingKey(const wxKeyEvent& event) {
        if (event.MetaDown()) return false;
        if (event.GetKeyCode() == WXK_ESCAPE && !event.HasAnyModifiers()) {
            // CMD_ESCAPE cancels completion UI, not the core's selection modes.
            if (window_.Send(Message::AutoCActive))
                window_.Send(Message::AutoCCancel);
            else if (window_.Send(Message::CallTipActive))
                window_.Send(Message::CallTipCancel);
            return true;
        }
        // IDR_MAINWND accelerators in pinned Notepad4.rc precede KeyMap.cxx.
        // Keep application commands on their original keys, rather than exposing
        // unrelated core defaults such as Ctrl+L (LineCut) or Ctrl+T (transpose).
        Message applicationCommand{};
        if (event.ControlDown() && !event.AltDown()) {
            switch (event.GetKeyCode()) {
            case WXK_TAB:
                if (!event.ShiftDown()) {
                    const auto useTabs = window_.Send(Message::GetUseTabs);
                    const auto tabIndents = window_.Send(Message::GetTabIndents);
                    window_.Send(Message::SetTabIndents, 0);
                    window_.Send(Message::SetUseTabs, 1);
                    window_.Send(Message::Tab);
                    window_.Send(Message::SetUseTabs, useTabs);
                    window_.Send(Message::SetTabIndents, tabIndents);
                    return true;
                }
                break;
            case 'C':
            case 'X':
                if (event.ShiftDown()) {
                    window_.Send(event.GetKeyCode() == 'C' ? Message::LineCopy : Message::LineCut, 1);
                    return true;
                }
                break;
            case 'D':
                applicationCommand = event.ShiftDown() ? Message::LineDelete : Message::LineDuplicate;
                break;
            case 'Y':
                applicationCommand = event.ShiftDown() ? Message::Undo : Message::Redo;
                break;
            case 'Z':
                applicationCommand = event.ShiftDown() ? Message::Redo : Message::Undo;
                break;
            case 'U':
                window_.Send(Message::CustomCaseMapping,
                             static_cast<uintptr_t>(event.ShiftDown() ? CaseMapping::upper : CaseMapping::lower));
                return true;
            default:
                break;
            }
        } else if (event.AltDown() && !event.ControlDown() && !event.ShiftDown()) {
            if (event.GetKeyCode() == 'D') applicationCommand = Message::SelectionDuplicate;
            if (event.GetKeyCode() == 'S') applicationCommand = Message::LineTranspose;
        }
        if (applicationCommand != Message{}) {
            window_.Send(applicationCommand);
            return true;
        }
        Scintilla::Keys key;
        switch (event.GetKeyCode()) {
        case WXK_RETURN:
        case WXK_NUMPAD_ENTER:
            key = Scintilla::Keys::Return;
            break;
        case WXK_TAB:
            key = Scintilla::Keys::Tab;
            break;
        case WXK_BACK:
            key = Scintilla::Keys::Back;
            break;
        case WXK_DELETE:
            key = Scintilla::Keys::Delete;
            break;
        case WXK_INSERT:
            key = Scintilla::Keys::Insert;
            break;
        case WXK_ESCAPE:
            key = Scintilla::Keys::Escape;
            break;
        case 'A':
        case 'C':
        case 'V':
        case 'X':
        case 'Y':
        case 'Z':
        case '[':
        case ']':
            key = static_cast<Scintilla::Keys>(event.GetKeyCode());
            break;
        default:
            return false;
        }
        bool consumed = false;
        KeyDownWithModifiers(key,
                             static_cast<Scintilla::KeyMod>((event.ControlDown() ? 2 : 0) |
                                                            (event.ShiftDown() ? 1 : 0) | (event.AltDown() ? 4 : 0)),
                             &consumed);
        return consumed;
    }
    bool Navigation(const wxKeyEvent& event) {
        if (event.MetaDown()) return false;
        Scintilla::Keys coreKey;
        switch (event.GetKeyCode()) {
        case WXK_LEFT:
            coreKey = Scintilla::Keys::Left;
            break;
        case WXK_RIGHT:
            coreKey = Scintilla::Keys::Right;
            break;
        case WXK_UP:
            coreKey = Scintilla::Keys::Up;
            break;
        case WXK_DOWN:
            coreKey = Scintilla::Keys::Down;
            break;
        case WXK_HOME:
            coreKey = Scintilla::Keys::Home;
            break;
        case WXK_END:
            coreKey = Scintilla::Keys::End;
            break;
        case WXK_PAGEUP:
            coreKey = Scintilla::Keys::Prior;
            break;
        case WXK_PAGEDOWN:
            coreKey = Scintilla::Keys::Next;
            break;
        default:
            return false;
        }
        bool consumed = false;
        KeyDownWithModifiers(coreKey,
                             static_cast<Scintilla::KeyMod>((event.ControlDown() ? 2 : 0) |
                                                            (event.ShiftDown() ? 1 : 0) | (event.AltDown() ? 4 : 0)),
                             &consumed);
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
        if (type == wxEVT_SCROLLWIN_LINEUP)
            position = current - line;
        else if (type == wxEVT_SCROLLWIN_LINEDOWN)
            position = current + line;
        else if (type == wxEVT_SCROLLWIN_PAGEUP)
            position = current - page;
        else if (type == wxEVT_SCROLLWIN_PAGEDOWN)
            position = current + page;
        else if (type == wxEVT_SCROLLWIN_TOP)
            position = 0;
        else if (type == wxEVT_SCROLLWIN_BOTTOM)
            position = vertical ? static_cast<int>(MaxScrollPos()) : scrollWidth;
        if (vertical)
            ScrollTo(position);
        else
            HorizontalScrollTo(std::clamp(position, 0, std::max(0, scrollWidth - window_.GetClientSize().x)));
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
            RepairFoldSelection();
        } else if (event.GetEventType() == wxEVT_LEFT_UP) {
            ButtonUpWithModifiers(point, MouseTime(), MouseModifiers(event));
        } else {
            ButtonMoveWithModifiers(point, MouseTime(), MouseModifiers(event));
        }
    }
    void CaptureLost() { ChangeMouseCapture(false); }
    void SyncSystemCaret() { UpdateSystemCaret(); }
    void RepairFoldSelection() {
        // The core keeps contraction state but can leave the selection in
        // hidden text. Move only hidden endpoints to their visible header.
        const auto visibleEndpoint = [this](Sci::Position position) {
            auto line = WndProc(Message::LineFromPosition, position, 0);
            if (WndProc(Message::GetLineVisible, line, 0)) return position;
            while (line > 0 && !WndProc(Message::GetLineVisible, line, 0)) {
                line = std::max<intptr_t>(0, WndProc(Message::GetFoldParent, line, 0));
            }
            return static_cast<Sci::Position>(WndProc(Message::PositionFromLine, line, 0));
        };
        const auto caret = visibleEndpoint(sel.MainCaret());
        const auto anchor = visibleEndpoint(sel.MainAnchor());
        if (caret != sel.MainCaret() || anchor != sel.MainAnchor()) {
            SetSelection(caret, anchor);
            EnsureCaretVisible();
            UpdateSystemCaret();
        }
    }

private:
    EditorControl& window_;
    int verticalWheel_ = 0;
    int horizontalWheel_ = 0;
    void NotifyCaretMove() const noexcept override {}
    void UpdateSystemCaret() override {
#ifdef __WXMSW__
        if (!window_.HasFocus() || !window_.GetCaret()) return;
        const auto point = PointMainCaret();
        const int height = std::max(1, vs.lineHeight);
        window_.GetCaret()->SetSize(1, height);
        window_.GetCaret()->Move(static_cast<int>(point.x), static_cast<int>(point.y));
        const auto window = static_cast<HWND>(window_.GetHandle());
        if (const auto context = ImmGetContext(window)) {
            // Microsoft Pinyin needs explicit IMM forms in addition to the
            // system caret. All positions are in the editor's client coordinates.
            COMPOSITIONFORM composition{};
            composition.dwStyle = CFS_POINT;
            composition.ptCurrentPos = {static_cast<LONG>(point.x), static_cast<LONG>(point.y)};
            ::GetClientRect(window, &composition.rcArea);
            ImmSetCompositionWindow(context, &composition);
            CANDIDATEFORM candidate{};
            candidate.dwStyle = CFS_EXCLUDE;
            candidate.ptCurrentPos = {static_cast<LONG>(point.x), static_cast<LONG>(point.y) + height};
            candidate.rcArea = {static_cast<LONG>(point.x), static_cast<LONG>(point.y), static_cast<LONG>(point.x) + 1,
                                static_cast<LONG>(point.y) + height};
            ImmSetCandidateWindow(context, &candidate);
            LOGFONTW font{};
            font.lfHeight = -height;
            font.lfWeight = FW_NORMAL;
            font.lfCharSet = DEFAULT_CHARSET;
            const auto name = wxString::FromUTF8(core::Platform::DefaultFont()).ToStdWstring();
            std::wcsncpy(font.lfFaceName, name.c_str(), LF_FACESIZE - 1);
            ImmSetCompositionFontW(context, &font);
            ImmReleaseContext(window, context);
        }
#endif
    }
    void SetHorizontalScrollPos() override {
        window_.SetScrollPos(wxHORIZONTAL, xOffset);
        UpdateSystemCaret();
    }
    bool ModifyScrollBars(Sci::Line maximum, Sci::Line page) override {
        const auto before = window_.GetClientSize();
        window_.SetScrollbar(wxVERTICAL, static_cast<int>(topLine), static_cast<int>(page),
                             static_cast<int>(maximum + 1));
        window_.SetScrollbar(wxHORIZONTAL, xOffset, window_.GetClientSize().x, scrollWidth);
        return before != window_.GetClientSize();
    }
    void SetVerticalScrollPos() override {
        window_.SetScrollPos(wxVERTICAL, static_cast<int>(topLine));
        UpdateSystemCaret();
    }
    bool WriteClipboard(const core::SelectionText& text) const {
        if (wxTheClipboard->IsOpened()) return false;
        wxTheClipboard->UsePrimarySelection(false);
        wxClipboardLocker lock;
        if (!lock) return false;
#ifdef __WXGTK__
        // Retain each publication with GTK's owner lifecycle. The legacy wx
        // selection path returned empty text after repeat Wayland publications.
        auto* value = new std::string(text.AsView());
        auto* list = gtk_target_list_new(nullptr, 0);
        gtk_target_list_add_text_targets(list, 0);
        gint count = 0;
        auto* targets = gtk_target_table_new_from_list(list, &count);
        const bool copied = gtk_clipboard_set_with_data(
            gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), targets, count,
            [](GtkClipboard*, GtkSelectionData* data, guint, gpointer owner) {
                const auto* value = static_cast<const std::string*>(owner);
                gtk_selection_data_set_text(data, value->c_str(), -1);
            },
            [](GtkClipboard*, gpointer owner) { delete static_cast<std::string*>(owner); }, value);
        gtk_target_table_free(targets, count);
        gtk_target_list_unref(list);
        if (!copied) delete value;
        return copied;
#else
        return wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(text.Data(), text.Length())));
#endif
    }
    void Copy(bool) const override {
        if (sel.Empty()) return;
        core::SelectionText text;
        CopySelectionRange(text);
        WriteClipboard(text);
    }
    void CopyToClipboard(const core::SelectionText& text) const override { WriteClipboard(text); }
    void Cut(bool, bool = false) override {
        pdoc->CheckReadOnly();
        if (sel.Empty() || pdoc->IsReadOnly() || SelectionContainsProtected()) return;
        core::SelectionText text;
        CopySelectionRange(text);
        if (WriteClipboard(text)) ClearSelection();
    }
    bool CanPaste() const noexcept override {
        if (!core::Editor::CanPaste()) return false;
#ifdef __WXMSW__
        return ::IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
#else
        // Native clipboard queries may pump UI events. An update-ui callback
        // must not close the clipboard currently used by Copy/Paste.
        if (wxTheClipboard->IsOpened()) return false;
        wxTheClipboard->UsePrimarySelection(false);
        wxClipboardLocker lock;
        return !(!lock) && wxTheClipboard->IsSupported(wxDF_UNICODETEXT);
#endif
    }
    void Paste(bool) override {
        if (!core::Editor::CanPaste() || wxTheClipboard->IsOpened()) return;
        wxTextDataObject text;
        {
            wxTheClipboard->UsePrimarySelection(false);
            wxClipboardLocker lock;
            if (!lock || !wxTheClipboard->GetData(text)) return;
        }
        // Keep both owners alive through insertion. On UTF-8-native wx builds
        // ToUTF8() may borrow the source wxString's storage.
        const wxString value = text.GetText();
        const auto utf8 = value.ToUTF8();
        const core::UndoGroup undo(pdoc);
        ClearSelection();
        InsertPasteShape(std::string_view(utf8.data(), utf8.length()), PasteShape::stream);
    }
    void NotifyChange() const noexcept override {}
    void NotifyParent(Scintilla::NotificationData& notification) const noexcept override {
        SelectionRevision();
        if (notification.nmhdr.code == Scintilla::Notification::Modified &&
            (static_cast<int>(notification.modificationType) &
             static_cast<int>(Scintilla::ModificationFlags::InsertText | Scintilla::ModificationFlags::DeleteText))) {
            ++window_.documentRevision_;
            // An edit within the cached range changes its contents; an edit
            // before it can move its endpoints, even across a batch of edits
            // that returns them to their old positions before the next query.
            if (notification.position <= countedEnd_) selectionCountDirty_ = true;
            if (notification.linesAdded != 0) window_.lineNumberWidthDirty_ = true;
        }
        if (notification.nmhdr.code == Scintilla::Notification::UpdateUI ||
            notification.nmhdr.code == Scintilla::Notification::SavePointReached ||
            notification.nmhdr.code == Scintilla::Notification::SavePointLeft) {
            auto* event = new wxCommandEvent(wxEVT_EDITOR_STATE_CHANGED, window_.GetId());
            event->SetEventObject(&window_);
            wxQueueEvent(&window_, event);
        }
    }
    std::unique_ptr<core::CaseFolder> CaseFolderForEncoding() const override {
        // The document always contains UTF-8, including when disk encoding differs.
        return std::make_unique<core::CaseFolderUnicode>();
    }
    std::string CaseMapString(const std::string& value, CaseMapping mapping) const override {
        wxString text = wxString::FromUTF8(value);
        if (mapping == CaseMapping::upper)
            text.MakeUpper();
        else if (mapping == CaseMapping::lower)
            text.MakeLower();
        return text.ToStdString(wxConvUTF8);
    }
    void StartDrag() override {}
    bool FineTickerRunning(TickReason reason) const noexcept override {
        if (reason == TickReason::scroll) return window_.scrollTimer_.IsRunning();
        if (reason == TickReason::widen) return window_.widenTimer_.IsRunning();
        return reason == TickReason::caret && window_.caretTimer_.IsRunning();
    }
    void FineTickerStart(TickReason reason, int millis, int) noexcept override {
        if (reason == TickReason::caret)
            window_.caretTimer_.Start(millis);
        else if (reason == TickReason::scroll)
            window_.scrollTimer_.Start(millis);
        else if (reason == TickReason::widen)
            window_.widenTimer_.Start(millis);
    }
    void FineTickerCancel(TickReason reason) noexcept override {
        if (reason == TickReason::caret)
            window_.caretTimer_.Stop();
        else if (reason == TickReason::scroll)
            window_.scrollTimer_.Stop();
        else if (reason == TickReason::widen)
            window_.widenTimer_.Stop();
    }
    bool SetIdle(bool) noexcept override { return false; }
    void SetMouseCapture(bool on) noexcept override {
        if (on && !window_.HasCapture())
            window_.CaptureMouse();
        else if (!on && window_.HasCapture())
            window_.ReleaseMouse();
    }
    bool HaveMouseCapture() const noexcept override { return window_.HasCapture(); }
    void UpdateBaseElements() noexcept override {}
    bool ValidCodePage(int codepage) const noexcept override { return codepage == 65001; }
    std::string UTF8FromEncoded(std::string_view text) const override { return std::string(text); }
    std::string EncodedFromUTF8(std::string_view text) const override { return std::string(text); }
    intptr_t DefWndProc(Message, uintptr_t, intptr_t) override { return 0; }
    void SCICALL CreateCallTipWindow(core::PRectangle) noexcept override {}
    mutable Sci::Position observedAnchor_ = -1;
    mutable Sci::Position observedCaret_ = -1;
    mutable uint64_t selectionRevision_ = 0;
    intptr_t countedStart_ = -1;
    intptr_t countedEnd_ = -1;
    intptr_t selectedCharacters_ = 0;
    mutable bool selectionCountDirty_ = true;
};

EditorControl::EditorControl(wxWindow* parent)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                wxBORDER_SIMPLE | wxWANTS_CHARS | wxVSCROLL | wxHSCROLL),
      caretTimer_(this), scrollTimer_(this), widenTimer_(this) {
    // The native Notepad4 host initializes these tables before any document
    // copies its ASCII word classes. The wx host must do the same.
    [[maybe_unused]] static const bool unicodeDataInitialized = [] {
        core::CharClassify::InitUnicodeData();
        return true;
    }();
    SetBackgroundStyle(wxBG_STYLE_PAINT);
#ifdef __WXMSW__
    SetCaret(new wxCaret(this, 1, 1));
    GetCaret()->Show();
#endif
    core_ = std::make_unique<Core>(*this);
    Send(Message::SetMarginTypeN, 0, static_cast<intptr_t>(Scintilla::MarginType::Number));
    Send(Message::SetMarginMaskN, 0, 0);
    SetLineNumbersVisible(true);
#ifdef __WXMSW__
    // Use one native caret for display and for input methods on Windows.
    Send(Message::SetCaretStyle, 0);
#endif
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        core_->DpiChanged();
        Refresh(false);
        event.Skip();
    });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
        Send(Message::SetFocus, 1);
        core_->SyncSystemCaret();
        Refresh(false);
        event.Skip();
    });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        Send(Message::SetFocus, 0);
        Refresh(false);
        event.Skip();
    });
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
        if (core_->Navigation(event)) return;
        if (event.ControlDown() && !event.ShiftDown() && !event.AltDown() && !event.MetaDown() &&
            (event.GetKeyCode() == WXK_BACK || event.GetKeyCode() == WXK_DELETE)) {
            // Notepad4.cpp CMD_CTRLBACK/CMD_CTRLDEL run before the core key map.
            const auto caret = Send(Message::GetCurrentPos);
            if (caret != Send(Message::GetAnchor)) {
                Send(Message::SetSel, caret, caret);
            } else {
                const auto line = Send(Message::LineFromPosition, caret);
                const auto start = Send(Message::PositionFromLine, line);
                if (event.GetKeyCode() == WXK_BACK)
                    Send(caret == start                                        ? Message::DeleteBack
                         : caret <= Send(Message::GetLineIndentPosition, line) ? Message::DelLineLeft
                                                                               : Message::DelWordLeft);
                else
                    Send(start == Send(Message::GetLineEndPosition, line) ? Message::LineDelete
                                                                          : Message::DelWordRight);
            }
            return;
        }
        if (event.GetKeyCode() == WXK_TAB && event.ShiftDown() && !event.ControlDown() && !event.AltDown() &&
            !event.MetaDown()) {
            IndentLines(false);
            return;
        }
        if (!core_->EditingKey(event)) event.Skip();
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

void EditorControl::IndentLines(bool forwards) {
    if (Send(Message::GetReadOnly)) return;
    if (Send(Message::GetSelectionEmpty)) {
        // The core excludes an endpoint at a line start. Move an empty
        // selection to its line end so the explicit line command includes it.
        const auto line = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
        Send(Message::GotoPos, Send(Message::GetLineEndPosition, line));
    }
    Send(forwards ? Message::LineIndent : Message::LineDedent);
}

void EditorControl::ToggleWordWrap() {
    const auto mode = Send(Message::GetWrapMode) == static_cast<intptr_t>(Scintilla::Wrap::None)
                          ? Scintilla::Wrap::Word
                          : Scintilla::Wrap::None;
    Send(Message::SetWrapMode, static_cast<uintptr_t>(mode));
    core_->UpdateWrapLayout();
}

void EditorControl::SetIndentationWidth(int width) {
    Send(Message::SetTabWidth, width);
    Send(Message::SetIndent, width);
    core_->UpdateWrapLayout();
}

intptr_t EditorControl::Send(Message message, uintptr_t wParam, intptr_t lParam) {
    core_->SelectionRevision();
    const auto result = core_->WndProc(message, wParam, lParam);
    if (message == Message::StyleClearAll || message == Message::StyleSetFont || message == Message::StyleSetSize ||
        message == Message::StyleSetSizeFractional || message == Message::StyleSetBold ||
        message == Message::StyleSetItalic || message == Message::SetZoom)
        lineNumberWidthDirty_ = true;
    UpdateLineNumberMargin();
    if (message == Message::FoldAll) core_->RepairFoldSelection();
    core_->SelectionRevision();
    return result;
}

uint64_t EditorControl::SelectionRevision() const {
    return core_->SelectionRevision();
}

intptr_t EditorControl::SelectionCharacterCount() {
    return core_->SelectionCharacterCount();
}

void EditorControl::SetLineNumbersVisible(bool visible) {
    lineNumbersVisible_ = visible;
    lineNumberWidthDirty_ = true;
    UpdateLineNumberMargin();
    Send(Message::ScrollCaret);
}

void EditorControl::UpdateLineNumberMargin() {
    if (!lineNumberWidthDirty_) return;
    lineNumberWidthDirty_ = false;
    intptr_t width = 0;
    if (lineNumbersVisible_) {
        const auto count = core_->WndProc(Message::GetLineCount, 0, 0);
        const std::string digits(std::to_string(count).size(), '9');
        // Like Notepad4's UpdateLineNumberWidth, measure the core's number style.
        // Use the required digit count and native DIP padding, without reading text.
        width = core_->WndProc(Message::TextWidth, 33, reinterpret_cast<intptr_t>(digits.c_str())) + FromDIP(8);
    }
    if (core_->WndProc(Message::GetMarginWidthN, 0, 0) != width) {
        core_->WndProc(Message::SetMarginWidthN, 0, width);
        // A changed text width invalidates wrapping. Reveal the caret against
        // the new display rows, rather than scrolling with the old layout.
        core_->UpdateWrapLayout();
    }
}

std::optional<std::string> EditorControl::ExpandRegexReplacement(const std::string& replacement) {
    Sci::Position length = static_cast<Sci::Position>(replacement.size());
    const char* expanded = core_->pdoc->SubstituteByPosition(replacement.c_str(), &length);
    if (!expanded) return std::nullopt;
    return std::string(expanded, static_cast<size_t>(length));
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

#ifdef __WXMSW__
WXLRESULT EditorControl::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {
    if (core_ && (message == WM_IME_STARTCOMPOSITION || message == WM_IME_COMPOSITION || message == WM_IME_SETCONTEXT ||
                  message == WM_INPUTLANGCHANGE)) {
        core_->SyncSystemCaret();
    }
    return wxControl::MSWWindowProc(message, wParam, lParam);
}
#endif
