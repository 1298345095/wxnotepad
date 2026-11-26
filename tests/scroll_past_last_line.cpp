#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include <print>
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include <array>

namespace {
using enum Scintilla::Message;
constexpr std::array<const char*, 5> Labels = {"One Page", "No", "Half Page", "1/3 Page", "1/4 Page"};
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Choose(EditorFrame* frame, int mode) {
    const int id = frame->GetMenuBar()->FindMenuItem("Settings", Labels[mode]);
    Require(id != wxNOT_FOUND, "Settings Appearance exposes all original Scroll Past Last Line modes");
    Require(frame->GetMenuBar()->FindItem(id)->IsRadio(), "scroll choices are native radio items");
    NativeSearch::Command(frame, id);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    for (int index = 0; index < static_cast<int>(Labels.size()); ++index) {
        const int choice = frame->GetMenuBar()->FindMenuItem("Settings", Labels[index]);
        Require(choice != wxNOT_FOUND && frame->GetMenuBar()->IsChecked(choice) == (index == mode),
                "native radio state reports the active integer mode");
    }
}
void Modes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    Require(editor->Send(GetEndAtLastLine) == 1, "original default is No");
    std::string text;
    for (int line = 0; line < 200; ++line)
        text += "line\n";
    TestEditor::ResetStream(editor, text.c_str(), 12, 3);
    for (int mode = 0; mode < static_cast<int>(Labels.size()); ++mode) {
        Choose(frame, mode);
        Require(editor->Send(GetEndAtLastLine) == mode, "menu retains all five integer modes at the core boundary");
        Require(TestEditor::Text(editor) == text && !editor->Send(GetModify) && !editor->Send(CanUndo),
                "scroll modes preserve document bytes and saved state");
        TestEditor::RequireSelection(editor, 12, 3, "scroll modes preserve reversed selection");
    }
}
// Literal results from Notepad4 89b8b36 Editor::MaxScrollPos with a 24-row page.
constexpr int PageRows = 24;
constexpr std::array<intptr_t, 5> LongLimits = {200, 177, 189, 185, 183};
constexpr std::array<intptr_t, 5> FoldedLimits = {100, 77, 89, 85, 83};
void Page(EditorControl* editor, int rows, int width = 600) {
    editor->SetClientSize(width, rows * static_cast<int>(editor->Send(TextHeight, 0)));
    NativePaint::Complete(editor);
    Require(editor->Send(LinesOnScreen) == rows, "fixture presents the original fixed page height");
}
void Bottom(EditorControl* editor) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_VSCROLL, SB_BOTTOM, 0);
#elif defined(__WXGTK__)
    auto* scrollbar = editor->m_scrollBar[wxWindow::ScrollDir_Vert];
    Require(scrollbar != nullptr, "native vertical scrollbar exists");
    auto* adjustment = gtk_range_get_adjustment(scrollbar);
    gtk_range_set_value(scrollbar, gtk_adjustment_get_upper(adjustment));
#endif
    wxYield();
    editor->Update();
}
void Limits(EditorFrame* frame, const std::array<intptr_t, 5>& expected) {
    auto* editor = frame->Editor();
    for (int mode = 0; mode < static_cast<int>(Labels.size()); ++mode) {
        Choose(frame, mode);
        // A GTK resize/wrap allocation can follow menu dispatch. Observe the
        // settled native paint before scrolling, as in the resize fixtures.
        NativePaint::Complete(editor);
        const auto beforeScroll = editor->Send(GetFirstVisibleLine);
        Bottom(editor);
        const auto actual = editor->Send(GetFirstVisibleLine);
        if (actual != expected[mode]) {
            std::println(stderr, "mode={:d} before={:d} actual={:d} expected={:d} rows={:d} lines={:d} dpi={:d}", mode,
                         static_cast<long long>(beforeScroll), static_cast<long long>(actual),
                         static_cast<long long>(expected[mode]), static_cast<long long>(editor->Send(LinesOnScreen)),
                         static_cast<long long>(editor->Send(GetLineCount)), editor->GetDPI().y);
#ifdef __WXGTK__
            auto* adjustment = gtk_range_get_adjustment(editor->m_scrollBar[wxWindow::ScrollDir_Vert]);
            std::println(stderr, "native value={:.6g} upper={:.6g} page={:.6g}", gtk_adjustment_get_value(adjustment),
                         gtk_adjustment_get_upper(adjustment), gtk_adjustment_get_page_size(adjustment));
#endif
        }
        Require(actual == expected[mode], "native scrollbar reaches the original mode-specific display-line limit");
    }
}
void Geometry(EditorFrame* frame) {
    auto* editor = frame->Editor();
    if (editor->Send(GetWrapMode)) NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Word Wrap"));
    Page(editor, PageRows);
    TestEditor::ResetStream(editor, "");
    Limits(frame, {0, 0, 0, 0, 0});
    std::string text;
    for (int line = 0; line < 19; ++line)
        text += "line\n";
    TestEditor::ResetStream(editor, text.c_str());
    Limits(frame, {19, 0, 8, 4, 2});
    for (int line = 19; line < 200; ++line)
        text += "line\n";
    TestEditor::ResetStream(editor, text.c_str());
    Limits(frame, LongLimits);
    std::string addition;
    for (int line = 0; line < 50; ++line)
        addition += "added\n";
    editor->Send(AppendText, addition.size(), reinterpret_cast<intptr_t>(addition.c_str()));
    Limits(frame, {250, 227, 239, 235, 233});
    editor->Send(Undo);
    Limits(frame, LongLimits);
    Page(editor, 36);
    Limits(frame, {200, 165, 183, 177, 174});
    Page(editor, PageRows);
    editor->Send(SetZoom, 150);
    Page(editor, PageRows);
    Limits(frame, LongLimits);
    editor->Send(SetZoom, 100);
    Page(editor, PageRows);
    wxDPIChangedEvent dpi(editor->GetDPI(), editor->GetDPI());
    dpi.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(dpi);
    Limits(frame, LongLimits);

    // Native folding removes 100 display rows without changing the document.
    const auto base = static_cast<intptr_t>(Scintilla::FoldLevel::Base);
    editor->Send(SetFoldLevel, 0, base | static_cast<intptr_t>(Scintilla::FoldLevel::HeaderFlag));
    for (int line = 1; line <= 100; ++line)
        editor->Send(SetFoldLevel, line, base + 1);
    editor->Send(SetFoldLevel, 101, base);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "All Levels"));
    Require(!editor->Send(GetLineVisible, 100) && editor->Send(GetLineVisible, 101),
            "native collapse hides 100 child rows");
    Limits(frame, FoldedLimits);
    NativeSearch::RevealAllFolds(frame);
    Limits(frame, LongLimits);

    // A 41-glyph viewport puts each 80-glyph line on exactly two wrapped rows.
    editor->SetLineNumbersVisible(false);
    editor->SetBookmarkMarginVisible(false);
    editor->SetCodeFoldingVisible(false);
    editor->Send(SetMarginLeft, 0, 0);
    editor->Send(SetMarginRight, 0, 0);
    const std::string row(80, 'x');
    const std::string viewport(41, 'x');
    const int width = static_cast<int>(editor->Send(TextWidth, std::to_underlying(Scintilla::StylesCommon::Default),
                                                    reinterpret_cast<intptr_t>(viewport.c_str())));
    text.clear();
    for (int line = 0; line < 100; ++line)
        text += row + "\n";
    TestEditor::ResetStream(editor, text.c_str());
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Word Wrap"));
    Page(editor, PageRows, width);
    Require(editor->Send(WrapCount, 0) == 2, "original wrap fixture displays two rows per logical line");
    Limits(frame, LongLimits);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Word Wrap"));
    Page(editor, PageRows);
    Limits(frame, FoldedLimits);

    // Edits and history continue to work after changing every display-only mode.
    const auto encoding = frame->DocumentEncoding();
    TestEditor::ResetStream(editor, "abc\ndef", 3, 1);
    editor->Send(ReplaceSel, 0, reinterpret_cast<intptr_t>("edited"));
    const auto edited = TestEditor::Text(editor);
    for (int mode = 0; mode < static_cast<int>(Labels.size()); ++mode) {
        Choose(frame, mode);
        Require(TestEditor::Text(editor) == edited && frame->DocumentEncoding() == encoding &&
                    editor->Send(GetModify) && editor->Send(CanUndo),
                "mode changes preserve modified text, encoding and Undo");
    }
    editor->Send(Undo);
    for (int mode = 0; mode < static_cast<int>(Labels.size()); ++mode)
        Choose(frame, mode);
    Require(TestEditor::Text(editor) == "abc\ndef" && !editor->Send(GetModify) && editor->Send(CanRedo),
            "mode changes preserve the saved state and Redo");
    editor->Send(Redo);
    Require(TestEditor::Text(editor) == edited && editor->Send(GetModify), "Redo survives all scroll modes");
    Limits(frame, {1, 0, 0, 0, 0});
#ifdef __WXGTK__
    // GTK can allocate a new width after the explicit native scroll. Resizing
    // without changing wrapped row counts must retain the viewport, even when
    // the caret is at the start of the document and outside that viewport.
    // Run after the original geometry/history fixture to preserve its setup.
    TestEditor::ResetStream(editor, text.c_str());
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Word Wrap"));
    Page(editor, PageRows, width);
    Require(editor->Send(WrapCount, 0) == 2, "native resize fixture starts with the original two wrapped rows");
    const auto anchor = editor->Send(GetAnchor);
    const auto caret = editor->Send(GetCurrentPos);
    constexpr int allocationWidthChangeDip = 6;
    for (int mode = 0; mode < static_cast<int>(Labels.size()); ++mode) {
        Choose(frame, mode);
        NativePaint::Complete(editor);
        Bottom(editor);
        Require(editor->Send(GetFirstVisibleLine) == LongLimits[mode],
                "wrapped scrollbar reaches original limit before resize");
        Page(editor, PageRows, width + editor->FromDIP(allocationWidthChangeDip));
        Require(editor->Send(WrapCount, 0) == 2, "native width change keeps the original two wrapped rows");
        Require(editor->Send(GetFirstVisibleLine) == LongLimits[mode],
                "wrapped native resize retains the explicitly scrolled viewport");
        Require(editor->Send(GetAnchor) == anchor && editor->Send(GetCurrentPos) == caret,
                "wrapped native resize preserves the offscreen selection");
        Page(editor, PageRows, width);
    }
#endif
    std::puts("PASS: original native scroll limits, empty/short/long documents, resize, zoom, DPI notification, "
              "folding, wrapping and history");
}
class ScrollApp final : public wxApp {
public:
    bool OnInit() override {
        if (argc > 1) settings_ = std::make_unique<SettingsStore>();
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "scroll mode window is presented");
                if (settings_) {
                    Require(frame->Editor()->Send(GetEndAtLastLine) == wxAtoi(argv[1]),
                            "application restores normalized original EndAtLastLine value");
                    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                    const int checked = frame->GetMenuBar()->FindMenuItem("Settings", Labels[wxAtoi(argv[1])]);
                    Require(checked != wxNOT_FOUND && frame->GetMenuBar()->IsChecked(checked),
                            "reopened native radio reports the imported integer mode");
                    if (argc > 2 && argv[2] != "keep") Choose(frame, wxAtoi(argv[2]));
                    if (argc > 3 && argv[3] == "explicit")
                        NativeSearch::Command(frame,
                                              frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
                    result_ = 0;
                    frame->Close();
                    return;
                }
                Modes(frame);
                Geometry(frame);
                result_ = 0;
                std::puts("PASS: original five native scroll modes");
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(ScrollApp);
