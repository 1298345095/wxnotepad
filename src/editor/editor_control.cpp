#include "editor/editor_control.h"
#include "editor/font_settings.h"
#include "editor/display_units.h"
#include "editor/editor_ids.h"
#include "editor/platform/native_encoding.h"
#include "editor/editor_core.h"
#include "editor/platform/clipboard.h"
#include "diagnostics/trace.h"
#include <wx/dcclient.h>
#include <wx/wupdlock.h>
#include <wx/settings.h>
#include <cmath>
#include <algorithm>
#include <type_traits>
#include <utility>
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

using Scintilla::Message;
namespace core = Scintilla::Internal;
wxDEFINE_EVENT(wxEVT_EDITOR_STATE_CHANGED, wxCommandEvent);

namespace {
bool HandleControlDelete(EditorControl& editor, const wxKeyEvent& event) {
    using enum Scintilla::Message;
    if (!event.ControlDown() || event.ShiftDown() || event.AltDown() || event.MetaDown() ||
        (event.GetKeyCode() != WXK_BACK && event.GetKeyCode() != WXK_DELETE))
        return false;
    // Notepad4 CMD_CTRLBACK/CMD_CTRLDEL precede the core key map.
    if (const auto caret = editor.Send(GetCurrentPos); caret != editor.Send(GetAnchor)) {
        editor.Send(SetSel, caret, caret);
    } else {
        const auto line = editor.Send(LineFromPosition, caret);
        const auto start = editor.Send(PositionFromLine, line);
        if (event.GetKeyCode() == WXK_BACK) {
            auto message = DelWordLeft;
            if (caret == start)
                message = DeleteBack;
            else if (caret <= editor.Send(GetLineIndentPosition, line))
                message = DelLineLeft;
            editor.Send(message);
        } else
            editor.Send(start == editor.Send(GetLineEndPosition, line) ? LineDelete : DelWordRight);
    }
    return true;
}
} // namespace

template <typename Action> bool EditorControl::RunNativeEvent(Action&& action) {
    return core_->RunNativeOperation(std::forward<Action>(action));
}

template <typename Event, typename Handler>
void EditorControl::BindCoreEvent(const wxEventTypeTag<Event>& type, Handler handler, int id) {
    Bind(
        type,
        [this, handler](Event& event) {
            RunNativeEvent([&] {
                if constexpr (std::is_member_function_pointer_v<Handler>)
                    (this->*handler)(event);
                else
                    handler(event);
            });
        },
        id);
}

EditorControl::EditorControl(wxWindow* parent, int selectionOptions)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                wxBORDER_NONE | wxWANTS_CHARS | wxVSCROLL | wxHSCROLL),
      caretTimer_(this), scrollTimer_(this), widenTimer_(this), dwellTimer_(this) {
    // The native Notepad4 host initializes these tables before any document
    // copies its ASCII word classes. The wx host must do the same.
    [[maybe_unused]] static const bool unicodeDataInitialized = [] {
        core::CharClassify::InitUnicodeData();
        return true;
    }();
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    core_ = std::make_unique<Core>(*this, selectionOptions);
    // wxSTC initializes selection colors from the native text-control palette;
    // the core's light-grey selection cannot pair with a dark-theme foreground.
    for (const auto [element, systemColour] : {std::pair{Scintilla::Element::SelectionText, wxSYS_COLOUR_HIGHLIGHTTEXT},
                                               std::pair{Scintilla::Element::SelectionBack, wxSYS_COLOUR_HIGHLIGHT}}) {
        const auto colour = wxSystemSettings::GetColour(systemColour);
        Send(Message::SetElementColour, static_cast<uintptr_t>(element),
             0xFF000000U | colour.Red() | (colour.Green() << 8U) | (colour.Blue() << 16U));
    }
#ifdef __WXMSW__
    // A top-level move can change monitor calibration without changing DPI.
    // The wx member-function binding disconnects when this editor is destroyed.
    wxGetTopLevelParent(this)->Bind(wxEVT_MOVE, &EditorControl::OnHostMove, this);
#endif
    SetAutoInsertMask(AutoInsertMask_Default);
    // Notepad4 89b8b36 Notepad4.cpp startup / GetFoldDisplayEllipsis:
    // editor storage is UTF-8, independently of the document's file encoding.
    Send(Message::FoldDisplayTextSetStyle,
         static_cast<uintptr_t>(std::to_underlying(Scintilla::FoldDisplayTextStyle::Boxed)));
    Send(Message::SetDefaultFoldDisplayText, 0, reinterpret_cast<intptr_t>("\xC2\xB7\xC2\xB7\xC2\xB7"));
    occurrences_ = std::make_unique<OccurrenceMarking>(*this);
    // Original Notepad4.cpp::CreateEditControl current-line preferences.
    Send(Message::SetCaretLineVisibleAlways, 1);
    Send(Message::SetCaretLineHighlightSubLine, 1);
    SetCurrentLineHighlightMode(lineHighlightMode_);
    WXNOTE_TRACE("editor.create", "\ttarget=%p\thandle=%p", static_cast<void*>(this), GetHandle());
    Send(Message::SetMarginTypeN, EditorMargin::LineNumber,
         static_cast<intptr_t>(std::to_underlying(Scintilla::MarginType::Number)));
    Send(Message::SetMarginMaskN, EditorMargin::LineNumber, 0);
    SetLineNumbersVisible(true);
    Send(Message::SetMarginTypeN, EditorMargin::Bookmark,
         static_cast<intptr_t>(std::to_underlying(Scintilla::MarginType::Symbol)));
    Send(Message::SetMarginMaskN, EditorMargin::Bookmark, ~Scintilla::MaskFolders);
    Send(Message::SetMarginSensitiveN, EditorMargin::Bookmark, 1);
    Send(Message::SetMarginCursorN, EditorMargin::Bookmark,
         static_cast<intptr_t>(std::to_underlying(Scintilla::CursorShape::Arrow)));
    SetBookmarkMarginVisible(false);
    // Original Styles.cpp change-history defaults: saved green, modified blue.
    for (const auto marker :
         {Scintilla::MarkerOutline::HistoryRevertedToOrigin, Scintilla::MarkerOutline::HistorySaved,
          Scintilla::MarkerOutline::HistoryModified, Scintilla::MarkerOutline::HistoryRevertedToModified}) {
        const auto colour = marker <= Scintilla::MarkerOutline::HistorySaved ? 0xFF008000 : 0xFFFF8000;
        Send(Message::MarkerSetForeTranslucent, static_cast<uintptr_t>(std::to_underlying(marker)), colour);
        Send(Message::MarkerSetBackTranslucent, static_cast<uintptr_t>(std::to_underlying(marker)),
             marker == Scintilla::MarkerOutline::HistoryRevertedToOrigin ||
                     marker == Scintilla::MarkerOutline::HistoryRevertedToModified
                 ? 0xFF000000 | Send(Message::StyleGetBack,
                                     static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::LineNumber)))
                 : colour);
    }
    SetCurrentBlockHighlighted(true);
    ApplyCaretAppearance();
    BindCoreEvent(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
        core_->DpiChanged();
        bookmarkMarginWidthDirty_ = true;
        UpdateBookmarkMargin();
        ApplyStylePixelSettings();
        Refresh(false);
        event.Skip();
    });
    BindCoreEvent(wxEVT_SET_FOCUS, [this](wxFocusEvent& event) {
        WXNOTE_TRACE("editor.focus-in", "\ttarget=%p\tfocus=%p", static_cast<void*>(this),
                     static_cast<void*>(wxWindow::FindFocus()));
        Send(Message::SetFocus, 1);
        core_->SyncSystemCaret();
        Refresh(false);
        event.Skip();
    });
    BindCoreEvent(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        WXNOTE_TRACE("editor.focus-out", "\ttarget=%p\tfocus=%p", static_cast<void*>(this),
                     static_cast<void*>(wxWindow::FindFocus()));
        Send(Message::SetFocus, 0);
        core_->SyncSystemCaret();
        Refresh(false);
        event.Skip();
    });
    BindCoreEvent(wxEVT_TIMER, [this](wxTimerEvent&) { core_->TickCaret(); }, caretTimer_.GetId());
    BindCoreEvent(wxEVT_TIMER, [this](wxTimerEvent&) { core_->TickScroll(); }, scrollTimer_.GetId());
    BindCoreEvent(wxEVT_TIMER, [this](wxTimerEvent&) { core_->TickWiden(); }, widenTimer_.GetId());
    BindCoreEvent(wxEVT_TIMER, [this](wxTimerEvent&) { core_->TickDwell(); }, dwellTimer_.GetId());
    BindCoreEvent(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) { core_->Mouse(event); });
    BindCoreEvent(wxEVT_LEFT_UP, [this](wxMouseEvent& event) { core_->Mouse(event); });
    const auto rightDown = [this](wxMouseEvent& event) {
        core_->Mouse(event);
#ifdef __WXGTK__
        if (event.RightDClick()) {
            // wxGTK filters the second press into RIGHT_DCLICK and emits its
            // context-menu event only for RIGHT_DOWN. Keep rapid right-clicks
            // usable without emitting another event for ordinary presses.
            wxContextMenuEvent context(wxEVT_CONTEXT_MENU, GetId(), ClientToScreen(event.GetPosition()));
            context.SetEventObject(this);
            ProcessWindowEvent(context);
            return;
        }
#endif
        // wxSTC OnMouseRightDown: let wx generate one native context-menu
        // event after the original core has cancelled completion/CallTip.
        event.Skip();
    };
    BindCoreEvent(wxEVT_RIGHT_DOWN, rightDown);
    BindCoreEvent(wxEVT_RIGHT_DCLICK, rightDown);
    BindCoreEvent(wxEVT_MOTION, [this](wxMouseEvent& event) { core_->Mouse(event); });
    BindCoreEvent(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent&) { core_->MouseDeparted(); });
    BindCoreEvent(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) { core_->CaptureLost(); });
    BindCoreEvent(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& event) { core_->Wheel(event); });
    const auto scroll = [this](wxScrollWinEvent& event) { core_->Scroll(event); };
    BindCoreEvent(wxEVT_SCROLLWIN_TOP, scroll);
    BindCoreEvent(wxEVT_SCROLLWIN_BOTTOM, scroll);
    BindCoreEvent(wxEVT_SCROLLWIN_LINEUP, scroll);
    BindCoreEvent(wxEVT_SCROLLWIN_LINEDOWN, scroll);
    BindCoreEvent(wxEVT_SCROLLWIN_PAGEUP, scroll);
    BindCoreEvent(wxEVT_SCROLLWIN_PAGEDOWN, scroll);
    BindCoreEvent(wxEVT_SCROLLWIN_THUMBTRACK, scroll);
    BindCoreEvent(wxEVT_SCROLLWIN_THUMBRELEASE, scroll);
    BindCoreEvent(wxEVT_KEY_DOWN, &EditorControl::OnKeyDown);
#ifdef WXNOTEPAD_DIAGNOSTICS
    BindCoreEvent(wxEVT_KEY_UP, [this](wxKeyEvent& event) {
        WXNOTE_TRACE(
            "editor.key-up", "\ttarget=%p\tfocus=%p\tkey=%d\tmodifiers=%d\tlength=%lld\tanchor=%lld\tcaret=%lld",
            static_cast<void*>(this), static_cast<void*>(wxWindow::FindFocus()), event.GetKeyCode(),
            event.GetModifiers(), static_cast<long long>(Send(Message::GetLength)),
            static_cast<long long>(Send(Message::GetAnchor)), static_cast<long long>(Send(Message::GetCurrentPos)));
        event.Skip();
    });
#endif
    BindCoreEvent(wxEVT_CHAR, [this](wxKeyEvent& event) {
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
    BindCoreEvent(wxEVT_PAINT, &EditorControl::OnPaint);
    BindCoreEvent(wxEVT_SIZE, &EditorControl::OnSize);
}

EditorControl::~EditorControl() {
    WXNOTE_TRACE("editor.destroy", "\ttarget=%p", static_cast<void*>(this));
#ifdef __WXGTK__
    if (preeditStartHandler_) g_signal_handler_disconnect(m_imContext, preeditStartHandler_);
    if (preeditEndHandler_) g_signal_handler_disconnect(m_imContext, preeditEndHandler_);
#endif
    SetDropTarget(nullptr);
}

void EditorControl::FlushPendingUIUpdate() {
    core_->FlushPendingUIUpdate();
}

void EditorControl::SetColorCallTipMode(ColorCallTipMode mode) {
    constexpr int colorDwellMilliseconds = 250;
    colorCallTipMode_ = mode;
    Send(Message::SetMouseDwellTime, mode == ColorCallTipMode::Off ? Scintilla::TimeForever : colorDwellMilliseconds);
}

void EditorControl::SetLineSelectionMode(LineSelectionMode mode) {
    lineSelectionMode_ = mode;
    if (mode == LineSelectionMode::None)
        Send(Message::SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream)));
}

void EditorControl::SetCurrentLineHighlightMode(LineHighlightMode mode) {
    lineHighlightMode_ = mode;
    const auto element = static_cast<uintptr_t>(std::to_underlying(Scintilla::Element::CaretLineBack));
    if (mode == LineHighlightMode::None) {
        Send(Message::ResetElementColour, element);
        return;
    }
    // Original Styles.cpp::Style_HighlightCurrentLine and stlDefault.cpp.
    // One rounding step preserves ScaleStylePixel's DPI * zoom scaling.
    const bool outline = mode == LineHighlightMode::OutlineFrame;
    const auto style = Fonts().GlobalStyle("Current Line");
    if (StyleProperty(style, outline ? "fore" : "back").empty()) {
        Send(Message::ResetElementColour, element);
        return;
    }
    const int frameSize = std::max(1, StyleInteger(style, "size", 2));
    const int width = outline ? std::max(1, static_cast<int>(std::lround(
                                                static_cast<double>(frameSize) * GetDPI().y * Send(Message::GetZoom) /
                                                (DisplayUnits::DefaultDpi * DisplayUnits::ZoomPercentScale))))
                              : 0;
    Send(Message::SetCaretLineFrame, width);
    Send(Message::SetCaretLineLayer, static_cast<uintptr_t>(std::to_underlying(Scintilla::Layer::UnderText)));
    const auto alpha = std::clamp(StyleInteger(style, outline ? "outline" : "alpha", 255), 0, 255);
    Send(Message::SetElementColour, element,
         StyleColour(style, outline ? "fore" : "back", outline ? 0x00C3C0C2 : 0x0000FFFF) |
             (static_cast<unsigned>(alpha) << 24U));
}

void EditorControl::ApplyStylePixelSettings() {
    constexpr int StrokeWidthHundredthsPerPixel = 100;
    const double scale = static_cast<double>(GetDPI().y * Send(Message::GetZoom)) /
                         (DisplayUnits::DefaultDpi * DisplayUnits::ZoomPercentScale);
    const auto whitespace = Fonts().GlobalStyle("Whitespace");
    Send(Message::SetWhitespaceSize,
         std::max(1, static_cast<int>(std::lround(StyleInteger(whitespace, "size", 0) * scale))));
    const int spacing = static_cast<int>(
        std::lround(std::abs(StyleInteger(Fonts().GlobalStyle("Extra Line Spacing"), "size", 0)) * scale));
    Send(Message::SetExtraAscent, spacing - spacing / 2);
    Send(Message::SetExtraDescent, spacing / 2);
    for (auto id = std::to_underlying(Scintilla::MarkerOutline::FolderEnd);
         id <= std::to_underlying(Scintilla::MarkerOutline::FolderOpen); ++id)
        Send(Message::MarkerSetStrokeWidth, id,
             std::max(StrokeWidthHundredthsPerPixel,
                      static_cast<int>(std::lround(StrokeWidthHundredthsPerPixel * scale))));
    SetCurrentLineHighlightMode(lineHighlightMode_);
    ApplyCaretAppearance();
}

void EditorControl::Copy() {
    // Notepad4.cpp::IDM_EDIT_COPY at 89b8b36; see licenses/Notepad4.txt.
    if (Send(Message::GetSelectionEmpty) && lineSelectionMode_ != LineSelectionMode::None)
        Send(Message::LineCopy, static_cast<uintptr_t>(std::to_underlying(lineSelectionMode_)) & 1);
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
    text.Copy(*old, Scintilla::CpUtf8, false, false);
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
    Send(Message::LineCut, static_cast<uintptr_t>(std::to_underlying(lineSelectionMode_)) & 1);
    auto line = Send(Message::LineFromPosition, Send(Message::GetCurrentPos));
    if (lineSelectionMode_ == LineSelectionMode::OldVisualStudio && line > 0) --line;
    const Scintilla::CharacterRangeFull range{line, column};
    const auto position =
        Send(Message::FindColumnEx, static_cast<uintptr_t>(std::to_underlying(Scintilla::ColumnType::Default)),
             reinterpret_cast<intptr_t>(&range));
    JumpToPosition(position);
}

void EditorControl::CopyLine() {
    // Original IDM_EDIT_COPYLINE: the low mode bit controls line-copy metadata.
    Send(Message::LineCopy, static_cast<uintptr_t>(std::to_underlying(lineSelectionMode_)) & 1);
}

void EditorControl::CutLine() {
    // Explicit Cut Line retains the core's position, unlike caret-only Cut.
    Send(Message::LineCut, static_cast<uintptr_t>(std::to_underlying(lineSelectionMode_)) & 1);
}

void EditorControl::DeleteLine() {
    Send(Message::LineDelete);
}

void EditorControl::JumpToPosition(intptr_t position) {
    // Original EditSelectEx: unfold and center the destination, then restore
    // the normal policies and the preferred column for subsequent navigation.
    using enum Scintilla::CaretPolicy;
    const auto even = static_cast<uintptr_t>(std::to_underlying(Even));
    const auto slop = static_cast<uintptr_t>(std::to_underlying(Slop));
    const auto strictPolicy = slop | static_cast<uintptr_t>(std::to_underlying(Strict)) | even;
    Send(Message::SetXCaretPolicy, strictPolicy, 50);
    Send(Message::SetYCaretPolicy, strictPolicy, 5);
    Send(Message::EnsureVisibleEnforcePolicy, Send(Message::LineFromPosition, position));
    Send(Message::GotoPos, position);
    Send(Message::SetXCaretPolicy, slop | even, 50);
    Send(Message::SetYCaretPolicy, even, 0);
    Send(Message::ChooseCaretX);
}

void EditorControl::JumpToInitialPosition(intptr_t position) {
    JumpToPosition(position);
    // Native startup configuration can still shrink the viewport before its
    // first paint. Keep the requested caret visible through those size events.
    initialPositionPending_ = true;
}

void EditorControl::SetAutoShrinkScrollWidth(bool enabled) {
    autoShrinkScrollWidth_ = enabled;
    core_->RequestScrollWidthUpdate();
    Refresh(false);
}

void EditorControl::ToggleWordWrap() {
    SetWordWrapMode(Send(Message::GetWrapMode) ? Scintilla::Wrap::None : wordWrapMode_);
}

void EditorControl::SetWordWrapMode(Scintilla::Wrap mode) {
    if (mode != Scintilla::Wrap::None) wordWrapMode_ = mode;
    Send(Message::SetWrapMode, static_cast<uintptr_t>(std::to_underlying(mode)));
    core_->UpdateWrapLayout();
}

void EditorControl::SetWordWrapIndent(WrapContinuationIndent indent) {
    wordWrapIndent_ = indent;
    ApplyWrapIndent();
    core_->UpdateWrapLayout();
}

void EditorControl::ApplyWrapIndent() {
    // Notepad4 89b8b36 Edit.cpp::EditSetWrapIndentMode/EditSetWrapStartIndent.
    // Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
    using enum WrapContinuationIndent;
    auto mode = Scintilla::WrapIndentMode::Fixed;
    switch (wordWrapIndent_) {
    case Same:
        mode = Scintilla::WrapIndentMode::Same;
        break;
    case Indent:
        mode = Scintilla::WrapIndentMode::Indent;
        break;
    case DeepIndent:
        mode = Scintilla::WrapIndentMode::DeepIndent;
        break;
    default: {
        int indent = 0;
        const auto indentWidth = Send(Message::GetIndent);
        const int width = static_cast<int>(indentWidth ? indentWidth : Send(Message::GetTabWidth));
        switch (wordWrapIndent_) {
        case OneCharacter:
            indent = 1;
            break;
        case TwoCharacters:
            indent = 2;
            break;
        case OneLevel:
            indent = width;
            break;
        case TwoLevels:
            indent = 2 * width;
            break;
        default:
            break;
        }
        Send(Message::SetWrapStartIndent, indent);
        break;
    }
    }
    Send(Message::SetWrapIndentMode, std::to_underlying(mode));
}

void EditorControl::SetIndentationWidths(int tabWidth, int indentWidth) {
    Send(Message::SetTabWidth, tabWidth);
    Send(Message::SetIndent, indentWidth);
    ApplyWrapIndent();
    core_->UpdateWrapLayout();
}

bool EditorControl::CanPasteText(bool clipboardHasText) const noexcept {
    return core_->CanPasteText(clipboardHasText);
}

intptr_t EditorControl::Send(Message message, uintptr_t wParam, intptr_t lParam) {
    if (message == Message::ClearAll || message == Message::SetText || message == Message::SetDocPointer) {
        core_->WndProc(Message::AutoCCancel, 0, 0);
        typing_.ResetCompletionState();
    }
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
        message == Message::StyleSetItalic || message == Message::StyleSetWeight || message == Message::SetZoom ||
        message == Message::SetTechnology || message == Message::SetFontQuality || message == Message::SetExtraAscent ||
        message == Message::SetExtraDescent) {
        lineNumberWidthDirty_ = true;
        bookmarkMarginWidthDirty_ = true;
        foldMarginWidthDirty_ = true;
    }
    UpdateLineNumberMargin();
    UpdateBookmarkMargin();
    if (message == Message::FoldAll) core_->RepairFoldSelection();
    if (message == Message::SetZoom || message == Message::ZoomIn || message == Message::ZoomOut)
        ApplyStylePixelSettings();
    core_->SelectionRevision();
    return result;
}

NativeByteSnapshot EditorControl::RawNativeBytes() const {
    return core_->RawNativeBytes();
}

std::string EditorControl::NativeSelectedBytes(bool binary) {
    return core_->NativeSelectedBytes(binary);
}

std::string EditorControl::NativeBytesAfter(intptr_t position) {
    return core_->NativeBytesAfter(position);
}

void EditorControl::ReplaceTargetWithNativeBytes(std::string_view text, NativeByteRanges ranges) {
    core_->ReplaceTargetWithNativeBytes(text, std::move(ranges));
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
    Send(Message::MarkerDefine, EditorMarker::Bookmark,
         static_cast<intptr_t>(std::to_underlying(visible ? Scintilla::MarkerSymbol::VerticalBookmark
                                                          : Scintilla::MarkerSymbol::Background)));
    const auto style = Fonts().GlobalStyle("Bookmark");
    const auto colour = StyleColour(style, visible ? "fore" : "back", visible ? 0x00408040 : 0x0000ff00);
    const auto alpha = visible ? 255 : std::clamp(StyleInteger(style, "alpha", 40), 0, 255);
    Send(Message::MarkerSetBackTranslucent, EditorMarker::Bookmark, colour | (static_cast<unsigned>(alpha) << 24U));
    if (visible) Send(Message::MarkerSetForeTranslucent, EditorMarker::Bookmark, 0xff000000U | colour);
    Send(Message::MarkerSetLayer, EditorMarker::Bookmark,
         static_cast<intptr_t>(std::to_underlying(Scintilla::Layer::OverText)));
    bookmarkMarginWidthDirty_ = true;
    UpdateBookmarkMargin();
}

void EditorControl::UpdateBookmarkMargin() {
    if (!bookmarkMarginWidthDirty_) return;
    bookmarkMarginWidthDirty_ = false;
    const auto width =
        (bookmarkMarginVisible_ || changeHistoryOptions_ != 0) ? core_->WndProc(Message::TextHeight, 0, 0) - 2 : 0;
    if (core_->WndProc(Message::GetMarginWidthN, EditorMargin::Bookmark, 0) != width) {
        core_->WndProc(Message::SetMarginWidthN, EditorMargin::Bookmark, width);
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
    using enum Scintilla::ChangeHistoryOption;
    // Notepad4.cpp::IDM_VIEW_CHANGE_HISTORY_MARKER. Never discard undo to enable it.
    if (changeHistoryOptions_ != 0 || !Send(Message::CanUndo))
        SetChangeHistoryOptions(changeHistoryOptions_ == 0 ? std::to_underlying(Enabled) | std::to_underlying(Markers)
                                                           : std::to_underlying(Disabled));
}

void EditorControl::SetCodeFoldingVisible(bool visible) {
    codeFoldingVisible_ = visible;
    foldMarginWidthDirty_ = true;
    UpdateFoldMargin();
    if (!visible) Send(Message::FoldAll, static_cast<uintptr_t>(std::to_underlying(Scintilla::FoldAction::Expand)));
    core_->UpdateWrapLayout();
}

void EditorControl::ToggleAllFolds() {
    using enum Scintilla::FoldAction;
    {
        const wxWindowUpdateLocker redraw(this);
        core_->WndProc(Message::FoldAll,
                       static_cast<uintptr_t>(std::to_underlying(Toggle) | std::to_underlying(ContractEveryLevel)), 0);
    }
    FinishBatchFold();
}

void EditorControl::FinishBatchFold() {
    using enum Scintilla::Message;
    using enum Scintilla::CaretPolicy;
    constexpr int horizontalCaretSlop = 50;
    constexpr int verticalCaretSlop = 5;
    constexpr auto strictPolicy = static_cast<uintptr_t>(std::to_underlying(Slop | Strict | Even));
    Send(SetXCaretPolicy, strictPolicy, horizontalCaretSlop);
    Send(SetYCaretPolicy, strictPolicy, verticalCaretSlop);
    Send(ScrollCaret);
    Send(SetXCaretPolicy, static_cast<uintptr_t>(std::to_underlying(Slop | Even)), horizontalCaretSlop);
    Send(SetYCaretPolicy, static_cast<uintptr_t>(std::to_underlying(Even)), 0);
    Refresh(false);
}

void EditorControl::UpdateFoldMargin() {
    if (!foldMarginWidthDirty_) return;
    foldMarginWidthDirty_ = false;
    // Original Styles.cpp::UpdateFoldMarginWidth, default CodeFolding size 0.
    const auto width =
        codeFoldingVisible_
            ? core_->WndProc(Message::TextWidth,
                             static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::LineNumber)),
                             reinterpret_cast<intptr_t>("+_"))
            : 0;
    if (core_->WndProc(Message::GetMarginWidthN, EditorMargin::Folding, 0) != width) {
        core_->WndProc(Message::SetMarginWidthN, EditorMargin::Folding, width);
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
        width = core_->WndProc(Message::TextWidth,
                               static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::LineNumber)),
                               reinterpret_cast<intptr_t>(digits.c_str())) +
                FromDIP(8);
    }
    if (core_->WndProc(Message::GetMarginWidthN, EditorMargin::LineNumber, 0) != width) {
        core_->WndProc(Message::SetMarginWidthN, EditorMargin::LineNumber, width);
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

#ifdef __WXGTK__
void EditorControl::GTKHandleRealized() {
    wxControl::GTKHandleRealized();
    // wxGTK creates its IM context on realization, after our constructor.
    if (m_imContext && !preeditStartHandler_) {
        preeditStartHandler_ =
            g_signal_connect(m_imContext, "preedit-start", G_CALLBACK(+[](GtkIMContext*, gpointer value) {
                                 static_cast<EditorControl*>(value)->inputMethodComposing_ = true;
                             }),
                             this);
        preeditEndHandler_ =
            g_signal_connect(m_imContext, "preedit-end", G_CALLBACK(+[](GtkIMContext*, gpointer value) {
                                 static_cast<EditorControl*>(value)->inputMethodComposing_ = false;
                             }),
                             this);
    }
}
#endif

bool EditorControl::InputMethodComposing() const noexcept {
#ifdef __WXMSW__
    return core_->InputMethodComposing();
#elif defined(__WXGTK__)
    return inputMethodComposing_;
#else
    return false;
#endif
}

void EditorControl::OnKeyDown(wxKeyEvent& event) {
    if (event.GetKeyCode() == WXK_ESCAPE && InputMethodComposing()) {
        // wxGTK filters IM keys after KEY_DOWN; let preedit/candidates own Escape.
        event.Skip();
        return;
    }
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
    initialPositionPending_ = false;
}

void EditorControl::OnIdle(wxIdleEvent& event) {
    RunNativeEvent([&] { core_->OnIdle(event); });
}

void EditorControl::OnSize(wxSizeEvent& event) {
    core_->Resize();
    if (initialPositionPending_) core_->WndProc(Message::ScrollCaret, 0, 0);
    Refresh(false);
    event.Skip();
}

#ifdef __WXMSW__
void EditorControl::OnHostMove(wxMoveEvent& event) {
    RunNativeEvent([&] { core_->RenderingSettingsChanged(false); });
    event.Skip();
}

WXLRESULT EditorControl::MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) {
#ifdef WXNOTEPAD_DIAGNOSTICS
    if (message == WM_DESTROYCLIPBOARD) WXNOTE_TRACE("clipboard.cleared", "\ttarget=%p", static_cast<void*>(this));
    if (message == WM_RENDERFORMAT || message == WM_RENDERALLFORMATS)
        WXNOTE_TRACE("clipboard.transfer", "\ttarget=%p\tformat=%llu", static_cast<void*>(this),
                     static_cast<unsigned long long>(wParam));
#endif
    if (core_) {
        std::optional<intptr_t> result;
        if (!RunNativeEvent([&] {
                if (message == WM_SETTINGCHANGE) core_->RenderingSettingsChanged(true);
                result = core_->ImeMessage(message, wParam, lParam);
            }))
            return 0;
        if (result) {
            if (message == WM_IME_STARTCOMPOSITION) {
                // Inline composition suppresses DefWindowProc, but wx still
                // needs its native IME-entry bookkeeping to balance END below.
                WXLRESULT nativeResult = 0;
                wxControl::MSWHandleMessage(&nativeResult, message, wParam, lParam);
            }
            return *result;
        }
    }
    return wxControl::MSWWindowProc(message, wParam, lParam);
}
#endif
