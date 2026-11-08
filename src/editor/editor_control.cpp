#include "editor/editor_control.h"
#include "editor/platform/native_encoding.h"
#include "editor/editor_core.h"
#include "editor/platform/clipboard.h"
#include "diagnostics/trace.h"
#include <wx/dcclient.h>
#include <cmath>

using Scintilla::Message;
namespace core = Scintilla::Internal;
wxDEFINE_EVENT(wxEVT_EDITOR_STATE_CHANGED, wxCommandEvent);

namespace {
bool HandleControlDelete(EditorControl& editor, const wxKeyEvent& event) {
    if (!event.ControlDown() || event.ShiftDown() || event.AltDown() || event.MetaDown() ||
        (event.GetKeyCode() != WXK_BACK && event.GetKeyCode() != WXK_DELETE))
        return false;
    // Notepad4 CMD_CTRLBACK/CMD_CTRLDEL precede the core key map.
    const auto caret = editor.Send(Message::GetCurrentPos);
    if (caret != editor.Send(Message::GetAnchor)) {
        editor.Send(Message::SetSel, caret, caret);
    } else {
        const auto line = editor.Send(Message::LineFromPosition, caret);
        const auto start = editor.Send(Message::PositionFromLine, line);
        if (event.GetKeyCode() == WXK_BACK)
            editor.Send(caret == start                                               ? Message::DeleteBack
                        : caret <= editor.Send(Message::GetLineIndentPosition, line) ? Message::DelLineLeft
                                                                                     : Message::DelWordLeft);
        else
            editor.Send(start == editor.Send(Message::GetLineEndPosition, line) ? Message::LineDelete
                                                                                : Message::DelWordRight);
    }
    return true;
}
} // namespace

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
    core_ = std::make_unique<Core>(*this, selectionOptions);
    occurrences_ = std::make_unique<OccurrenceMarking>(*this);
    // Original Notepad4.cpp::CreateEditControl current-line preferences.
    Send(Message::SetCaretLineVisibleAlways, 1);
    Send(Message::SetCaretLineHighlightSubLine, 1);
    SetCurrentLineHighlightMode(lineHighlightMode_);
    WXNOTE_TRACE("editor.create", "\ttarget=%p\thandle=%p", static_cast<void*>(this), GetHandle());
    Send(Message::SetMarginTypeN, 0, static_cast<intptr_t>(Scintilla::MarginType::Number));
    Send(Message::SetMarginMaskN, 0, 0);
    SetLineNumbersVisible(true);
    Send(Message::SetMarginTypeN, 1, static_cast<intptr_t>(Scintilla::MarginType::Symbol));
    Send(Message::SetMarginMaskN, 1, 0x1FFFFFF);
    Send(Message::SetMarginSensitiveN, 1, 1);
    Send(Message::SetMarginCursorN, 1, static_cast<intptr_t>(Scintilla::CursorShape::Arrow));
    SetBookmarkMarginVisible(false);
    // Original Styles.cpp change-history defaults: saved green, modified blue.
    for (const int marker : {21, 22, 23, 24}) {
        const auto colour = marker <= 22 ? 0xFF008000 : 0xFFFF8000;
        Send(Message::MarkerSetForeTranslucent, marker, colour);
        Send(Message::MarkerSetBackTranslucent, marker,
             marker == 21 || marker == 24 ? 0xFF000000 | Send(Message::StyleGetBack, 33) : colour);
    }
    SetCurrentBlockHighlighted(true);
    ApplyCaretAppearance();
    Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        core_->DpiChanged();
        bookmarkMarginWidthDirty_ = true;
        UpdateBookmarkMargin();
        SetCurrentLineHighlightMode(lineHighlightMode_);
        ApplyCaretAppearance();
        Refresh(false);
        event.Skip();
    });
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
        WXNOTE_TRACE("editor.focus-in", "\ttarget=%p\tfocus=%p", static_cast<void*>(this),
                     static_cast<void*>(wxWindow::FindFocus()));
        Send(Message::SetFocus, 1);
        core_->SyncSystemCaret();
        Refresh(false);
        event.Skip();
    });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        WXNOTE_TRACE("editor.focus-out", "\ttarget=%p\tfocus=%p", static_cast<void*>(this),
                     static_cast<void*>(wxWindow::FindFocus()));
        Send(Message::SetFocus, 0);
        core_->SyncSystemCaret();
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
#ifdef WXNOTEPAD_DIAGNOSTICS
    Bind(wxEVT_KEY_UP, [this](wxKeyEvent& event) {
        WXNOTE_TRACE(
            "editor.key-up", "\ttarget=%p\tfocus=%p\tkey=%d\tmodifiers=%d\tlength=%lld\tanchor=%lld\tcaret=%lld",
            static_cast<void*>(this), static_cast<void*>(wxWindow::FindFocus()), event.GetKeyCode(),
            event.GetModifiers(), static_cast<long long>(Send(Message::GetLength)),
            static_cast<long long>(Send(Message::GetAnchor)), static_cast<long long>(Send(Message::GetCurrentPos)));
        event.Skip();
    });
#endif
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
    WXNOTE_TRACE("editor.destroy", "\ttarget=%p", static_cast<void*>(this));
    SetDropTarget(nullptr);
}

void EditorControl::SetLineSelectionMode(LineSelectionMode mode) {
    lineSelectionMode_ = mode;
    if (mode == LineSelectionMode::None)
        Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
}

void EditorControl::SetCurrentLineHighlightMode(LineHighlightMode mode) {
    lineHighlightMode_ = mode;
    const auto element = static_cast<uintptr_t>(Scintilla::Element::CaretLineBack);
    if (mode == LineHighlightMode::None) {
        Send(Message::ResetElementColour, element);
        return;
    }
    // Original Styles.cpp::Style_HighlightCurrentLine and stlDefault.cpp.
    // One rounding step preserves ScaleStylePixel's DPI * zoom scaling.
    const bool outline = mode == LineHighlightMode::OutlineFrame;
    const int width =
        outline ? std::max(1, static_cast<int>(std::lround(2.0 * GetDPI().y * Send(Message::GetZoom) / 9600.0))) : 0;
    Send(Message::SetCaretLineFrame, width);
    Send(Message::SetCaretLineLayer, static_cast<uintptr_t>(Scintilla::Layer::UnderText));
    Send(Message::SetElementColour, element, outline ? 0x5AC3C0C2 : 0x5A00FFFF);
}

void EditorControl::Copy() {
    // Notepad4.cpp::IDM_EDIT_COPY at 89b8b36; see licenses/Notepad4.txt.
    if (Send(Message::GetSelectionEmpty) && lineSelectionMode_ != LineSelectionMode::None)
        Send(Message::LineCopy, static_cast<uintptr_t>(lineSelectionMode_) & 1);
    else
        Send(Message::Copy);
}

bool EditorControl::CopyAdd() {
    // Edit.cpp::EditCopyAppend at 89b8b36. The missing-Unicode branch calls
    // the underlying Copy message, bypassing the application's line preference.
    if (!EditorClipboard::HasText()) {
        Send(Message::Copy);
        return true;
    }
    const bool empty = Send(Message::GetSelectionEmpty);
    if (!empty && Send(Message::SelectionIsRectangle)) return false;
    const auto length = empty ? Send(Message::GetLength) : Send(Message::GetSelText);
    std::string added(static_cast<size_t>(length) + 1, '\0');
    Send(empty ? Message::GetText : Message::GetSelText, empty ? length : 0, reinterpret_cast<intptr_t>(added.data()));
    added.resize(added.find('\0'));
    if (added.empty()) return true;
    auto old = EditorClipboard::ReadPlainText(*GetParent());
    if (!old) return true;
    old->append("\r\n\r\n");
    old->append(added);
    core::SelectionText text;
    text.Copy(*old, 65001, false, false);
    EditorClipboard::Write(*GetParent(), text);
    return true;
}

void EditorControl::SwapClipboard(bool defaultEncoding) {
    // Notepad4.cpp::IDM_EDIT_SWAP at 89b8b36; see licenses/Notepad4.txt.
    if (Send(Message::GetSelectionEmpty)) {
        const auto position = Send(Message::GetCurrentPos);
        core_->PasteWithEncoding(defaultEncoding);
        Send(Message::SetSel, position, Send(Message::GetCurrentPos));
        EditorClipboard::Clear(*GetParent());
        return;
    }
    auto text = EditorClipboard::ReadPlainText(*GetParent());
    if (!text || text->empty()) return;
    text = core::Document::TransformLineEnds(*text, static_cast<Scintilla::EndOfLine>(Send(Message::GetEOLMode)));
    const bool protectedDefault = defaultEncoding && Send(Message::GetReadOnly);
    size_t nativeLength = 0;
    if (defaultEncoding)
        text = NativeDefaultEncodingText(wxString::FromUTF8(*text), protectedDefault ? &nativeLength : nullptr)
                   .ToStdString(wxConvUTF8);
    // Protected writes insert nothing: retain the original raw native-byte
    // displacement and SetSel's clamp. Writable replacements use UTF-8 bytes.
    const auto replacementLength = static_cast<intptr_t>(protectedDefault ? nativeLength : text->size());
    auto caret = Send(Message::GetCurrentPos);
    auto anchor = Send(Message::GetAnchor);
    Send(Message::BeginUndoAction);
    Send(Message::Cut);
    Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(text->c_str()));
    if (caret > anchor)
        caret = anchor + replacementLength;
    else
        anchor = caret + replacementLength;
    Send(Message::SetSel, anchor, caret);
    Send(Message::EndUndoAction);
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

void EditorControl::SetAutoShrinkScrollWidth(bool enabled) {
    autoShrinkScrollWidth_ = enabled;
    core_->RequestScrollWidthUpdate();
    Refresh(false);
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
    if (occurrences_ &&
        (message == Message::ClearAll || message == Message::SetText || message == Message::SetDocPointer))
        occurrences_->Stop();
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
        message == Message::StyleSetItalic || message == Message::SetZoom) {
        lineNumberWidthDirty_ = true;
        bookmarkMarginWidthDirty_ = true;
        foldMarginWidthDirty_ = true;
    }
    UpdateLineNumberMargin();
    UpdateBookmarkMargin();
    if (message == Message::FoldAll) core_->RepairFoldSelection();
    if (message == Message::SetZoom || message == Message::ZoomIn || message == Message::ZoomOut)
        ApplyCaretAppearance();
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

void EditorControl::SetBookmarkMarginVisible(bool visible) {
    bookmarkMarginVisible_ = visible;
    // Original Styles.cpp::Style_SetBookmark at 89b8b36.
    Send(Message::MarkerDefine, 0,
         static_cast<intptr_t>(visible ? Scintilla::MarkerSymbol::VerticalBookmark
                                       : Scintilla::MarkerSymbol::Background));
    Send(Message::MarkerSetBackTranslucent, 0, visible ? 0xFF408040 : 0x2800FF00);
    if (visible) Send(Message::MarkerSetForeTranslucent, 0, 0xFF408040);
    Send(Message::MarkerSetLayer, 0, static_cast<intptr_t>(Scintilla::Layer::OverText));
    bookmarkMarginWidthDirty_ = true;
    UpdateBookmarkMargin();
}

void EditorControl::UpdateBookmarkMargin() {
    if (!bookmarkMarginWidthDirty_) return;
    bookmarkMarginWidthDirty_ = false;
    const auto width =
        (bookmarkMarginVisible_ || changeHistoryOptions_ != 0) ? core_->WndProc(Message::TextHeight, 0, 0) - 2 : 0;
    if (core_->WndProc(Message::GetMarginWidthN, 1, 0) != width) {
        core_->WndProc(Message::SetMarginWidthN, 1, width);
        core_->UpdateWrapLayout();
    }
}

void EditorControl::SetChangeHistoryOptions(int options) {
    changeHistoryOptions_ = options;
    Send(Message::SetChangeHistory, static_cast<uintptr_t>(options));
    bookmarkMarginWidthDirty_ = true;
    UpdateBookmarkMargin();
}

void EditorControl::ToggleChangeHistory() {
    // Notepad4.cpp::IDM_VIEW_CHANGE_HISTORY_MARKER. Never discard undo to enable it.
    if (changeHistoryOptions_ != 0 || !Send(Message::CanUndo))
        SetChangeHistoryOptions(changeHistoryOptions_ == 0 ? 3 : 0);
}

void EditorControl::SetCodeFoldingVisible(bool visible) {
    codeFoldingVisible_ = visible;
    foldMarginWidthDirty_ = true;
    UpdateFoldMargin();
    if (!visible) Send(Message::FoldAll, static_cast<uintptr_t>(Scintilla::FoldAction::Expand));
    core_->UpdateWrapLayout();
}

void EditorControl::UpdateFoldMargin() {
    if (!foldMarginWidthDirty_) return;
    foldMarginWidthDirty_ = false;
    // Original Styles.cpp::UpdateFoldMarginWidth, default CodeFolding size 0.
    const auto width = codeFoldingVisible_ ? core_->WndProc(Message::TextWidth,
                                                            static_cast<uintptr_t>(Scintilla::StylesCommon::LineNumber),
                                                            reinterpret_cast<intptr_t>("+_"))
                                           : 0;
    if (core_->WndProc(Message::GetMarginWidthN, 2, 0) != width) {
        core_->WndProc(Message::SetMarginWidthN, 2, width);
        core_->UpdateWrapLayout();
    }
}

void EditorControl::SetCurrentBlockHighlighted(bool enabled) {
    highlightCurrentBlock_ = enabled;
    Send(Message::MarkerEnableHighlight, enabled);
}

void EditorControl::UpdateLineNumberMargin() {
    UpdateFoldMargin();
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
    WXNOTE_TRACE("editor.key-down", "\ttarget=%p\tfocus=%p\tkey=%d\tmodifiers=%d\tlength=%lld\tanchor=%lld\tcaret=%lld",
                 static_cast<void*>(this), static_cast<void*>(wxWindow::FindFocus()), event.GetKeyCode(),
                 event.GetModifiers(), static_cast<long long>(Send(Message::GetLength)),
                 static_cast<long long>(Send(Message::GetAnchor)),
                 static_cast<long long>(Send(Message::GetCurrentPos)));
    if (core_->Navigation(event)) return;
    if (HandleControlDelete(*this, event)) return;
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
#ifdef WXNOTEPAD_DIAGNOSTICS
    if (message == WM_DESTROYCLIPBOARD) WXNOTE_TRACE("clipboard.cleared", "\ttarget=%p", static_cast<void*>(this));
    if (message == WM_RENDERFORMAT || message == WM_RENDERALLFORMATS)
        WXNOTE_TRACE("clipboard.transfer", "\ttarget=%p\tformat=%llu", static_cast<void*>(this),
                     static_cast<unsigned long long>(wParam));
#endif
    if (core_) {
        if (const auto result = core_->ImeMessage(message, wParam, lParam)) return *result;
    }
    return wxControl::MSWWindowProc(message, wParam, lParam);
}
#endif
