#include "diagnostics/print.h"
#include "native_renderer_helpers.h"
#include "native_completion_helpers.h"
#include "native_paint_helpers.h"
#include <wx/button.h>
#include <wx/popupwin.h>
#include <wx/vlbox.h>
#include <wx/display.h>
#include <cstdio>
#include <print>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw TestEditor::Failure(message);
}
wxVListBox* CompletionList(wxWindow* parent) {
    for (auto* child : parent->GetChildren()) {
        if (auto* list = wxDynamicCast(child, wxVListBox)) return list;
        if (auto* list = CompletionList(child)) return list;
    }
    return nullptr;
}
void CheckInitialPopup(EditorControl* editor) {
    using enum Scintilla::Message;
    TestEditor::ResetStream(editor, "al", 2, 2);
    editor->Send(AutoCSetSeparator, 32);
    editor->Send(AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine"));
    wxYield();
    auto* list = CompletionList(editor);
    Require(list && list->GetParent()->IsTopLevel(), "completion uses a native top-level popup");
    Require(!list->AcceptsFocus() && !list->GetParent()->AcceptsFocus(),
            "completion list and popup preserve editor focus");
    const auto popup = list->GetParent()->GetScreenRect();
    const auto caret = editor->ClientToScreen(wxPoint(static_cast<int>(editor->Send(PointXFromPosition, 0, 2)),
                                                      static_cast<int>(editor->Send(PointYFromPosition, 0, 2))));
    Require(popup.y >= caret.y && popup.x <= caret.x, "popup aligns below the completion root");
    Require(popup.height > editor->Send(TextHeight, 0), "popup sizes native item rows");
    const auto focus = wxWindow::FindFocus();
    NativeCompletion::DoubleClick(list,
                                  list->GetItemRect(1).GetPosition() + wxPoint(8, list->GetItemRect(1).height / 2));
    Require(TestEditor::Text(editor) == "alpine" && !editor->Send(AutoCActive),
            "native mouse double-click confirms selected candidate");
    Require(wxWindow::FindFocus() == focus, "mouse confirmation preserves keyboard focus");
    editor->Send(Undo);
    Require(TestEditor::Text(editor) == "al", "mouse completion is one Undo step");
    editor->Send(AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine"));
#ifdef __WXMSW__
    NativeCompletion::Key(editor, VK_ESCAPE);
#else
    NativeCompletion::Key(editor, GDK_KEY_Escape);
#endif
    Require(!editor->Send(AutoCActive) && TestEditor::Text(editor) == "al",
            "native Escape cancels without changing document");
}
void CheckPopupWorkArea(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const auto bounds = wxDisplay(frame).GetClientArea();
#ifdef __WXGTK__
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(GTK_WIDGET(frame->GetHandle())))) {
        // Wayland clients cannot position top-level windows. Fullscreen
        // places the owned editor's last visible line at the output edge.
        frame->ShowFullScreen(true);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (frame->GetClientSize().y < bounds.height && std::chrono::steady_clock::now() < deadline) {
            wxYield();
            wxMilliSleep(5);
        }
        Require(frame->GetClientSize().y >= bounds.height, "Wayland compositor applies native fullscreen geometry");
    } else
#endif
        frame->SetSize(bounds.GetRight() - 460, bounds.GetBottom() - 260, 460, 260);
    wxYield();
    std::string scrolled;
    for (int line = 0; line < 80; ++line)
        scrolled += std::string(80, 'x') + char(10);
    scrolled += std::string(180, 'x') + "al";
    TestEditor::ResetStream(editor, scrolled.c_str(), scrolled.size(), scrolled.size());
    editor->Send(GotoPos, scrolled.size());
    wxYield();
    editor->Send(AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine alphabetically_long_candidate"));
    wxYield();
    auto* list = CompletionList(editor);
    const auto scrolledPopup = list ? list->GetParent()->GetScreenRect() : wxRect();
    if (!list || !bounds.Contains(scrolledPopup)) {
        const auto frameRect = frame->GetScreenRect();
        const auto editorRect = editor->GetScreenRect();
        const auto position = editor->Send(GetCurrentPos);
        const auto caret =
            editor->ClientToScreen(wxPoint(static_cast<int>(editor->Send(PointXFromPosition, 0, position)),
                                           static_cast<int>(editor->Send(PointYFromPosition, 0, position))));
        std::println(stderr,
                     "completion boundary: bounds={:d},{:d},{:d},{:d} popup={:d},{:d},{:d},{:d} list={:p} active={:d} "
                     "frame={:d},{:d},{:d},{:d} editor={:d},{:d},{:d},{:d} caret={:d},{:d} DPI={:d},{:d} scale={:.6g} "
                     "first={:d} xOffset={:d}",
                     bounds.x, bounds.y, bounds.width, bounds.height, scrolledPopup.x, scrolledPopup.y,
                     scrolledPopup.width, scrolledPopup.height, static_cast<void*>(list), editor->Send(AutoCActive),
                     frameRect.x, frameRect.y, frameRect.width, frameRect.height, editorRect.x, editorRect.y,
                     editorRect.width, editorRect.height, caret.x, caret.y, editor->GetDPI().x, editor->GetDPI().y,
                     editor->GetContentScaleFactor(), editor->Send(GetFirstVisibleLine), editor->Send(GetXOffset));
    }
    Require(list && bounds.Contains(scrolledPopup), "scrolled popup fits monitor work area at bottom-right edge");
    const auto current = editor->Send(GetCurrentPos);
    const auto caretY =
        editor->ClientToScreen(wxPoint(0, static_cast<int>(editor->Send(PointYFromPosition, 0, current)))).y;
    Require(list->GetParent()->GetScreenRect().GetBottom() <= caretY, "popup opens above caret near monitor bottom");
    editor->Send(AutoCCancel);
    Require(!editor->Send(AutoCActive), "cancel ends completion lifetime");
}
void CheckPopupWheelAndFocus(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
#ifdef __WXGTK__
    // Finish native allocation and clamp before saving the wheel baseline;
    // the core's deferred widening timer also performs this clamp.
    editor->SendSizeEvent();
    NativePaint::Complete(editor);
    editor->Send(LineScroll, 0, 0);
#endif
    const auto firstLine = editor->Send(GetFirstVisibleLine);
    editor->Send(AutoCSetMaxHeight, 4);
    editor->Send(AutoCShow, 2,
                 reinterpret_cast<intptr_t>("alpha alpine amber amble ample anchor ancient angel anger "
                                            "angle animal annual answer antenna antique"));
    auto* list = CompletionList(editor);
#ifdef __WXMSW__
    NativeCompletion::Key(editor, VK_DOWN);
#else
    NativeCompletion::Key(editor, GDK_KEY_Down);
#endif
    Require(list && list->IsRowVisible(0), "native Down keeps earlier visible rows while the new selection fits");
    editor->Send(AutoCSelect, 0, reinterpret_cast<intptr_t>("antique"));
    const auto lineBeforeWheel = editor->Send(GetFirstVisibleLine);
    const auto activeBeforeWheel = editor->Send(AutoCActive);
    auto* focusBeforeWheel = wxWindow::FindFocus();
    NativeCompletion::Wheel(editor);
    const auto lineAfterWheel = editor->Send(GetFirstVisibleLine);
    const auto activeAfterWheel = editor->Send(AutoCActive);
    if (lineAfterWheel != firstLine || !activeAfterWheel)
        std::println(stderr,
                     "completion wheel: first={:d} before={:d} after={:d} active={:d},{:d} "
                     "focus={:p},{:p} editor={:p} list={:p}",
                     firstLine, lineBeforeWheel, lineAfterWheel, activeBeforeWheel, activeAfterWheel,
                     static_cast<void*>(focusBeforeWheel), static_cast<void*>(wxWindow::FindFocus()),
                     static_cast<void*>(editor), static_cast<void*>(list));
    Require(editor->Send(GetFirstVisibleLine) == firstLine && editor->Send(AutoCActive),
            "native wheel scrolls the active completion list while document stays fixed");
    editor->Send(AutoCCancel);
    auto* focusTarget = new wxButton(frame, wxID_ANY, "Focus target");
    editor->SetFocus();
    wxYield();
    editor->Send(AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine"));
    focusTarget->SetFocus();
    wxYield();
    Require(!editor->Send(AutoCActive), "native focus loss ends popup lifetime");
}
void CheckPopup(EditorFrame* frame) {
    auto* editor = frame->Editor();
#ifdef __WXGTK__
    // Start popup assertions with the editor's acknowledged native focus.
    wxString focusFile;
    NativeKeys::FocusEditor(frame, "wxnotepad", focusFile);
    NativePaint::Complete(editor);
    // Freeze the assertion, preserving its short-circuited core query, before
    // collecting any failure-only observations.
    const bool wxHasFocus = editor->HasFocus();
    const auto coreFocus = wxHasFocus ? editor->Send(Message::GetFocus) : 0;
    const bool focusReady = wxHasFocus && coreFocus;
    if (!focusReady) {
        std::println(stderr,
                     "[completion-focus] predicate mono_us={} ownedPid={} wx_has_focus={:d} "
                     "core_focus_evaluated={:d} core_focus={}",
                     g_get_monotonic_time(), wxGetProcessId(), wxHasFocus, wxHasFocus, coreFocus);
        auto* frameWidget = GTK_WIDGET(frame->GetHandle());
        auto* editorWidget = editor->GetConnectWidget();
        std::println(stderr,
                     "[completion-focus] end observation wx_has_focus={:d} core_focus={} wx_focus={} "
                     "editor={} connect_widget={} gtk_focus_widget={} gtk_has_focus={:d} gtk_is_focus={:d} "
                     "gtk_window_active={:d}",
                     editor->HasFocus(), editor->Send(Message::GetFocus), static_cast<void*>(wxWindow::FindFocus()),
                     static_cast<void*>(editor), static_cast<void*>(editorWidget),
                     static_cast<void*>(gtk_window_get_focus(GTK_WINDOW(frameWidget))),
                     gtk_widget_has_focus(editorWidget), gtk_widget_is_focus(editorWidget),
                     gtk_window_is_active(GTK_WINDOW(frameWidget)));
        std::fflush(stderr);
        // FocusEditor returned with its ACK. This is a later postcondition,
        // not an activation timeout; no original focus deadline applies here.
        NativeKeys::ObserveFocusFailure(frame, frameWidget, focusFile, "completion-after-paint", {}, true, 0, 0);
    }
    Require(focusReady, "editor owns native focus after presentation settles");
#endif
    CheckInitialPopup(editor);
    CheckPopupWorkArea(frame);
    CheckPopupWheelAndFocus(frame);
    std::print("PASS: native completion popup placement, nonfocusable list, cancellation; DPI={:d}x{:d}",
               editor->GetDPI().x, editor->GetDPI().y);
}
class CompletionPopupApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        TestRenderer::Select(frame->Editor());
#ifdef __WXMSW__
        frame->ShowWithoutActivating();
#else
        frame->Show();
#endif
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native compositor presents completion test editor");
                CheckPopup(frame);
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                result_ = 1;
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
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(CompletionPopupApp);
