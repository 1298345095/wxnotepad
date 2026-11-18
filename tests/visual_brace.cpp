#include "brace_observation.h"
#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "visual_brace_fixtures.h"
#include "encoded_file_helpers.h"
#include <array>
#include <utility>
#include <fstream>
#include <wx/dcclient.h>
#include <functional>
#include <cstdio>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Paint(EditorControl* editor) {
    NativePaint::Complete(editor);
}
void Expect(EditorControl* editor, intptr_t first, intptr_t second, intptr_t style, intptr_t guide) {
    Paint(editor);
    const auto actual = EditorCoreObservation::Braces(*editor);
    std::printf("brace=%lld,%lld style=%lld guide=%lld\n", static_cast<long long>(actual[0]),
                static_cast<long long>(actual[1]), static_cast<long long>(actual[2]),
                static_cast<long long>(editor->Send(Message::GetHighlightGuide)));
    Require(actual == std::array<intptr_t, 3>{first, second, style} &&
                editor->Send(Message::GetHighlightGuide) == guide,
            "automatic brace decoration agrees with original caret-adjacent positions/style/guide");
}
void Reference(EditorFrame* frame) {
    auto* editor = frame->Editor();
    size_t language = 0;
    for (const auto* name : {"Plain Text", "C/C++", "Python", "JSON"}) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", name));
        for (const auto& fixture : BraceReference::fixtures) {
            using enum Scintilla::Message;
            editor->Send(SetText, 0, reinterpret_cast<intptr_t>(fixture.text));
            editor->Send(SetSel, fixture.anchor, fixture.caret);
            editor->Send(EmptyUndoBuffer);
            editor->Send(SetSavePoint);
            editor->Send(Colourise, 0, -1);
            std::printf("reference %s/%s ", name, fixture.name);
            const auto match = fixture.match[language];
            Expect(editor, fixture.position, match, match < 0 && fixture.position >= 0 ? 35 : 34, fixture.guide);
            Require(NativeSearch::Text(editor) == fixture.text && editor->Send(GetAnchor) == fixture.anchor &&
                        editor->Send(GetCurrentPos) == fixture.caret && !editor->Send(GetModify) &&
                        !editor->Send(CanUndo),
                    "automatic feedback preserves original document, selection and history");
        }
        ++language;
    }
    std::puts("PASS: 72 captured original adjacency, UTF-8, selection, nested and lexer-style cases");
}
std::function<wxColour(int, int)> PixelReader(EditorControl* editor) {
#ifdef __WXGTK__
    const auto size = editor->GetClientSize();
    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, size.x, size.y);
    auto* context = cairo_create(surface);
    gtk_widget_draw(editor->GetConnectWidget(), context);
    cairo_destroy(context);
    auto* pixels = gdk_pixbuf_get_from_surface(surface, 0, 0, size.x, size.y);
    cairo_surface_destroy(surface);
    Require(pixels != nullptr, "actual GTK widget paints into its owned native target");
    wxImage image(size.x, size.y);
    const auto* data = gdk_pixbuf_get_pixels(pixels);
    const int stride = gdk_pixbuf_get_rowstride(pixels);
    const int channels = gdk_pixbuf_get_n_channels(pixels);
    for (int y = 0; y < size.y; ++y) {
        for (int x = 0; x < size.x; ++x) {
            const auto* pixel = data + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * channels;
            image.SetRGB(x, y, pixel[0], pixel[1], pixel[2]);
        }
    }
    g_object_unref(pixels);
    return [image](int x, int y) { return wxColour(image.GetRed(x, y), image.GetGreen(x, y), image.GetBlue(x, y)); };
#else
    return [dc = std::make_shared<wxClientDC>(editor)](int x, int y) {
        wxColour pixel;
        Require(dc->GetPixel(x, y, &pixel), "read actual owned native editor pixels");
        return pixel;
    };
#endif
}
int ColouredPixels(EditorControl* editor, intptr_t position, bool bad) {
    Paint(editor);
    const auto read = PixelReader(editor);
    const int left = static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position));
    const int right = static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position + 1));
    const int top = static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position));
    const int height = static_cast<int>(editor->Send(Message::TextHeight, 0));
    int count = 0;
    for (int y = top + 1; y < top + height - 1; ++y) {
        for (int x = left; x < right; ++x) {
            const auto colour = read(x, y);
            // Original alpha50 / outline200 over white. Narrow channels exclude
            // coloured ClearType glyph fringes from the decoration observation.
            const auto within = [](int value, int expected) { return std::abs(value - expected) <= 2; };
            const bool coloured =
                bad ? colour.Red() == 255 && ((within(colour.Green(), 205) && within(colour.Blue(), 205)) ||
                                              (within(colour.Green(), 55) && within(colour.Blue(), 55)))
                    : (within(colour.Red(), 205) && within(colour.Green(), 251) && within(colour.Blue(), 207)) ||
                          (within(colour.Red(), 57) && within(colour.Green(), 239) && within(colour.Blue(), 63));
            if (coloured) ++count;
        }
    }
    std::printf("native brace pixels position=%lld bad=%d count=%d dpi=%d\n", static_cast<long long>(position), bad,
                count, editor->GetDPI().y);
    return count;
}
void BraceAppearance(EditorFrame* frame, int toggle) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    for (const auto [indicator, colour] : {std::pair{9, 0x0AEA02}, std::pair{10, 0x0000FF}}) {
        Require(editor->Send(IndicGetStyle, indicator) ==
                        static_cast<intptr_t>(std::to_underlying(Scintilla::IndicatorStyle::RoundBox)) &&
                    editor->Send(IndicGetFore, indicator) == colour && editor->Send(IndicGetAlpha, indicator) == 50 &&
                    editor->Send(IndicGetOutlineAlpha, indicator) == 200,
                "brace indicators use original light round-box green/red alpha50 outline200 styles");
    }
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Plain Text"));
    editor->SetCurrentLineHighlightMode(LineHighlightMode::None);
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>("  (x)  y"));
    editor->Send(GotoPos, 2);
    Expect(editor, 2, 4, 34, 2);
    Require(ColouredPixels(editor, 2, false) > 0 && ColouredPixels(editor, 4, false) > 0 &&
                ColouredPixels(editor, 7, false) == 0,
            "native widget paints green overlays at both braces and leaves unrelated text unchanged");
    NativeSearch::Command(frame, toggle);
    Require(ColouredPixels(editor, 2, false) == 0 && ColouredPixels(editor, 4, false) == 0,
            "turning automatic feedback off removes its visible overlays");
    NativeSearch::Command(frame, toggle);
    Require(ColouredPixels(editor, 2, false) > 0, "enabling feedback restores visible green at a stationary caret");
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>("  (x"));
    editor->Send(GotoPos, 2);
    Expect(editor, 2, -1, 35, 0);
    Require(ColouredPixels(editor, 2, true) > 0, "native widget paints original red for an unmatched brace");
    std::puts("PASS: original indicator styles and native matched/bad/off/on rendering");
}
void Lifecycle(EditorFrame* frame, int toggle) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>("  (x)"));
    editor->Send(GotoPos, 2);
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    editor->Send(DeleteRange, 4, 1);
    Expect(editor, 2, -1, 35, 0);
    editor->Send(Undo);
    Expect(editor, 2, 4, 34, 2);
    editor->Send(Redo);
    Expect(editor, 2, -1, 35, 0);
    editor->Send(Undo);
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Rectangle)));
    editor->Send(SetRectangularSelectionAnchor, 0);
    editor->Send(SetRectangularSelectionCaret, 2);
    editor->Send(SetReadOnly, 1);
    const auto rectangleAnchor = editor->Send(GetAnchor);
    const auto rectangleCaret = editor->Send(GetCurrentPos);
    const auto rectangleRedo = editor->Send(CanRedo);
    Require(editor->Send(SelectionIsRectangle), "fixture creates a real rectangular selection");
    NativeSearch::Command(frame, toggle);
    NativeSearch::Command(frame, toggle);
    Expect(editor, 2, 4, 34, 2);
    Require(editor->Send(SelectionIsRectangle) && editor->Send(GetAnchor) == rectangleAnchor &&
                editor->Send(GetCurrentPos) == rectangleCaret && !editor->Send(GetModify) &&
                editor->Send(CanRedo) == rectangleRedo,
            "read-only rectangular selection and its edit history survive appearance changes");
    editor->Send(SetReadOnly, 0);
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream)));
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>("[\"]\"]"));
    editor->Send(SetSel, 0, 0);
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    TestEditor::UseFixedWidthFont(editor, 14.5);
    editor->Send(SetZoom, 150);
    std::array<char, 256> font{};
    editor->Send(StyleGetFont, 0, reinterpret_cast<intptr_t>(font.data()));
    const std::string originalFont(font.data());
    const auto dpi = editor->GetDPI();
    for (const auto* language : {"C/C++", "Plain Text", "Python", "JSON", "Plain Text"}) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", language));
        Expect(editor, 0, std::string_view(language) == "Plain Text" ? 2 : 4, 34, 0);
        editor->Send(StyleGetFont, 0, reinterpret_cast<intptr_t>(font.data()));
        Require(editor->Send(StyleGetSizeFractional, 0) == 1450 && editor->Send(GetZoom) == 150 &&
                    originalFont == font.data() && editor->GetDPI() == dpi && editor->Send(GetAnchor) == 0 &&
                    editor->Send(GetCurrentPos) == 0 && !editor->Send(GetModify) && !editor->Send(CanUndo),
                "syntax/highlighting changes refresh stationary context and preserve font/zoom/selection/history");
    }
    Require(frame->NewDocument(), "clean original fixture can be replaced through New");
    Expect(editor, -1, -1, 34, 0);
    editor->Send(ClearAll);
    Expect(editor, -1, -1, 34, 0);
    std::puts("PASS: edit/Undo/Redo, read-only rectangle, stationary syntax changes and New/empty replacement");
}
void SavedState(EditorFrame* frame, int toggle) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-visual-brace");
    std::filesystem::create_directory(directory);
    const std::array<TestFiles::EncodedFixture, 3> fixtures = {
        {{"utf8", FileEncoding::Utf8, "(x)", "(x)!"},
         {"utf16", FileEncoding::Utf16Le, std::string("\x{FF}\x{FE}(\0x\0)\0", 8),
          std::string("\x{FF}\x{FE}(\0x\0)\0!\0", 10)},
         {"gbk", FileEncoding::Gbk, "(\x{D6}\x{D0})", "(\x{D6}\x{D0})!"}}};
    for (const auto& fixture : fixtures) {
        using enum Scintilla::Message;
        const auto path = directory / (std::string(fixture.name) + ".txt");
        std::ofstream(path, std::ios::binary) << fixture.before;
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "encoded fixture opens through real application file lifecycle");
        editor->Send(GotoPos, editor->Send(GetLength));
        editor->Send(ReplaceSel, 0, reinterpret_cast<intptr_t>("!"));
        editor->Send(Undo);
        editor->Send(SetSel, editor->Send(GetLength), 0);
        const auto eol = editor->Send(GetEOLMode);
        const auto bom = frame->DocumentHasBom();
        const auto length = editor->Send(GetLength);
        NativeSearch::Command(frame, toggle);
        NativeSearch::Command(frame, toggle);
        Expect(editor, 0, length - 1, 34, 0);
        Require(editor->Send(GetAnchor) == length && editor->Send(GetCurrentPos) == 0 && editor->Send(CanRedo) &&
                    !editor->Send(GetModify) && editor->Send(GetEOLMode) == eol && frame->DocumentHasBom() == bom &&
                    frame->DocumentEncoding() == fixture.encoding,
                "visual feedback preserves encoded document metadata, reversed selection and Redo");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "appearance-only save retains independently read original disk bytes");
        editor->Send(Redo);
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"redo saves exact edited bytes", "undo saves exact original bytes",
                                         "second redo saves exact edited bytes"});
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "same-path replacement retains visual preference");
        editor->Send(GotoPos, 0);
        Expect(editor, 0, length - 1, 34, 0);
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: UTF-8/UTF-16LE/CP936 real Open/Save/reopen and encoded Undo/Redo preservation");
}
class VisualBraceApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        auto* frame = new EditorFrame;
        TestEditor::UseFixedWidthFont(frame->Editor());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                using enum Scintilla::Message;
                Require(presented, "owned editor is presented before native scenarios");
                auto* editor = frame->Editor();
                const int toggle = frame->GetMenuBar()->FindMenuItem("View", "Visual Brace Matching");
                Require(toggle != wxNOT_FOUND, "native View menu exposes original Visual Brace Matching command");
                const std::unique_ptr<wxAcceleratorEntry> accelerator(
                    frame->GetMenuBar()->FindItem(toggle)->GetAccel());
                Require(accelerator && accelerator->GetKeyCode() == 'V' &&
                            accelerator->GetFlags() == (wxACCEL_CTRL | wxACCEL_SHIFT),
                        "automatic matching has the exact original Ctrl+Shift+V accelerator");
                const std::unique_ptr<wxAcceleratorEntry> paste(frame->GetMenuBar()->FindItem(wxID_PASTE)->GetAccel());
                Require(paste && paste->GetKeyCode() == 'V' && paste->GetFlags() == wxACCEL_CTRL,
                        "Paste retains its exact native Ctrl+V accelerator");
                editor->Send(SetText, 0, reinterpret_cast<intptr_t>("  (x)"));
                editor->Send(GotoPos, 2);
                Expect(editor, 2, 4, 34, 2);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsChecked(toggle), "visual brace matching defaults enabled");
                NativeSearch::Command(frame, toggle);
                Expect(editor, -1, -1, 34, 2);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetMenuBar()->IsChecked(toggle), "native checked state follows disabled feedback");
                NativeKeys::Shortcut(frame, 'B', true, false, false);
                Require(editor->Send(GetCurrentPos) == 4 && editor->Send(GetAnchor) == 4,
                        "native Ctrl+B remains usable while automatic visual feedback is off");
                NativeKeys::Shortcut(frame, 'B', true, false, true);
                Require(editor->Send(GetCurrentPos) == 2 && editor->Send(GetAnchor) == 5,
                        "native Ctrl+Shift+B retains original directed selection while feedback is off");
                Expect(editor, -1, -1, 34, 2);
                editor->Send(SetSel, 2, 2);
                NativeKeys::Shortcut(frame, 'V', true, false, true);
                Expect(editor, 2, 4, 34, 2);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsChecked(toggle), "native checked state follows enabled feedback");
                std::puts("PASS: native checked menu and Ctrl+Shift+V clear and refresh stationary caret");
                Reference(frame);
                BraceAppearance(frame, toggle);
                Lifecycle(frame, toggle);
                SavedState(frame, toggle);
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
wxIMPLEMENT_APP(VisualBraceApp);
