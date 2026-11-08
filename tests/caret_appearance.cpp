#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "encoded_file_helpers.h"
#include "native_key_helpers.h"
#include <wx/wx.h>
#include <wx/menu.h>
#include <wx/dcclient.h>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#ifdef __WXGTK__
#include <wx/graphics.h>
#include <cairo.h>
#endif

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame, const char* name) {
    const int id = frame->GetMenuBar()->FindMenuItem("View", name);
    Require(id != wxNOT_FOUND, "original caret menu command exists");
    NativeSearch::Command(frame, id);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
}
void Check(EditorFrame* frame, const char* name, bool expected) {
    const int id = frame->GetMenuBar()->FindMenuItem("View", name);
    Require(id != wxNOT_FOUND && frame->GetMenuBar()->IsChecked(id) == expected,
            "native caret menu check matches original preference");
}
void Modes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto systemPeriod = editor->Send(Message::GetCaretPeriod);
    Require(editor->Send(Message::GetCaretStyle) == 1, "original default draws a line caret in the original core");
    Menu(frame, "No Blink");
    Require(editor->Send(Message::GetCaretPeriod) == 0, "No Blink selects original zero period");
    Check(frame, "No Blink", true);
    for (const auto& [label, style, width] : {std::tuple{"Block (INS Mode)", 2, 0},
                                              {"Line Width 1", 1, 1},
                                              {"Line Width 2", 1, 2},
                                              {"Line Width 3", 1, 3}}) {
        Menu(frame, label);
        Require(editor->Send(Message::GetCaretStyle) == style, "insertion mode uses original core style flags");
        if (width) Require(editor->Send(Message::GetCaretWidth) >= width, "line caret respects requested width");
        Check(frame, label, true);
    }
    Menu(frame, "Block (OVR Mode)");
    Require(editor->Send(Message::GetCaretStyle) == 17, "overstrike block is independent of insertion line width");
    Menu(frame, "Block (INS Mode)");
    Require(editor->Send(Message::GetCaretStyle) == 18, "insertion and overstrike blocks combine original flags");
    Check(frame, "Draw Block Caret in Selection", true);
    Menu(frame, "Draw Block Caret in Selection");
    Require(editor->Send(Message::GetCaretStyle) == 274, "unchecked selection mode uses original BlockAfter flag");
    Check(frame, "Draw Block Caret in Selection", false);
    Menu(frame, "No Blink");
    Require(editor->Send(Message::GetCaretPeriod) == systemPeriod, "turning No Blink off restores system period");
    Check(frame, "No Blink", false);
    std::puts("PASS: native caret menus, independent flags, checks and No Blink restore");
}

// Observe actual widget paint output, including Wayland's widget Cairo surface.
class PaintRows final : public wxEvtHandler {
public:
    explicit PaintRows(EditorControl* editor) : editor_(editor) { editor_->PushEventHandler(this); }
    ~PaintRows() override { editor_->PopEventHandler(false); }
    bool ProcessEvent(wxEvent& event) override {
        const bool handled = wxEvtHandler::ProcessEvent(event);
#ifdef __WXGTK__
        if (event.GetEventType() == wxEVT_PAINT && pending_) {
            wxPaintDC dc(editor_);
            auto* context = static_cast<cairo_t*>(dc.GetGraphicsContext()->GetNativeContext());
            double x = x_, y = y_;
            cairo_user_to_device(context, &x, &y);
            Read(dc, static_cast<int>(x), static_cast<int>(y));
            pending_ = false;
        }
#endif
        return handled;
    }
    std::vector<unsigned> Row(int y) { return Capture(0, y, 400); }
    unsigned Pixel(int x, int y) { return Capture(x, y, 1).front(); }

private:
    std::vector<unsigned> Capture(int x, int y, int count) {
        x_ = x;
        y_ = y;
        count_ = count;
        pending_ = true;
        row_.clear();
        editor_->Refresh(false);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        do {
            editor_->Update();
            wxYield();
#ifndef __WXGTK__
            wxClientDC dc(editor_);
            Read(dc, x_, y_);
            pending_ = false;
#endif
            if (pending_) wxMilliSleep(5);
        } while (pending_ && std::chrono::steady_clock::now() < deadline);
        Require(!pending_ && !row_.empty(), "read pixels from the owned native editor paint");
        return row_;
    }

    void Read(wxDC& dc, int x, int y) {
        for (int column = 0; column < count_; ++column) {
            wxColour colour;
            Require(dc.GetPixel(x + column, y, &colour), "native paint row is readable");
            row_.push_back(colour.GetRGB());
        }
    }
    EditorControl* editor_;
    int x_ = 0;
    int y_ = 0;
    int count_ = 400;
    bool pending_ = false;
    std::vector<unsigned> row_;
};

std::pair<int, int> Span(EditorControl* editor, PaintRows& paint, int y) {
    const auto style = editor->Send(Message::GetCaretStyle);
    editor->Send(Message::SetCaretStyle, 0);
    const auto background = paint.Row(y);
    editor->Send(Message::SetCaretStyle, style);
    const auto foreground = paint.Row(y);
    int first = -1, last = -1;
    for (size_t x = 0; x < background.size(); ++x) {
        if (background[x] != foreground[x]) {
            if (first < 0) first = static_cast<int>(x);
            last = static_cast<int>(x) + 1;
        }
    }
    std::printf("caret native span y=%d [%d,%d) style=%lld\n", y, first, last, static_cast<long long>(style));
    return {first, last};
}

void Rendering(EditorFrame* frame, PaintRows& paint) {
    auto* editor = frame->Editor();
    // Source and numeric original captures independently establish geometry:
    // insertion blocks use the character cell, forward selections use the last
    // selected character unless BlockAfter is set, and overstrike bars use the
    // bottom two pixels. Platform font metrics remain native.
    Menu(frame, "No Blink");
    Menu(frame, "Draw Block Caret in Selection");
    Menu(frame, "Block (OVR Mode)");
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("Wi中😀é \r\n"));
    std::string raw(16, '\0');
    editor->Send(Message::GetText, raw.size(), reinterpret_cast<intptr_t>(raw.data()));
    raw.resize(15);
    Require(raw == "Wi\xE4\xB8\xAD\xF0\x9F\x98\x80"
                   "e\xCC\x81 \r\n" &&
                editor->Send(Message::GetLength) == 15 && editor->Send(Message::GetLineEndPosition, 0) == 13 &&
                editor->Send(Message::GetCodePage) == 65001 && editor->Send(Message::GetBidirectional) == 0,
            "native caret fixture preserves original 15 UTF-8 bytes, line end 13 and LTR layout");
    std::puts("PASS: original raw 15-byte UTF-8 fixture, line end 13 and LTR layout");
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    editor->SetCurrentLineHighlightMode(LineHighlightMode::None);
    editor->SetFocus();
    wxYield();
    Require(editor->Send(Message::GetFocus) != 0, "native focus activates the original caret");
    Menu(frame, "Block (INS Mode)");
    const auto xAt = [editor](intptr_t position) {
        return static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position));
    };
    Require(xAt(13) > xAt(12), "the trailing space after a combining cluster has positive native advance");
    for (const auto& [position, after] : {std::pair{0, 1}, {1, 2}, {2, 5}, {5, 9}, {9, 10}, {12, 13}}) {
        editor->Send(Message::SetSel, position, position);
        editor->Send(Message::SetOvertype, 0);
        auto [first, last] = Span(editor, paint, 2);
        const int queriedStart = xAt(position);
        const int queriedEnd = xAt(after);
        Require(std::abs(first - queriedStart) <= 1 && std::abs(last - queriedEnd) <= 1,
                "block caret covers the native variable-width/Unicode character cell");
    }
    editor->Send(Message::SetSel, 0, 5);
    auto inside = Span(editor, paint, 2);
    Require(std::abs(inside.first - xAt(2)) <= 1 && std::abs(inside.second - xAt(5)) <= 1,
            "forward block selection draws inside its final Unicode character");
    Menu(frame, "Draw Block Caret in Selection");
    auto after = Span(editor, paint, 2);
    Require(std::abs(after.first - xAt(5)) <= 1 && std::abs(after.second - xAt(9)) <= 1,
            "unchecked Draw Block Caret in Selection places it after the forward selection");
    editor->Send(Message::SetSel, 5, 0);
    auto reverse = Span(editor, paint, 2);
    Require(std::abs(reverse.first - xAt(0)) <= 1 && std::abs(reverse.second - xAt(1)) <= 1,
            "reverse selection keeps its block at the leading character");
    editor->Send(Message::SetSel, 0, 0);
    Menu(frame, "Line Width 3");
    const auto line = Span(editor, paint, 2);
    Require(line.second - line.first >= 3, "native line caret paints the requested width");
    editor->Send(Message::SetOvertype, 1);
    Require(Span(editor, paint, 2).first < 0, "default overstrike bar leaves the upper character cell unpainted");
    const int height = static_cast<int>(editor->Send(Message::TextHeight, 0));
    Require(Span(editor, paint, height - 1).first >= 0, "default overstrike bar paints the bottom of the cell");
    Menu(frame, "Block (OVR Mode)");
    Require(Span(editor, paint, 2).second - xAt(0) >= 3, "overstrike block paints the character cell");
    editor->Send(Message::SetSel, 0, 5);
    auto overstrikeAfter = Span(editor, paint, 2);
    Require(std::abs(overstrikeAfter.first - xAt(5)) <= 1 && std::abs(overstrikeAfter.second - xAt(9)) <= 1,
            "overstrike block obeys unchecked selection placement");
    Menu(frame, "Draw Block Caret in Selection");
    auto overstrikeInside = Span(editor, paint, 2);
    Require(std::abs(overstrikeInside.first - xAt(2)) <= 1 && std::abs(overstrikeInside.second - xAt(5)) <= 1,
            "overstrike block obeys checked selection placement");
    Menu(frame, "Draw Block Caret in Selection");
    for (int position : {13, 15}) {
        editor->Send(Message::SetSel, position, position);
        const int y = static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position));
        Require(Span(editor, paint, y + 2).first >= 0, "block caret remains visible at line/document boundaries");
    }
    Require(!editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
            "appearance, selection and overstrike preferences preserve document save-point/undo state");
    std::puts("PASS: actual native caret pixels, Unicode cells, selection placement, overstrike and boundaries");
}

void BlinkAndLifecycle(EditorFrame* frame, PaintRows& paint) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetSel, 0, 0);
    editor->Send(Message::SetOvertype, 0);
    Menu(frame, "Line Width 3");
    const auto style = editor->Send(Message::GetCaretStyle);
    editor->Send(Message::SetCaretStyle, 0);
    const auto background = paint.Row(2);
    editor->Send(Message::SetCaretStyle, style);
    const auto steady = paint.Row(2);
    size_t pixel = 0;
    while (pixel < steady.size() && steady[pixel] == background[pixel])
        ++pixel;
    Require(pixel < steady.size(), "locate an actual painted caret pixel before observing blink");
    auto appearance = editor->GetCaretAppearance();
    appearance.blinkPeriod = 400;
    editor->SetCaretAppearance(appearance);
    bool sawOn = false, sawOff = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    unsigned sample = 0;
    while ((!sawOn || !sawOff) && std::chrono::steady_clock::now() < deadline) {
        // One observed pixel keeps native DPI virtualization/readback latency
        // below the blink period. Full-row reads can alias whole blink cycles.
        const bool visible = paint.Pixel(static_cast<int>(pixel), 2) != background[pixel];
        std::printf("native blink sample=%u visible=%d focus=%lld period=%lld\n", sample, visible,
                    static_cast<long long>(editor->Send(Message::GetFocus)),
                    static_cast<long long>(editor->Send(Message::GetCaretPeriod)));
        sawOn |= visible;
        sawOff |= !visible;
        wxMilliSleep(13 * (1 + sample++ % 5));
        wxYield();
    }
    Require(sawOn && sawOff, "configured period produces both native visible and invisible caret paint phases");
    Menu(frame, "No Blink");
    for (int sample = 0; sample < 6; ++sample) {
        wxMilliSleep(150);
        wxYield();
        Require(paint.Pixel(static_cast<int>(pixel), 2) != background[pixel],
                "No Blink continuously paints the caret across timer intervals");
    }
    auto* other = new wxButton(frame, wxID_ANY, "caret focus target", wxPoint(450, 100));
    other->SetFocus();
    wxYield();
    Require(Span(editor, paint, 2).first < 0, "native focus loss drops the visual caret");
    editor->SetFocus();
    wxYield();
    Require(Span(editor, paint, 2).second - Span(editor, paint, 2).first >= 3,
            "native focus return restores chosen width and No Blink");
    other->Destroy();
    for (const char* language : {"C/C++", "Python", "JSON", "Plain Text"})
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", language));
    Require(editor->Send(Message::GetCaretStyle) == 273 && editor->Send(Message::GetCaretPeriod) == 0,
            "language changes retain chosen caret flags and period");
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(editor->GetHandle());
    const auto interaction = editor->Send(Message::GetIMEInteraction);
    editor->Send(Message::SetIMEInteraction, static_cast<uintptr_t>(Scintilla::IMEInteraction::Windowed));
    POINT native{};
    Require(GetCaretPos(&native) && native.x == editor->Send(Message::PointXFromPosition, 0, 0) &&
                native.y == editor->Send(Message::PointYFromPosition, 0, 0),
            "transparent native IME caret remains at original insertion geometry");
    SendMessageW(handle, WM_IME_STARTCOMPOSITION, 0, 0);
    Require(Span(editor, paint, 2).first < 0,
            "windowed IME start drops the visual caret while preserving native geometry");
    SendMessageW(handle, WM_IME_ENDCOMPOSITION, 0, 0);
    Require(Span(editor, paint, 2).first >= 0 && editor->Send(Message::GetCaretPeriod) == 0 &&
                editor->Send(Message::GetCaretStyle) == 273,
            "IME end/cancel restores chosen appearance and No Blink");
    editor->Send(Message::SetIMEInteraction, interaction);
#endif
    Require(!editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
            "blink, focus, language and empty IME lifecycle preserve original text and undo state");
    std::puts("PASS: controlled native blink, focus, language and windowed IME caret lifecycle");
}

void ZoomAndDpi(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto size =
        editor->Send(Message::StyleGetSizeFractional, static_cast<uintptr_t>(Scintilla::StylesCommon::Default));
    const auto quality = editor->Send(Message::GetFontQuality);
    const int dpi = editor->GetDPI().y;
    int systemWidth = 1;
#ifdef __WXMSW__
    DWORD width = 1;
    if (SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &width, 0)) systemWidth = static_cast<int>(width);
#endif
    // Independent original captures at 144 DPI and complementary 96-DPI fixture.
    const int zooms[] = {50, 100, 150, 200, 500};
    const int widths96[] = {1, 1, 2, 2, 5};
    const int widths144[] = {1, 2, 2, 3, 8};
    Require(dpi == 96 || dpi == 144, "caret DPI fixture supports captured 96 and 144 DPI windows");
    for (int mode : {1, 2, 3}) {
        Menu(frame, mode == 1 ? "Line Width 1" : mode == 2 ? "Line Width 2" : "Line Width 3");
        for (int i = 0; i < 5; ++i) {
            editor->Send(Message::SetZoom, zooms[i]);
            const int expected = systemWidth > mode ? systemWidth
                                 : mode == 1        ? (dpi == 96 ? widths96[i] : widths144[i])
                                                    : mode;
            std::printf("caret width mode=%d zoom=%d dpi=%d queried=%lld expected=%d\n", mode, zooms[i], dpi,
                        static_cast<long long>(editor->Send(Message::GetCaretWidth)), expected);
            Require(editor->Send(Message::GetCaretWidth) == std::min(expected, 20),
                    "default width scales with original DPI/zoom while explicit/accessibility widths remain pixels");
        }
    }
    editor->Send(Message::SetZoom, 100);
    Require(editor->Send(Message::StyleGetSizeFractional, static_cast<uintptr_t>(Scintilla::StylesCommon::Default)) ==
                    size &&
                editor->Send(Message::GetFontQuality) == quality,
            "caret width scaling preserves native font point size and clarity");
    std::puts("PASS: original width distinction, native DPI/zoom and preserved font settings");
}

void SavedDocument(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-caret");
    std::filesystem::create_directory(directory);
    const auto path = directory / "caret.txt";
    const TestFiles::EncodedFixture fixture{"utf8", FileEncoding::Utf8, "a中😀\r\nlast", "a中😀\r\nlast!"};
    std::ofstream(path, std::ios::binary) << fixture.before;
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                editor->Send(Message::GetCaretStyle) == 273 && editor->Send(Message::GetCaretPeriod) == 0,
            "opening a document retains chosen caret appearance and blink policy");
    editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
    editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 4, 1);
    Menu(frame, "Block (INS Mode)");
    Menu(frame, "Draw Block Caret in Selection");
    Require(editor->Send(Message::GetAnchor) == 4 && editor->Send(Message::GetCurrentPos) == 1 &&
                !editor->Send(Message::GetModify) && editor->Send(Message::CanRedo),
            "caret commands preserve selection direction, save point and pending redo");
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
            "caret preferences leave independently read saved bytes unchanged");
    editor->Send(Message::Redo);
    TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                    {"redo saves original edited bytes", "undo saves original input bytes",
                                     "second redo saves original edited bytes"});
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                editor->Send(Message::GetCaretStyle) == 18 && editor->Send(Message::GetCaretPeriod) == 0,
            "same-path reopen retains the selected block and No Blink");
    Require(frame->NewDocument() && editor->Send(Message::GetCaretStyle) == 18 &&
                editor->Send(Message::GetCaretPeriod) == 0,
            "New retains caret preferences after reopening and editing");
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    std::puts("PASS: open/reopen/New caret policy and independently read saved bytes/selection/Undo/Redo");
}
class CaretApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_CARET_DPI96")) {
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create complementary owned 96-DPI caret window");
        }
#endif
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native window must be presented");
                Modes(frame);
                PaintRows paint(frame->Editor());
                Rendering(frame, paint);
                BlinkAndLifecycle(frame, paint);
                ZoomAndDpi(frame);
                SavedDocument(frame);
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
wxIMPLEMENT_APP(CaretApp);
