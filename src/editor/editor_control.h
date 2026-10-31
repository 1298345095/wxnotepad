#pragma once

#include <ScintillaMessages.h>
#include <wx/control.h>
#include <wx/timer.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

wxDECLARE_EVENT(wxEVT_EDITOR_STATE_CHANGED, wxCommandEvent);

// A wx window backed by exactly one original Notepad4 Scintilla document.
// Commands use the pinned core's public message API; no mirrored wx text exists.
class EditorControl final : public wxControl {
public:
    explicit EditorControl(wxWindow* parent);
    ~EditorControl() override;
    intptr_t Send(Scintilla::Message message, uintptr_t wParam = 0, intptr_t lParam = 0);
    uint64_t DocumentRevision() const { return documentRevision_; }
    uint64_t SelectionRevision() const;
    void ToggleWordWrap();
    void IndentLines(bool forwards);
    void SetIndentationWidth(int width);
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
    void UpdateLineNumberMargin();
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
#ifdef __WXMSW__
    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam) override;
#endif
};
