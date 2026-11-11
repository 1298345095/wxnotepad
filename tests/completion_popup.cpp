#include "native_search_helpers.h"
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
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                TestEditor::ResetStream(editor, "al", 2, 2);
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
                editor->Send(Message::AutoCCancel);
                Require(!editor->Send(Message::AutoCActive), "cancel ends completion lifetime");
                std::puts("PASS: native completion popup placement, nonfocusable list, cancellation");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override { wxApp::OnRun(); return result_; }
private:
    int result_ = 0;
};
}
wxIMPLEMENT_APP(CompletionPopupApp);
