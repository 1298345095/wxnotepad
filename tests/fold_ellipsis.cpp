#include "native_key_helpers.h"
#include "native_fold_helpers.h"
#include "native_selection_helpers.h"
#include "file_helpers.h"
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/image.h>
#include <wx/dcmemory.h>
#include <wx/menu.h>
#include <algorithm>
#include <array>

namespace {
using enum Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void View(EditorFrame* frame, const char* command) {
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", command));
}
void Scheme(EditorFrame* frame, const char* language) {
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", language));
    NativeFold::Paint(frame->Editor());
}
std::vector<wxColour> Pixels(EditorControl* editor, const wxRect& area) {
#ifdef __WXMSW__
    // Read the same actual owned-window pixels in one native transfer. Calling
    // GetPixel for every sample is too slow on the shared high-DPI desktop.
    NativeFold::Paint(editor);
    wxClientDC source(editor);
    wxBitmap bitmap(area.width, area.height, 32);
    wxMemoryDC target(bitmap);
    Require(target.Blit(0, 0, area.width, area.height, &source, area.x, area.y), "read actual native header pixels");
    target.SelectObject(wxNullBitmap);
    const auto image = bitmap.ConvertToImage();
    std::vector<wxColour> pixels;
    pixels.reserve(static_cast<size_t>(area.width) * area.height);
    for (int y = 0; y < area.height; ++y)
        for (int x = 0; x < area.width; ++x)
            pixels.emplace_back(image.GetRed(x, y), image.GetGreen(x, y), image.GetBlue(x, y));
    return pixels;
#else
    class Capture final {
    public:
        Capture(EditorControl* editor, const wxRect& area) : widget_(editor->GetConnectWidget()), area_(area) {
            handler_ = g_signal_connect_after(widget_, "draw", G_CALLBACK(Draw), this);
        }
        ~Capture() { g_signal_handler_disconnect(widget_, handler_); }
        std::vector<wxColour> pixels;
        bool pending = true;

    private:
        static gboolean Draw(GtkWidget*, cairo_t* context, gpointer data) {
            static_cast<Capture*>(data)->Sample(context);
            return FALSE;
        }
        void Sample(cairo_t* context) {
            // GTK's native after-draw signal runs after the production wx paint
            // handler. A pushed wx event handler can run before that handler,
            // observing a fresh blank high-DPI Wayland buffer instead.
            auto* surface = cairo_get_group_target(context);
            cairo_surface_flush(surface);
            double scaleX, scaleY, offsetX, offsetY;
            cairo_surface_get_device_scale(surface, &scaleX, &scaleY);
            cairo_surface_get_device_offset(surface, &offsetX, &offsetY);
            auto* image = cairo_surface_map_to_image(surface, nullptr);
            Require(cairo_surface_status(image) == CAIRO_STATUS_SUCCESS, "map the actual native paint surface");
            const int stride = cairo_image_surface_get_stride(image);
            const auto* data = cairo_image_surface_get_data(image);
            pixels.clear();
            for (int y = area_.y; y <= area_.GetBottom(); ++y) {
                for (int x = area_.x; x <= area_.GetRight(); ++x) {
                    double deviceX = x, deviceY = y;
                    cairo_user_to_device(context, &deviceX, &deviceY);
                    const int nativeX = static_cast<int>(std::lround(deviceX * scaleX + offsetX));
                    const int nativeY = static_cast<int>(std::lround(deviceY * scaleY + offsetY));
                    Require(nativeX >= 0 && nativeX < cairo_image_surface_get_width(image) && nativeY >= 0 &&
                                nativeY < cairo_image_surface_get_height(image),
                            "sample inside actual native paint surface");
                    const auto pixel = reinterpret_cast<const uint32_t*>(data + nativeY * stride)[nativeX];
                    pixels.emplace_back((pixel >> 16U) & 0xffU, (pixel >> 8U) & 0xffU, pixel & 0xffU);
                }
            }
            cairo_surface_unmap_image(surface, image);
            pending = false;
        }
        GtkWidget* widget_;
        gulong handler_ = 0;
        wxRect area_;
    } capture(editor, area);
    NativeFold::Paint(editor);
    Require(!capture.pending, "observe the actual native fold paint");
    return capture.pixels;
#endif
}
void Evidence(EditorControl* editor, int line, const wxString& name) {
    wxString prefix;
    if (!wxGetEnv("WXNOTEPAD_FOLD_ELLIPSIS_EVIDENCE", &prefix)) return;
    const int top = static_cast<int>(editor->Send(PointYFromPosition, 0, editor->Send(GetLineEndPosition, line)));
    const wxRect area(0, top, std::min(editor->GetClientSize().x, editor->FromDIP(600)),
                      static_cast<int>(editor->Send(TextHeight, line)));
    const auto pixels = Pixels(editor, area);
    wxImage image(area.width, area.height);
    for (int y = 0; y < area.height; ++y)
        for (int x = 0; x < area.width; ++x) {
            const auto& pixel = pixels[static_cast<size_t>(y) * area.width + x];
            image.SetRGB(x, y, pixel.Red(), pixel.Green(), pixel.Blue());
        }
    const auto path = prefix + "-" + name +
                      wxString::Format("-dpi%d-scale%.1f.png", editor->GetDPI().y, editor->GetContentScaleFactor());
    Require(image.SaveFile(path, wxBITMAP_TYPE_PNG), "retain actual native header pixels");
}
wxRect Box(EditorControl* editor, int line, bool visible) {
    const auto end = editor->Send(GetLineEndPosition, line);
    const int x = static_cast<int>(editor->Send(PointXFromPosition, 0, end)) + 1;
    const int y = static_cast<int>(editor->Send(PointYFromPosition, 0, end));
    const wxRect area(x, y, std::min(editor->FromDIP(100), editor->GetClientSize().x - x),
                      static_cast<int>(editor->Send(TextHeight, line)));
    Require(area.width > 0 && area.y >= 0 && area.GetBottom() < editor->GetClientSize().y,
            "fold suffix must be inside the visible native viewport");
    const auto pixels = Pixels(editor, area);
    int left = area.width, right = -1, top = area.height, bottom = -1;
    for (int row = 0; row < area.height; ++row)
        for (int column = 0; column < area.width; ++column)
            if (pixels[static_cast<size_t>(row) * area.width + column] == wxColour(128, 128, 128)) {
                left = std::min(left, column);
                right = std::max(right, column);
                top = std::min(top, row);
                bottom = std::max(bottom, row);
            }
    if (!visible) {
        Require(right == -1, "expanded headers must have no painted fold ellipsis");
        return {};
    }
    Require(right > left + 6 && bottom > top + 3, "collapsed header must paint the original grey box");
    // A closed border and three separate interior glyphs distinguish the boxed
    // middle dots from an unboxed string or a border with missing text.
    for (int column = left + 1; column < right; ++column)
        Require(pixels[static_cast<size_t>(top) * area.width + column] == wxColour(128, 128, 128) &&
                    pixels[static_cast<size_t>(bottom) * area.width + column] == wxColour(128, 128, 128),
                "fold display text must have both horizontal box borders");
    int groups = 0;
    bool previous = false;
    for (int column = left + 2; column < right - 1; ++column) {
        bool ink = false;
        for (int row = top + 2; row < bottom - 1; ++row) {
            const auto& pixel = pixels[static_cast<size_t>(row) * area.width + column];
            ink |= pixel.Red() < 240 && pixel.Green() < 240 && pixel.Blue() < 240;
        }
        if (ink && !previous) ++groups;
        previous = ink;
    }
    std::printf("box: line=%d bounds=%d,%d..%d,%d glyphs=%d dpi=%d zoom=%lld renderer=%lld\n", line, x + left, y + top,
                x + right, y + bottom, groups, editor->GetDPI().y, static_cast<long long>(editor->Send(GetZoom)),
                static_cast<long long>(editor->Send(GetTechnology)));
    Require(groups == 3, "the actual box must contain three separate middle-dot glyphs");
    return {x + left, y + top, right - left + 1, bottom - top + 1};
}
void Languages(EditorFrame* frame, const wxString& renderer) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* language;
        const char* name;
        const char* text;
        int nested;
    };
    const std::array fixtures = {
        Fixture{"C/C++", "cpp", "void outer() {\n  if (true) {\n    call();\n  }\n}\nafter();\n", 1},
        Fixture{"Python", "python", "class Outer:\n    def inner(self):\n        call()\n    tail()\nafter()\n", 1},
        Fixture{"JSON", "json", "{\n  \"items\": [\n    1,\n    2\n  ]\n}\n", 1},
        Fixture{"TOML", "toml", "[outer]\nvalue = 1\n[outer.inner]\nvalue = 2\n[after]\nvalue = 3\n", 2},
    };
    for (const auto& fixture : fixtures) {
        editor->Send(SetText, 0, reinterpret_cast<intptr_t>(fixture.text));
        Scheme(frame, fixture.language);
        editor->Send(EmptyUndoBuffer);
        editor->Send(SetSavePoint);
        editor->Send(SetSel, 0, 0);
        Box(editor, 0, false);
        NativeFold::MarginClick(editor, 0, "native folding margin must be visible");
        Require(!editor->Send(GetFoldExpanded, 0) && !editor->Send(GetLineVisible, 1),
                "native margin must collapse the original language header");
        const auto box = Box(editor, 0, true);
        Evidence(editor, 0, renderer + "-" + fixture.name + "-collapsed");
        const wxPoint centre(box.x + box.width / 2, box.y + box.height / 2);
        NativeSelection::Mouse(editor, wxEVT_LEFT_DOWN, centre);
        NativeSelection::Mouse(editor, wxEVT_LEFT_UP, centre);
        Require(!editor->Send(GetFoldExpanded, 0) && editor->Send(GetCurrentPos) == editor->Send(GetLineEndPosition, 0),
                "clicking the original core decoration places the caret at line end without a new fold action");
        NativeFold::MarginClick(editor, 0, "native folding margin must remain clickable");
        Box(editor, 0, false);
        Evidence(editor, 0, renderer + "-" + fixture.name + "-expanded");
        editor->Send(FoldAll, static_cast<uintptr_t>(Scintilla::FoldAction::Contract));
        Box(editor, 0, true);
        Require(editor->Send(GetFoldExpanded, fixture.nested),
                "original default-level folding must leave nested expansion state intact");
        View(frame, "Expand All");
        View(frame, "Collapse All");
        Box(editor, 0, true);
        NativeFold::MarginClick(editor, 0, "outer fold must reopen with nested collapse retained");
        Require(editor->Send(GetFoldExpanded, 0) && !editor->Send(GetFoldExpanded, fixture.nested),
                "Collapse All must retain the nested header's own collapse state");
        Box(editor, fixture.nested, true);
        View(frame, "Expand All");
        Box(editor, 0, false);
        View(frame, "Collapse All");
        View(frame, "Show Code Folding");
        Box(editor, 0, false);
        View(frame, "Show Code Folding");
        NativeFold::MarginClick(editor, 0, "reshown margin must retain original fold display setup");
        Box(editor, 0, true);
        Require(NativeSearch::Text(editor) == fixture.text && !editor->Send(GetModify) && !editor->Send(CanUndo),
                "fold ellipsis and native hit testing must preserve document bytes and history");
        View(frame, "Expand All");
    }
}
void Layout(EditorFrame* frame, const wxString& renderer) {
    auto* editor = frame->Editor();
    const std::string source = "void " + std::string(160, 'a') + "() {\n  call();\n}\nafter();\n";
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
    Scheme(frame, "C/C++");
    editor->Send(SetSel, 0, 0);
    NativeFold::MarginClick(editor, 0, "long header must fold through its margin");
    const auto end = editor->Send(GetLineEndPosition, 0);
    const auto endX = editor->Send(PointXFromPosition, 0, end);
    Require(endX > editor->GetClientSize().x, "long header must extend beyond the native viewport");
    const int textLeft = static_cast<int>(editor->Send(PointXFromPosition, 0, 0));
    const auto clipped = Pixels(editor, wxRect(textLeft, 0, editor->GetClientSize().x - textLeft,
                                               static_cast<int>(editor->Send(TextHeight, 0))));
    Require(std::count(clipped.begin(), clipped.end(), wxColour(128, 128, 128)) == 0,
            "off-screen fold suffix must be clipped out of the actual text viewport");
    editor->Send(SetXOffset, endX - editor->GetClientSize().x + editor->FromDIP(100));
    NativeFold::Paint(editor);
    const auto normal = Box(editor, 0, true);
    Evidence(editor, 0, renderer + "-scrolled");
    View(frame, "Zoom In");
    const auto zoomEnd = editor->Send(PointXFromPosition, 0, end);
    editor->Send(SetXOffset, editor->Send(GetXOffset) + zoomEnd - editor->GetClientSize().x + editor->FromDIP(100));
    NativeFold::Paint(editor);
    const auto zoomed = Box(editor, 0, true);
    Require(zoomed.height > normal.height, "original box must grow with native editor zoom");
    View(frame, "Reset Zoom");
    editor->Send(SetXOffset, 0);
    View(frame, "Word Wrap");
    NativeFold::Paint(editor);
    Require(editor->Send(WrapCount, 0) > 1, "long header must wrap in the native editor");
    Box(editor, 0, true);
    Evidence(editor, 0, renderer + "-wrapped");
    frame->SetClientSize(frame->FromDIP(wxSize(560, 440)));
    NativeFold::Paint(editor);
    Box(editor, 0, true);
    View(frame, "Expand All");
    Box(editor, 0, false);
    Scheme(frame, "Plain Text");
    Scheme(frame, "C/C++");
    NativeFold::MarginClick(editor, 0, "syntax reapplication must keep the original fold display setup");
    Box(editor, 0, true);
    View(frame, "Expand All");
    View(frame, "Word Wrap");
    frame->SetClientSize(frame->FromDIP(wxSize(720, 440)));
}
void Document(EditorFrame* frame) {
    auto* editor = frame->Editor();
    constexpr std::string_view source = "void outer() {\n  call();\n}\n";
    std::string utf16 = "\xFF\xFE";
    for (const char ch : source) {
        utf16 += ch;
        utf16 += '\0';
    }
    const auto path = TestFiles::TemporaryDirectory() / "fold-ellipsis.cpp";
    TestFiles::WriteBytes(path, utf16);
    editor->Send(SetSavePoint);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "load actual UTF-16LE file");
    NativeFold::Paint(editor);
    Require(editor->Send(GetCodePage) == Scintilla::CpUtf8, "UTF-16 disk encoding must retain UTF-8 editor storage");
    NativeFold::MarginClick(editor, 0, "reloaded header must retain boxed display text");
    Box(editor, 0, true);
    editor->Send(SelectAll);
    NativeSearch::Command(frame, wxID_COPY);
    {
        wxClipboardLocker lock;
        wxTextDataObject copied;
        Require(!(!lock) && wxTheClipboard->GetData(copied) &&
                    copied.GetText() == wxString::FromUTF8(source.data(), source.size()),
                "actual Copy must contain all hidden document text and no fold decoration");
    }
    editor->Send(SetTargetStart, 0);
    editor->Send(SetTargetEnd, editor->Send(GetLength));
    constexpr char dots[] = "\xC2\xB7\xC2\xB7\xC2\xB7";
    Require(editor->Send(SearchInTarget, sizeof(dots) - 1, reinterpret_cast<intptr_t>(dots)) == -1,
            "core document search must not match visible fold decoration");
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == utf16 &&
                NativeSearch::Text(editor) == source && !editor->Send(GetModify) && !editor->Send(CanUndo),
            "fold decoration must be absent from actual text and independently read UTF-16 saved bytes");
    editor->Send(SetSel, 0, 0);
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("x"));
    editor->Send(Undo);
    View(frame, "Expand All");
    View(frame, "Collapse All");
    Require(editor->Send(CanRedo) && !editor->Send(GetModify), "fold display must preserve the save point and redo");
    editor->Send(Redo);
    Require(NativeSearch::Text(editor) == std::string(source) + "x", "redo must restore only the document edit");
    editor->Send(Undo);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "reload the saved folded file");
    NativeFold::Paint(editor);
    Box(editor, 0, false);
    NativeFold::MarginClick(editor, 0, "collapse after reload must restore the original box");
    Box(editor, 0, true);
    std::filesystem::remove(path);
}
class EllipsisApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_FOLDING_DPI96"))
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create complementary normal-DPI fold ellipsis window");
#endif
        wxInitAllImageHandlers();
        auto* frame = new EditorFrame;
        frame->SetClientSize(frame->FromDIP(wxSize(720, 440)));
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "fold ellipsis native window must be presented");
                frame->Editor()->SetCurrentLineHighlightMode(LineHighlightMode::None);
#ifdef __WXMSW__
                for (const auto technology : {Scintilla::Technology::Default, Scintilla::Technology::DirectWrite}) {
                    frame->Editor()->Send(SetTechnology, static_cast<uintptr_t>(technology));
                    Require(frame->Editor()->Send(GetTechnology) == std::to_underlying(technology),
                            "use the actual requested Windows renderer");
                    Languages(frame, technology == Scintilla::Technology::Default ? "gdi" : "directwrite");
                    Layout(frame, technology == Scintilla::Technology::Default ? "gdi" : "directwrite");
                    Document(frame);
                }
#else
                Languages(frame, "pango");
                Layout(frame, "pango");
                Document(frame);
#endif
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(EllipsisApp);
