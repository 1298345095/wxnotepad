#include "native_search_helpers.h"
#include <wx/vlbox.h>
namespace {
using Scintilla::Message;
void Require(bool value, const char* message) { if (!value) throw TestEditor::Failure(message); }
class WordCompletionApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                TestEditor::ResetStream(editor, "alpha al", 8, 8);
                const auto id = frame->GetMenuBar()->FindMenuItem("Edit", "Complete Word");
                Require(id != wxNOT_FOUND, "Edit > Special > Complete Word exists");
                NativeSearch::Command(frame, id);
                Require(TestEditor::Text(editor) == "alpha alpha", "manual completion inserts original sole candidate");
                TestEditor::RequireSelection(editor, 11, 11, "single candidate caret");
                Require(!editor->Send(Message::AutoCActive), "single candidate has no popup");
                editor->Send(Message::Undo);
                Require(TestEditor::Text(editor) == "alpha al" && !editor->Send(Message::CanUndo), "one Undo restores completion");
                std::puts("PASS: original Complete Word single-candidate command and Undo");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what()); result_ = 1;
            }
            frame->Destroy(); ExitMainLoop();
        }); return true;
    }
    int OnRun() override { wxApp::OnRun(); return result_; }
private: int result_ = 0;
};
}
wxIMPLEMENT_APP(WordCompletionApp);
