#include "native_renderer_helpers.h"
#include "native_completion_helpers.h"
#include <wx/button.h>
#include <wx/popupwin.h>
#include <wx/vlbox.h>
#include <wx/display.h>

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
                auto* editor = frame->Editor();
                TestEditor::ResetStream(editor, "al", 2, 2);
                editor->Send(Message::AutoCSetSeparator, 32);
                editor->Send(Message::AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine"));
                wxYield();
                auto* list = CompletionList(editor);
                Require(list && list->GetParent()->IsTopLevel(), "completion uses a native top-level popup");
                Require(!list->AcceptsFocus() && !list->GetParent()->AcceptsFocus(),
                        "completion list and popup preserve editor focus");
                const auto popup = list->GetParent()->GetScreenRect();
                const auto caret = editor->ClientToScreen(wxPoint(editor->Send(Message::PointXFromPosition, 0, 2),
                                                                  editor->Send(Message::PointYFromPosition, 0, 2)));
                Require(popup.y >= caret.y && popup.x <= caret.x, "popup aligns below the completion root");
                Require(popup.height > editor->Send(Message::TextHeight, 0), "popup sizes native item rows");
                const auto focus = wxWindow::FindFocus();
                NativeCompletion::DoubleClick(list, list->GetItemRect(1).GetPosition() +
                                                        wxPoint(8, list->GetItemRect(1).height / 2));
                Require(TestEditor::Text(editor) == "alpine" && !editor->Send(Message::AutoCActive),
                        "native mouse double-click confirms selected candidate");
                Require(wxWindow::FindFocus() == focus, "mouse confirmation preserves keyboard focus");
                editor->Send(Message::Undo);
                Require(TestEditor::Text(editor) == "al", "mouse completion is one Undo step");
                editor->Send(Message::AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine"));
#ifdef __WXMSW__
                NativeCompletion::Key(editor, VK_ESCAPE);
#else
                NativeCompletion::Key(editor, GDK_KEY_Escape);
#endif
                Require(!editor->Send(Message::AutoCActive) && TestEditor::Text(editor) == "al",
                        "native Escape cancels without changing document");
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
                    Require(frame->GetClientSize().y >= bounds.height,
                            "Wayland compositor applies native fullscreen geometry");
                } else
#endif
                    frame->SetSize(bounds.GetRight() - 460, bounds.GetBottom() - 260, 460, 260);
                wxYield();
                std::string scrolled;
                for (int line = 0; line < 80; ++line)
                    scrolled += std::string(80, 'x') + char(10);
                scrolled += std::string(180, 'x') + "al";
                TestEditor::ResetStream(editor, scrolled.c_str(), scrolled.size(), scrolled.size());
                editor->Send(Message::GotoPos, scrolled.size());
                wxYield();
                editor->Send(Message::AutoCShow, 2,
                             reinterpret_cast<intptr_t>("alpha alpine alphabetically_long_candidate"));
                wxYield();
                list = CompletionList(editor);
                Require(list && bounds.Contains(list->GetParent()->GetScreenRect()),
                        "scrolled popup fits monitor work area at bottom-right edge");
                const auto current = editor->Send(Message::GetCurrentPos);
                const auto caretY =
                    editor->ClientToScreen(wxPoint(0, editor->Send(Message::PointYFromPosition, 0, current))).y;
                Require(list->GetParent()->GetScreenRect().GetBottom() <= caretY,
                        "popup opens above caret near monitor bottom");
                editor->Send(Message::AutoCCancel);
                Require(!editor->Send(Message::AutoCActive), "cancel ends completion lifetime");
#ifdef __WXGTK__
                // Clamp the completed fixture scroll before saving the wheel baseline;
                // the core's deferred widening timer also performs this clamp.
                editor->Send(Message::LineScroll, 0, 0);
#endif
                const auto firstLine = editor->Send(Message::GetFirstVisibleLine);
                editor->Send(Message::AutoCSetMaxHeight, 4);
                editor->Send(Message::AutoCShow, 2,
                             reinterpret_cast<intptr_t>("alpha alpine amber amble ample anchor ancient angel anger "
                                                        "angle animal annual answer antenna antique"));
                list = CompletionList(editor);
#ifdef __WXMSW__
                NativeCompletion::Key(editor, VK_DOWN);
#else
                NativeCompletion::Key(editor,GDK_KEY_Down);
#endif
                Require(list && list->IsRowVisible(0),
                        "native Down keeps earlier visible rows while the new selection fits");
                editor->Send(Message::AutoCSelect, 0, reinterpret_cast<intptr_t>("antique"));
                const auto lineBeforeWheel = editor->Send(Message::GetFirstVisibleLine);
                const auto activeBeforeWheel = editor->Send(Message::AutoCActive);
                auto* focusBeforeWheel = wxWindow::FindFocus();
                NativeCompletion::Wheel(editor);
                const auto lineAfterWheel = editor->Send(Message::GetFirstVisibleLine);
                const auto activeAfterWheel = editor->Send(Message::AutoCActive);
                if (lineAfterWheel != firstLine || !activeAfterWheel)
                    std::fprintf(stderr,
                                 "completion wheel: first=%zd before=%zd after=%zd active=%zd,%zd "
                                 "focus=%p,%p editor=%p list=%p\n",
                                 firstLine, lineBeforeWheel, lineAfterWheel, activeBeforeWheel, activeAfterWheel,
                                 static_cast<void*>(focusBeforeWheel), static_cast<void*>(wxWindow::FindFocus()),
                                 static_cast<void*>(editor), static_cast<void*>(list));
                Require(editor->Send(Message::GetFirstVisibleLine) == firstLine && editor->Send(Message::AutoCActive),
                        "native wheel scrolls the active completion list while document stays fixed");
                editor->Send(Message::AutoCCancel);
                auto* focusTarget = new wxButton(frame, wxID_ANY, "Focus target");
                editor->SetFocus();
                wxYield();
                editor->Send(Message::AutoCShow, 2, reinterpret_cast<intptr_t>("alpha alpine"));
                focusTarget->SetFocus();
                wxYield();
                Require(!editor->Send(Message::AutoCActive), "native focus loss ends popup lifetime");
                std::printf("PASS: native completion popup placement, nonfocusable list, cancellation; DPI=%dx%d",
                            editor->GetDPI().x, editor->GetDPI().y);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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
