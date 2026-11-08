#include "editor/editor_core.h"
#include "editor/display_units.h"
#include "editor/editor_ids.h"
#include "editor/platform/native_encoding.h"
#include "editor/platform/clipboard.h"
#include "editor/platform/custom_case.h"
#include "editor/platform/gtk_drag_drop.h"
#include <wx/dcclient.h>
#include <wx/dataobj.h>
#include <wx/dnd.h>
#include <wx/settings.h>
#include <chrono>
#include <cmath>

using Scintilla::Message;
namespace core = Scintilla::Internal;

namespace {
unsigned MouseTime() {
    return static_cast<unsigned>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
Scintilla::KeyMod KeyModifiers(const wxKeyboardState& event) {
    return static_cast<Scintilla::KeyMod>((event.ShiftDown() ? 1 : 0) | (event.ControlDown() ? 2 : 0) |
                                          (event.AltDown() ? 4 : 0));
}
std::optional<Scintilla::Keys> CoreKeyCode(int code) {
    using enum Scintilla::Keys;
    switch (code) {
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
        return Return;
    case WXK_TAB:
        return Tab;
    case WXK_BACK:
        return Back;
    case WXK_DELETE:
        return Delete;
    case WXK_INSERT:
        return Insert;
    case WXK_ESCAPE:
        return Escape;
    case 'A':
    case 'C':
    case 'V':
    case 'X':
    case 'Y':
    case 'Z':
    case '[':
    case ']':
        return static_cast<Scintilla::Keys>(code);
    default:
        return std::nullopt;
    }
}

bool HandleClipboardKey(EditorControl& window, const wxKeyEvent& event) {
    if (event.AltDown()) return false;
    switch (event.GetKeyCode()) {
    case WXK_INSERT:
        if (!event.ControlDown() || event.ShiftDown()) return false;
        window.Copy();
        return true;
    case WXK_DELETE:
        if (!event.ShiftDown() || event.ControlDown()) return false;
        window.Cut();
        return true;
    case 'C':
        if (!event.ControlDown()) return false;
        if (event.ShiftDown())
            window.CopyLine();
        else
            window.Copy();
        return true;
    case 'X':
        if (!event.ControlDown()) return false;
        if (event.ShiftDown())
            window.CutLine();
        else
            window.Cut();
        return true;
    default:
        return false;
    }
}

bool HandleControlKey(EditorControl& window, const wxKeyEvent& event) {
    switch (event.GetKeyCode()) {
    case '\\':
        if (event.ShiftDown()) return false;
        window.Send(Message::SetZoom, DisplayUnits::ZoomPercentScale);
        return true;
    case WXK_TAB: {
        if (event.ShiftDown()) return false;
        const auto useTabs = window.Send(Message::GetUseTabs);
        const auto tabIndents = window.Send(Message::GetTabIndents);
        window.Send(Message::SetTabIndents, 0);
        window.Send(Message::SetUseTabs, 1);
        window.Send(Message::Tab);
        window.Send(Message::SetUseTabs, useTabs);
        window.Send(Message::SetTabIndents, tabIndents);
        return true;
    }
    default:
        return false;
    }
}

Message ApplicationKeyCommand(const wxKeyEvent& event) {
    if (event.AltDown() && !event.ControlDown() && !event.ShiftDown()) {
        if (event.GetKeyCode() == 'D') return Message::SelectionDuplicate;
        if (event.GetKeyCode() == 'S') return Message::LineTranspose;
    }
    if (!event.ControlDown() || event.AltDown()) return Message{};
    switch (event.GetKeyCode()) {
    case '+':
    case '=':
    case WXK_NUMPAD_ADD:
        return event.ShiftDown() ? Message{} : Message::ZoomIn;
    case '-':
    case WXK_NUMPAD_SUBTRACT:
        return event.ShiftDown() ? Message{} : Message::ZoomOut;
    case 'D':
        return event.ShiftDown() ? Message::LineDelete : Message::LineDuplicate;
    case 'Y':
        return event.ShiftDown() ? Message::Undo : Message::Redo;
    case 'Z':
        return event.ShiftDown() ? Message::Redo : Message::Undo;
    default:
        return Message{};
    }
}
} // namespace

class EditorControl::Core::TextDropTarget final : public wxDropTarget {
public:
    explicit TextDropTarget(Core& editor) : wxDropTarget(new wxTextDataObject), editor_(editor) {
        SetDefaultAction(wxDragMove);
    }
    wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult result) override {
        if (editor_.pdoc->IsReadOnly() || (result != wxDragCopy && result != wxDragMove)) {
            editor_.ClearDropFeedback();
            return wxDragNone;
        }
#ifdef __WXGTK__
        // wxGTK's DefaultMove preference suppresses GTK's Ctrl suggestion.
        // The original host uses Ctrl for copy, including Ctrl+Shift.
        if (wxGetKeyState(WXK_CONTROL)) result = wxDragCopy;
#endif
        editor_.ShowDropFeedback(x, y);
        return result;
    }
    void OnLeave() override { editor_.ClearDropFeedback(); }
    bool OnDrop(wxCoord, wxCoord) override { return !editor_.pdoc->IsReadOnly(); }
    wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult result) override {
        const auto action = OnDragOver(x, y, result);
        editor_.ClearDropFeedback();
        if (action == wxDragNone || !GetData()) return wxDragNone;
        const auto& value = static_cast<wxTextDataObject*>(GetDataObject())->GetText();
        const auto utf8 = value.ToUTF8();
        if (utf8.length() == 0) return wxDragNone;
        const auto position =
            editor_.SPositionFromLocation(core::Point(x, y), false, false, editor_.UserVirtualSpace());
        editor_.DropAt(position, std::string_view(utf8.data(), utf8.length()), action == wxDragMove, false);
        return action;
    }

private:
    Core& editor_;
};

intptr_t EditorControl::Core::WndProc(Message message, uintptr_t wParam, intptr_t lParam) {
    if (message == Message::SetLexer || message == Message::ClearDocumentStyle || message == Message::Colourise)
        braceStyleChanged_ = true;
    // Original ScintillaWin::WndProc reports message failures through GetStatus.
    // Read-only enclosure can intentionally attempt a target beyond EOF after
    // the preceding insertion was rejected, then continue selection repair.
    try {
        const auto result = ScintillaBase::WndProc(message, wParam, lParam);
        if ((message == Message::SetText || message == Message::ClearAll) && !pdoc->IsReadOnly())
            ScintillaBase::WndProc(Message::SetScrollWidth, 1, 0);
        return result;
    } catch (const std::bad_alloc&) {
        errorStatus = Scintilla::Status::BadAlloc;
    } catch (const core::Failure& failure) {
        errorStatus = failure.status;
    }
    return 0;
}

EditorControl::Core::Core(EditorControl& window, int selectionOptions) : window_(window) {
    wMain = static_cast<wxWindow*>(&window_);
    Initialise();
    // Original Notepad4.cpp: wrapped Home/End visit visual then logical boundaries.
    WndProc(Message::AssignCmdKey, static_cast<uintptr_t>(Scintilla::Keys::Home),
            static_cast<intptr_t>(Message::VCHomeWrap));
    WndProc(Message::AssignCmdKey,
            static_cast<uintptr_t>(Scintilla::Keys::Home) | (static_cast<uintptr_t>(Scintilla::KeyMod::Shift) << 16),
            static_cast<intptr_t>(Message::VCHomeWrapExtend));
    WndProc(Message::AssignCmdKey, static_cast<uintptr_t>(Scintilla::Keys::End),
            static_cast<intptr_t>(Message::LineEndWrap));
    WndProc(Message::AssignCmdKey,
            static_cast<uintptr_t>(Scintilla::Keys::End) | (static_cast<uintptr_t>(Scintilla::KeyMod::Shift) << 16),
            static_cast<intptr_t>(Message::LineEndWrapExtend));
    // Original application overrides the core's Ctrl+Page scroll bindings.
    WndProc(Message::AssignCmdKey,
            static_cast<uintptr_t>(Scintilla::Keys::Prior) | (static_cast<uintptr_t>(Scintilla::KeyMod::Ctrl) << 16),
            static_cast<intptr_t>(Message::ParaUp));
    WndProc(Message::AssignCmdKey,
            static_cast<uintptr_t>(Scintilla::Keys::Next) | (static_cast<uintptr_t>(Scintilla::KeyMod::Ctrl) << 16),
            static_cast<intptr_t>(Message::ParaDown));
    WndProc(Message::AssignCmdKey,
            static_cast<uintptr_t>(Scintilla::Keys::Prior) |
                ((static_cast<uintptr_t>(Scintilla::KeyMod::Ctrl) | static_cast<uintptr_t>(Scintilla::KeyMod::Shift))
                 << 16),
            static_cast<intptr_t>(Message::ParaUpExtend));
    WndProc(Message::AssignCmdKey,
            static_cast<uintptr_t>(Scintilla::Keys::Next) |
                ((static_cast<uintptr_t>(Scintilla::KeyMod::Ctrl) | static_cast<uintptr_t>(Scintilla::KeyMod::Shift))
                 << 16),
            static_cast<intptr_t>(Message::ParaDownExtend));
    WndProc(Message::SetCodePage, Scintilla::CpUtf8, 0);
    // Match unconfigured Notepad4 on every host; saving preserves actual EOLs.
    WndProc(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::CrLf), 0);
    const auto defaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
    WndProc(Message::StyleSetFont, defaultStyle, reinterpret_cast<intptr_t>(core::Platform::DefaultFont()));
    WndProc(Message::StyleSetSize, defaultStyle, core::Platform::DefaultFontSize());
    WndProc(Message::StyleSetBack, defaultStyle, 0xFFFFFF);
    WndProc(Message::StyleClearAll, 0, 0);
    // Original Mark Occurrences indicator and default bookmark background.
    WndProc(Message::IndicSetStyle, EditorIndicator::Occurrence,
            static_cast<intptr_t>(Scintilla::IndicatorStyle::RoundBox));
    WndProc(Message::IndicSetFore, EditorIndicator::Occurrence,
            wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT).GetRGB());
    WndProc(Message::IndicSetAlpha, EditorIndicator::Occurrence, 25);
    WndProc(Message::MarkerDefine, 0, static_cast<intptr_t>(Scintilla::MarkerSymbol::Background));
    WndProc(Message::MarkerSetBackTranslucent, 0, 0x2800FF00);
    WndProc(Message::MarkerSetLayer, 0, static_cast<intptr_t>(Scintilla::Layer::OverText));
    WndProc(Message::SetCaretStyle, static_cast<uintptr_t>(Scintilla::CaretStyle::Line), 0);
    WndProc(Message::SetMarginWidthN, EditorMargin::Bookmark, 0);
    WndProc(Message::SetScrollWidth, 1, 0);
    WndProc(Message::SetScrollWidthTracking, 1, 0);
#ifdef __WXMSW__
    WndProc(Message::SetIMEInteraction, static_cast<uintptr_t>(Scintilla::IMEInteraction::Inline), 0);
#endif
    WndProc(Message::SetMultipleSelection, selectionOptions & SelectOption_EnableMultipleSelection, 0);
    WndProc(Message::SetAdditionalSelectionTyping, 1, 0);
    WndProc(Message::SetMultiPaste, static_cast<uintptr_t>(Scintilla::MultiPaste::Each), 0);
    WndProc(Message::SetVirtualSpaceOptions, static_cast<uintptr_t>(Scintilla::VirtualSpace::RectangularSelection), 0);
    const auto history = (selectionOptions & SelectOption_UndoRedoRememberSelection)
                             ? static_cast<uintptr_t>(Scintilla::UndoSelectionHistoryOption::Enabled) |
                                   static_cast<uintptr_t>(Scintilla::UndoSelectionHistoryOption::Scroll)
                             : 0;
    WndProc(Message::SetUndoSelectionHistory, history, 0);
#ifdef __WXGTK__
    gtkDragDrop_ = std::make_unique<GtkTextDragDrop>(
        window_,
        GtkTextDragDrop::Callbacks{[this] { return pdoc->IsReadOnly(); },
                                   [this](int x, int y) { ShowDropFeedback(x, y); }, [this] { ClearDropFeedback(); },
                                   [this](int x, int y, std::string_view text, bool move) {
                                       const auto position =
                                           SPositionFromLocation(core::Point(x, y), false, false, UserVirtualSpace());
                                       DropAt(position, text, move, false);
                                   }});
#else
    window_.SetDropTarget(new TextDropTarget(*this));
#endif
    WndProc(Message::EmptyUndoBuffer, 0, 0);
    WndProc(Message::SetSavePoint, 0, 0);
}

EditorControl::Core::~Core() {
#ifdef __WXMSW__
    DestroySystemCaret();
#endif
#ifdef __WXGTK__
    gtkDragDrop_.reset();
#endif
    Finalise();
}

uint64_t EditorControl::Core::SelectionRevision() const noexcept {
    const auto anchor = sel.MainAnchor();
    const auto caret = sel.MainCaret();
    if (anchor != observedAnchor_ || caret != observedCaret_) {
        observedAnchor_ = anchor;
        observedCaret_ = caret;
        ++selectionRevision_;
    }
    return selectionRevision_;
}

intptr_t EditorControl::Core::SelectionCharacterCount() {
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

void EditorControl::Core::Draw(wxDC& dc) {
    window_.UpdateLineNumberMargin();
    auto surface = core::Surface::Allocate(Scintilla::Technology::Default);
    surface->Init(&dc, &window_);
    surface->SetMode({Scintilla::CpUtf8});
    const auto size = window_.GetClientSize();
    rcPaint = core::PRectangle(0, 0, size.x, size.y);
    paintState = PaintState::painting;
    paintingAllText = true;
    const bool shrink = window_.AutoShrinkScrollWidth() && scrollWidthDirty_ && !Wrapping();
    if (shrink) {
        scrollWidthDirty_ = false;
        view.lineWidthMaxSeen = 0;
    }
    Paint(surface.get(), rcPaint);
    paintState = PaintState::notPainting;
    if (shrink && view.lineWidthMaxSeen < scrollWidth) {
        // Reuse the core's normal visible-line measurements, avoiding a full
        // document scan on each edit. Offscreen lines grow the range when shown.
        scrollWidth = std::max(1, view.lineWidthMaxSeen);
        const int page = static_cast<int>(GetTextRectangle().Width());
        HorizontalScrollTo(std::min(xOffset, std::max(0, scrollWidth - page + 1)));
        SetScrollBars();
    }
    // Style-only notifications do not queue UpdateUI in the pinned core.
    // Refresh after its real styling/paint pass, then redraw changed overlays.
    if (braceStyleChanged_) {
        braceStyleChanged_ = false;
        UpdateVisualBraceMatching();
    }
    UpdateSystemCaret();
}

void EditorControl::Core::Resize() {
    ChangeSize();
    if (Wrapping()) UpdateWrapLayout();
}

void EditorControl::Core::UpdateWrapLayout() {
    WrapLines(WrapScope::wsAll);
    EnsureCaretVisible();
}

void EditorControl::Core::DpiChanged() {
    for (int marker = static_cast<int>(Scintilla::MarkerOutline::FolderEnd);
         marker <= static_cast<int>(Scintilla::MarkerOutline::FolderOpen); ++marker)
        WndProc(Message::MarkerSetStrokeWidth, marker, window_.FromDIP(100));
    InvalidateStyleRedraw();
    window_.lineNumberWidthDirty_ = true;
    window_.foldMarginWidthDirty_ = true;
    window_.UpdateLineNumberMargin();
    ChangeSize();
}

void EditorControl::Core::Character(std::string_view text) {
    InsertCharacter(text, Scintilla::CharacterSource::DirectInput);
    // Character events bypass WndProc's end-of-message selection snapshot.
    RememberCurrentSelectionForRedoOntoStack();
}

bool EditorControl::Core::EditingKey(const wxKeyEvent& event) {
    if (event.MetaDown()) return false;
    if (HandleApplicationKey(event)) return true;
    const auto key = CoreKeyCode(event.GetKeyCode());
    if (!key) return false;
    bool consumed = false;
    KeyDownWithModifiers(*key, KeyModifiers(event), &consumed);
    return consumed;
}

bool EditorControl::Core::Navigation(const wxKeyEvent& event) {
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
                         static_cast<Scintilla::KeyMod>((event.ControlDown() ? 2 : 0) | (event.ShiftDown() ? 1 : 0) |
                                                        (event.AltDown() ? 4 : 0)),
                         &consumed);
    return consumed;
}

void EditorControl::Core::TickCaret() {
    TickFor(TickReason::caret);
}

void EditorControl::Core::TickScroll() {
    TickFor(TickReason::scroll);
}

void EditorControl::Core::TickWiden() {
    TickFor(TickReason::widen);
}

void EditorControl::Core::Scroll(wxScrollWinEvent& event) {
    const bool vertical = event.GetOrientation() == wxVERTICAL;
    int position = event.GetPosition();
    const int current = vertical ? static_cast<int>(topLine) : xOffset;
    const int line = vertical ? 1 : std::max(1, static_cast<int>(vs.spaceWidth));
    const int page = vertical ? static_cast<int>(LinesOnScreen()) : static_cast<int>(GetTextRectangle().Width());
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
    else {
        const int maximum = horizontalScrollBarVisible && !Wrapping() ? std::max(0, scrollWidth - page + 1) : 0;
        HorizontalScrollTo(std::clamp(position, 0, maximum));
    }
}

void EditorControl::Core::Wheel(wxMouseEvent& event) {
    // Pinned ScintillaWin: horizontal/Shift precedes Ctrl; the physical
    // wheel axis chooses the accumulator even when Shift changes scrolling.
    const bool horizontalAxis = event.GetWheelAxis() == wxMOUSE_WHEEL_HORIZONTAL;
    const bool horizontal = horizontalAxis || event.ShiftDown();
    if (horizontal ? Wrapping() || event.GetColumnsPerAction() == 0 : event.GetLinesPerAction() == 0) return;
    int& remainder = horizontalAxis ? horizontalWheel_ : verticalWheel_;
    remainder += event.GetWheelRotation();
    const int delta = event.GetWheelDelta();
    if (!delta) return;
    const int steps = remainder / delta;
    remainder %= delta;
    if (!steps) return;
    if (horizontal) {
        const int direction = horizontalAxis ? 1 : -1;
        const auto distance =
            static_cast<int>(std::lround(direction * steps * event.GetColumnsPerAction() * vs.aveCharWidth));
        const auto pageWidth = static_cast<int>(GetTextRectangle().Width());
        const int maximum = horizontalScrollBarVisible ? std::max(scrollWidth - pageWidth + 1, 0) : 0;
        HorizontalScrollTo(std::clamp(xOffset + distance, 0, maximum));
    } else if (event.ControlDown()) {
        // One core zoom action per accumulated event, independent of the
        // system's lines per action and the number of notches in the batch.
        window_.Send(steps > 0 ? Message::ZoomIn : Message::ZoomOut);
    } else {
        const auto distance =
            std::max<Sci::Line>(event.IsPageScroll() ? LinesOnScreen() - 1 : event.GetLinesPerAction(), 1);
        ScrollTo(topLine - steps * distance);
    }
}

void EditorControl::Core::Mouse(wxMouseEvent& event) {
    const core::Point point(event.GetX(), event.GetY());
    // Additional selection-shape payloads remain a separate migration slice.
    dragDropEnabled = sel.selType == core::Selection::SelTypes::stream && sel.Count() == 1;
    if (event.GetEventType() == wxEVT_LEFT_DOWN) {
        window_.SetFocus();
        ButtonDownWithModifiers(point, MouseTime(), KeyModifiers(event));
        RepairFoldSelection();
    } else if (event.GetEventType() == wxEVT_LEFT_UP) {
        ButtonUpWithModifiers(point, MouseTime(), KeyModifiers(event));
    } else {
        ButtonMoveWithModifiers(point, MouseTime(), KeyModifiers(event));
    }
}

void EditorControl::Core::CaptureLost() {
    ChangeMouseCapture(false);
}

void EditorControl::Core::SyncSystemCaret() {
    UpdateSystemCaret();
}

void EditorControl::Core::RepairFoldSelection() {
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

void EditorControl::Core::ShowDropFeedback(int x, int y) {
    SetDragPosition(SPositionFromLocation(core::Point(x, y), false, false, UserVirtualSpace()));
}

void EditorControl::Core::ClearDropFeedback() {
    SetDragPosition(core::SelectionPosition(Sci::invalidPosition));
}

bool EditorControl::Core::HandleApplicationKey(const wxKeyEvent& event) {
    if (HandleClipboardKey(window_, event)) return true;
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
    if (event.ControlDown() && !event.AltDown()) {
        if (HandleControlKey(window_, event)) return true;
        if (event.GetKeyCode() == 'U') {
            window_.Send(Message::CustomCaseMapping,
                         static_cast<uintptr_t>(event.ShiftDown() ? CaseMapping::upper : CaseMapping::lower));
            return true;
        }
        if (event.GetKeyCode() == 'V' && !event.ShiftDown() && !event.MetaDown()) {
            // The original application accelerator consumes disabled Ctrl+V.
            // Direct SCI_PASTE retains its separate selection repair.
            if (CanPaste()) window_.Send(Message::Paste);
            return true;
        }
    }
    const auto applicationCommand = ApplicationKeyCommand(event);
    if (applicationCommand != Message{}) {
        window_.Send(applicationCommand);
        return true;
    }
    return false;
}

void EditorControl::Core::NotifyCaretMove() const noexcept {
    // Native caret placement is handled by UpdateSystemCaret; wx UI state
    // notifications are forwarded through NotifyParent.
}

#ifndef __WXMSW__
void EditorControl::Core::UpdateSystemCaret() {
    // Linux input methods use the wx host; the original core paints its caret.
}
#endif

void EditorControl::Core::SetHorizontalScrollPos() {
    window_.SetScrollPos(wxHORIZONTAL, xOffset);
    UpdateSystemCaret();
}

bool EditorControl::Core::ModifyScrollBars(Sci::Line maximum, Sci::Line page) {
    const auto before = window_.GetClientSize();
    window_.SetScrollbar(wxVERTICAL, static_cast<int>(topLine), static_cast<int>(page), static_cast<int>(maximum + 1));
    // ScintillaWin::GetHorizontalScrollRange uses the text viewport and an
    // inclusive document endpoint; wx's range is an item count.
    const int pageWidth = static_cast<int>(GetTextRectangle().Width());
    const int documentEnd = std::max({scrollWidth, pageWidth - 1, 0});
    const int horizontalPage = horizontalScrollBarVisible && !Wrapping() ? pageWidth : documentEnd + 1;
    window_.SetScrollbar(wxHORIZONTAL, xOffset, horizontalPage, documentEnd + 1);
    if (scrollWidth < horizontalPage) HorizontalScrollTo(0);
    return before != window_.GetClientSize();
}

void EditorControl::Core::SetVerticalScrollPos() {
    window_.SetScrollPos(wxVERTICAL, static_cast<int>(topLine));
    UpdateSystemCaret();
}

void EditorControl::Core::Copy(bool) const {
    if (sel.Empty()) return;
    core::SelectionText text;
    CopySelectionRange(text);
    EditorClipboard::Write(window_, text);
}

void EditorControl::Core::CopyToClipboard(const core::SelectionText& text) const {
    EditorClipboard::Write(window_, text);
}

void EditorControl::Core::Cut(bool, bool lineCopy) {
    pdoc->CheckReadOnly();
    if (pdoc->IsReadOnly() || SelectionContainsProtected()) return;
    if (sel.Empty() && !lineCopy) {
        // Original Cut skips Copy on an empty stream but still filters carets.
        ClearSelection();
        return;
    }
    core::SelectionText text;
    if (lineCopy)
        text.Copy(RangeText(sel.RangeMain().Start().Position(), sel.RangeMain().End().Position()), pdoc->dbcsCodePage,
                  false, true);
    else
        CopySelectionRange(text);
    if (EditorClipboard::Write(window_, text)) ClearSelection();
}

bool EditorControl::Core::CanPaste() const noexcept {
    return core::Editor::CanPaste() && EditorClipboard::HasText();
}

void EditorControl::Core::Paste(bool) {
    PasteWithEncoding(false);
}

void EditorControl::Core::PasteWithEncoding(bool defaultEncoding) {
    // Swap supplies the document's encoding context to the same native Paste
    // boundary; ordinary Paste keeps its existing UTF-8 behavior.
    // Original ScintillaWin::Paste repairs selections even when document writes
    // are rejected. Native Paste availability and its actual outcome differ.
    if (!pdoc->IsReadOnly() && !core::Editor::CanPaste()) return;
    auto text = EditorClipboard::Read(window_, [this] { return sel.Empty(); });
    if (!text) return;
    if (defaultEncoding) text->text = NativeDefaultEncodingText(wxString::FromUTF8(text->text)).ToStdString(wxConvUTF8);
    const core::UndoGroup undo(pdoc);
    ClearSelection(multiPasteMode == Scintilla::MultiPaste::Each);
    auto shape = PasteShape::stream;
    if (text->rectangular)
        shape = PasteShape::rectangular;
    else if (text->lineCopy)
        shape = PasteShape::line;
    InsertPasteShape(text->text, shape);
}

void EditorControl::Core::NotifyChange() const noexcept {
    // NotifyParent handles document revisions and forwards wx state changes.
}

void EditorControl::Core::NotifyParent(Scintilla::NotificationData& notification) const noexcept {
    if (notification.nmhdr.code == Scintilla::Notification::Modified &&
        (static_cast<unsigned>(notification.modificationType) &
         static_cast<unsigned>(Scintilla::ModificationFlags::DeleteText)))
        scrollWidthDirty_ = true;
    if (notification.nmhdr.code == Scintilla::Notification::UpdateUI && window_.occurrences_)
        window_.occurrences_->Update(notification.updated);
    // Original bookmark-margin clicks use the clicked line and keep the caret.
    if (notification.nmhdr.code == Scintilla::Notification::MarginClick && notification.margin == 1 &&
        window_.BookmarkMarginVisible()) {
        const auto line = window_.Send(Message::LineFromPosition, notification.position);
        window_.Send((window_.Send(Message::MarkerGet, line) & 1) ? Message::MarkerDelete : Message::MarkerAdd, line,
                     0);
    }
    if (notification.nmhdr.code == Scintilla::Notification::Modified &&
        (static_cast<unsigned>(notification.modificationType) &
         static_cast<unsigned>(Scintilla::ModificationFlags::ChangeStyle | Scintilla::ModificationFlags::LexerState)))
        braceStyleChanged_ = true;
    // Notepad4.cpp SCN_CHARADDED: ignore tentative/multiple-selection typing.
    // The pinned CRLF condition handles CR, after the complete EOL is inserted.
    if (notification.nmhdr.code == Scintilla::Notification::CharAdded && !notification.listType &&
        window_.autoIndent_ && (notification.ch == '\r' || notification.ch == '\n') &&
        (pdoc->eolMode != Scintilla::EndOfLine::CrLf || notification.ch != '\n'))
        window_.AutoIndent();
    SelectionRevision();
    if (notification.nmhdr.code == Scintilla::Notification::UpdateUI &&
        (static_cast<unsigned>(notification.updated) &
         ~static_cast<unsigned>(Scintilla::Update::VScroll | Scintilla::Update::HScroll))) {
        // The pinned NotifyParent virtual is const. EditorControl owns this
        // mutable Core, and brace decoration updates use its normal WndProc path.
        const_cast<Core*>(this)->UpdateVisualBraceMatching();
    }
    if (notification.nmhdr.code == Scintilla::Notification::Zoom) {
        window_.lineNumberWidthDirty_ = true;
        window_.bookmarkMarginWidthDirty_ = true;
        window_.foldMarginWidthDirty_ = true;
        window_.SetCurrentLineHighlightMode(window_.GetCurrentLineHighlightMode());
    }
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
        notification.nmhdr.code == Scintilla::Notification::SavePointLeft ||
        notification.nmhdr.code == Scintilla::Notification::Zoom) {
        auto event = std::make_unique<wxCommandEvent>(wxEVT_EDITOR_STATE_CHANGED, window_.GetId());
        event->SetEventObject(&window_);
        wxQueueEvent(&window_, event.release());
    }
}

std::unique_ptr<core::CaseFolder> EditorControl::Core::CaseFolderForEncoding() const {
    // The document always contains UTF-8, including when disk encoding differs.
    return std::make_unique<core::CaseFolderUnicode>();
}

std::string EditorControl::Core::CaseMapString(const std::string& value, CaseMapping mapping) const {
    if (mapping == CaseMapping::upper) return core::CaseConvertString(value, core::CaseConversion::upper);
    if (mapping == CaseMapping::lower) return core::CaseConvertString(value, core::CaseConversion::lower);
    return MapCustomCase(value, static_cast<int>(mapping));
}

bool SCICALL EditorControl::Core::DragThreshold(core::Point start, core::Point now) const noexcept {
#ifdef __WXMSW__
    const auto dpi = GetDpiForWindow(static_cast<HWND>(window_.GetHandle()));
    return std::abs(start.x - now.x) > GetSystemMetricsForDpi(SM_CXDRAG, dpi) ||
           std::abs(start.y - now.y) > GetSystemMetricsForDpi(SM_CYDRAG, dpi);
#else
    return core::Editor::DragThreshold(start, now);
#endif
}

void EditorControl::Core::StartDrag() {
    // DropAt owns internal deletion/position adjustment and its undo group.
    // Only a completed native move outside this control deletes the source.
    inDragDrop = DragDrop::dragging;
    dropWentOutside = true;
#ifdef __WXGTK__
    // wxGTK 3.3.3 exits its source loop on pointer events before Wayland
    // acknowledges asynchronous transfers. Retain native GTK ownership
    // through drag-end, including failed and cancelled drops.
    const auto result = gtkDragDrop_->Start(drag.AsView());
#else
    wxTextDataObject data(wxString::FromUTF8(drag.Data(), drag.Length()));
    wxDropSource source(&window_);
    source.SetData(data);
    const auto result = source.DoDragDrop(wxDrag_DefaultMove);
#endif
    if (result == wxDragMove && dropWentOutside) ClearSelection();
    inDragDrop = DragDrop::none;
    ClearDropFeedback();
    drag.Clear();
}

bool EditorControl::Core::FineTickerRunning(TickReason reason) const noexcept {
    if (reason == TickReason::scroll) return window_.scrollTimer_.IsRunning();
    if (reason == TickReason::widen) return window_.widenTimer_.IsRunning();
    return reason == TickReason::caret && window_.caretTimer_.IsRunning();
}

void EditorControl::Core::FineTickerStart(TickReason reason, int millis, int) noexcept {
    if (reason == TickReason::caret)
        window_.caretTimer_.Start(millis);
    else if (reason == TickReason::scroll)
        window_.scrollTimer_.Start(millis);
    else if (reason == TickReason::widen)
        window_.widenTimer_.Start(millis);
}

void EditorControl::Core::FineTickerCancel(TickReason reason) noexcept {
    if (reason == TickReason::caret)
        window_.caretTimer_.Stop();
    else if (reason == TickReason::scroll)
        window_.scrollTimer_.Stop();
    else if (reason == TickReason::widen)
        window_.widenTimer_.Stop();
}

bool EditorControl::Core::SetIdle(bool) noexcept {
    return false;
}

void EditorControl::Core::SetMouseCapture(bool on) noexcept {
    if (on && !window_.HasCapture())
        window_.CaptureMouse();
    else if (!on && window_.HasCapture())
        window_.ReleaseMouse();
}

bool EditorControl::Core::HaveMouseCapture() const noexcept {
    return window_.HasCapture();
}

void EditorControl::Core::UpdateBaseElements() noexcept {
    // Element colours are supplied by the editor's configured style scheme.
}

bool EditorControl::Core::ValidCodePage(int codepage) const noexcept {
    return codepage == Scintilla::CpUtf8;
}

std::string EditorControl::Core::UTF8FromEncoded(std::string_view text) const {
    return std::string(text);
}

std::string EditorControl::Core::EncodedFromUTF8(std::string_view text) const {
    return std::string(text);
}

intptr_t EditorControl::Core::DefWndProc(Message, uintptr_t, intptr_t) {
    return 0;
}

void SCICALL EditorControl::Core::CreateCallTipWindow(core::PRectangle) noexcept {
    // Call-tip popup windows are outside the currently supported host UI.
}
