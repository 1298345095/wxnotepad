#include "native_key_helpers.h"
#include <array>
#include "native_fold_helpers.h"
#include "native_paint_helpers.h"
#include <wx/menu.h>
#include <algorithm>
#include <utility>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void EllipsisStyle(EditorControl* editor) {
    using enum Scintilla::Message;
    constexpr auto fold = static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::FoldDisplayText));
    constexpr auto base = static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Default));
    Require(editor->Send(StyleGetFore, fold) == 0x808080 && editor->Send(StyleGetWeight, fold) == 700,
            "original Fold Ellipsis style must use bold grey text");
    Require(editor->Send(StyleGetSizeFractional, fold) ==
                std::max<intptr_t>(200, editor->Send(StyleGetSizeFractional, base) - 200),
            "original Fold Ellipsis size must be two points smaller, with the original two-point minimum");
    for (const auto message : {StyleGetItalic, StyleGetUnderline, StyleGetStrike, StyleGetBack})
        Require(editor->Send(message, fold) == editor->Send(message, base),
                "unspecified Fold Ellipsis effects and background must inherit the effective default");
}
std::vector<wxColour> MarkerPixels(EditorControl* editor, int line) {
    using enum Scintilla::Message;
    const int left = static_cast<int>(editor->Send(GetMarginWidthN, 0) + editor->Send(GetMarginWidthN, 1));
    const auto width = static_cast<int>(editor->Send(GetMarginWidthN, 2));
    const auto position = editor->Send(PositionFromLine, line);
    const int top = static_cast<int>(editor->Send(PointYFromPosition, 0, position));
    const auto height = static_cast<int>(editor->Send(TextHeight, line));
    return NativePaint::Read(editor, wxRect(left + 2, top + 1, width - 4, height - 2));
}
void BlockHighlightPaint(EditorFrame* frame, int highlight) {
    auto* editor = frame->Editor();
    const auto selected = MarkerPixels(editor, 1);
    const auto outer = MarkerPixels(editor, 0);
    Require(std::count(selected.begin(), selected.end(), wxColour(255, 0, 0)) > 0,
            "current nested block must paint original red selected fold markers");
    NativeSearch::Command(frame, highlight);
    const auto disabled = MarkerPixels(editor, 1);
    Require(std::count(disabled.begin(), disabled.end(), wxColour(255, 0, 0)) == 0 &&
                std::count(disabled.begin(), disabled.end(), wxColour(128, 128, 255)) > 0 &&
                MarkerPixels(editor, 0) == outer,
            "disabling block highlight must restore blue markers without changing the outer block");
    NativeSearch::Command(frame, highlight);
    Require(MarkerPixels(editor, 1) == selected,
            "reenabling at a stationary caret must restore the selected folding marker paint");
}
void FoldGeometryAndMarkers(EditorFrame* frame, intptr_t width) {
    auto* editor = frame->Editor();
    for (const int line : {0, 2, 4, 5}) {
        editor->Send(Message::GotoPos, editor->Send(Message::PositionFromLine, line));
        const auto outerMarker = MarkerPixels(editor, 0);
        const auto nestedMarker = MarkerPixels(editor, 1);
        const auto red = [](const auto& pixels) {
            return std::count(pixels.begin(), pixels.end(), wxColour(255, 0, 0));
        };
        std::printf("caret line %d: outer red=%lld nested red=%lld\n", line, static_cast<long long>(red(outerMarker)),
                    static_cast<long long>(red(nestedMarker)));
        Require(line == 2   ? red(nestedMarker) > 0 && red(outerMarker) == 0
                : line == 5 ? red(nestedMarker) == 0 && red(outerMarker) == 0
                            : red(outerMarker) > 0 && red(nestedMarker) > 0,
                "native marker emphasis must follow nested header, body, closing and outside carets");
    }
    frame->SetClientSize(frame->FromDIP(wxSize(620, 420)));
    NativeFold::Paint(editor);
    Require(editor->Send(Message::GetMarginWidthN, 2) == width,
            "native window resize must retain measured folding width");
    const int numbers = frame->GetMenuBar()->FindMenuItem("View", "Line Numbers");
    NativeSearch::Command(frame, numbers);
    NativeFold::MarginClick(editor, 1, "fold hit geometry must follow hidden line numbers");
    Require(!editor->Send(Message::GetLineVisible, 2), "fold hit after hiding line numbers must reach nested header");
    NativeSearch::Command(frame, numbers);
}
void ReadOnlyLanguageFolds(EditorFrame* frame, int show) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* language;
        const char* text;
        bool header;
    };
    const std::array<Fixture, 4> fixtures = {{
        {"C/C++", "void outer() {\n  if (true) {\n    call();\n  }\n}\nafter();\n", true},
        {"Python", "class Outer:\n    def inner(self):\n        call()\n    tail()\nafter()\n", true},
        {"JSON", "{\n  \"items\": [\n    1,\n    2\n  ]\n}\n", true},
        {"Plain Text", "plain\nlast", false},
    }};
    for (const auto& fixture : fixtures) {
        using enum Scintilla::Message;
        editor->Send(SetText, 0, reinterpret_cast<intptr_t>(fixture.text));
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", fixture.language));
        NativeFold::Paint(editor);
        editor->Send(EmptyUndoBuffer);
        editor->Send(SetSavePoint);
        editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("a"));
        editor->Send(BeginUndoAction);
        editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("b"));
        editor->Send(EndUndoAction);
        NativeSearch::Command(frame, wxID_UNDO);
        editor->Send(SetSel, 2, 1);
        const auto dirtyText = NativeSearch::Text(editor);
        const auto revision = editor->DocumentRevision();
        const auto undo = editor->Send(CanUndo);
        const auto redo = editor->Send(CanRedo);
        Require(undo && redo && editor->Send(GetModify), "fold preference history fixture must have undo and redo");
        editor->Send(SetReadOnly, 1);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "All Levels"));
        if (fixture.header) Require(!editor->Send(GetLineVisible, 2), "language fixture must contract nested content");
        const auto caret = editor->Send(GetCurrentPos);
        const auto anchor = editor->Send(GetAnchor);
        NativeSearch::Command(frame, show);
        for (intptr_t line = 0; line < editor->Send(GetLineCount); ++line)
            Require(editor->Send(GetLineVisible, line),
                    "hidden folding must reveal all supported and header-free content");
        NativeSearch::Command(frame, show);
        std::printf(
            "%s selection=%lld/%lld->%lld/%lld undo=%lld->%lld redo=%lld->%lld dirty=%lld "
            "readonly=%lld revision=%llu/%llu bytes=%d\n",
            fixture.language, static_cast<long long>(caret), static_cast<long long>(anchor),
            static_cast<long long>(editor->Send(GetCurrentPos)), static_cast<long long>(editor->Send(GetAnchor)),
            static_cast<long long>(undo), static_cast<long long>(editor->Send(CanUndo)), static_cast<long long>(redo),
            static_cast<long long>(editor->Send(CanRedo)), static_cast<long long>(editor->Send(GetModify)),
            static_cast<long long>(editor->Send(GetReadOnly)), static_cast<unsigned long long>(revision),
            static_cast<unsigned long long>(editor->DocumentRevision()), NativeSearch::Text(editor) == dirtyText);
        Require(editor->Send(GetCurrentPos) == caret && editor->Send(GetAnchor) == anchor &&
                    editor->Send(GetReadOnly) && editor->Send(GetModify) && !editor->Send(CanUndo) &&
                    !editor->Send(CanRedo) && editor->DocumentRevision() == revision &&
                    NativeSearch::Text(editor) == dirtyText,
                "fold visibility must preserve read-only selection, dirty bytes and existing history");
        editor->Send(SetReadOnly, 0);
        Require(editor->Send(CanUndo) == undo && editor->Send(CanRedo) == redo,
                "original read-only history gates must reopen with the unchanged undo/redo stack");
        NativeSearch::Command(frame, wxID_UNDO);
        Require(NativeSearch::Text(editor) == fixture.text && !editor->Send(GetModify),
                "Undo after visibility changes must still reach the original fixture save point");
        NativeSearch::Command(frame, wxID_REDO);
        Require(NativeSearch::Text(editor) == dirtyText,
                "Redo after visibility changes must restore the exact dirty fixture bytes");
    }
}
class FoldViewApp final : public wxApp {
public:
    bool OnInit() override {
#ifdef __WXMSW__
        if (wxGetenv("WXNOTEPAD_FOLDING_DPI96"))
            Require(SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE) != nullptr,
                    "create the complementary emulated 96-DPI folding window");
#endif
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                using enum Scintilla::Message;
                Require(ready, "native folding window must be presented");
                auto* editor = frame->Editor();
                Require(editor->Send(FoldDisplayTextGetStyle) ==
                            std::to_underlying(Scintilla::FoldDisplayTextStyle::Boxed),
                        "collapsed headers must use the original boxed fold display text");
                std::array<char, 16> ellipsis{};
                editor->Send(GetDefaultFoldDisplayText, ellipsis.size(), reinterpret_cast<intptr_t>(ellipsis.data()));
                Require(std::string_view(ellipsis.data()) == "\xC2\xB7\xC2\xB7\xC2\xB7",
                        "UTF-8 editor storage must use the original three middle dots");
                const int show = frame->GetMenuBar()->FindMenuItem("View", "Show Code Folding");
                const int highlight = frame->GetMenuBar()->FindMenuItem("View", "Highlight Current Block");
                Require(show != wxNOT_FOUND && highlight != wxNOT_FOUND,
                        "original native folding visibility and block highlight commands must exist");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsChecked(show) && frame->GetMenuBar()->IsChecked(highlight),
                        "both original folding preferences must default enabled");
                const char* source = "void outer() {\n  if (true) {\n    call();\n  }\n}\nafter();\n";
                editor->Send(SetText, 0, reinterpret_cast<intptr_t>(source));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
                EllipsisStyle(editor);
                NativeFold::Paint(editor);
                editor->Send(EmptyUndoBuffer);
                editor->Send(SetSavePoint);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "All Levels"));
                Require(!editor->Send(GetLineVisible, 2), "fixture must contain hidden nested content");
                NativeSearch::Command(frame, show);
                Require(editor->Send(GetMarginWidthN, 2) == 0, "hiding folding must remove its actual margin");
                for (intptr_t line = 0; line < editor->Send(GetLineCount); ++line)
                    Require(editor->Send(GetLineVisible, line), "hiding folding must expand every line");
                Require(editor->Send(GetFoldExpanded, 0) && editor->Send(GetFoldExpanded, 1),
                        "hiding folding must clear the nested collapse map");
                NativeSearch::Command(frame, show);
                Require(editor->Send(GetFoldExpanded, 0) && editor->Send(GetFoldExpanded, 1) &&
                            !editor->Send(GetModify) && !editor->Send(CanUndo) && NativeSearch::Text(editor) == source,
                        "showing folding must retain expanded content, bytes and history");
                const auto width = editor->Send(GetMarginWidthN, 2);
                Require(width == editor->Send(
                                     TextWidth,
                                     static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::LineNumber)),
                                     reinterpret_cast<intptr_t>("+_")),
                        "fold margin must use the original line-number-font +_ measurement");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Zoom In"));
                NativeFold::Paint(editor);
                Require(editor->Send(GetMarginWidthN, 2) > width,
                        "native zoom must grow the actual folding hit geometry");
                NativeFold::MarginClick(editor, 1, "zoomed folding margin must remain clickable");
                Require(!editor->Send(GetLineVisible, 2), "zoomed native margin hit must fold its header");
                NativeSearch::RevealAllFolds(frame);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Reset Zoom"));
                editor->Send(GotoPos, editor->Send(PositionFromLine, 2));
                NativeFold::Paint(editor);
                BlockHighlightPaint(frame, highlight);
                NativeKeys::Shortcut(frame, 'F', true, true, true);
                Require(editor->Send(GetMarginWidthN, 2) == 0, "native Ctrl+Shift+Alt+F must hide folding");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("View", "All Levels")),
                        "original fold commands must remain enabled while folding is hidden");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "All Levels"));
                Require(editor->Send(GetLineVisible, 2),
                        "fold commands while hidden must retain the original no-op behavior");
                NativeKeys::Shortcut(frame, 'F', true, true, true);
                Require(editor->Send(GetMarginWidthN, 2) == width,
                        "native Ctrl+Shift+Alt+F must restore folding geometry");
                FoldGeometryAndMarkers(frame, width);
                ReadOnlyLanguageFolds(frame, show);
                std::printf("fold geometry: dpi=%d zoom=%lld width=%lld\n", editor->GetDPI().y,
                            static_cast<long long>(editor->Send(GetZoom)),
                            static_cast<long long>(editor->Send(GetMarginWidthN, 2)));
                std::puts("PASS: native folding toggles preserve document and expand hidden content");
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
wxIMPLEMENT_APP(FoldViewApp);
