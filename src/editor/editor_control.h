#pragma once

#include "settings/original_settings_constants.h"
#include "editor/align_lines.h"
#include "editor/completion_styles.h"
#include "editor/color_calltip_mode.h"
#include "editor/word_completion.h"
#include "editor/sort_lines.h"
#include "editor/date_time.h"
#include "editor/caret_appearance.h"
#include "editor/special_insert.h"
#include "editor/url_conversion.h"
#include "editor/base64_conversion.h"
#include "editor/native_byte_history.h"
#include "editor/occurrence_marking.h"
#include <ScintillaMessages.h>
#include <ScintillaTypes.h>
#include <wx/control.h>
#include <wx/timer.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

wxDECLARE_EVENT(wxEVT_EDITOR_STATE_CHANGED, wxCommandEvent);
struct SyntaxDefinition;
struct SyntaxOriginalMetadata;
namespace Scintilla {
struct CharacterRangeFull;
struct Rectangle;
namespace Internal {
struct PrintSurface;
}
} // namespace Scintilla

// Original Edit.h::LineSelectionMode, including its built-in Visual Studio default.
enum class LineSelectionMode { None, VisualStudio, Normal, OldVisualStudio };
// Original Styles.h::LineHighlightMode.
enum class LineHighlightMode { None, BackgroundColor, OutlineFrame };
enum class BlockNavigation { Start, End, Previous, Next, PreviousSibling, NextSibling };
enum class FoldCommand { DefaultLevels, AllLevels, CurrentBlock, CurrentLevel, NumericLevel };

// A wx window backed by exactly one original Notepad4 Scintilla document.
// Commands use the pinned core's public message API; no mirrored wx text exists.
struct FontSettings;

class EditorControl final : public wxControl {
public:
    explicit EditorControl(wxWindow* parent, int selectionOptions = SelectOption_Default);
    ~EditorControl() override;
    intptr_t Send(Scintilla::Message message, uintptr_t wParam = 0, intptr_t lParam = 0);
    // Use an already obtained clipboard offer without dispatching native events.
    bool CanPasteText(bool clipboardHasText) const noexcept;
    bool InputMethodComposing() const noexcept;
    Scintilla::Position FormatPrintRange(const Scintilla::Internal::PrintSurface& printer, bool draw,
                                         Scintilla::CharacterRangeFull range, Scintilla::Rectangle area);
    void SetColorCallTipMode(ColorCallTipMode mode);
    uint64_t DocumentRevision() const { return documentRevision_; }
    uint64_t SelectionRevision() const;
    intptr_t SelectionCharacterCount();
    void ToggleWordWrap();
    void SetWordWrapMode(Scintilla::Wrap mode);
    Scintilla::Wrap PreferredWordWrapMode() const { return wordWrapMode_; }
    void SetWordWrapIndent(WrapContinuationIndent indent);
    WrapContinuationIndent WordWrapIndent() const { return wordWrapIndent_; }
    bool AutoShrinkScrollWidth() const { return autoShrinkScrollWidth_; }
    void SetAutoShrinkScrollWidth(bool enabled);
    void IndentLines(bool forwards);
    // False reports the original unsupported rectangular-selection operation.
    bool SpacesToTabs(bool onlyIndent = false);
    bool TabsToSpaces(bool onlyIndent = false);
    // Original EditConvertNumRadix main-selection operation; false reports a rectangle.
    bool ConvertNumberRadix(unsigned radix);
    // False reports the original unsupported rectangular-selection operation.
    bool ToggleLineComments(const char* prefix, bool spaceAfterComment);
    bool ToggleBlockComments(const char* open, const char* close, int commentStyleLast);
    bool EncloseSelection(const wxString& before, const wxString& after);
    bool ConvertUrl(UrlConversion command);
    bool ConvertBase64(Base64Conversion command, const wxString& path = {}, bool defaultEncoding = false);
    void CopyAsRtf(bool nativeAnsi);
    NativeByteSnapshot RawNativeBytes() const;
    std::string NativeSelectedBytes(bool binary = true);
    std::string NativeBytesAfter(intptr_t position);
    wxString CaptureTitleExcerpt(bool defaultEncoding);
    void InsertDateTime(DateTimeInsertion command, const wxString& format = {});
    void InsertGuid();
    void InsertEncodingIdentifier(std::string_view name, const SyntaxDefinition& syntax);
    void InsertScriptShebangLine(const SyntaxDefinition& syntax);
    void InsertUnicodeControlCharacter(UnicodeControlCharacter character, bool defaultEncoding);
    AlignmentResult AlignLines(AlignmentMode mode);
    bool ModifyLines(const wxString& prefix, const wxString& suffix, bool skipEmpty = false);
    bool CompressWhitespace();
    bool RemoveBlankLines(bool merge);
    void MoveLines(bool upwards);
    void StripLeadingBlanks();
    void StripTrailingBlanks(bool wholeDocument = false);
    // False reports the original unsupported rectangular-selection operation.
    bool StripFirstCharacter();
    bool StripLastCharacter();
    // The original duplicate-line caller uses (true, true) inside its own undo action.
    void PadWithSpaces(bool skipEmpty = false, bool noUndoGroup = false);
    void JoinLines();
    void SplitLines();
    bool WrapToColumn(int column);
    // False reports the original unsupported rectangular-selection operation.
    bool JoinParagraphs();
    void MergeDuplicateLines();
    void RemoveDuplicateLines();
    void SortLines(unsigned options = SortOption::Ascending);
    void SetIndentationWidths(int tabWidth, int indentWidth);
    void SetCompletionSettings(const WordCompletionSettings& settings) { completionSettings_ = settings; }
    const WordCompletionSettings& GetCompletionSettings() const { return completionSettings_; }
    WordCompletionState& CompletionState() { return completionState_; }
    void SetWordCompletionHandler(std::function<void(WordCompletionCondition)> handler) {
        Send(Scintilla::Message::AutoCCancel);
        completionState_ = {};
        wordCompletionHandler_ = std::move(handler);
    }
    void SetAutoInsertMask(int mask);
    bool IsOpenBraceMatched(intptr_t position, intptr_t startPosition);
    void SetFontSettings(const FontSettings& settings);
    const FontSettings& Fonts() const;
    void SetCompletionStyles(CompletionStyles styles) { completionStyles_ = std::move(styles); }
    bool AutoIndentEnabled() const { return autoIndent_; }
    void SetAutoIndent(bool enabled) { autoIndent_ = enabled; }
    void Copy();
    // Original EditCopyAppend; false reports a nonempty rectangular selection.
    bool CopyAdd();
    void SwapClipboard(bool defaultEncoding = false);
    void Cut();
    void CopyLine();
    void CutLine();
    void DeleteLine();
    void JumpToPosition(intptr_t position);
    void GoToBlock(BlockNavigation command);
    void SelectWord();
    void SelectLines(bool currentBlock = false);
    void SelectCurrentBlockLines();
    void FocusSelectionBoundary(bool start);
    void MatchBrace(bool select = false);
    bool VisualBraceMatching() const { return matchBraces_; }
    void SetVisualBraceMatching(bool enabled);
    void ToggleBookmark();
    void GoToBookmark(bool forwards);
    void SelectAllBookmarks();
    LineSelectionMode GetLineSelectionMode() const { return lineSelectionMode_; }
    void SetLineSelectionMode(LineSelectionMode mode);
    bool LineNumbersVisible() const { return lineNumbersVisible_; }
    void SetLineNumbersVisible(bool visible);
    bool BookmarkMarginVisible() const { return bookmarkMarginVisible_; }
    void SetBookmarkMarginVisible(bool visible);
    int ChangeHistoryOptions() const { return changeHistoryOptions_; }
    void SetChangeHistoryOptions(int options);
    void ToggleChangeHistory();
    bool CodeFoldingVisible() const { return codeFoldingVisible_; }
    void SetCodeFoldingVisible(bool visible);
    void ToggleFolds(FoldCommand command, const SyntaxOriginalMetadata& syntax, int level = 0);
    bool CurrentBlockHighlighted() const { return highlightCurrentBlock_; }
    void SetCurrentBlockHighlighted(bool enabled);
    LineHighlightMode GetCurrentLineHighlightMode() const { return lineHighlightMode_; }
    void SetCurrentLineHighlightMode(LineHighlightMode mode);
    void ApplyStylePixelSettings();
    const CaretAppearance& GetCaretAppearance() const { return caretAppearance_; }
    void SetCaretAppearance(CaretAppearance appearance);
    OccurrenceMarking& Occurrences() { return *occurrences_; }
    void FlushPendingUIUpdate();
    // Requires a freshly revalidated regex match; copies the original formatter's buffer.
    std::optional<std::string> ExpandRegexReplacement(const std::string& replacement);

private:
    friend struct EditorCoreObservation;
    friend class DocumentSession;
    class Core;
    template <typename Action> bool RunNativeEvent(Action&& action);
    template <typename Event, typename Handler>
    void BindCoreEvent(const wxEventTypeTag<Event>& type, Handler handler, int id = wxID_ANY);
    wxTimer caretTimer_;
#ifdef __WXGTK__
    bool inputMethodComposing_ = false;
    unsigned long preeditStartHandler_ = 0;
    unsigned long preeditEndHandler_ = 0;
    void GTKHandleRealized() override;
#endif
    wxTimer scrollTimer_;
    wxTimer widenTimer_;
    wxTimer dwellTimer_;
    ColorCallTipMode colorCallTipMode_ = ColorCallTipMode::Off;
    struct ColorCallTipRange {
        intptr_t start;
        intptr_t end;
        uint32_t color;
    };
    std::optional<ColorCallTipRange> colorCallTip_;
    void ShowColorCallTip(intptr_t position);
    void QueueColorCallTipEdit();
    void EditColorCallTip(ColorCallTipRange range);
    std::unique_ptr<Core> core_;
    // Select the replacement using the main selection's original direction.
    void ReplaceRangeAndSelect(intptr_t start, intptr_t end, std::string_view replacement);
    void ReplaceTargetWithNativeBytes(std::string_view text, NativeByteRanges ranges);
    std::unique_ptr<OccurrenceMarking> occurrences_;
    uint64_t documentRevision_ = 0;
    bool lineNumbersVisible_ = true;
    bool lineNumberWidthDirty_ = true;
    bool bookmarkMarginVisible_ = false;
    bool bookmarkMarginWidthDirty_ = true;
    int changeHistoryOptions_ = 0;
    bool codeFoldingVisible_ = true;
    bool foldMarginWidthDirty_ = true;
    bool highlightCurrentBlock_ = true;
    WordCompletionSettings completionSettings_;
    WordCompletionState completionState_;
    std::function<void(WordCompletionCondition)> wordCompletionHandler_;
    int autoInsertMask_ = AutoInsertMask_Default;
    std::shared_ptr<const FontSettings> fontSettings_;
    CompletionStyles completionStyles_;
    void AutoInsertCharacter(int ch);
    // False retains the original failed-tag member-arrow completion path.
    bool AutoCloseTag();
    bool autoIndent_ = true;
    bool autoShrinkScrollWidth_ = false;
    Scintilla::Wrap wordWrapMode_ = Scintilla::Wrap::Auto;
    WrapContinuationIndent wordWrapIndent_ = WrapContinuationIndent::None;
    void ApplyWrapIndent();
    bool matchBraces_ = true;
    void AutoIndent();
    LineSelectionMode lineSelectionMode_ = LineSelectionMode::VisualStudio;
    LineHighlightMode lineHighlightMode_ = LineHighlightMode::OutlineFrame;
    CaretAppearance caretAppearance_;
    void ApplyCaretAppearance();
    void UpdateLineNumberMargin();
    void UpdateBookmarkMargin();
    void UpdateFoldMargin();
    void ToggleAllFolds();
    void FinishBatchFold();
    void OnPaint(wxPaintEvent& event);
    void OnIdle(wxIdleEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnSize(wxSizeEvent& event);
#ifdef __WXMSW__
    void OnHostMove(wxMoveEvent& event);
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif
};
