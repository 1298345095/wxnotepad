#include "app/editor_frame.h"
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

namespace {
wxString Number(intptr_t value) {
    return wxNumberFormatter::ToString(static_cast<wxLongLong_t>(value), wxNumberFormatter::Style_WithThousandsSep);
}
wxString DocumentSize(intptr_t bytes) {
    if (bytes < 1024) return wxString::Format(_("%s bytes"), Number(bytes));
    constexpr std::array units{"KB", "MB", "GB", "TB"};
    double size = static_cast<double>(bytes) / 1024;
    size_t unit = 0;
    while (size >= 1024 && unit + 1 < units.size()) {
        size /= 1024;
        ++unit;
    }
    return wxString::Format("%.2f %s", size, units[unit]);
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
    set(StatusField::Line, wxString::Format(_("Ln %s / %s"), Number(line + 1), Number(editor_->Send(GetLineCount))));
    set(StatusField::Column, wxString::Format(_("Col %s / %s"), Number(column), Number(statusLineColumns_)));
    set(StatusField::Character, wxString::Format(_("Ch %s / %s"), Number(character), Number(statusLineCharacters_)));
    const auto selectionStart = editor_->Send(GetSelectionStart);
    const auto selectionEnd = editor_->Send(GetSelectionEnd);
    const bool selected = selectionStart != selectionEnd;
    const bool rectangle = editor_->Send(SelectionIsRectangle) != 0;
    const wxString characters = selected && rectangle ? wxString("--") : Number(editor_->SelectionCharacterCount());
    const wxString bytes = selected && rectangle ? wxString("--") : Number(selected ? editor_->Send(GetSelText) : 0);
    set(StatusField::Selection, wxString::Format(_("Sel %s / %s"), characters, bytes));
    const auto first = editor_->Send(LineFromPosition, selectionStart);
    const auto last = editor_->Send(LineFromPosition, selectionEnd);
    const auto lines = selected ? last - first + (editor_->Send(PositionFromLine, last) != selectionEnd) : 0;
    set(StatusField::SelectedLines, wxString::Format(_("SelLn %s"), Number(lines)));
    const auto& marking = editor_->Occurrences();
    set(StatusField::Matches, wxString::Format(_("Fnd %s%s"), Number(static_cast<intptr_t>(marking.MatchCount())),
                                               marking.Pending() ? " ..." : ""));
    set(StatusField::Scheme,
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
#endif
    const int feedbackWidth = status->GetTextExtent(status->GetStatusText(StatusField::Feedback)).x;
    if (statusPreferredWidths_ == widths && statusMeasuredSize_ == status->GetClientSize() &&
        statusFeedbackWidth_ == feedbackWidth)
        return;
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
}

void EditorFrame::CreatePositionStatus() {
    auto* status = CreateStatusBar(StatusField::Count);
    SetStatusBarPane(StatusField::Feedback);
    status->Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        UpdateStatusWidths();
        event.Skip();
    });
    status->Bind(wxEVT_LEFT_DCLICK, [this, status](wxMouseEvent& event) {
        int id = wxNOT_FOUND;
        for (int field = 0; field < StatusField::Count; ++field) {
            wxRect rectangle;
            if (!status->GetFieldRect(field, rectangle) || !rectangle.Contains(event.GetPosition())) continue;
            switch (field) {
            case StatusField::Line:
                id = FindMenuCommand(_("&Goto Line...\tCtrl+G"));
                break;
            case StatusField::Matches:
                id = wxID_FIND;
                break;
            case StatusField::Encoding:
                id = FindMenuCommand(_("&More...\tF9"));
                break;
            case StatusField::Scheme:
                PopupMenu(GetMenuBar()->GetMenu(4));
                break;
            case StatusField::Overtype:
                editor_->Send(Scintilla::Message::EditToggleOvertype);
                UpdatePositionStatus();
                break;
            case StatusField::Eol: {
                const auto mode = static_cast<Scintilla::EndOfLine>(editor_->Send(Scintilla::Message::GetEOLMode));
                id = FindMenuCommand(mode == Scintilla::EndOfLine::CrLf ? _("&Unix/macOS (LF)")
                                     : mode == Scintilla::EndOfLine::Lf ? _("Classic &Mac OS (CR)")
                                                                        : _("&Windows (CR+LF)"));
                break;
            }
            default:
                break;
            }
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
            wxCommandEvent command(wxEVT_MENU, FindMenuCommand(_("Zoom &Level...")));
            GetEventHandler()->ProcessEvent(command);
        } else if (status->GetFieldRect(StatusField::Eol, rectangle) && rectangle.Contains(event.GetPosition())) {
            const auto mode = static_cast<Scintilla::EndOfLine>(editor_->Send(Scintilla::Message::GetEOLMode));
            const auto label = mode == Scintilla::EndOfLine::CrLf ? _("&Windows (CR+LF)")
                               : mode == Scintilla::EndOfLine::Lf ? _("&Unix/macOS (LF)")
                                                                  : _("Classic &Mac OS (CR)");
            wxCommandEvent command(wxEVT_MENU, FindMenuCommand(label));
            GetEventHandler()->ProcessEvent(command);
        }
        event.Skip();
    });
}
