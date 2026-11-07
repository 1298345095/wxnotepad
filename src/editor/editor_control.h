#pragma once

#include "editor/align_lines.h"
#include "editor/sort_lines.h"
#include "editor/date_time.h"
#include "editor/url_conversion.h"
#include <ScintillaMessages.h>
#include <wx/control.h>
#include <wx/timer.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

wxDECLARE_EVENT(wxEVT_EDITOR_STATE_CHANGED, wxCommandEvent);

// Original Edit.h::LineSelectionMode, including its built-in Visual Studio default.
enum class LineSelectionMode { None, VisualStudio, Normal, OldVisualStudio };

// A wx window backed by exactly one original Notepad4 Scintilla document.
// Commands use the pinned core's public message API; no mirrored wx text exists.
class EditorControl final : public wxControl {
public:
    explicit EditorControl(wxWindow* parent, int selectionOptions = 15);
    ~EditorControl() override;
    intptr_t Send(Scintilla::Message message, uintptr_t wParam = 0, intptr_t lParam = 0);
    uint64_t DocumentRevision() const { return documentRevision_; }
    uint64_t SelectionRevision() const;
    intptr_t SelectionCharacterCount();
    void ToggleWordWrap();
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
    void InsertDateTime(DateTimeInsertion command, const wxString& format = {});
    AlignmentResult AlignLines(AlignmentMode mode);
    bool ModifyLines(const wxString& prefix, const wxString& suffix, bool skipEmpty = false);
    bool CompressWhitespace();
    bool RemoveBlankLines(bool merge);
    void MoveLines(bool upwards);
    void StripLeadingBlanks();
    void StripTrailingBlanks();
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
    bool AutoIndentEnabled() const { return autoIndent_; }
    void SetAutoIndent(bool enabled) { autoIndent_ = enabled; }
    void Copy();
    // Original EditCopyAppend; false reports a nonempty rectangular selection.
    bool CopyAdd();
    void Cut();
    void CopyLine();
    void CutLine();
    void DeleteLine();
    void JumpToPosition(intptr_t position);
    void MatchBrace(bool select = false);
    void ToggleBookmark();
    void GoToBookmark(bool forwards);
    void SelectAllBookmarks();
    LineSelectionMode GetLineSelectionMode() const { return lineSelectionMode_; }
    void SetLineSelectionMode(LineSelectionMode mode);
    bool LineNumbersVisible() const { return lineNumbersVisible_; }
    void SetLineNumbersVisible(bool visible);
    // Requires a freshly revalidated regex match; copies the original formatter's buffer.
    std::optional<std::string> ExpandRegexReplacement(const std::string& replacement);

private:
    class Core;
    wxTimer caretTimer_;
    wxTimer scrollTimer_;
    wxTimer widenTimer_;
    std::unique_ptr<Core> core_;
    uint64_t documentRevision_ = 0;
    bool lineNumbersVisible_ = true;
    bool lineNumberWidthDirty_ = true;
    bool autoIndent_ = true;
    void AutoIndent();
    LineSelectionMode lineSelectionMode_ = LineSelectionMode::VisualStudio;
    void UpdateLineNumberMargin();
    void OnPaint(wxPaintEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnSize(wxSizeEvent& event);
#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif
};
