#include "editor_control.h"
#include "editor_frame.h"
#include "native_search_helpers.h"
#include <ScintillaTypes.h>
#include <wx/wx.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Paint(EditorControl* editor) {
    for (int attempt = 0; attempt < 40; ++attempt) {
        wxYield();
        editor->Refresh(false);
        editor->Update();
        wxYield();
        if (editor->Send(Message::GetEndStyled) == editor->Send(Message::GetLength)) return;
        wxMilliSleep(10);
    }
}
void MarginClick(EditorControl* editor, int line) {
    const auto width = editor->Send(Message::GetMarginWidthN, 1);
    Require(width == editor->FromDIP(16), "C++ must expose a DPI-scaled fold margin");
    const auto position = editor->Send(Message::PositionFromLine, line);
    const int x = static_cast<int>(editor->Send(Message::GetMarginWidthN, 0) + width / 2);
    const int y = static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position) +
                                   editor->Send(Message::TextHeight, line) / 2);
    Require(y >= 0 && y < editor->GetClientSize().y, "native click must target an on-screen document line");
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(editor->GetHandle());
    const LPARAM point = MAKELPARAM(x, y);
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(handle, WM_LBUTTONUP, 0, point);
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    for (const auto type : {GDK_BUTTON_PRESS, GDK_BUTTON_RELEASE}) {
        auto* event = gdk_event_new(type);
        event->button.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
        event->button.send_event = TRUE;
        event->button.time = GDK_CURRENT_TIME;
        event->button.x = x;
        event->button.y = y;
        event->button.button = 1;
        event->button.state = static_cast<GdkModifierType>(type == GDK_BUTTON_RELEASE ? GDK_BUTTON1_MASK : 0);
        const auto* display = gtk_widget_get_display(widget);
        gdk_event_set_device(event,
                             gdk_seat_get_pointer(gdk_display_get_default_seat(const_cast<GdkDisplay*>(display))));
        gtk_widget_event(widget, event);
        gdk_event_free(event);
    }
#endif
    Paint(editor);
}
std::function<wxColour(int, int)> PixelReader(EditorControl* editor) {
#ifdef __WXGTK__
    // Wayland does not support reading pixels back from a window DC. Ask the
    // actual native widget to paint its margin into an owned Cairo target.
    const int width =
        static_cast<int>(editor->Send(Message::GetMarginWidthN, 0) + editor->Send(Message::GetMarginWidthN, 1));
    const int height = editor->GetClientSize().y;
    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
    auto* context = cairo_create(surface);
    gtk_widget_draw(editor->GetConnectWidget(), context);
    cairo_destroy(context);
    auto* pixels = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);
    cairo_surface_destroy(surface);
    Require(pixels != nullptr, "native widget margin must paint into its owned target");
    wxImage image(width, height);
    const auto* data = gdk_pixbuf_get_pixels(pixels);
    const int stride = gdk_pixbuf_get_rowstride(pixels);
    const int channels = gdk_pixbuf_get_n_channels(pixels);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto* pixel = data + y * stride + x * channels;
            image.SetRGB(x, y, pixel[0], pixel[1], pixel[2]);
        }
    }
    g_object_unref(pixels);
    return [image](int x, int y) { return wxColour(image.GetRed(x, y), image.GetGreen(x, y), image.GetBlue(x, y)); };
#else
    return [dc = std::make_shared<wxClientDC>(editor)](int x, int y) {
        wxColour pixel;
        Require(dc->GetPixel(x, y, &pixel), "native control must expose its painted margin pixels");
        return pixel;
    };
#endif
}
void MarginBackground(EditorControl* editor) {
    const auto read = PixelReader(editor);
    const int left = static_cast<int>(editor->Send(Message::GetMarginWidthN, 0));
    const int width = static_cast<int>(editor->Send(Message::GetMarginWidthN, 1));
    for (const int y : {150, editor->GetClientSize().y - 20}) {
        for (const int x : {left + 1, left + width - 2}) {
            const auto pixel = read(x, y);
            std::printf("margin pixel %d,%d = %u,%u,%u\n", x, y, pixel.Red(), pixel.Green(), pixel.Blue());
            Require(pixel == wxColour(240, 240, 240),
                    "fold background must cover the complete margin beyond one 8x8 tile");
        }
    }
}
std::vector<unsigned int> MarkerPixels(EditorControl* editor, int line) {
    const auto read = PixelReader(editor);
    const int left = static_cast<int>(editor->Send(Message::GetMarginWidthN, 0));
    const int width = static_cast<int>(editor->Send(Message::GetMarginWidthN, 1));
    const auto position = editor->Send(Message::PositionFromLine, line);
    const int top = static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position));
    const int height = static_cast<int>(editor->Send(Message::TextHeight, line));
    std::vector<unsigned int> pixels;
    for (int y = top + 1; y < top + height - 1; ++y) {
        for (int x = left + 2; x < left + width - 2; ++x) {
            const auto pixel = read(x, y);
            pixels.push_back(pixel.GetRGB());
        }
    }
    return pixels;
}
class FoldApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                const std::string source = "void outer() {\n  if (true) {\n    call();\n  }\n  tail();\n}\nafter();\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                Paint(editor);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 0);
                const auto openPixels = MarkerPixels(editor, 1);
                MarginClick(editor, 1);
                Require(!editor->Send(Message::GetFoldExpanded, 1) && !editor->Send(Message::GetLineVisible, 2) &&
                            editor->Send(Message::GetLineVisible, 4),
                        "native header click must collapse only its lexer-defined block");
                const auto closedPixels = MarkerPixels(editor, 1);
                const auto differences = std::inner_product(openPixels.begin(), openPixels.end(), closedPixels.begin(),
                                                            size_t{0}, std::plus<>(), std::not_equal_to<>());
                std::printf("native folder marker changed pixels: %zu\n", differences);
                Require(differences > 0, "native folder marker must visibly change from minus to plus");
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "folding must preserve bytes, save point and history");
                std::puts("PASS: native C++ margin click folds lexer-defined block without editing");
                MarginClick(editor, 0);
                Require(!editor->Send(Message::GetLineVisible, 1), "outer header must hide its descendants");
                MarginClick(editor, 0);
                Require(editor->Send(Message::GetLineVisible, 1) && !editor->Send(Message::GetLineVisible, 2) &&
                            !editor->Send(Message::GetFoldExpanded, 1),
                        "reopening the parent must retain the collapsed nested block");
                MarginClick(editor, 4);
                Require(!editor->Send(Message::GetFoldExpanded, 1) && editor->Send(Message::GetFoldExpanded, 0),
                        "ordinary non-header margin click must leave related headers unchanged");
                MarginClick(editor, 1);
                const auto caret = editor->Send(Message::PositionFromLine, 2) + 4;
                editor->Send(Message::SetSel, caret + 2, caret);
                MarginClick(editor, 0);
                Require(editor->Send(Message::GetLineVisible,
                                     editor->Send(Message::LineFromPosition, editor->Send(Message::GetCurrentPos))) &&
                            editor->Send(Message::GetLineVisible,
                                         editor->Send(Message::LineFromPosition, editor->Send(Message::GetAnchor))),
                        "collapsing a block containing the selection must leave caret and anchor visible");
                Require(!editor->Send(Message::GetFoldExpanded, 0) && NativeSearch::Text(editor) == source &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                        "caret repair must retain collapsed visibility, bytes, save point and history");
                std::puts(
                    "PASS: nested states and non-header clicks are stable; hidden selection moves to visible header");
                MarginBackground(editor);
                editor->Send(Message::SetBufferedDraw, 0);
                Paint(editor);
                MarginBackground(editor);
                std::puts("PASS: buffered and unbuffered native margin backgrounds cover the full control height");
                MarginClick(editor, 0);
                MarginClick(editor, 0);
                const auto brace = source.find('{');
                editor->Send(Message::SetTargetStart, brace);
                editor->Send(Message::SetTargetEnd, brace + 1);
                editor->Send(Message::ReplaceTarget, 0, reinterpret_cast<intptr_t>(""));
                Paint(editor);
                Require(editor->Send(Message::GetLineVisible, 1) &&
                            !(static_cast<uintptr_t>(editor->Send(Message::GetFoldLevel, 0)) &
                              static_cast<uintptr_t>(Scintilla::FoldLevel::HeaderFlag)),
                        "deleting a collapsed header must expose its former descendants and recalculate levels");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::GetLineVisible, 1) &&
                            (static_cast<uintptr_t>(editor->Send(Message::GetFoldLevel, 0)) &
                             static_cast<uintptr_t>(Scintilla::FoldLevel::HeaderFlag)),
                        "Undo must restore the header and save point with reachable text");
                NativeSearch::Command(frame, wxID_REDO);
                Paint(editor);
                Require(editor->Send(Message::GetLineVisible, 1), "Redo of header deletion must retain reachable text");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                std::puts("PASS: header deletion, Undo and Redo repair fold levels and visibility");
                MarginClick(editor, 0);
                const auto hidden = editor->Send(Message::PositionFromLine, 2);
                editor->Send(Message::SetTargetStart, hidden);
                editor->Send(Message::SetTargetEnd, hidden);
                editor->Send(Message::ReplaceTarget, 7, reinterpret_cast<intptr_t>("edit();"));
                Paint(editor);
                Require(editor->Send(Message::GetLineVisible, 2),
                        "editing hidden text must reveal its containing fold");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::GetLineVisible, 2),
                        "Undo of hidden-text edit must restore bytes and reachable state");
                const auto directory =
                    std::filesystem::temp_directory_path() / ("wxnotepad-fold-" + std::to_string(wxGetProcessId()));
                std::filesystem::create_directory(directory);
                const auto path = directory / "fold.cpp";
#ifdef __WXMSW__
                const wxString filename(path.wstring());
#else
                const auto filename = wxString::FromUTF8(path.string());
#endif
                Require(frame->SaveAs(filename).status == SaveStatus::Saved, "fold fixture must save");
                MarginClick(editor, 0);
                Require(frame->Save().status == SaveStatus::Saved && !editor->Send(Message::GetFoldExpanded, 0),
                        "unchanged-language Save must preserve collapsed folds");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                Require(!editor->Send(Message::GetFoldExpanded, 0), "repeated language selection must preserve folds");
                Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled &&
                            frame->Open(filename + ".missing").status == OpenStatus::Failed &&
                            !editor->Send(Message::GetFoldExpanded, 0),
                        "cancelled and failed Open must preserve folds");
                std::ifstream saved(path, std::ios::binary);
                Require(std::string(std::istreambuf_iterator<char>(saved), {}) == source,
                        "folding and Save must retain independent disk bytes");
                saved.close();
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "Plain Text"));
                Require(editor->Send(Message::GetMarginWidthN, 1) == 0 && editor->Send(Message::GetLineVisible, 2) &&
                            !(static_cast<uintptr_t>(editor->Send(Message::GetFoldLevel, 0)) &
                              static_cast<uintptr_t>(Scintilla::FoldLevel::HeaderFlag)),
                        "Plain Text must hide and disable the margin and clear stale fold state");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                Paint(editor);
                Require(editor->Send(Message::GetFoldExpanded, 0) && editor->Send(Message::GetLineVisible, 2),
                        "changing back to C++ must start expanded");
                MarginClick(editor, 0);
                Require(frame->Open(filename).status == OpenStatus::Opened, "same-language Open must succeed");
                Paint(editor);
                Require(editor->Send(Message::GetFoldExpanded, 0) && editor->Send(Message::GetLineVisible, 2),
                        "successful same-language Open must reset all contraction state");
                MarginClick(editor, 0);
                NativeSearch::Command(frame, wxID_NEW);
                Require(editor->Send(Message::GetLength) == 0 && editor->Send(Message::GetMarginWidthN, 1) == 0 &&
                            editor->Send(Message::GetLineVisible, 0),
                        "New must reset folding and return to unnamed Plain Text");
                std::filesystem::remove_all(directory);
                std::puts("PASS: language changes and New/Open reset folds; Save and failed operations preserve them");
                std::string scrolled;
                for (int line = 0; line < 40; ++line)
                    scrolled += "prefix();\n";
                scrolled += source + source;
                for (int line = 0; line < 40; ++line)
                    scrolled += "suffix();\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(scrolled.c_str()));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                editor->Send(Message::SetFirstVisibleLine, 38);
                Paint(editor);
                MarginClick(editor, 41);
                MarginClick(editor, 40);
                Require(!editor->Send(Message::GetLineVisible, 41),
                        "scrolled parent click must hide its mapped children");
                frame->SetClientSize(frame->FromDIP(wxSize(540, 300)));
                Paint(editor);
                MarginClick(editor, 47);
                Require(!editor->Send(Message::GetFoldExpanded, 47) && !editor->Send(Message::GetLineVisible, 48) &&
                            !editor->Send(Message::GetFoldExpanded, 40),
                        "resized and scrolled native click must find the later header after hidden display lines");
                MarginClick(editor, 40);
                Require(editor->Send(Message::GetLineVisible, 41) && !editor->Send(Message::GetLineVisible, 42) &&
                            !editor->Send(Message::GetLineVisible, 48),
                        "reopening scrolled parent must preserve its nested state and unrelated sibling fold");
                std::puts("PASS: scrolling, contraction mapping and resizing preserve native margin hit positions");
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
wxIMPLEMENT_APP(FoldApp);
