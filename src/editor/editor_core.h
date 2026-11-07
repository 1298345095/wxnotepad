#pragma once

#include "editor/editor_control.h"
#include "editor/platform/core_headers.h"
#ifdef __WXGTK__
class GtkTextDragDrop;
#endif

class EditorControl::Core final : public Scintilla::Internal::ScintillaBase {
public:
    Core(EditorControl& window, int selectionOptions);
    ~Core() override;
    intptr_t WndProc(Scintilla::Message message, uintptr_t wParam, intptr_t lParam) override;

    uint64_t SelectionRevision() const noexcept;

    intptr_t SelectionCharacterCount();

    void Draw(wxDC& dc);
    void Resize();
    void UpdateWrapLayout();
    void DpiChanged();
    void Character(std::string_view text);
    bool EditingKey(const wxKeyEvent& event);
    bool Navigation(const wxKeyEvent& event);
    void PasteWithEncoding(bool defaultEncoding);
    void TickCaret();
    void TickScroll();
    void TickWiden();
    void Scroll(wxScrollWinEvent& event);
    void Wheel(wxMouseEvent& event);
    void Mouse(wxMouseEvent& event);
    void CaptureLost();
    void SyncSystemCaret();
    void RepairFoldSelection();

private:
    class TextDropTarget;
    void ShowDropFeedback(int x, int y);
    void ClearDropFeedback();
    bool dropFeedback_ = false;
#ifdef __WXGTK__
    std::unique_ptr<GtkTextDragDrop> gtkDragDrop_;
#endif
    bool HandleApplicationKey(const wxKeyEvent& event);

    EditorControl& window_;
    int verticalWheel_ = 0;
    int horizontalWheel_ = 0;
    void NotifyCaretMove() const noexcept override;
    void UpdateSystemCaret() override;
    void SetHorizontalScrollPos() override;
    bool ModifyScrollBars(Sci::Line maximum, Sci::Line page) override;
    void SetVerticalScrollPos() override;
    void Copy(bool) const override;
    void CopyToClipboard(const Scintilla::Internal::SelectionText& text) const override;
    void Cut(bool, bool lineCopy = false) override;
    bool CanPaste() const noexcept override;
    void Paste(bool) override;
    void NotifyChange() const noexcept override;
    void NotifyParent(Scintilla::NotificationData& notification) const noexcept override;
    std::unique_ptr<Scintilla::Internal::CaseFolder> CaseFolderForEncoding() const override;
    std::string CaseMapString(const std::string& value, CaseMapping mapping) const override;
    bool SCICALL DragThreshold(Scintilla::Internal::Point start,
                               Scintilla::Internal::Point now) const noexcept override;
    void StartDrag() override;
    bool FineTickerRunning(TickReason reason) const noexcept override;
    void FineTickerStart(TickReason reason, int millis, int) noexcept override;
    void FineTickerCancel(TickReason reason) noexcept override;
    bool SetIdle(bool) noexcept override;
    void SetMouseCapture(bool on) noexcept override;
    bool HaveMouseCapture() const noexcept override;
    void UpdateBaseElements() noexcept override;
    bool ValidCodePage(int codepage) const noexcept override;
    std::string UTF8FromEncoded(std::string_view text) const override;
    std::string EncodedFromUTF8(std::string_view text) const override;
    intptr_t DefWndProc(Scintilla::Message, uintptr_t, intptr_t) override;
    void SCICALL CreateCallTipWindow(Scintilla::Internal::PRectangle) noexcept override;
    mutable Sci::Position observedAnchor_ = -1;
    mutable Sci::Position observedCaret_ = -1;
    mutable uint64_t selectionRevision_ = 0;
    intptr_t countedStart_ = -1;
    intptr_t countedEnd_ = -1;
    intptr_t selectedCharacters_ = 0;
    mutable bool selectionCountDirty_ = true;
};
