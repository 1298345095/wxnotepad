#include "native_key_helpers.h"
#include <print>
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
#include <memory>

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
struct PaintPixels {
    std::vector<wxColour> pixels;
    int width = 0;
    int height = 0;
    double scaleX = 1;
    double scaleY = 1;
};
PaintPixels WindowPixels(EditorControl* editor, const wxRect& area) {
    // Read the same actual owned-window pixels in one native transfer. Calling
    // GetPixel for every sample is too slow on the shared high-DPI desktop.
    NativeFold::Paint(editor);
    wxClientDC source(editor);
    wxBitmap bitmap;
    Require(bitmap.CreateWithLogicalSize(area.GetSize(), editor->GetContentScaleFactor(), 32),
            "allocate native window pixel capture");
    wxMemoryDC target(bitmap);
    Require(target.Blit(0, 0, area.width, area.height, &source, area.x, area.y), "read actual native header pixels");
    target.SelectObject(wxNullBitmap);
    const auto image = bitmap.ConvertToImage();
    PaintPixels result{{},
                       image.GetWidth(),
                       image.GetHeight(),
                       static_cast<double>(image.GetWidth()) / area.width,
                       static_cast<double>(image.GetHeight()) / area.height};
    result.pixels.reserve(static_cast<size_t>(result.width) * result.height);
    for (int y = 0; y < result.height; ++y)
        for (int x = 0; x < result.width; ++x)
            result.pixels.emplace_back(image.GetRed(x, y), image.GetGreen(x, y), image.GetBlue(x, y));
    return result;
}
PaintPixels Pixels(EditorControl* editor, const wxRect& area) {
#ifdef __WXMSW__
    return WindowPixels(editor, area);
#else
    if (!GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(editor->GetConnectWidget()))) return WindowPixels(editor, area);
    class Capture final {
    public:
        Capture(EditorControl* editor, const wxRect& area) : widget_(editor->GetConnectWidget()), area_(area) {
            handler_ = g_signal_connect_after(widget_, "draw", G_CALLBACK(Draw), this);
        }
        ~Capture() { g_signal_handler_disconnect(widget_, handler_); }
        PaintPixels result;
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
            double left = area_.x, top = area_.y;
            cairo_user_to_device(context, &left, &top);
            const int nativeLeft = static_cast<int>(std::lround(left * scaleX + offsetX));
            const int nativeTop = static_cast<int>(std::lround(top * scaleY + offsetY));
            cairo_matrix_t matrix;
            cairo_get_matrix(context, &matrix);
            result.scaleX = scaleX * matrix.xx;
            result.scaleY = scaleY * matrix.yy;
            result.width = static_cast<int>(std::lround(area_.width * result.scaleX));
            result.height = static_cast<int>(std::lround(area_.height * result.scaleY));
            pending = true;
            const cairo_rectangle_int_t region{nativeLeft, nativeTop, result.width, result.height};
            const auto unmap = [surface](cairo_surface_t* image) { cairo_surface_unmap_image(surface, image); };
            const std::unique_ptr<cairo_surface_t, decltype(unmap)> mapped(cairo_surface_map_to_image(surface, &region),
                                                                           unmap);
            auto* image = mapped.get();
            if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) return;
            const int stride = cairo_image_surface_get_stride(image);
            const auto* data = cairo_image_surface_get_data(image);
            result.pixels.clear();
            for (int y = 0; y < result.height; ++y) {
                for (int x = 0; x < result.width; ++x) {
                    const auto pixel = reinterpret_cast<const uint32_t*>(data + y * stride)[x];
                    result.pixels.emplace_back((pixel >> 16U) & 0xffU, (pixel >> 8U) & 0xffU, pixel & 0xffU);
                }
            }
            pending = false;
        }
        GtkWidget* widget_;
        gulong handler_ = 0;
        wxRect area_;
    } capture(editor, area);
    NativeFold::Paint(editor);
    Require(!capture.pending, "observe the actual native fold paint");
    return capture.result;
#endif
}
void Evidence(EditorControl* editor, int line, const wxString& name) {
    wxString prefix;
    if (!wxGetEnv("WXNOTEPAD_FOLD_ELLIPSIS_EVIDENCE", &prefix)) return;
    const int top = static_cast<int>(editor->Send(PointYFromPosition, 0, editor->Send(GetLineEndPosition, line)));
    const wxRect area(0, top, std::min(editor->GetClientSize().x, editor->FromDIP(600)),
                      static_cast<int>(editor->Send(TextHeight, line)));
    const auto pixels = Pixels(editor, area);
    wxImage image(pixels.width, pixels.height);
    for (int y = 0; y < pixels.height; ++y)
        for (int x = 0; x < pixels.width; ++x) {
            const auto& pixel = pixels.pixels[static_cast<size_t>(y) * pixels.width + x];
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
    int left = pixels.width, right = -1, top = pixels.height, bottom = -1;
    for (int row = 0; row < pixels.height; ++row)
        for (int column = 0; column < pixels.width; ++column)
            if (pixels.pixels[static_cast<size_t>(row) * pixels.width + column] == wxColour(128, 128, 128)) {
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
        Require(pixels.pixels[static_cast<size_t>(top) * pixels.width + column] == wxColour(128, 128, 128) &&
                    pixels.pixels[static_cast<size_t>(bottom) * pixels.width + column] == wxColour(128, 128, 128),
                "fold display text must have both horizontal box borders");
    int groups = 0;
    bool previous = false;
    const int insetX = static_cast<int>(std::ceil(2 * pixels.scaleX));
    const int insetY = static_cast<int>(std::ceil(2 * pixels.scaleY));
    for (int column = left + insetX; column < right - insetX + 1; ++column) {
        bool ink = false;
        for (int row = top + insetY; row < bottom - insetY + 1; ++row) {
            const auto& pixel = pixels.pixels[static_cast<size_t>(row) * pixels.width + column];
            ink |= pixel.Red() < 240 && pixel.Green() < 240 && pixel.Blue() < 240;
        }
        if (ink && !previous) ++groups;
        previous = ink;
    }
    std::println("box: line={:d} bounds={:d},{:d}..{:d},{:d} glyphs={:d} dpi={:d} zoom={:d} renderer={:d}", line,
                 x + left, y + top, x + right, y + bottom, groups, editor->GetDPI().y,
                 static_cast<long long>(editor->Send(GetZoom)), static_cast<long long>(editor->Send(GetTechnology)));
    Require(groups == 3, "the actual box must contain three separate middle-dot glyphs");
    return {x + static_cast<int>(left / pixels.scaleX), y + static_cast<int>(top / pixels.scaleY),
            static_cast<int>((right - left + 1) / pixels.scaleX), static_cast<int>((bottom - top + 1) / pixels.scaleY)};
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
        NativeSearch::RevealAllFolds(frame);
        View(frame, "All Levels");
        Box(editor, 0, true);
        NativeFold::MarginClick(editor, 0, "outer fold must reopen with nested collapse retained");
        Require(editor->Send(GetFoldExpanded, 0) && !editor->Send(GetFoldExpanded, fixture.nested),
                "Collapse All must retain the nested header's own collapse state");
        Box(editor, fixture.nested, true);
        NativeSearch::RevealAllFolds(frame);
        Box(editor, 0, false);
        View(frame, "All Levels");
        View(frame, "Show Code Folding");
        Box(editor, 0, false);
        View(frame, "Show Code Folding");
        NativeFold::MarginClick(editor, 0, "reshown margin must retain original fold display setup");
        Box(editor, 0, true);
        Require(NativeSearch::Text(editor) == fixture.text && !editor->Send(GetModify) && !editor->Send(CanUndo),
                "fold ellipsis and native hit testing must preserve document bytes and history");
        NativeSearch::RevealAllFolds(frame);
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
    Require(std::count(clipped.pixels.begin(), clipped.pixels.end(), wxColour(128, 128, 128)) == 0,
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
    NativeSearch::RevealAllFolds(frame);
    Box(editor, 0, false);
    Scheme(frame, "Plain Text");
    Scheme(frame, "C/C++");
    NativeFold::MarginClick(editor, 0, "syntax reapplication must keep the original fold display setup");
    Box(editor, 0, true);
    NativeSearch::RevealAllFolds(frame);
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
    NativeSearch::RevealAllFolds(frame);
    View(frame, "All Levels");
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
                std::println(stderr, "FAIL: {}", error.what());
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
