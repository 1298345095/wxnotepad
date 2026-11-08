#include "editor/editor_control.h"
#include "editor/editor_core.h"
#include <wx/caret.h>
#include <wx/settings.h>
#include <algorithm>
#include <cmath>

void EditorControl::SetCaretAppearance(CaretAppearance appearance) {
    caretAppearance_ = appearance;
    ApplyCaretAppearance();
}

void EditorControl::ApplyCaretAppearance() {
    // Notepad4 89b8b36 Styles.cpp::Style_UpdateCaret. The original core owns
    // selection placement, Unicode block geometry, overstrike and blink drawing.
    const auto& appearance = caretAppearance_;
    const int style = (appearance.lineWidth ? 1 : 2) | (appearance.overstrikeBlock ? 16 : 0) |
                      (appearance.blockAfterSelection ? 256 : 0);
    core_->WndProc(Scintilla::Message::SetCaretStyle, style, 0);
    if (appearance.lineWidth) {
        int width = appearance.lineWidth;
#ifdef __WXMSW__
        DWORD systemWidth = 0;
        if (SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &systemWidth, 0) && systemWidth > static_cast<DWORD>(width))
            width = static_cast<int>(systemWidth);
        else {
#endif
            if (width == 1) {
                width =
                    std::max(1, static_cast<int>(std::lround(GetDPI().y * Send(Scintilla::Message::GetZoom) / 9600.0)));
            }
#ifdef __WXMSW__
        }
#endif
        core_->WndProc(Scintilla::Message::SetCaretWidth, width, 0);
    }
    int period = appearance.blinkPeriod;
    if (period < 0) {
#ifdef __WXMSW__
        period = static_cast<int>(GetCaretBlinkTime());
#else
        period = wxSystemSettings::GetMetric(wxSYS_CARET_ON_MSEC, this);
        if (period < 0) period = wxCaret::GetBlinkTime();
#endif
    }
    core_->WndProc(Scintilla::Message::SetCaretPeriod, period, 0);
    core_->SyncSystemCaret();
}
