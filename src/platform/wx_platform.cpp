// wx rendering and window adapter for the pinned Notepad4 Scintilla core.
// Basic LTR rendering is supported; complex layout and popup UI are not implemented.
#include "core_headers.h"
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/display.h>
#include <wx/listbox.h>
#include <chrono>
#include <cstdarg>
#include <cstdio>

#ifdef _WIN32
HANDLE g_hDefaultHeap=GetProcessHeap();
#endif
unsigned int dwUrlThreshold=0;
namespace Scintilla::Internal {
static wxWindow* Host(WindowID id) {return static_cast<wxWindow*>(id);}
static wxRect Rect(PRectangle r) {return wxRect(std::lround(r.left),std::lround(r.top),std::max(0L,std::lround(r.Width())),std::max(0L,std::lround(r.Height())));}
static PRectangle Area(wxRect r) {return PRectangle(r.x,r.y,r.x+r.width,r.y+r.height);}
static wxColour Color(ColourRGBA c) {return wxColour(c.GetRed(),c.GetGreen(),c.GetBlue(),c.GetAlpha());}
class WxFont final : public Font {
public:
    wxFont font;
    explicit WxFont(const FontParameters& p):font(wxFontInfo(p.size).FaceName(wxString::FromUTF8(p.faceName))
        .Weight(static_cast<wxFontWeight>(static_cast<int>(p.weight))).Italic(p.italic)) {}
};
std::shared_ptr<Font> Font::Allocate(const FontParameters& p) {return std::make_shared<WxFont>(p);}
class WxSurface final : public Surface {
    wxDC* dc_=nullptr;
    std::unique_ptr<wxDC> owned_;
    wxBitmap bitmap_;
    bool utf8_=true;
    void FontSet(const Font* f) {dc_->SetFont(static_cast<const WxFont*>(f)->font);}
    wxString String(std::string_view s) const {return utf8_?wxString::FromUTF8(s.data(),s.size()):wxString::From8BitData(s.data(),s.size());}
    void StrokeSet(Stroke s) {dc_->SetPen(wxPen(Color(s.colour),std::max(1,static_cast<int>(std::lround(s.width)))));}
public:
    void Init(WindowID id) noexcept override {owned_=std::make_unique<wxClientDC>(Host(id)); dc_=owned_.get();}
    void Init(SurfaceID dc,WindowID,bool) noexcept override {dc_=static_cast<wxDC*>(dc);}
    std::unique_ptr<Surface> AllocatePixMap(int width,int height) override {
        auto p=std::make_unique<WxSurface>(); p->bitmap_=wxBitmap(std::max(1,width),std::max(1,height));
#ifdef __WXMSW__
        // wxMemoryDC sizes point fonts using the selected bitmap's scale factor.
        p->bitmap_.SetScaleFactor(dc_->GetContentScaleFactor());
#endif
        auto dc=std::make_unique<wxMemoryDC>(); dc->SelectObject(p->bitmap_); p->dc_=dc.get(); p->owned_=std::move(dc); return p;
    }
    void SetMode(SurfaceMode m) noexcept override {utf8_=m.codePage==65001;}
    void SetRenderingParams(void*,void*) noexcept override {}
    void Release() noexcept override {owned_.reset(); dc_=nullptr;}
    bool SupportsFeature(Scintilla::Supports) const noexcept override {return false;}
    bool Initialised() const noexcept override {return dc_!=nullptr;}
    int LogPixelsY() const noexcept override {return dc_?dc_->GetPPI().y:96;}
    int PixelDivisions() const noexcept override {return 1;}
    int DeviceHeightFont(int points) const noexcept override {return points;}
    void SCICALL LineDraw(Point a,Point b,Stroke s) override {StrokeSet(s); dc_->DrawLine(a.x,a.y,b.x,b.y);}
    void SCICALL PolyLine(const Point* p,size_t n,Stroke s) override {
        StrokeSet(s); std::vector<wxPoint> points; points.reserve(n);
        for(size_t i=0;i<n;++i) points.emplace_back(p[i].x,p[i].y); dc_->DrawLines(n,points.data());
    }
    void SCICALL Polygon(const Point* p,size_t n,FillStroke s) override {
        StrokeSet(s.stroke); dc_->SetBrush(wxBrush(Color(s.fill.colour))); std::vector<wxPoint> points; points.reserve(n);
        for(size_t i=0;i<n;++i) points.emplace_back(p[i].x,p[i].y); dc_->DrawPolygon(n,points.data());
    }
    void SCICALL RectangleDraw(PRectangle r,FillStroke s) override {StrokeSet(s.stroke); dc_->SetBrush(wxBrush(Color(s.fill.colour))); dc_->DrawRectangle(Rect(r));}
    void SCICALL RectangleFrame(PRectangle r,Stroke s) override {StrokeSet(s); dc_->SetBrush(*wxTRANSPARENT_BRUSH); dc_->DrawRectangle(Rect(r));}
    void SCICALL FillRectangle(PRectangle r,Fill f) override {dc_->SetPen(*wxTRANSPARENT_PEN); dc_->SetBrush(wxBrush(Color(f.colour))); dc_->DrawRectangle(Rect(r));}
    void SCICALL FillRectangleAligned(PRectangle r,Fill f) override {FillRectangle(r,f);}
    void SCICALL FillRectangle(PRectangle r,Surface& pattern) override {Copy(r,Point(0,0),pattern);}
    void SCICALL RoundedRectangle(PRectangle r,FillStroke s) override {StrokeSet(s.stroke); dc_->SetBrush(wxBrush(Color(s.fill.colour))); dc_->DrawRoundedRectangle(Rect(r),3);}
    void SCICALL AlphaRectangle(PRectangle r,XYPOSITION radius,FillStroke s) override {StrokeSet(s.stroke); dc_->SetBrush(wxBrush(Color(s.fill.colour))); dc_->DrawRoundedRectangle(Rect(r),radius);}
    void SCICALL GradientRectangle(PRectangle r,const std::vector<ColourStop>& stops,GradientOptions o) override {
        if(!stops.empty()) dc_->GradientFillLinear(Rect(r),Color(stops.front().colour),Color(stops.back().colour),o==GradientOptions::leftToRight?wxEAST:wxSOUTH);
    }
    void SCICALL DrawRGBAImage(PRectangle r,int width,int height,const unsigned char* data) override {
        wxImage image(width,height); image.InitAlpha();
        const size_t pixels=static_cast<size_t>(width)*static_cast<size_t>(height);
        for(size_t i=0;i<pixels;++i) {std::memcpy(image.GetData()+3*i,data+4*i,3); image.GetAlpha()[i]=data[4*i+3];}
        dc_->DrawBitmap(wxBitmap(image),r.left,r.top,true);
    }
    void SCICALL Ellipse(PRectangle r,FillStroke s) override {StrokeSet(s.stroke); dc_->SetBrush(wxBrush(Color(s.fill.colour))); dc_->DrawEllipse(Rect(r));}
    void SCICALL Stadium(PRectangle r,FillStroke s,Ends) override {RoundedRectangle(r,s);}
    void SCICALL Copy(PRectangle r,Point from,Surface& source) override {auto& other=static_cast<WxSurface&>(source); dc_->Blit(r.left,r.top,r.Width(),r.Height(),other.dc_,from.x,from.y);}
    std::unique_ptr<IScreenLineLayout> Layout(const IScreenLine*) override {return nullptr;}
    FontMetrics Metrics(const Font* f) noexcept override {
        FontSet(f); int width,height,descent,leading; dc_->GetTextExtent("Hg",&width,&height,&descent,&leading);
        return {static_cast<double>(height-descent),static_cast<double>(descent),0,static_cast<double>(height)};
    }
    void SCICALL DrawTextTransparent(PRectangle r,const Font* f,XYPOSITION base,std::string_view text,ColourRGBA fore) override {
        FontSet(f); dc_->SetTextForeground(Color(fore)); dc_->SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
        dc_->DrawText(String(text),r.left,base-Metrics(f).ascent);
    }
    void SCICALL DrawTextNoClip(PRectangle r,const Font* f,XYPOSITION base,std::string_view text,ColourRGBA fore,ColourRGBA back) override {FillRectangle(r,Fill(back)); DrawTextTransparent(r,f,base,text,fore);}
    void SCICALL DrawTextClipped(PRectangle r,const Font* f,XYPOSITION base,std::string_view text,ColourRGBA fore,ColourRGBA back) override {
        wxDCClipper clip(*dc_,Rect(r)); DrawTextNoClip(r,f,base,text,fore,back);
    }
    void SCICALL MeasureWidths(const Font* f,std::string_view text,XYPOSITION* positions) override {
        FontSet(f); wxArrayInt extents; dc_->GetPartialTextExtents(String(text),extents);
        size_t character=0;
        for(size_t byte=0;byte<text.size();) {
            const unsigned char lead=text[byte]; size_t width=1;
            if(utf8_ && lead>=0xc0) width=lead<0xe0?2:lead<0xf0?3:4;
            width=std::min(width,text.size()-byte);
            const size_t units=utf8_ && width==4 && sizeof(wchar_t)==2?2:1;
            character+=units; const double x=character && character<=extents.size()?extents[character-1]:0;
            for(size_t j=0;j<width;++j) positions[byte+j]=x;
            byte+=width;
        }
    }
    XYPOSITION WidthText(const Font* f,std::string_view text) override {FontSet(f); return dc_->GetTextExtent(String(text)).x;}
    void SCICALL DrawTextNoClipUTF8(PRectangle r,const Font* f,XYPOSITION y,std::string_view t,ColourRGBA a,ColourRGBA b) override {utf8_=true; DrawTextNoClip(r,f,y,t,a,b);}
    void SCICALL DrawTextClippedUTF8(PRectangle r,const Font* f,XYPOSITION y,std::string_view t,ColourRGBA a,ColourRGBA b) override {utf8_=true; DrawTextClipped(r,f,y,t,a,b);}
    void SCICALL DrawTextTransparentUTF8(PRectangle r,const Font* f,XYPOSITION y,std::string_view t,ColourRGBA a) override {utf8_=true; DrawTextTransparent(r,f,y,t,a);}
    void SCICALL MeasureWidthsUTF8(const Font* f,std::string_view t,XYPOSITION* p) override {utf8_=true; MeasureWidths(f,t,p);}
    XYPOSITION WidthTextUTF8(const Font* f,std::string_view t) override {utf8_=true; return WidthText(f,t);}
    void SCICALL SetClip(PRectangle r) noexcept override {dc_->SetClippingRegion(Rect(r));}
    void PopClip() noexcept override {dc_->DestroyClippingRegion();}
    void FlushCachedState() noexcept override {}
    void FlushDrawing() noexcept override {}
};
std::unique_ptr<Surface> Surface::Allocate(Scintilla::Technology) {return std::make_unique<WxSurface>();}

void Window::Destroy() noexcept {if(wid) Host(wid)->Destroy(); wid=nullptr;}
PRectangle Window::GetPosition() const noexcept {return wid?Area(Host(wid)->GetRect()):PRectangle();}
void SCICALL Window::SetPosition(PRectangle r) noexcept {if(wid) Host(wid)->SetSize(Rect(r));}
void SCICALL Window::SetPositionRelative(PRectangle r,const Window* relative) noexcept {
    if(wid) {auto p=Host(relative->wid)->ClientToScreen(wxPoint(r.left,r.top)); Host(wid)->SetSize(p.x,p.y,r.Width(),r.Height());}
}
PRectangle Window::GetClientPosition() const noexcept {return wid?Area(Host(wid)->GetClientRect()):PRectangle();}
void Window::Show(bool show) const noexcept {if(wid) Host(wid)->Show(show);}
void Window::InvalidateAll() noexcept {if(wid) Host(wid)->Refresh(false);}
void SCICALL Window::InvalidateRectangle(PRectangle r) noexcept {if(wid) Host(wid)->RefreshRect(Rect(r),false);}
void Window::SetCursor(Cursor c) const noexcept {
    if(wid) Host(wid)->SetCursor(wxCursor(c==Cursor::text?wxCURSOR_IBEAM:c==Cursor::hand?wxCURSOR_HAND:c==Cursor::wait?wxCURSOR_WAIT:wxCURSOR_ARROW));
}
PRectangle SCICALL Window::GetMonitorRect(Point point) const noexcept {
    int index=wxDisplay::GetFromPoint(wxPoint(point.x,point.y)); if(index==wxNOT_FOUND) index=0;
    return Area(wxDisplay(index).GetClientArea());
}
class WxListBox final : public ListBox {
    wxListBox* list_=nullptr; int rows_=8; IListBoxDelegate* delegate_=nullptr;
public:
    void SetFont(std::shared_ptr<Font> f) noexcept override {if(list_) list_->SetFont(static_cast<WxFont*>(f.get())->font);}
    void SCICALL Create(Window& p,int id,Point at,int,int,Scintilla::Technology) noexcept override {
        list_=new wxListBox(Host(p.GetID()),id,wxPoint(at.x,at.y)); wid=list_;
        list_->Bind(wxEVT_LISTBOX,[this](wxCommandEvent&){if(delegate_) {ListBoxEvent e(ListBoxEvent::EventType::selectionChange); delegate_->ListNotify(&e);}});
    }
    void SetAverageCharWidth(int,double) noexcept override {}
    void SetVisibleRows(int rows) noexcept override {rows_=rows;}
    int GetVisibleRows() const noexcept override {return rows_;}
    PRectangle GetDesiredRect() override {return PRectangle(0,0,240,rows_*22);}
    int CaretFromEdge() const noexcept override {return 0;}
    void Clear() noexcept override {if(list_) list_->Clear();}
    int Length() const noexcept override {return list_?list_->GetCount():0;}
    void Select(int n) override {if(list_) list_->SetSelection(n);}
    int GetSelection() const noexcept override {return list_?list_->GetSelection():-1;}
    std::string GetValue(int n) const override {return list_?list_->GetString(n).ToStdString(wxConvUTF8):"";}
    void RegisterImage(int,const char*) override {}
    void RegisterRGBAImage(int,int,int,const unsigned char*) override {}
    void ClearRegisteredImages() noexcept override {}
    void SetDelegate(IListBoxDelegate* d) noexcept override {delegate_=d;}
    void SetList(const char* value,char separator,char) override {if(list_) {list_->Clear(); for(const auto& s:wxSplit(wxString::FromUTF8(value),separator)) list_->Append(s);}}
    void SCICALL SetOptions(const ListOptions&) noexcept override {}
};
std::unique_ptr<ListBox> ListBox::Allocate() {return std::make_unique<WxListBox>();}
// Same batch bookkeeping as ScintillaWin, with wx redraw suspension.
void Editor::BeginBatchUpdate() noexcept {
    if(++batchUpdateDepth==1) {
        batchUpdateState.modEventMask=modEventMask;modEventMask=Scintilla::ModificationFlags::None;
        batchUpdateState.actions=pdoc->UndoActions();batchUpdateState.lines=pdoc->LinesTotal();Host(wMain.GetID())->Freeze();
    }
}
void Editor::EndBatchUpdate() noexcept {
    if(--batchUpdateDepth==0) {
        modEventMask=batchUpdateState.modEventMask;Host(wMain.GetID())->Thaw();wMain.InvalidateAll();
        if(batchUpdateState.actions!=pdoc->UndoActions()) {
            Scintilla::NotificationData notice{};notice.nmhdr.code=Scintilla::Notification::Modified;
            notice.linesAdded=pdoc->LinesTotal()-batchUpdateState.lines;NotifyParent(notice);
        }
    }
}
void Menu::CreatePopUp() noexcept {}
void Menu::Destroy() noexcept {}
void SCICALL Menu::Show(Point,const Window&) noexcept {}
int64_t QueryPerformanceFrequency() noexcept {return 1000000000;}
int64_t QueryPerformanceCounter() noexcept {return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
namespace Platform {
ColourRGBA Chrome() noexcept {return ColourRGBA(240,240,240);}
ColourRGBA ChromeHighlight() noexcept {return ColourRGBA(255,255,255);}
const char* DefaultFont() noexcept {
#ifdef _WIN32
    return "Consolas";
#else
    return "monospace";
#endif
}
int DefaultFontSize() noexcept {return 12;}
unsigned int DoubleClickTime() noexcept {return 500;}
void DebugDisplay(const char* s) noexcept {std::fputs(s,stderr);}
void DebugPrintf(const char* fmt,...) noexcept {va_list args; va_start(args,fmt); std::vfprintf(stderr,fmt,args); va_end(args);}
bool ShowAssertionPopUps(bool value) noexcept {return value;}
void Assert(const char* c,const char* file,int line) noexcept {std::fprintf(stderr,"%s:%d: %s\n",file,line,c); std::abort();}
}
}
