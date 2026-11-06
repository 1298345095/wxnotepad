#include "editor/editor_control.h"
#include "editor/editor_core.h"
#include <wx/dcclient.h>
#ifdef __WXMSW__
#include <wx/caret.h>
#endif

using Scintilla::Message;
namespace core = Scintilla::Internal;
wxDEFINE_EVENT(wxEVT_EDITOR_STATE_CHANGED, wxCommandEvent);

EditorControl::EditorControl(wxWindow* parent, int selectionOptions)
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
    core_ = std::make_unique<Core>(*this, selectionOptions);
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
    Bind(wxEVT_KEY_DOWN, &EditorControl::OnKeyDown, this);
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

EditorControl::~EditorControl() {
    SetDropTarget(nullptr);
}

void EditorControl::SetLineSelectionMode(LineSelectionMode mode) {
    lineSelectionMode_ = mode;
    if (mode == LineSelectionMode::None)
        Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
}

void EditorControl::Copy() {
    // Notepad4.cpp::IDM_EDIT_COPY at 89b8b36; see licenses/Notepad4.txt.
    if (Send(Message::GetSelectionEmpty) && lineSelectionMode_ != LineSelectionMode::None)
        Send(Message::LineCopy, static_cast<uintptr_t>(lineSelectionMode_) & 1);
    else
        Send(Message::Copy);
}

void EditorControl::Cut() {
    // Original IDM_EDIT_CUT and EditJumpTo (1-based line/column).
    if (!Send(Message::GetSelectionEmpty) || lineSelectionMode_ == LineSelectionMode::None) {
        Send(Message::Cut);
        return;
    }
    const auto column = Send(Message::GetColumn, Send(Message::GetCurrentPos), -1);
    Send(Message::LineCut, static_cast<uintptr_t>(lineSelectionMode_) & 1);
    auto line = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
    if (lineSelectionMode_ == LineSelectionMode::OldVisualStudio && line > 0) --line;
    const Scintilla::CharacterRangeFull range{line, column};
    const auto position = Send(Message::FindColumnEx, static_cast<uintptr_t>(Scintilla::ColumnType::Default),
                               reinterpret_cast<intptr_t>(&range));
    JumpToPosition(position);
}

void EditorControl::CopyLine() {
    // Original IDM_EDIT_COPYLINE: the low mode bit controls line-copy metadata.
    Send(Message::LineCopy, static_cast<uintptr_t>(lineSelectionMode_) & 1);
}

void EditorControl::CutLine() {
    // Explicit Cut Line retains the core's position, unlike caret-only Cut.
    Send(Message::LineCut, static_cast<uintptr_t>(lineSelectionMode_) & 1);
}

void EditorControl::DeleteLine() {
    Send(Message::LineDelete);
}

void EditorControl::JumpToPosition(intptr_t position) {
    // Original EditSelectEx: unfold and center the destination, then restore
    // the normal policies and the preferred column for subsequent navigation.
    using Scintilla::CaretPolicy;
    const auto even = static_cast<uintptr_t>(CaretPolicy::Even);
    const auto slop = static_cast<uintptr_t>(CaretPolicy::Slop);
    const auto strictPolicy = slop | static_cast<uintptr_t>(CaretPolicy::Strict) | even;
    Send(Message::SetXCaretPolicy, strictPolicy, 50);
    Send(Message::SetYCaretPolicy, strictPolicy, 5);
    Send(Message::EnsureVisibleEnforcePolicy, Send(Message::LineFromPosition, position));
    Send(Message::GotoPos, position);
    Send(Message::SetXCaretPolicy, slop | even, 50);
    Send(Message::SetYCaretPolicy, even, 0);
    Send(Message::ChooseCaretX);
}

void EditorControl::ToggleWordWrap() {
    const auto mode = Send(Message::GetWrapMode) == static_cast<intptr_t>(Scintilla::Wrap::None)
                          ? Scintilla::Wrap::Word
                          : Scintilla::Wrap::None;
    Send(Message::SetWrapMode, static_cast<uintptr_t>(mode));
    core_->UpdateWrapLayout();
}

void EditorControl::SetIndentationWidths(int tabWidth, int indentWidth) {
    Send(Message::SetTabWidth, tabWidth);
    Send(Message::SetIndent, indentWidth);
    core_->UpdateWrapLayout();
}

intptr_t EditorControl::Send(Message message, uintptr_t wParam, intptr_t lParam) {
    core_->SelectionRevision();
    const auto result = core_->WndProc(message, wParam, lParam);
    if (message == Message::ClearAll) {
        // A document replacement also invalidates search state when both the
        // old and new document are empty, so no text notification was emitted.
        ++documentRevision_;
        auto* event = new wxCommandEvent(wxEVT_EDITOR_STATE_CHANGED, GetId());
        event->SetEventObject(this);
        wxQueueEvent(this, event);
    }
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

void EditorControl::OnKeyDown(wxKeyEvent& event) {
    if (core_->Navigation(event)) return;
    if (event.ControlDown() && !event.ShiftDown() && !event.AltDown() && !event.MetaDown() &&
        (event.GetKeyCode() == WXK_BACK || event.GetKeyCode() == WXK_DELETE)) {
        // Notepad4 CMD_CTRLBACK/CMD_CTRLDEL precede the core key map.
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
                Send(start == Send(Message::GetLineEndPosition, line) ? Message::LineDelete : Message::DelWordRight);
        }
        return;
    }
    if (event.GetKeyCode() == WXK_TAB && event.ShiftDown() && !event.ControlDown() && !event.AltDown() &&
        !event.MetaDown()) {
        IndentLines(false);
        return;
    }
    if (!core_->EditingKey(event)) event.Skip();
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
