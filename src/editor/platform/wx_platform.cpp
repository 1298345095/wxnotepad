// wx rendering and window adapter for the pinned Notepad4 Scintilla core.
// Basic LTR rendering and native nonactivating completion UI.
#include "core_headers.h"
#include "editor/display_units.h"
#include "text_measurement.h"
#include "diagnostics/trace.h"
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#ifdef __WXMSW__
#include <wx/graphics.h>
#endif
#ifdef WXNOTEPAD_PROTOTYPE_DIRECTWRITE
#include "prototype_directwrite/directwrite_surface.h"
#endif
#include <wx/display.h>
#include <wx/popupwin.h>
#include <wx/vlbox.h>
#include <wx/sizer.h>
#include <wx/renderer.h>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
HANDLE g_hDefaultHeap = GetProcessHeap();
#endif
unsigned int dwUrlThreshold = 0;
namespace Scintilla::Internal {
static wxWindow* Host(WindowID id) {
    return static_cast<wxWindow*>(id);
}
static wxRect Rect(PRectangle r) {
    return wxRect(std::lround(r.left), std::lround(r.top), std::max(0L, std::lround(r.Width())),
                  std::max(0L, std::lround(r.Height())));
}
static PRectangle Area(wxRect r) {
    return PRectangle(r.x, r.y, r.x + r.width, r.y + r.height);
}
static wxColour Color(ColourRGBA c) {
    return wxColour(c.GetRed(), c.GetGreen(), c.GetBlue(), c.GetAlpha());
}
#ifdef WXNOTEPAD_PROTOTYPE_DIRECTWRITE
std::shared_ptr<Font> Font::Allocate(const FontParameters& p) {
    return AllocateDirectWriteFont(p);
}
std::unique_ptr<Surface> Surface::Allocate(Scintilla::Technology) {
    return AllocateDirectWriteSurface();
}
#else
class WxFont final : public Font {
public:
    wxFont font;
    MeasurementFont measurement;
    explicit WxFont(const FontParameters& p)
        : font(wxFontInfo(p.size)
                   .FaceName(wxString::FromUTF8(p.faceName))
                   .Weight(static_cast<wxFontWeight>(static_cast<int>(p.weight)))
                   .Italic(p.italic)),
          measurement(font) {}
};
std::shared_ptr<Font> Font::Allocate(const FontParameters& p) {
    return std::make_shared<WxFont>(p);
}
class WxSurface final : public Surface {
    wxDC* dc_ = nullptr;
    std::unique_ptr<wxDC> owned_;
    wxBitmap bitmap_;
    std::vector<std::unique_ptr<wxDCClipper>> clips_;
    std::unique_ptr<TextMeasurement> measurement_;
    bool utf8_ = true;
    void FontSet(const Font* f) { dc_->SetFont(static_cast<const WxFont*>(f)->font); }
    wxString String(std::string_view s) const {
        return utf8_ ? wxString::FromUTF8(s.data(), s.size()) : wxString::From8BitData(s.data(), s.size());
    }
    void StrokeSet(Stroke s) {
        dc_->SetPen(wxPen(Color(s.colour), std::max(1, static_cast<int>(std::lround(s.width)))));
    }

public:
    void Init(WindowID id) noexcept override {
        owned_ = std::make_unique<wxClientDC>(Host(id));
        dc_ = owned_.get();
        measurement_ = std::make_unique<TextMeasurement>(*dc_);
    }
    void Init(SurfaceID dc, WindowID, bool) noexcept override {
        dc_ = static_cast<wxDC*>(dc);
        measurement_ = std::make_unique<TextMeasurement>(*dc_);
    }
    std::unique_ptr<Surface> AllocatePixMap(int width, int height) override {
        auto p = std::make_unique<WxSurface>();
        // GTK logical coordinates need a full-resolution backing bitmap; MSW
        // keeps the physical dimensions and uses the scale for point fonts.
        p->bitmap_.CreateWithLogicalSize(wxSize(std::max(1, width), std::max(1, height)), dc_->GetContentScaleFactor());
        auto dc = std::make_unique<wxMemoryDC>();
        dc->SelectObject(p->bitmap_);
        p->dc_ = dc.get();
        p->owned_ = std::move(dc);
        p->measurement_ = std::make_unique<TextMeasurement>(*p->dc_);
        return p;
    }
    void SetMode(SurfaceMode m) noexcept override { utf8_ = m.codePage == Scintilla::CpUtf8; }
    void SetRenderingParams(void*, void*) noexcept override {
        // wxDC owns native rendering parameters; DirectWrite overrides do not apply.
    }
    void Release() noexcept override {
        clips_.clear();
        measurement_.reset();
        owned_.reset();
        dc_ = nullptr;
    }
    bool SupportsFeature(Scintilla::Supports) const noexcept override { return false; }
    bool Initialised() const noexcept override { return dc_ != nullptr; }
    int LogPixelsY() const noexcept override { return dc_ ? dc_->GetPPI().y : DisplayUnits::DefaultDpi; }
    int PixelDivisions() const noexcept override { return 1; }
    int DeviceHeightFont(int points) const noexcept override { return points; }
    void SCICALL LineDraw(Point a, Point b, Stroke s) override {
        StrokeSet(s);
        dc_->DrawLine(a.x, a.y, b.x, b.y);
    }
    void SCICALL PolyLine(const Point* p, size_t n, Stroke s) override {
        StrokeSet(s);
        std::vector<wxPoint> points;
        points.reserve(n);
        for (size_t i = 0; i < n; ++i)
            points.emplace_back(p[i].x, p[i].y);
        dc_->DrawLines(n, points.data());
    }
    void SCICALL Polygon(const Point* p, size_t n, FillStroke s) override {
        StrokeSet(s.stroke);
        dc_->SetBrush(wxBrush(Color(s.fill.colour)));
        std::vector<wxPoint> points;
        points.reserve(n);
        for (size_t i = 0; i < n; ++i)
            points.emplace_back(p[i].x, p[i].y);
        dc_->DrawPolygon(n, points.data());
    }
    void SCICALL RectangleDraw(PRectangle r, FillStroke s) override {
        StrokeSet(s.stroke);
        dc_->SetBrush(wxBrush(Color(s.fill.colour)));
        dc_->DrawRectangle(Rect(r));
    }
    void SCICALL RectangleFrame(PRectangle r, Stroke s) override {
        StrokeSet(s);
        dc_->SetBrush(*wxTRANSPARENT_BRUSH);
        dc_->DrawRectangle(Rect(r));
    }
    void SCICALL FillRectangle(PRectangle r, Fill f) override {
#ifdef __WXMSW__
        // Native wxDC brushes ignore alpha on Windows. Keep native text and
        // opaque drawing, using a graphics context only for translucent fills.
        if (f.colour.GetAlpha() < 255) {
            std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::CreateFromUnknownDC(*dc_));
            if (graphics) {
                graphics->SetAntialiasMode(wxANTIALIAS_NONE);
                graphics->SetPen(*wxTRANSPARENT_PEN);
                graphics->SetBrush(wxBrush(Color(f.colour)));
                const auto rectangle = Rect(r);
                graphics->DrawRectangle(rectangle.x, rectangle.y, rectangle.width, rectangle.height);
                return;
            }
        }
#endif
        dc_->SetPen(*wxTRANSPARENT_PEN);
        dc_->SetBrush(wxBrush(Color(f.colour)));
        dc_->DrawRectangle(Rect(r));
    }
    void SCICALL FillRectangleAligned(PRectangle r, Fill f) override { FillRectangle(r, f); }
    void SCICALL FillRectangle(PRectangle r, Surface& pattern) override {
        const auto& other = static_cast<WxSurface&>(pattern);
        const auto tile = other.dc_->GetSize();
        const auto destination = Rect(r);
        for (int y = destination.y; y < destination.GetBottom() + 1;) {
            const int sourceY = (y % tile.y + tile.y) % tile.y;
            const int height = std::min(tile.y - sourceY, destination.GetBottom() + 1 - y);
            for (int x = destination.x; x < destination.GetRight() + 1;) {
                const int sourceX = (x % tile.x + tile.x) % tile.x;
                const int width = std::min(tile.x - sourceX, destination.GetRight() + 1 - x);
                dc_->Blit(x, y, width, height, other.dc_, sourceX, sourceY);
                x += width;
            }
            y += height;
        }
    }
    void SCICALL RoundedRectangle(PRectangle r, FillStroke s) override {
        StrokeSet(s.stroke);
        dc_->SetBrush(wxBrush(Color(s.fill.colour)));
        dc_->DrawRoundedRectangle(Rect(r), 3);
    }
    void SCICALL AlphaRectangle(PRectangle r, XYPOSITION radius, FillStroke s) override {
#ifdef __WXMSW__
        // Windows wxDC ignores pen/brush alpha. Brace and occurrence round-box
        // indicators need the same native graphics alpha path as translucent fills.
        std::unique_ptr<wxGraphicsContext> graphics(wxGraphicsContext::CreateFromUnknownDC(*dc_));
        if (graphics) {
            graphics->SetAntialiasMode(wxANTIALIAS_NONE);
            const int width = std::max(1, static_cast<int>(std::lround(s.stroke.width)));
            graphics->SetPen(wxPen(Color(s.stroke.colour), width));
            graphics->SetBrush(wxBrush(Color(s.fill.colour)));
            const auto rectangle = Rect(r);
            const double inset = width / 2.0;
            graphics->DrawRoundedRectangle(rectangle.x + inset, rectangle.y + inset,
                                           std::max(0, rectangle.width - width), std::max(0, rectangle.height - width),
                                           radius);
            return;
        }
#endif
        StrokeSet(s.stroke);
        dc_->SetBrush(wxBrush(Color(s.fill.colour)));
        dc_->DrawRoundedRectangle(Rect(r), radius);
    }
    void SCICALL GradientRectangle(PRectangle r, const std::vector<ColourStop>& stops, GradientOptions o) override {
        if (!stops.empty())
            dc_->GradientFillLinear(Rect(r), Color(stops.front().colour), Color(stops.back().colour),
                                    o == GradientOptions::leftToRight ? wxEAST : wxSOUTH);
    }
    void SCICALL DrawRGBAImage(PRectangle r, int width, int height, const unsigned char* data) override {
        wxImage image(width, height);
        image.InitAlpha();
        const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
        for (size_t i = 0; i < pixels; ++i) {
            std::memcpy(image.GetData() + 3 * i, data + 4 * i, 3);
            image.GetAlpha()[i] = data[4 * i + 3];
        }
        wxBitmap bitmap(image);
        wxMemoryDC source(bitmap);
        const auto destination = Rect(r);
        // The core supplies logical marker dimensions separately from the
        // source pixels. Let the native DC scale and composite the full image.
        dc_->StretchBlit(destination.GetPosition(), destination.GetSize(), &source, wxPoint(0, 0),
                         wxSize(width, height), wxCOPY, true);
    }
    void SCICALL Ellipse(PRectangle r, FillStroke s) override {
        StrokeSet(s.stroke);
        dc_->SetBrush(wxBrush(Color(s.fill.colour)));
        dc_->DrawEllipse(Rect(r));
    }
    void SCICALL Stadium(PRectangle r, FillStroke s, Ends) override { RoundedRectangle(r, s); }
    void SCICALL Copy(PRectangle r, Point from, Surface& source) override {
        auto& other = static_cast<WxSurface&>(source);
        dc_->Blit(r.left, r.top, r.Width(), r.Height(), other.dc_, from.x, from.y);
    }
    std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine*) override { return nullptr; }
    FontMetrics Metrics(const Font* f) noexcept override {
        FontSet(f);
        int width, height, descent, leading;
        dc_->GetTextExtent("Hg", &width, &height, &descent, &leading);
        return {static_cast<double>(height - descent), static_cast<double>(descent), 0, static_cast<double>(height)};
    }
    void SCICALL DrawTextTransparent(PRectangle r, const Font* f, XYPOSITION base, std::string_view text,
                                     ColourRGBA fore) override {
        FontSet(f);
        dc_->SetTextForeground(Color(fore));
        dc_->SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
        dc_->DrawText(String(text), r.left, base - Metrics(f).ascent);
    }
    void SCICALL DrawTextNoClip(PRectangle r, const Font* f, XYPOSITION base, std::string_view text, ColourRGBA fore,
                                ColourRGBA back) override {
        FillRectangle(r, Fill(back));
        DrawTextTransparent(r, f, base, text, fore);
    }
    void SCICALL DrawTextClipped(PRectangle r, const Font* f, XYPOSITION base, std::string_view text, ColourRGBA fore,
                                 ColourRGBA back) override {
        wxDCClipper clip(*dc_, Rect(r));
        DrawTextNoClip(r, f, base, text, fore, back);
    }
    void SCICALL MeasureWidths(const Font* f, std::string_view text, XYPOSITION* positions) override {
        measurement_->MeasureWidths(static_cast<const WxFont*>(f)->measurement, text, utf8_, positions);
        WXNOTE_TRACE("layout.measurement", "\tsurface=%p\tbytes=%zu", static_cast<void*>(this), text.size());
    }
    XYPOSITION WidthText(const Font* f, std::string_view text) override {
        return measurement_->WidthText(static_cast<const WxFont*>(f)->measurement, text, utf8_);
    }
    void SCICALL DrawTextNoClipUTF8(PRectangle r, const Font* f, XYPOSITION y, std::string_view t, ColourRGBA a,
                                    ColourRGBA b) override {
        utf8_ = true;
        DrawTextNoClip(r, f, y, t, a, b);
    }
    void SCICALL DrawTextClippedUTF8(PRectangle r, const Font* f, XYPOSITION y, std::string_view t, ColourRGBA a,
                                     ColourRGBA b) override {
        utf8_ = true;
        DrawTextClipped(r, f, y, t, a, b);
    }
    void SCICALL DrawTextTransparentUTF8(PRectangle r, const Font* f, XYPOSITION y, std::string_view t,
                                         ColourRGBA a) override {
        utf8_ = true;
        DrawTextTransparent(r, f, y, t, a);
    }
    void SCICALL MeasureWidthsUTF8(const Font* f, std::string_view t, XYPOSITION* p) override {
        measurement_->MeasureWidths(static_cast<const WxFont*>(f)->measurement, t, true, p);
        WXNOTE_TRACE("layout.measurement", "\tsurface=%p\tbytes=%zu", static_cast<void*>(this), t.size());
    }
    XYPOSITION WidthTextUTF8(const Font* f, std::string_view t) override {
        return measurement_->WidthText(static_cast<const WxFont*>(f)->measurement, t, true);
    }
    void SCICALL SetClip(PRectangle r) noexcept override {
        clips_.push_back(std::make_unique<wxDCClipper>(*dc_, Rect(r)));
    }
    void PopClip() noexcept override { clips_.pop_back(); }
    void FlushCachedState() noexcept override {
        // All drawing state is applied directly to the wxDC.
    }
    void FlushDrawing() noexcept override {
        // wxDC drawing is synchronous and has no adapter-owned command buffer.
    }
};
std::unique_ptr<Surface> Surface::Allocate(Scintilla::Technology) {
    return std::make_unique<WxSurface>();
}
#endif

void Window::Destroy() noexcept {
    if (wid) {
        Host(wid)->Hide();
        Host(wid)->Destroy();
    }
    wid = nullptr;
}
PRectangle Window::GetPosition() const noexcept {
    return wid ? Area(Host(wid)->GetRect()) : PRectangle();
}
void SCICALL Window::SetPosition(PRectangle r) noexcept {
    if (wid) Host(wid)->SetSize(Rect(r));
}
void SCICALL Window::SetPositionRelative(PRectangle r, const Window* relative) noexcept {
    if (wid) {
        auto point = Host(relative->wid)->ClientToScreen(wxPoint(r.left, r.top));
        int display = wxDisplay::GetFromPoint(point);
        if (display == wxNOT_FOUND) display = wxDisplay::GetFromWindow(Host(relative->wid));
        if (display == wxNOT_FOUND) display = 0;
        const auto bounds = wxDisplay(display).GetClientArea();
        const int width = std::min(static_cast<int>(r.Width()), bounds.width);
        const int height = std::min(static_cast<int>(r.Height()), bounds.height);
        point.x = std::clamp(point.x, bounds.x, bounds.GetRight() + 1 - width);
        point.y = std::clamp(point.y, bounds.y, bounds.GetBottom() + 1 - height);
        Host(wid)->SetSize(point.x, point.y, width, height);
    }
}
PRectangle Window::GetClientPosition() const noexcept {
    return wid ? Area(Host(wid)->GetClientRect()) : PRectangle();
}
void Window::Show(bool show) const noexcept {
    if (wid) Host(wid)->Show(show);
}
void Window::InvalidateAll() noexcept {
    if (wid) Host(wid)->Refresh(false);
}
void SCICALL Window::InvalidateRectangle(PRectangle r) noexcept {
    if (wid) Host(wid)->RefreshRect(Rect(r), false);
}
void Window::SetCursor(Cursor c) const noexcept {
    if (wid)
        Host(wid)->SetCursor(wxCursor(c == Cursor::text   ? wxCURSOR_IBEAM
                                      : c == Cursor::hand ? wxCURSOR_HAND
                                      : c == Cursor::wait ? wxCURSOR_WAIT
                                                          : wxCURSOR_ARROW));
}
PRectangle SCICALL Window::GetMonitorRect(Point point) const noexcept {
    const auto screen = Host(wid)->ClientToScreen(wxPoint(point.x, point.y));
    int index = wxDisplay::GetFromPoint(screen);
    if (index == wxNOT_FOUND) index = 0;
    auto bounds = wxDisplay(index).GetClientArea();
    bounds.SetPosition(Host(wid)->ScreenToClient(bounds.GetPosition()));
    return Area(bounds);
}
// Adapted from wxWidgets 3.3.3 src/stc/PlatWX.cpp: wxSTCPopupBase,
// wxSTCListBox. The original core retains all keyboard and completion state.
class CompletionPopup final : public wxPopupWindow {
public:
    explicit CompletionPopup(wxWindow* parent) : wxPopupWindow(parent, wxPU_CONTAINS_CONTROLS | wxBORDER_NONE) {
        SetName("Word completion");
    }
    bool AcceptsFocus() const override { return false; }
#ifdef __WXMSW__
    bool Show(bool show) override {
        if (!show) return wxPopupWindow::Show(false);
        const bool changed = wxWindowBase::Show(true);
        if (changed) {
            ::ShowWindow(static_cast<HWND>(GetHandle()), SW_SHOWNA);
            ::SetWindowPos(static_cast<HWND>(GetHandle()), HWND_NOTOPMOST, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return changed;
    }
    bool MSWHandleMessage(WXLRESULT* result, WXUINT message, WXWPARAM first, WXLPARAM second) override {
        if (message == WM_MOUSEACTIVATE) {
            *result = MA_NOACTIVATE;
            return true;
        }
        return wxPopupWindow::MSWHandleMessage(result, message, first, second);
    }
#endif
};
class CompletionList final : public wxVListBox {
public:
    std::vector<wxString> labels;
    IListBoxDelegate* delegate = nullptr;
    int itemHeight = 1;
    explicit CompletionList(wxWindow* parent, int id)
        : wxVListBox(parent, id, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE) {
        SetName("Completion candidates");
        Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { Notify(ListBoxEvent::EventType::selectionChange); });
        Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { Notify(ListBoxEvent::EventType::doubleClick); });
        Bind(wxEVT_DPI_CHANGED, [this](wxDPIChangedEvent& event) {
            Measure();
            event.Skip();
        });
    }
    bool AcceptsFocus() const override { return false; }
    void SetFocus() override {
        // Completion keeps keyboard input and IME composition in the editor.
    }
    void Notify(ListBoxEvent::EventType type) {
        if (delegate) {
            ListBoxEvent event(type);
            delegate->ListNotify(&event);
        }
    }
    void Measure() {
        itemHeight = GetCharHeight() + 2 * FromDIP(2);
        RefreshAll();
    }

private:
    wxCoord OnMeasureItem(size_t) const override { return itemHeight; }
    void OnDrawItem(wxDC& dc, const wxRect& rect, size_t item) const override {
        dc.SetFont(GetFont());
        dc.SetTextForeground(
            wxSystemSettings::GetColour(IsSelected(item) ? wxSYS_COLOUR_HIGHLIGHTTEXT : wxSYS_COLOUR_LISTBOXTEXT));
        dc.DrawText(labels[item], rect.x + FromDIP(2), rect.y + (rect.height - GetCharHeight()) / 2);
    }
};
class WxListBox final : public ListBox {
    CompletionList* list_ = nullptr;
    int rows_ = 8;

public:
    void SetFont(std::shared_ptr<Font> font) noexcept override {
#ifdef WXNOTEPAD_PROTOTYPE_DIRECTWRITE
        list_->SetFont(DirectWriteListFont(*font));
#else
        list_->SetFont(static_cast<WxFont*>(font.get())->font);
#endif
        list_->Measure();
    }
    void SCICALL Create(Window& parent, int id, Point, int, int, Scintilla::Technology) noexcept override {
        auto* popup = new CompletionPopup(Host(parent.GetID()));
        list_ = new CompletionList(popup, id);
        popup->Bind(wxEVT_MOUSEWHEEL, [list = list_](wxMouseEvent& event) {
            wxMouseEvent routed(event);
            routed.SetEventObject(list);
            list->GetEventHandler()->ProcessEvent(routed);
        });
        auto* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(list_, 1, wxEXPAND | wxALL, popup->FromDIP(1));
        popup->SetSizer(sizer);
        wid = popup;
    }
    void SetAverageCharWidth(int, double) noexcept override {
        // GetDesiredRect measures the actual labels using the native list font.
    }
    void SetVisibleRows(int rows) noexcept override { rows_ = rows; }
    int GetVisibleRows() const noexcept override { return rows_; }
    PRectangle GetDesiredRect() override {
        int width = list_->GetCharWidth() * 3;
        for (const auto& label : list_->labels)
            width = std::max(width, list_->GetTextExtent(label).x);
        width += list_->GetCharWidth() * 3 + CaretFromEdge() * 2;
        if (Length() > rows_) width += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, list_);
        return PRectangle(0, 0, width, std::min(Length(), rows_) * list_->itemHeight + 2 * list_->FromDIP(1));
    }
    int CaretFromEdge() const noexcept override { return list_->FromDIP(3); }
    void Clear() noexcept override {
        list_->labels.clear();
        list_->SetItemCount(0);
    }
    int Length() const noexcept override { return static_cast<int>(list_->labels.size()); }
    void Select(int item) override {
        list_->SetSelection(item);
        list_->Notify(ListBoxEvent::EventType::selectionChange);
    }
    int GetSelection() const noexcept override { return list_->GetSelection(); }
    std::string GetValue(int item) const override { return list_->labels[item].ToStdString(wxConvUTF8); }
    void RegisterImage(int, const char*) override {
        // This text-only candidate list does not retain registered images.
    }
    void RegisterRGBAImage(int, int, int, const unsigned char*) override {
        // This text-only candidate list does not retain registered images.
    }
    void ClearRegisteredImages() noexcept override {
        // There are no retained candidate images to release.
    }
    void SetDelegate(IListBoxDelegate* delegate) noexcept override { list_->delegate = delegate; }
    void SetList(const char* value, char separator, char typeSeparator) override {
        Clear();
        for (auto label : wxSplit(wxString::FromUTF8(value), separator)) {
            if (const auto type = label.find(typeSeparator); type != wxString::npos) label.Truncate(type);
            list_->labels.push_back(label);
        }
        list_->SetItemCount(list_->labels.size());
        list_->Measure();
    }
    void SCICALL SetOptions(const ListOptions&) noexcept override {
        // The native text-only list uses system selection colors and its own metrics.
    }
};
std::unique_ptr<ListBox> ListBox::Allocate() {
    return std::make_unique<WxListBox>();
}
// Same batch bookkeeping as ScintillaWin, with wx redraw suspension.
void Editor::BeginBatchUpdate() noexcept {
    if (++batchUpdateDepth == 1) {
        batchUpdateState.modEventMask = modEventMask;
        modEventMask = Scintilla::ModificationFlags::None;
        batchUpdateState.actions = pdoc->UndoActions();
        batchUpdateState.lines = pdoc->LinesTotal();
        Host(wMain.GetID())->Freeze();
    }
}
void Editor::EndBatchUpdate() noexcept {
    if (--batchUpdateDepth == 0) {
        modEventMask = batchUpdateState.modEventMask;
        Host(wMain.GetID())->Thaw();
        wMain.InvalidateAll();
        if (batchUpdateState.actions != pdoc->UndoActions()) {
            Scintilla::NotificationData notice{};
            notice.nmhdr.code = Scintilla::Notification::Modified;
            notice.linesAdded = pdoc->LinesTotal() - batchUpdateState.lines;
            NotifyParent(notice);
        }
    }
}
void Menu::CreatePopUp() noexcept {
    // Original-core popup menus are outside the currently supported host UI.
}
void Menu::Destroy() noexcept {
    // This original-core menu adapter owns no host menu resources.
}
void SCICALL Menu::Show(Point, const Window&) noexcept {
    // Original-core popup menus are outside the currently supported host UI.
}
int64_t QueryPerformanceFrequency() noexcept {
    return std::nano::den;
}
int64_t QueryPerformanceCounter() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
namespace Platform {
ColourRGBA Chrome() noexcept {
    return ColourRGBA(240, 240, 240);
}
ColourRGBA ChromeHighlight() noexcept {
    return ColourRGBA(255, 255, 255);
}
const char* DefaultFont() noexcept {
#ifdef _WIN32
    return "Consolas";
#else
    return "monospace";
#endif
}
int DefaultFontSize() noexcept {
    return 12;
}
unsigned int DoubleClickTime() noexcept {
    return 500;
}
void DebugDisplay(const char* s) noexcept {
    std::fputs(s, stderr);
}
void DebugPrintf(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
}
bool ShowAssertionPopUps(bool value) noexcept {
    return value;
}
void Assert(const char* c, const char* file, int line) noexcept {
    std::fprintf(stderr, "%s:%d: %s\n", file, line, c);
    std::abort();
}
} // namespace Platform
} // namespace Scintilla::Internal
