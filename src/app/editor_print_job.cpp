#include "editor_print_job.h"

#include "diagnostics/trace.h"
#include "editor/editor_control.h"
#include "editor/font_settings.h"
#include "editor/platform/native_drawing_error.h"
#include "editor/platform/print_surface.h"
#include "editor/print_font.h"
#include "editor/platform/text_measurement.h"
#include "editor_printing.h"
#include "settings/print_options.h"
#include <ScintillaStructures.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <wx/datetime.h>
#include <wx/dcprint.h>
#include <wx/intl.h>
#include <wx/msgdlg.h>
#include <wx/numformatter.h>
#include <wx/uilocale.h>
#include <wx/printdlg.h>
#ifdef __WXMSW__
#include <wx/fontutil.h>
#include <wx/msw/dcprint.h>
#include <wx/msw/printdlg.h>
#include <wx/msw/wrapwin.h>
#include <commdlg.h>
#else
#include <gtk/gtk.h>
#include <wx/gtk/print.h>
#endif

namespace EditorPrinting {
namespace {
using Scintilla::Message;
using Scintilla::Position;
using Scintilla::Internal::NativeDrawingError;
using Scintilla::Internal::PrintSurface;
using Scintilla::Internal::TextMeasurement;

struct PageArea {
    wxRect paper;
    wxRect imageable;
};

wxString DateLabel(PrintHeader header) {
    if (header != PrintHeader::FilenameAndDate && header != PrintHeader::FilenameAndDateTime) return {};
#ifdef __WXMSW__
    SYSTEMTIME local{};
    GetLocalTime(&local);
    std::array<wchar_t, 128> date{};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date.data(), date.size(), nullptr);
    if (header == PrintHeader::FilenameAndDate) return wxString(date.data());
    std::array<wchar_t, 64> time{};
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time.data(), time.size());
    return wxString(date.data()) + ' ' + time.data();
#else
    const auto now = wxDateTime::Now();
    auto result = now.FormatDate();
    if (header == PrintHeader::FilenameAndDateTime) {
        const auto format = wxUILocale::GetCurrent().GetInfo(wxLOCALE_TIME_FMT);
        const bool twelveHours = format.Contains("%I") || format.Contains("%r") || format.Contains("%p");
        result += ' ' + now.Format(twelveHours ? "%I:%M %p" : "%H:%M");
    }
    return result;
#endif
}

// Bridge.cpp expresses cell heights and insets in printer device units.
constexpr int PointsPerInch = 72;
constexpr int LabelInset = 5;
constexpr int RuleWidth = 1;

#ifdef __WXMSW__
class DecorationContext {
public:
    explicit DecorationContext(wxDC& dc) : dc_(static_cast<HDC>(dc.GetHDC())), saved_(SaveDC(dc_)) {
        if (!saved_) throw NativeDrawingError("Could not save the printer drawing state");
    }
    ~DecorationContext() { RestoreDC(dc_, saved_); }
    DecorationContext(const DecorationContext&) = delete;
    DecorationContext& operator=(const DecorationContext&) = delete;

    void UseFont(const wxFont& font) const {
        const auto previous = SelectObject(dc_, static_cast<HFONT>(font.GetHFONT()));
        if (!previous || previous == HGDI_ERROR) throw NativeDrawingError("Could not select a printer heading font");
    }
    int LineHeight() const {
        TEXTMETRICW metrics{};
        if (!GetTextMetricsW(dc_, &metrics)) throw NativeDrawingError("Could not measure a printer heading font");
        return metrics.tmHeight + metrics.tmExternalLeading;
    }
    int Width(const wxString& text) const {
        SIZE size{};
        if (!GetTextExtentPoint32W(dc_, text.wc_str(), static_cast<int>(text.size()), &size))
            throw NativeDrawingError("Could not measure a printer heading");
        return size.cx;
    }
    void DrawLabel(const wxString& text, int x, int y, const RECT& rect, UINT align) const {
        SetTextAlign(dc_, align);
        SetTextColor(dc_, RGB(0, 0, 0));
        SetBkColor(dc_, RGB(255, 255, 255));
        ExtTextOutW(dc_, x, y, ETO_OPAQUE, &rect, text.wc_str(), static_cast<UINT>(text.size()), nullptr);
    }
    void DrawRule(int left, int right, int y) const {
        const auto pen = CreatePen(PS_SOLID, RuleWidth, RGB(0, 0, 0));
        if (!pen) throw NativeDrawingError("Could not create the printer rule pen");
        const auto previous = SelectObject(dc_, pen);
        if (previous && previous != HGDI_ERROR) {
            MoveToEx(dc_, left, y, nullptr);
            LineTo(dc_, right, y);
            SelectObject(dc_, previous);
        }
        DeleteObject(pen);
    }

private:
    HDC dc_;
    int saved_;
};
#endif

int DecorationLineHeight(wxDC& dc, const wxFont& font) {
#ifdef __WXMSW__
    const DecorationContext context(dc);
    context.UseFont(font);
    return context.LineHeight();
#else
    dc.SetFont(font);
    wxCoord width = 0;
    wxCoord height = 0;
    wxCoord leading = 0;
    dc.GetTextExtent("Hg", &width, &height, nullptr, &leading);
    return height + leading;
#endif
}

wxFont DecorationFont(wxDC& dc, const wxString& face, int size) {
    constexpr int pointUnitsPerInch = PointsPerInch * Scintilla::FontSizeMultiplier;
#ifdef __WXMSW__
    // Positive LOGFONT height requests the complete cell, including internal leading.
    wxNativeFontInfo native;
    native.SetFaceName(face);
    native.lf.lfHeight = std::max(1, MulDiv(size, dc.GetPPI().y, pointUnitsPerInch));
    native.lf.lfWeight = FW_NORMAL;
    native.lf.lfCharSet = DEFAULT_CHARSET;
    native.lf.lfQuality = DEFAULT_QUALITY;
    wxFont font;
    font.SetNativeFontInfo(native);
    return font;
#else
    wxFont font = FontForPrinter(
        wxFont(wxFontInfo(static_cast<double>(size) / Scintilla::FontSizeMultiplier).FaceName(face)), dc);
    // Pango specifies the em size. Scale it against the printer layout's
    // measured cell so the requested height means the same thing as in GDI.
    const double cell = static_cast<double>(size) * dc.GetPPI().y / pointUnitsPerInch;
    const int measured = DecorationLineHeight(dc, font);
    if (measured > 0) font.SetFractionalPointSize(font.GetFractionalPointSize() * cell / measured);
    return font;
#endif
}

// The original EditPrint reserves a line and a half for each enabled heading.
class PageLayout {
public:
    PageLayout(wxDC& dc, PageArea area, const PrintOptions& options, int unitsPerInch, wxFont header, wxFont footer)
        : header_(std::move(header)), footer_(std::move(footer)) {
        const auto dpi = dc.GetPPI();
        const auto pixels = [&](int margin, int resolution, int paperSize) {
            return static_cast<int>(std::min(static_cast<double>(paperSize),
                                             std::round(static_cast<double>(margin) * resolution / unitsPerInch)));
        };
        const auto left = pixels(options.margins[0], dpi.x, area.paper.width);
        const auto top = pixels(options.margins[1], dpi.y, area.paper.height);
        const auto right = pixels(options.margins[2], dpi.x, area.paper.width);
        const auto bottom = pixels(options.margins[3], dpi.y, area.paper.height);
        margins_ = wxRect(area.paper.x + left, area.paper.y + top, std::max(0, area.paper.width - left - right),
                          std::max(0, area.paper.height - top - bottom))
                       .Intersect(area.imageable);
        if (options.header != PrintHeader::Blank) headerHeight_ = DecorationLineHeight(dc, header_);
        if (options.footer != PrintFooter::Blank) footerHeight_ = DecorationLineHeight(dc, footer_);
        content_ = {margins_.x, margins_.y + headerHeight_ + headerHeight_ / 2, margins_.GetRight() + 1,
                    margins_.GetBottom() + 1 - footerHeight_ - footerHeight_ / 2};
    }

    bool HasContentArea() const { return content_.right > content_.left && content_.bottom > content_.top; }

    Scintilla::Rectangle Content() const { return content_; }

    void DrawHeader(wxDC& dc, const wxString& title, const wxString& date) const {
        if (!headerHeight_) return;
        const int bottom = content_.top - headerHeight_ / 2;
        const int rule = content_.top - headerHeight_ / 4;
#ifdef __WXMSW__
        const DecorationContext context(dc);
        RECT area{content_.left, bottom - headerHeight_, content_.right, bottom};
        context.UseFont(header_);
        context.DrawLabel(title, area.left + LabelInset, bottom, area, TA_BOTTOM);
        if (!date.empty()) {
            context.UseFont(footer_);
            area.left = area.right - 2 * LabelInset - context.Width(date);
            context.DrawLabel(date, area.left + LabelInset, bottom, area, TA_BOTTOM);
        }
        context.DrawRule(content_.left, content_.right, rule);
#else
        dc.SetTextForeground(*wxBLACK);
        dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
        dc.SetFont(footer_);
        const auto dateSize = dc.GetTextExtent(date);
        const int dateX = content_.right - LabelInset - dateSize.x;
        {
            const int titleRight = date.empty() ? content_.right : dateX - LabelInset;
            const int titleSpace = titleRight - content_.left;
            if (titleSpace > LabelInset) {
                wxDCClipper titleClip(dc, content_.left, bottom - headerHeight_, titleSpace, headerHeight_);
                dc.SetFont(header_);
                dc.DrawText(title, content_.left + LabelInset, bottom - dc.GetTextExtent(title).y);
            }
        }
        dc.SetFont(footer_);
        if (!date.empty()) {
            wxDCClipper dateClip(dc, content_.left, bottom - headerHeight_, margins_.width, headerHeight_);
            dc.DrawText(date, dateX, bottom - dateSize.y);
        }
        dc.SetPen(wxPen(*wxBLACK, RuleWidth));
        dc.DrawLine(content_.left, rule, content_.right, rule);
#endif
    }

    void DrawFooter(wxDC& dc, int page) const {
        if (!footerHeight_) return;
        const int top = content_.bottom + footerHeight_ / 2;
        const int rule = content_.bottom + footerHeight_ / 4;
        // TRANSLATORS: IDS_PRINT_PAGENUM footer; %s is the already localized page number.
        const auto label = wxString::Format(_("Page %s"), wxNumberFormatter::ToString(static_cast<long>(page)));
#ifdef __WXMSW__
        const DecorationContext context(dc);
        const RECT area{content_.left, top, content_.right, top + footerHeight_};
        context.UseFont(footer_);
        context.DrawLabel(label, area.right - LabelInset - context.Width(label), top, area, TA_TOP);
        context.DrawRule(content_.left, content_.right, rule);
#else
        dc.SetFont(footer_);
        dc.SetTextForeground(*wxBLACK);
        dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
        dc.DrawText(label, content_.right - LabelInset - dc.GetTextExtent(label).x, top);
        dc.SetPen(wxPen(*wxBLACK, RuleWidth));
        dc.DrawLine(content_.left, rule, content_.right, rule);
#endif
    }

private:
    wxFont header_;
    wxFont footer_;
    wxRect margins_;
    Scintilla::Rectangle content_{};
    int headerHeight_ = 0;
    int footerHeight_ = 0;
};

class PrintBuffer {
public:
    PrintBuffer(EditorControl& editor, const wxString& title, PrintOptions& options)
        : editor_(editor), title_(title), options_(options),
          selection_{editor.Send(Message::GetSelectionStart), editor.Send(Message::GetSelectionEnd)} {}

    bool HasSelection() const { return selection_.cpMin < selection_.cpMax; }
    const wxString& Title() const { return title_; }
    int PageCount() const { return static_cast<int>(pages_.size()); }

    bool Prepare(PrintSurface& surface, PageArea area) {
        unitsPerInch_ = InitializeMargins(options_);
        date_ = DateLabel(options_.header);
        SetPrintFormat();
        const auto layout = Layout(surface.dc, area);
        if (!layout.HasContentArea()) return false;
        Scintilla::CharacterRangeFull range{0, editor_.Send(Message::GetLength)};
        if (HasSelection()) {
            const auto firstLine = editor_.Send(Message::LineFromPosition, selection_.cpMin);
            const auto lastLine = editor_.Send(Message::LineFromPosition, selection_.cpMax - 1);
            range = {editor_.Send(Message::PositionFromLine, firstLine),
                     editor_.Send(Message::GetLineEndPosition, lastLine)};
        }
        pages_.clear();
        auto position = range.cpMin;
        do {
            const auto next = std::min(Format(layout, surface, false, position, range.cpMax), range.cpMax);
            if (next <= position && position != range.cpMax) return false;
            pages_.push_back({position, range.cpMax});
            WXNOTE_TRACE("print.page", "\tpage=%zu\tstart=%td\tend=%td", pages_.size(), position, next);
            position = next;
        } while (position < range.cpMax);
        return true;
    }

    bool Draw(PrintSurface& surface, PageArea area, int index) {
        if (index < 0 || index >= PageCount()) return false;
        SetPrintFormat();
        const auto layout = Layout(surface.dc, area);
        layout.DrawHeader(surface.dc, title_, date_);
        const auto range = pages_[index];
        const auto next = std::min(Format(layout, surface, true, range.cpMin, range.cpMax), range.cpMax);
        layout.DrawFooter(surface.dc, index + 1);
        const auto expected = index + 1 == PageCount() ? range.cpMax : pages_[index + 1].cpMin;
        return next == expected;
    }

private:
    PageLayout Layout(wxDC& dc, PageArea area) const {
        const auto style = static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Default));
        constexpr int points = Scintilla::FontSizeMultiplier;
        const int baseSize = static_cast<int>(editor_.Send(Message::StyleGetSizeFractional, style));
        const auto face = DefaultStyleFont(editor_.Fonts(), false).GetFaceName();
        const auto font = [&](int adjustment) {
            return DecorationFont(dc, face, std::max(points, baseSize + adjustment));
        };
        return {dc, area, options_, unitsPerInch_, font(-points / 2), font(-2 * points)};
    }

    void SetPrintFormat() {
        editor_.Send(Message::SetPrintColourMode, static_cast<uintptr_t>(std::to_underlying(options_.colour)));
        editor_.Send(Message::SetPrintMagnification, options_.zoom);
    }

    Position Format(const PageLayout& layout, PrintSurface& surface, bool draw, Position first, Position last) {
        return editor_.FormatPrintRange(surface, draw, {first, last}, layout.Content());
    }

    EditorControl& editor_;
    const wxString& title_;
    PrintOptions& options_;
    int unitsPerInch_ = 0;
    wxString date_;
    Scintilla::CharacterRangeFull selection_;
    std::vector<Scintilla::CharacterRangeFull> pages_;
};

#ifdef __WXMSW__
bool NativePrint(wxWindow& parent, PrintBuffer& job, wxPrintData& driver) {
    // Match the original common dialog, including its Win11 native host and
    // driver-managed copies. The chosen HDC goes through the wxSTC-style bridge.
    constexpr WORD maxNativePage = 65535;
    auto draft = CopyPrinterData(driver);
    auto& native = *static_cast<wxWindowsPrintNativeData*>(draft.GetNativeData());
    PRINTDLGW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = static_cast<HWND>(parent.GetHandle());
    dialog.Flags = PD_USEDEVMODECOPIES | PD_RETURNDC | (job.HasSelection() ? PD_SELECTION : PD_NOSELECTION);
    dialog.nMinPage = 1;
    dialog.nMaxPage = maxNativePage;
    dialog.nFromPage = 1;
    dialog.nToPage = 1;
    dialog.nCopies = 1;
    dialog.hDevMode = static_cast<HGLOBAL>(native.GetDevMode());
    dialog.hDevNames = static_cast<HGLOBAL>(native.GetDevNames());
    native.SetDevMode(nullptr);
    native.SetDevNames(nullptr);
    const bool accepted = PrintDlgW(&dialog);
    native.SetDevMode(dialog.hDevMode);
    native.SetDevNames(dialog.hDevNames);
    if (!accepted) return CommDlgExtendedError() == 0;
    if (!dialog.hDC) return false;
    wxPrinterDCFromHDC dc(dialog.hDC);
    if (!dc.IsOk()) return false;
    draft.ConvertFromNative();
    const auto measurement = [&] {
        const auto lock = [](HGLOBAL handle) {
            const auto unlock = [handle](void*) { GlobalUnlock(handle); };
            return std::unique_ptr<void, decltype(unlock)>(GlobalLock(handle), unlock);
        };
        const auto mode = lock(dialog.hDevMode);
        const auto names = lock(dialog.hDevNames);
        if (!mode || !names) return std::unique_ptr<TextMeasurement>{};
        const auto* device =
            static_cast<const wchar_t*>(names.get()) + static_cast<const DEVNAMES*>(names.get())->wDeviceOffset;
        return std::make_unique<TextMeasurement>(dc, device, *static_cast<const DEVMODEW*>(mode.get()));
    }();
    if (!measurement) return false;
    PrintSurface surface{dc, *measurement};
    driver = draft;
    const PageArea area{dc.GetPaperRect(), wxRect(wxPoint(), dc.GetSize())};
    if (!job.Prepare(surface, area) || !dc.StartDoc(job.Title())) return false;
    struct PrintDocument {
        HDC dc;
        ~PrintDocument() {
            if (dc) AbortDoc(dc);
        }
    } document{dialog.hDC};
    for (int index = 0; index < job.PageCount(); ++index) {
        const int page = index + 1;
        if ((dialog.Flags & PD_PAGENUMS) && (page < dialog.nFromPage || page > dialog.nToPage)) continue;
        dc.StartPage();
        const bool drawn = job.Draw(surface, area, index);
        dc.EndPage();
        if (!drawn) return false;
    }
    document.dc = nullptr;
    dc.EndDoc();
    return true;
}
#endif

#ifdef __WXGTK__
struct GObjectDeleter {
    void operator()(void* object) const noexcept { g_object_unref(object); }
};

template <typename Object> using GObjectPtr = std::unique_ptr<Object, GObjectDeleter>;

struct GtkPrintJob {
    PrintBuffer& buffer;
    wxPrintData data;
    bool failed = false;

    wxGtkPrintNativeData& NativeData() { return *static_cast<wxGtkPrintNativeData*>(data.GetNativeData()); }

    struct ContextBinding {
        explicit ContextBinding(wxGtkPrintNativeData& data, GtkPrintContext* context) : data_(data) {
            data_.SetPrintContext(context);
        }
        ~ContextBinding() { data_.SetPrintContext(nullptr); }
        wxGtkPrintNativeData& data_;
    };

    static PageArea Area(wxPrinterDC& dc, GtkPrintContext* context) {
        const wxRect paper(wxPoint(), dc.GetSize());
        const auto dpi = dc.GetPPI();
        double top = 0;
        double bottom = 0;
        double left = 0;
        double right = 0;
        if (!gtk_print_context_get_hard_margins(context, &top, &bottom, &left, &right)) return {paper, paper};
        constexpr double pointsPerInch = 72;
        const int x = static_cast<int>(std::ceil(left * dpi.x / pointsPerInch));
        const int y = static_cast<int>(std::ceil(top * dpi.y / pointsPerInch));
        return {paper, wxRect(x, y, paper.width - x - static_cast<int>(std::ceil(right * dpi.x / pointsPerInch)),
                              paper.height - y - static_cast<int>(std::ceil(bottom * dpi.y / pointsPerInch)))};
    }
};

void BeginGtkPrint(GtkPrintOperation* operation, GtkPrintContext* context, gpointer userData) {
    auto& job = *static_cast<GtkPrintJob*>(userData);
    try {
        const auto resolution = static_cast<int>(std::lround(gtk_print_context_get_dpi_y(context)));
        if (resolution > 0) job.data.SetQuality(resolution);
        GtkPrintJob::ContextBinding bind(job.NativeData(), context);
        wxPrinterDC dc(job.data);
        const TextMeasurement measurement(*context, dc.GetPPI().y);
        PrintSurface surface{dc, measurement};
        job.failed = !dc.IsOk() || !job.buffer.Prepare(surface, GtkPrintJob::Area(dc, context));
        if (!job.failed) {
            gtk_print_operation_set_n_pages(operation, job.buffer.PageCount());
            return;
        }
    } catch (const std::exception&) {
        job.failed = true;
    }
    gtk_print_operation_cancel(operation);
}

void DrawGtkPage(GtkPrintOperation* operation, GtkPrintContext* context, gint page, gpointer userData) {
    auto& job = *static_cast<GtkPrintJob*>(userData);
    try {
        GtkPrintJob::ContextBinding bind(job.NativeData(), context);
        wxPrinterDC dc(job.data);
        const TextMeasurement measurement(*context, dc.GetPPI().y);
        PrintSurface surface{dc, measurement};
        dc.StartPage();
        job.failed = !job.buffer.Draw(surface, GtkPrintJob::Area(dc, context), page);
        dc.EndPage();
        if (!job.failed) return;
    } catch (const std::exception&) {
        job.failed = true;
    }
    gtk_print_operation_cancel(operation);
}

bool NativePrint(wxWindow& parent, PrintBuffer& buffer, wxPrintData& driver) {
    GtkPrintJob job{buffer, CopyPrinterData(driver)};
    job.data.ConvertToNative();
    auto* settings = job.NativeData().GetPrintConfig();
    gtk_print_settings_set_print_pages(settings, GTK_PRINT_PAGES_ALL);
    const GObjectPtr<GtkPageSetup> paper(gtk_page_setup_new());
    if (auto* paperSize = gtk_print_settings_get_paper_size(settings)) {
        gtk_page_setup_set_paper_size_and_default_margins(paper.get(), paperSize);
        gtk_paper_size_free(paperSize);
    }
    gtk_page_setup_set_orientation(paper.get(), gtk_print_settings_get_orientation(settings));
    const GObjectPtr<GtkPrintOperation> operation(gtk_print_operation_new());
    gtk_print_operation_set_job_name(operation.get(), buffer.Title().utf8_str());
    gtk_print_operation_set_print_settings(operation.get(), settings);
    gtk_print_operation_set_default_page_setup(operation.get(), paper.get());
    gtk_print_operation_set_embed_page_setup(operation.get(), true);
    gtk_print_operation_set_unit(operation.get(), GTK_UNIT_POINTS);
    gtk_print_operation_set_use_full_page(operation.get(), true);
    gtk_print_operation_set_track_print_status(operation.get(), true);
    g_signal_connect(operation.get(), "begin-print", G_CALLBACK(BeginGtkPrint), &job);
    g_signal_connect(operation.get(), "draw-page", G_CALLBACK(DrawGtkPage), &job);
    GError* error = nullptr;
    const auto result = gtk_print_operation_run(operation.get(), GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG,
                                                GTK_WINDOW(gtk_widget_get_toplevel(parent.GetHandle())), &error);
    if (error) g_error_free(error);
    if (job.failed || result == GTK_PRINT_OPERATION_RESULT_ERROR) return false;
    if (result == GTK_PRINT_OPERATION_RESULT_APPLY) {
        auto* applied = gtk_print_operation_get_print_settings(operation.get());
        job.NativeData().SetPrintConfig(applied);
        job.data.ConvertFromNative();
        driver = job.data;
    }
    return true;
}
#endif
} // namespace

void Print(wxWindow& parent, EditorControl& editor, const wxString& title, PrintOptions& options, wxPrintData& driver) {
    try {
        PrintBuffer job(editor, title, options);
        if (NativePrint(parent, job, driver)) return;
    } catch (const std::exception&) {
        // Native printer creation and layout failures use the same UI as a job error.
    }
    // TRANSLATORS: IDS_PRINT_ERROR; %s is the current document's name or Untitled label.
    const auto message = wxString::Format(_("Could not print \"%s\"."), title);
    // TRANSLATORS: Title of the print failure message.
    wxMessageBox(message, _("Print"), wxOK | wxICON_ERROR, &parent);
}
} // namespace EditorPrinting
