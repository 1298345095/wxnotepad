#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "encoded_file_helpers.h"
#include "native_key_helpers.h"
#include <wx/wx.h>
#include <wx/dcclient.h>
#include <wx/menu.h>
#include <wx/button.h>
#include <cstdio>
#include <stdexcept>
#include <fstream>
#include <chrono>
#include <utility>
#ifdef __WXGTK__
#include <wx/graphics.h>
#include <cairo.h>
#endif

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame, const char* name) {
    const int id = frame->GetMenuBar()->FindMenuItem("View", name);
    Require(id != wxNOT_FOUND, "original current-line menu command exists");
    wxCommandEvent command(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(command), "real current-line menu command is handled");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
}
void Modes(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const auto element = static_cast<uintptr_t>(std::to_underlying(Scintilla::Element::CaretLineBack));
    Require(editor->Send(GetElementIsSet, element) && editor->Send(GetElementColour, element) == 0x5AC3C0C2 &&
                editor->Send(GetCaretLineHighlightSubLine) && editor->Send(GetCaretLineVisibleAlways),
            "original default is outline alpha90 with independent subline and always-visible state");
    Menu(frame, "Background Color");
    Require(editor->Send(GetCaretLineFrame) == 0 && editor->Send(GetElementColour, element) == 0x5A00FFFF,
            "Background Color uses original yellow alpha90 fill");
    Menu(frame, "Highlight Subline");
    Menu(frame, "No Highlight");
    Require(!editor->Send(GetElementIsSet, element) && !editor->Send(GetCaretLineHighlightSubLine),
            "None disables the element and retains independent unchecked subline state");
    Menu(frame, "Outline Frame");
    Menu(frame, "Outline Frame");
    Require(editor->Send(GetElementColour, element) == 0x5AC3C0C2 && !editor->Send(GetCaretLineHighlightSubLine) &&
                editor->Send(GetCaretLineLayer) == 1,
            "outline selection is idempotent and uses original under-text style");
    Menu(frame, "Highlight Subline");
    const int outline = frame->GetMenuBar()->FindMenuItem("View", "Outline Frame");
    Require(frame->GetMenuBar()->IsChecked(outline), "native radio state reflects current outline mode");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Background Color"));
    Require(editor->Send(GetCaretLineFrame) == 0, "native menu activates background mode");
    NativeKeys::Shortcut(frame, 'F', true, false, true);
    Require(editor->Send(GetCaretLineFrame) > 0, "native Ctrl+Shift+F selects outline");
    NativeKeys::Shortcut(frame, 'I', true, false, true);
    Require(editor->Send(GetCaretLineFrame) == 0, "native Ctrl+Shift+I selects background");
    wxKeyEvent hook(wxEVT_CHAR_HOOK);
    hook.m_keyCode = 'F';
    hook.SetControlDown(true);
    hook.SetShiftDown(true);
    frame->GetEventHandler()->ProcessEvent(hook);
    Require(editor->Send(GetCaretLineFrame) > 0, "frame hook dispatches shifted outline shortcut");
    hook.m_keyCode = 'I';
    frame->GetEventHandler()->ProcessEvent(hook);
    Require(editor->Send(GetCaretLineFrame) == 0, "frame hook dispatches shifted background before Split Lines");
    std::puts("PASS: original default, three modes and independent subline menu commands");
}
void Paint(EditorControl* editor) {
    editor->Refresh(false);
    editor->Update();
    wxYield();
}
#ifdef __WXGTK__
// Wayland client DCs are separate Cairo surfaces. Observe the actual widget's
// paint event after its normal handler, while the native GTK paint context lives.
class PaintObservation final : public wxEvtHandler {
public:
    explicit PaintObservation(EditorControl* editor) : editor_(editor) { editor_->PushEventHandler(this); }
    ~PaintObservation() override { editor_->PopEventHandler(false); }
    bool ProcessEvent(wxEvent& event) override {
        const bool handled = wxEvtHandler::ProcessEvent(event);
        if (event.GetEventType() == wxEVT_PAINT && pending_) {
            wxPaintDC dc(editor_);
            // GTK's paint surface belongs to the native top-level window;
            // account for its client translation before reading surface pixels.
            auto* context = static_cast<cairo_t*>(dc.GetGraphicsContext()->GetNativeContext());
            double x = x_;
            double y = y_;
            cairo_user_to_device(context, &x, &y);
            sampled_ = dc.GetPixel(static_cast<int>(x), static_cast<int>(y), &colour_);
            pending_ = false;
        }
        return handled;
    }
    wxColour Read(int x, int y) {
        x_ = x;
        y_ = y;
        sampled_ = false;
        pending_ = true;
        editor_->Refresh(false);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (pending_ && std::chrono::steady_clock::now() < deadline) {
            editor_->Update();
            wxYield();
            if (pending_) wxMilliSleep(10);
        }
        Require(!pending_ && sampled_, "sample actual GTK widget pixels during native paint");
        return colour_;
    }

private:
    EditorControl* editor_;
    int x_ = 0;
    int y_ = 0;
    bool pending_ = false;
    bool sampled_ = false;
    wxColour colour_;
};
#endif
class PixelReader {
public:
    explicit PixelReader(EditorControl* editor)
#ifdef __WXGTK__
        : observation_(editor)
#else
        : editor_(editor)
#endif
    {
    }
    wxColour Read(int x, int y) {
#ifdef __WXGTK__
        const auto colour = observation_.Read(x, y);
#else
        Paint(editor_);
        wxClientDC pixels(editor_);
        wxColour colour;
        Require(pixels.GetPixel(x, y, &colour), "read the actual owned editor's painted background");
#endif
        std::printf("sample %d,%d=%u,%u,%u\n", x, y, colour.Red(), colour.Green(), colour.Blue());
        return colour;
    }

private:
#ifdef __WXGTK__
    PaintObservation observation_;
#else
    EditorControl* editor_;
#endif
};
bool Yellow(const wxColour& colour) {
    return colour.Red() == 255 && colour.Green() == 255 && colour.Blue() >= 164 && colour.Blue() <= 166;
}
bool Gray(const wxColour& colour) {
    return colour.Red() >= 232 && colour.Red() <= 234 && colour.Green() >= 232 && colour.Green() <= 234 &&
           colour.Blue() >= 233 && colour.Blue() <= 235;
}
void WrapAndLifecycle(EditorFrame* frame, PixelReader& pixels) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const std::string line = std::string(220, ' ') + "end\nnext\nlast";
    TestEditor::ResetStream(editor, line.c_str());
    editor->Send(SetWrapMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::Wrap::Word)));
    frame->SetClientSize(400, 240);
    Paint(editor);
    Require(editor->Send(WrapCount, 0) > 2, "fixture has multiple native wrapped display rows");
    Menu(frame, "Background Color");
    const auto height = static_cast<int>(editor->Send(TextHeight, 0));
    Require(Yellow(pixels.Read(200, height / 2)) && !Yellow(pixels.Read(200, height + height / 2)),
            "subline mode paints only the active wrapped display row");
    Menu(frame, "Highlight Subline");
    Require(Yellow(pixels.Read(200, height / 2)) && Yellow(pixels.Read(200, height + height / 2)),
            "whole-line mode paints all wrapped rows of the caret's logical line");
    Menu(frame, "Highlight Subline");
    const auto secondRow = editor->Send(PositionFromPointClose, editor->FromDIP(70), height + height / 2);
    editor->Send(GotoPos, secondRow);
    Require(!Yellow(pixels.Read(200, height / 2)) && Yellow(pixels.Read(200, height + height / 2)),
            "subline highlight follows caret between wrapped rows");
    auto* sibling = new wxButton(frame, wxID_ANY, "owned focus target", wxPoint(250, 170));
    sibling->SetFocus();
    wxYield();
    Require(Yellow(pixels.Read(200, height + height / 2)), "highlight remains visible after native focus transfer");
    sibling->Destroy();
    editor->Send(GotoPos, 0);
    Menu(frame, "Outline Frame");
    Require(Gray(pixels.Read(200, 0)) && Gray(pixels.Read(200, height - 1)),
            "subline outline paints top and bottom borders around only the active display row");
    Menu(frame, "Highlight Subline");
    Require(Gray(pixels.Read(200, 0)) && !Gray(pixels.Read(200, height - 1)),
            "logical-line outline omits internal wrapped-row borders");
    Menu(frame, "Highlight Subline");
    editor->Send(SetReadOnly, 1);
    Menu(frame, "Outline Frame");
    editor->Send(SetSel, 10, 2);
    const auto anchor = editor->Send(GetAnchor);
    const auto caret = editor->Send(GetCurrentPos);
    for (const char* language : {"C/C++", "Plain Text", "Python", "JSON"}) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", language));
        Require(editor->Send(GetElementColour, static_cast<uintptr_t>(std::to_underlying(
                                                   Scintilla::Element::CaretLineBack))) == 0x5AC3C0C2 &&
                    editor->Send(GetCaretLineHighlightSubLine),
                "chosen current-line style survives Plain Text and supported lexer transitions");
    }
    Require(editor->Send(GetAnchor) == anchor && editor->Send(GetCurrentPos) == caret && !editor->Send(GetModify) &&
                !editor->Send(CanUndo),
            "focus, wrap, selection, read-only and language changes preserve document state");
    editor->Send(SetReadOnly, 0);
    Menu(frame, "Outline Frame");
    editor->Send(SetZoom, 200);
    const auto wide = editor->Send(GetCaretLineFrame);
    editor->Send(SetZoom, 100);
    const auto normal = editor->Send(GetCaretLineFrame);
    Require((editor->GetDPI().y == 144 && normal == 3 && wide == 6) ||
                (editor->GetDPI().y == 96 && normal == 2 && wide == 4),
            "original frame scales once with actual window DPI and zoom");
    Require(editor->Send(StyleGetSize, static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Default))) ==
                12,
            "highlight keeps delivered native 12-point base font");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    TestEditor::ResetStream(editor, "int main() {\n    return 0;\n}\nlast\n");
    editor->Send(Colourise, 0, -1);
    editor->Send(SetFoldLevel, 0, 0x2400);
    editor->Send(SetFoldLevel, 1, 0x401);
    editor->Send(FoldAll, static_cast<uintptr_t>(std::to_underlying(Scintilla::FoldAction::Contract)));
    Require(!editor->Send(GetLineVisible, 1), "fixture has an actually collapsed fold");
    Menu(frame, "Background Color");
    editor->Send(LineScroll, 0, 1);
    editor->Send(GotoLine, 3);
    Menu(frame, "Outline Frame");
    Require(editor->Send(GetCaretLineFrame) == normal && !editor->Send(GetModify),
            "scrolling folded code retains the current-line style without editing");
    Require(frame->NewDocument(), "clean document can be replaced by New");
    Require(editor->Send(GetCaretLineFrame) == normal &&
                editor->Send(GetElementIsSet,
                             static_cast<uintptr_t>(std::to_underlying(Scintilla::Element::CaretLineBack))),
            "New retains the chosen highlight");
    std::puts("PASS: native wrapped row pixels, caret/focus, selection, read-only, language/New and zoom lifecycle");
}
void SavedState(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-current-line");
    std::filesystem::create_directory(directory);
    const TestFiles::EncodedFixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "A\x{E4}\x{B8}\x{AD}\r\nlast", "A\x{E4}\x{B8}\x{AD}\r\nlast!"},
        {"utf16", FileEncoding::Utf16Le,
         std::string("\x{FF}\x{FE}"
                     "A\0-\x{4E}\r\0\n\0",
                     10),
         std::string("\x{FF}\x{FE}"
                     "A\0-\x{4E}\r\0\n\0!\0",
                     12)},
        {"gbk", FileEncoding::Gbk, "A\x{D6}\x{D0}\nlast", "A\x{D6}\x{D0}\nlast!"}};
    for (const auto& fixture : fixtures) {
        using enum Scintilla::Message;
        const auto path = directory / (std::string(fixture.name) + ".txt");
        std::ofstream(path, std::ios::binary) << fixture.before;
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "encoded fixture opens through real file command");
        editor->Send(GotoPos, editor->Send(GetLength));
        editor->Send(ReplaceSel, 0, reinterpret_cast<intptr_t>("!"));
        editor->Send(Undo);
        editor->Send(SetSel, 4, 1);
        const auto eol = editor->Send(GetEOLMode);
        const auto bom = frame->DocumentHasBom();
        for (const char* mode : {"Background Color", "No Highlight", "Outline Frame", "Highlight Subline"})
            Menu(frame, mode);
        Require(editor->Send(GetAnchor) == 4 && editor->Send(GetCurrentPos) == 1 && !editor->Send(GetModify) &&
                    editor->Send(CanRedo) && editor->Send(GetEOLMode) == eol && frame->DocumentHasBom() == bom &&
                    frame->DocumentEncoding() == fixture.encoding,
                "highlight commands retain selection direction, encoding/BOM/EOL, dirty state and pending redo");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "unchanged save matches independent original encoded bytes");
        editor->Send(Redo);
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"redo save retains exact edited bytes",
                                         "undo save retains exact original bytes",
                                         "second redo retains exact edited bytes"});
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened &&
                    editor->Send(GetElementColour, static_cast<uintptr_t>(std::to_underlying(
                                                       Scintilla::Element::CaretLineBack))) == 0x5AC3C0C2,
                "same-path reopen retains current-line style");
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: independent encoded unchanged-save, metadata/selection and Undo/Redo preservation");
}
class CurrentLineApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_CURRENT_LINE_DPI96")) {
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create the complementary emulated 96-DPI owned window");
            std::puts("Complementary 96-DPI unaware-thread emulation; monitor scaling remains unchanged");
        }
#endif
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                using enum Scintilla::Message;
                Require(presented, "private compositor must present the owned editor before native scenarios");
                auto* editor = frame->Editor();
                PixelReader pixels(editor);
#ifdef __WXGTK__
                // Native keyboard activation follows the widget's first real paint.
                pixels.Read(0, 0);
#endif
                Modes(frame);
                WrapAndLifecycle(frame, pixels);
                SavedState(frame);
                // Public editor messages isolate the real widget's alpha rendering
                // before the original menu/default behavior is restored.
                editor->Send(SetCaretLineVisibleAlways, 1);
                editor->Send(SetCaretLineLayer, 1);
                editor->Send(SetCaretLineFrame, 0);
                editor->Send(SetElementColour,
                             static_cast<uintptr_t>(std::to_underlying(Scintilla::Element::CaretLineBack)), 0x5A00FFFF);
                editor->Send(SetText, 0, reinterpret_cast<intptr_t>(""));
                editor->Send(SetSavePoint);
                const int y = static_cast<int>(editor->Send(TextHeight, 0)) / 2;
                const auto colour = pixels.Read(200, y);
                std::printf("current-line pixel=%u,%u,%u at 200,%d dpi=%d\n", colour.Red(), colour.Green(),
                            colour.Blue(), y, editor->GetDPI().y);
                Require(colour.Red() == 255 && colour.Green() == 255 && colour.Blue() >= 164 && colour.Blue() <= 166,
                        "original alpha90 yellow must blend over the white editor background");
                std::puts("PASS: actual editor current-line alpha rendering");
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
wxIMPLEMENT_APP(CurrentLineApp);
