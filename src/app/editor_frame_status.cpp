#include "app/editor_frame.h"
#include "app/command_ids.h"
#include "app/status_fields.h"
#include "editor/editor_control.h"
#include "editor/occurrence_marking.h"
#include "syntax/syntax_scheme.h"
#include <ScintillaStructures.h>
#include <wx/statusbr.h>
#include <wx/menu.h>
#include <wx/numformatter.h>
#include <wx/translation.h>
#include <algorithm>
#include <array>
#include <utility>
#ifdef __WXGTK__
#include <wx/control.h>
#include <wx/dc.h>
#include <wx/fontutil.h>
#include <wx/graphics.h>
#include <gtk/gtk.h>
#include <pango/pangocairo.h>
#include <cmath>
#include <memory>
#endif
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <commctrl.h>
#include <wx/settings.h>
#endif

namespace {
#ifdef __WXGTK__
struct StatusTextMetrics {
    double ascent;
    double height;
};
StatusTextMetrics MeasureStatusText(PangoLayout* layout, const wxString& text) {
    const auto bytes = text.utf8_str();
    pango_layout_set_text(layout, bytes.data(), static_cast<int>(bytes.length()));
    PangoRectangle logical;
    pango_layout_get_extents(layout, nullptr, &logical);
    const int baseline = pango_layout_get_baseline(layout);
    // Pango can shift alphabetic runs relative to the line's CJK baseline.
    // Measure a Latin anchor without drawing it, removing any line-metric
    // change caused by the anchor itself.
    const auto anchored = text + " M";
    const auto anchoredBytes = anchored.utf8_str();
    pango_layout_set_text(layout, anchoredBytes.data(), static_cast<int>(anchoredBytes.length()));
    const std::unique_ptr<PangoLayoutIter, decltype(&pango_layout_iter_free)> iter(pango_layout_get_iter(layout),
                                                                                   &pango_layout_iter_free);
    int alphabeticBaseline = 0;
    do {
        if (pango_layout_iter_get_run_readonly(iter.get()))
            alphabeticBaseline = pango_layout_iter_get_run_baseline(iter.get());
    } while (pango_layout_iter_next_run(iter.get()));
    return {static_cast<double>(baseline + alphabeticBaseline - pango_layout_get_baseline(layout)) / PANGO_SCALE,
            static_cast<double>(logical.height) / PANGO_SCALE};
}
class BaselineStatusBar final : public wxStatusBar {
public:
    using wxStatusBar::wxStatusBar;

protected:
    void DrawField(wxDC& dc, int field, int textHeight) override {
        if (field == 0) MeasureLine(dc, textHeight);
        wxStatusBar::DrawField(dc, field, textHeight_);
    }
    void DrawFieldText(wxDC& dc, const wxRect& rectangle, int field, int textHeight) override {
        // wxGTK DrawText uses each Pango layout's top, whose distance to the
        // baseline changes when CJK fallback fonts participate. Translate only
        // text drawing, keeping its physical clip at the original field.
        auto* graphics = dc.GetGraphicsContext();
        graphics->PushState();
        graphics->Clip(rectangle.x, rectangle.y, rectangle.width, rectangle.height);
        graphics->Translate(0, ascent_ - ascents_[field]);
        wxStatusBar::DrawFieldText(dc, rectangle, field, textHeight);
        graphics->PopState();
    }

private:
    void MeasureLine(const wxDC& dc, int textHeight) {
        ascent_ = 0;
        double descent = 0;
        auto* context = static_cast<cairo_t*>(dc.GetGraphicsContext()->GetNativeContext());
        const std::unique_ptr<PangoLayout, decltype(&g_object_unref)> layout(pango_cairo_create_layout(context),
                                                                             &g_object_unref);
        // Match wxCairoContext::ApplyFont, including GTK's font DPI. Integer
        // text extents lose fractional ascents at native scale 2.
        constexpr double StandardFontDpi = 96;
        const auto fontScale =
            static_cast<float>(gdk_screen_get_resolution(gtk_widget_get_screen(GetConnectWidget())) / StandardFontDpi);
        const auto font = fontScale == 1.0F ? dc.GetFont() : dc.GetFont().Scaled(fontScale);
        pango_layout_set_font_description(layout.get(), font.GetNativeFontInfo()->description);
        font.GTKSetPangoAttrs(layout.get());
        for (int index = 0; index < GetFieldsCount(); ++index) {
            const auto metrics = MeasureStatusText(layout.get(), RenderedText(dc, index));
            ascents_[index] = metrics.ascent;
            ascent_ = std::max(ascent_, metrics.ascent);
            descent = std::max(descent, metrics.height - metrics.ascent);
        }
        textHeight_ = std::max(textHeight, static_cast<int>(std::ceil(ascent_ + descent)));
    }
    wxString RenderedText(const wxDC& dc, int field) const {
        wxRect rectangle;
        if (!GetFieldRect(field, rectangle) || rectangle.width <= 0) return {};
        auto text = GetStatusText(field);
        wxEllipsizeMode mode = wxELLIPSIZE_NONE;
        if (HasFlag(wxSTB_ELLIPSIZE_START))
            mode = wxELLIPSIZE_START;
        else if (HasFlag(wxSTB_ELLIPSIZE_MIDDLE))
            mode = wxELLIPSIZE_MIDDLE;
        else if (HasFlag(wxSTB_ELLIPSIZE_END))
            mode = wxELLIPSIZE_END;
        if (mode == wxELLIPSIZE_NONE) return text;
        // Match wxStatusBarGeneric's displayed string, including the last
        // field's grip reservation: ellipsizing can remove a fallback font.
        constexpr int FieldTextMargin = 2;
        int width = rectangle.width - 2 * FieldTextMargin;
        if (ShowsSizeGrip() && ((GetLayoutDirection() == wxLayout_RightToLeft && field == 0) ||
                                (GetLayoutDirection() != wxLayout_RightToLeft && field == GetFieldsCount() - 1)))
            width -= GetSizeGripRect().width;
        return wxControl::Ellipsize(text, dc, mode, width, wxELLIPSIZE_FLAGS_EXPAND_TABS);
    }
    std::array<double, StatusField::Count> ascents_{};
    double ascent_ = 0;
    int textHeight_ = 0;
};
#endif
int StatusFieldAtPoint(wxStatusBar& status, const wxPoint& position) {
    for (int field = 0; field < StatusField::Count; ++field) {
        wxRect rectangle;
        if (status.GetFieldRect(field, rectangle) && rectangle.Contains(position)) return field;
    }
    return wxNOT_FOUND;
}
void ApplyNarrowNativeLayout(wxStatusBar* status) {
#ifdef __WXMSW__
    // Notepad4's native SB_SETPARTS path permits coincident pane edges. wxMSW
    // otherwise adds a margin even to each width-zero pane, showing fragments
    // and charging the size field for five hidden metadata panes.
    const auto window = static_cast<HWND>(status->GetHandle());
    std::array<int, StatusField::Count> edges{};
    ::SendMessageW(window, SB_GETPARTS, edges.size(), reinterpret_cast<LPARAM>(edges.data()));
    const int grip = wxSystemSettings::GetMetric(wxSYS_HSCROLL_ARROW_X, status);
    const int sizeWidth =
        status->GetTextExtent(status->GetStatusText(StatusField::DocumentSize)).x + status->GetCharHeight() / 2 + grip;
    const int sizeLeft = std::max(edges[StatusField::Matches], status->GetClientSize().x - sizeWidth);
    for (int field = StatusField::Feedback; field <= StatusField::Zoom; ++field)
        edges[field] = sizeLeft;
    edges[StatusField::DocumentSize] = status->GetClientSize().x;
    ::SendMessageW(window, SB_SETPARTS, edges.size(), reinterpret_cast<LPARAM>(edges.data()));
    // Keep wx's complete metadata: its normal width update restores it when
    // the window grows. Only the hidden native captions are suppressed.
    for (int field = StatusField::Scheme; field <= StatusField::Zoom; ++field)
        ::SendMessageW(window, SB_SETTEXTW, field, reinterpret_cast<LPARAM>(L""));
    for (const int field : {StatusField::Feedback, StatusField::DocumentSize})
        ::SendMessageW(window, SB_SETTEXTW, field, reinterpret_cast<LPARAM>(status->GetStatusText(field).wc_str()));
#else
    (void)status;
#endif
}
wxString Number(intptr_t value) {
    return wxNumberFormatter::ToString(static_cast<wxLongLong_t>(value), wxNumberFormatter::Style_WithThousandsSep);
}
intptr_t SelectionBytes(EditorControl& editor) {
    using enum Scintilla::Message;
    const auto selections = editor.Send(GetSelections);
    intptr_t bytes = 0;
    for (intptr_t selection = 0; selection < selections; ++selection)
        bytes += editor.Send(GetSelectionNEnd, selection) - editor.Send(GetSelectionNStart, selection);
    if (selections > 1) {
        auto separatorBytes = editor.Send(GetCopySeparator);
        if (!separatorBytes)
            separatorBytes = editor.Send(GetEOLMode) == std::to_underlying(Scintilla::EndOfLine::CrLf) ? 2 : 1;
        bytes += (selections - 1) * separatorBytes;
    }
    return bytes;
}
wxString DocumentSize(intptr_t bytes) {
    if (bytes < 1024)
        // TRANSLATORS: IDS_STATUSITEM_FORMAT document size; %s is the locale-formatted byte count.
        return wxString::Format(wxPLURAL("%s byte", "%s bytes", static_cast<unsigned>(bytes)), Number(bytes));
    constexpr std::array units{"KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes) / 1024;
    size_t unit = 0;
    while (size >= 1024 && unit + 1 < units.size()) {
        size /= 1024;
        ++unit;
    }
    return wxString::Format("%.2f %s", size, units[unit]);
}
AppCommand EolCommand(Scintilla::EndOfLine mode) {
    using enum AppCommand;
    if (mode == Scintilla::EndOfLine::CrLf) return EolCrLf;
    if (mode == Scintilla::EndOfLine::Lf) return EolLf;
    return EolCr;
}
AppCommand NextEolCommand(Scintilla::EndOfLine mode) {
    using enum AppCommand;
    if (mode == Scintilla::EndOfLine::CrLf) return EolLf;
    if (mode == Scintilla::EndOfLine::Lf) return EolCr;
    return EolCrLf;
}
} // namespace

void EditorFrame::UpdatePositionStatus() {
    using enum Scintilla::Message;
    auto* status = GetStatusBar();
    if (!status) return;
    const auto set = [status](int field, const wxString& text) {
        if (status->GetStatusText(field) != text) status->SetStatusText(text, field);
    };
    const auto caret = editor_->Send(GetCurrentPos);
    const auto line = editor_->Send(LineFromPosition, caret);
    const auto start = editor_->Send(PositionFromLine, line);
    Scintilla::TextToFindFull count{{start, caret}, nullptr, 0, {0, 0}};
    editor_->Send(CountCharactersAndColumns, 0, reinterpret_cast<intptr_t>(&count));
    const auto character = count.chrgText.cpMin + 1;
    const auto column = count.chrgText.cpMax + 1;
    const auto revision = editor_->DocumentRevision();
    const auto tabWidth = editor_->Send(GetTabWidth);
    if (statusLine_ != line || statusRevision_ != revision || statusTabWidth_ != tabWidth) {
        count.chrg.cpMin = caret;
        count.chrg.cpMax = editor_->Send(GetLineEndPosition, line);
        editor_->Send(CountCharactersAndColumns, 0, reinterpret_cast<intptr_t>(&count));
        statusLineCharacters_ = count.chrgText.cpMin;
        statusLineColumns_ = count.chrgText.cpMax;
        statusLine_ = line;
        statusRevision_ = revision;
        statusTabWidth_ = tabWidth;
    }
    // TRANSLATORS: IDS_STATUSITEM_FORMAT; current one-based line / total document lines, both formatted numbers.
    set(StatusField::Line, wxString::Format(_("Ln %s / %s"), Number(line + 1), Number(editor_->Send(GetLineCount))));
    // TRANSLATORS: IDS_STATUSITEM_FORMAT; current one-based visual column / line columns, including tab expansion.
    set(StatusField::Column, wxString::Format(_("Col %s / %s"), Number(column), Number(statusLineColumns_)));
    // TRANSLATORS: IDS_STATUSITEM_FORMAT; current one-based character position / line Unicode character count.
    set(StatusField::Character, wxString::Format(_("Ch %s / %s"), Number(character), Number(statusLineCharacters_)));
    const auto selectionStart = editor_->Send(GetSelectionStart);
    const auto selectionEnd = editor_->Send(GetSelectionEnd);
    const bool selected = selectionStart != selectionEnd;
    const bool rectangle = editor_->Send(SelectionIsRectangle) != 0;
    const wxString characters = selected && rectangle ? wxString("--") : Number(editor_->SelectionCharacterCount());
    const wxString bytes = selected && rectangle ? wxString("--") : Number(selected ? SelectionBytes(*editor_) : 0);
    // TRANSLATORS: IDS_STATUSITEM_FORMAT; selected Unicode characters / bytes; rectangular counts use "--".
    set(StatusField::Selection, wxString::Format(_("Sel %s / %s"), characters, bytes));
    const auto first = editor_->Send(LineFromPosition, selectionStart);
    const auto last = editor_->Send(LineFromPosition, selectionEnd);
    const auto lines = selected ? last - first + (editor_->Send(PositionFromLine, last) != selectionEnd) : 0;
    // TRANSLATORS: IDS_STATUSITEM_FORMAT; %s is the formatted number of selected logical lines.
    set(StatusField::SelectedLines, wxString::Format(_("SelLn %s"), Number(lines)));
    const auto& marking = editor_->Occurrences();
    // TRANSLATORS: IDS_STATUSITEM_FORMAT; match count followed by " ..." while occurrence marking is pending.
    set(StatusField::Matches, wxString::Format(_("Fnd %s%s"), Number(static_cast<intptr_t>(marking.MatchCount())),
                                               marking.Pending() ? " ..." : ""));
    set(StatusField::Scheme,
        // TRANSLATORS: IDS_STATUSITEM_FORMAT syntax-name suffix for a manually selected scheme, including its space.
        LanguageName(document_.EffectiveLanguage()) + (document_.LanguageOverride() ? _(" (override)") : wxString{}));
    const auto eol = static_cast<Scintilla::EndOfLine>(editor_->Send(GetEOLMode));
    set(StatusField::Eol, eol == Scintilla::EndOfLine::CrLf ? "CR+LF" : eol == Scintilla::EndOfLine::Cr ? "CR" : "LF");
    set(StatusField::Overtype, editor_->Send(GetOvertype) ? "OVR" : "INS");
    set(StatusField::Zoom, wxString::Format("%lld%%", static_cast<long long>(editor_->Send(GetZoom))));
    set(StatusField::DocumentSize, DocumentSize(editor_->Send(GetLength)));
    UpdateStatusWidths();
}

void EditorFrame::UpdateStatusWidths() {
    auto* status = GetStatusBar();
    if (!status) return;
    std::array<int, StatusField::Count> widths{};
#ifdef __WXMSW__
    // wxMSW adds the native pane border and its own text margins.
    constexpr int padding = 0;
#elif defined(__WXGTK__)
    // wxStatusBarGeneric subtracts both borders and two fixed text margins
    // when painting. Small rendered fonts still need these native insets.
    constexpr int NativeFieldTextMargin = 2;
    const int padding = std::max(status->GetCharHeight() / 2, 2 * (status->GetBorderX() + NativeFieldTextMargin));
#else
    const int padding = status->GetCharHeight() / 2;
#endif
    for (int field = 0; field < StatusField::Count; ++field) {
        if (field == StatusField::Feedback) continue;
        widths[field] = status->GetTextExtent(status->GetStatusText(field)).x + padding;
    }
    // The original keeps the left counters and document size when space is
    // exhausted. Native controls clip anything beyond the window's right edge.
    widths[StatusField::Feedback] = -1;
#ifdef __WXMSW__
    // The native final pane's text rectangle also reserves the sizing grip.
    // One measured character provides its extra text margin without a fixed
    // device-pixel assumption or charging every metadata pane for it.
    widths[StatusField::DocumentSize] += status->GetCharWidth();
#elif defined(__WXGTK__)
    // wxGTK reserves the native size-grip width again when drawing the last
    // field text, after allocating its field rectangle.
    widths[StatusField::DocumentSize] += status->GetClientSize().y;
#endif
    const int feedbackWidth = status->GetTextExtent(status->GetStatusText(StatusField::Feedback)).x;
    if (statusPreferredWidths_ == widths && statusMeasuredSize_ == status->GetClientSize() &&
        statusFeedbackWidth_ == feedbackWidth) {
        if (status->GetStatusWidth(StatusField::Scheme) == 0) ApplyNarrowNativeLayout(status);
        return;
    }
    statusPreferredWidths_ = widths;
    statusMeasuredSize_ = status->GetClientSize();
    statusFeedbackWidth_ = feedbackWidth;
    status->SetStatusWidths(StatusField::Count, widths.data());
    wxRect last;
    wxRect feedback;
    const bool fits = status->GetFieldRect(StatusField::DocumentSize, last) &&
                      last.GetRight() <= status->GetClientSize().x &&
                      last.width >= status->GetTextExtent(status->GetStatusText(StatusField::DocumentSize)).x &&
                      (feedbackWidth == 0 ||
                       (status->GetFieldRect(StatusField::Feedback, feedback) && feedback.width >= feedbackWidth));
    if (!fits) {
        for (int field = StatusField::Scheme; field <= StatusField::Zoom; ++field) {
            widths[field] = 0;
        }
    }
    bool changed = false;
    for (int field = 0; field < StatusField::Count; ++field)
        changed |= status->GetStatusWidth(field) != widths[field];
    if (changed) status->SetStatusWidths(StatusField::Count, widths.data());
    if (!fits) ApplyNarrowNativeLayout(status);
}

void EditorFrame::CreatePositionStatus() {
#ifdef __WXGTK__
    auto ownedStatus = std::make_unique<BaselineStatusBar>(this);
    ownedStatus->SetFieldsCount(StatusField::Count);
    auto* status = ownedStatus.get();
    SetStatusBar(ownedStatus.release());
#else
    auto* status = CreateStatusBar(StatusField::Count);
#endif
    SetStatusBarPane(StatusField::Feedback);
    status->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        UpdateStatusWidths();
        event.Skip();
    });
    status->Bind(wxEVT_LEFT_DCLICK, [this, status](wxMouseEvent& event) {
        int id = wxNOT_FOUND;
        switch (StatusFieldAtPoint(*status, event.GetPosition())) {
        case StatusField::Line:
            id = std::to_underlying(AppCommand::GoToLine);
            break;
        case StatusField::Matches:
            id = wxID_FIND;
            break;
        case StatusField::Encoding:
            id = std::to_underlying(AppCommand::ChooseEncoding);
            break;
        case StatusField::Scheme: {
            // Pinned Notepad4.cpp StatusItem_Lexer opens CSV Options directly.
            if (document_.Language() == SyntaxLanguage::Csv)
                ChooseCsvLanguage();
            else if (wxMenu* scheme = nullptr;
                     GetMenuBar()->FindItem(std::to_underlying(AppCommand::SyntaxScheme), &scheme))
                PopupMenu(scheme);
            break;
        }
        case StatusField::Overtype:
            editor_->Send(Scintilla::Message::EditToggleOvertype);
            UpdatePositionStatus();
            break;
        case StatusField::Eol: {
            const auto mode = static_cast<Scintilla::EndOfLine>(editor_->Send(Scintilla::Message::GetEOLMode));
            id = std::to_underlying(NextEolCommand(mode));
            break;
        }
        default:
            break;
        }
        if (id != wxNOT_FOUND) {
            wxCommandEvent command(wxEVT_MENU, id);
            GetEventHandler()->ProcessEvent(command);
        }
        editor_->SetFocus();
    });
    status->Bind(wxEVT_LEFT_UP, [this, status](wxMouseEvent& event) {
        wxRect rectangle;
        if (status->GetFieldRect(StatusField::Zoom, rectangle) && rectangle.Contains(event.GetPosition())) {
            wxCommandEvent command(wxEVT_MENU, std::to_underlying(AppCommand::ZoomLevel));
            GetEventHandler()->ProcessEvent(command);
        } else if (status->GetFieldRect(StatusField::Eol, rectangle) && rectangle.Contains(event.GetPosition())) {
            const auto mode = static_cast<Scintilla::EndOfLine>(editor_->Send(Scintilla::Message::GetEOLMode));
            const int id = std::to_underlying(EolCommand(mode));
            wxCommandEvent command(wxEVT_MENU, id);
            GetEventHandler()->ProcessEvent(command);
        }
        event.Skip();
    });
}
