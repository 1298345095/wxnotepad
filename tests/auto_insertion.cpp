#include "diagnostics/print.h"
#include "native_renderer_helpers.h"
#include "native_search_helpers.h"
#include "syntax/syntax_scheme.h"
#include "document/document_session.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <wx/timer.h>
#include <initializer_list>
#include "auto_insertion_reference.h"
#include <cstdio>
#include <print>
#include <stdexcept>
#include <utility>
namespace {
void Character(EditorControl* editor, int ch) {
    wxKeyEvent event(wxEVT_CHAR);
    event.m_keyCode = ch;
    event.m_uniChar = ch;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void State(EditorControl* editor, const AutoInsertionReference::State& state) {
    using enum Scintilla::Message;
    if (NativeSearch::Text(editor) != state.text) {
        std::println(stderr, "actual='{}' expected='{}'", NativeSearch::Text(editor).c_str(), state.text);
        throw std::runtime_error("character notification must reproduce original text");
    }
    if (editor->Send(GetAnchor) != state.anchor || editor->Send(GetCurrentPos) != state.caret)
        throw std::runtime_error("original anchor and caret direction");
    if (editor->Send(GetSelections) != static_cast<intptr_t>(state.selections.size()))
        throw std::runtime_error("original selection count");
    size_t index = 0;
    for (const auto& selection : state.selections) {
        if (editor->Send(GetSelectionNAnchor, index) != selection.anchor ||
            editor->Send(GetSelectionNCaret, index) != selection.caret)
            throw std::runtime_error("original additional selection positions");
        ++index;
    }
    if (bool(editor->Send(GetModify)) != state.modified || bool(editor->Send(CanUndo)) != state.canUndo ||
        bool(editor->Send(CanRedo)) != state.canRedo)
        throw std::runtime_error("original modification and history state");
}
class AutoInsertionApp final : public wxApp {
public:
    bool OnInit() override {
        frame_ = new EditorFrame;
        TestRenderer::Select(frame_->Editor());
        frame_->ShowWithoutActivating();
        timer_.SetOwner(this);
        Bind(wxEVT_TIMER, [this](wxTimerEvent&) { Step(); }, timer_.GetId());
        CallAfter([this] { Step(); });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    void Step() {
        try {
            using enum Scintilla::Message;
            auto* editor = frame_->Editor();
            const auto& item = AutoInsertionReference::cases[next_];
            std::println("case {:d} language={:d} ch={:d}", next_++,
                         static_cast<int>(std::to_underlying(item.language)), item.character);
            editor->Send(SetReadOnly, 0);
            ApplySyntaxLanguage(*editor, item.language);
            editor->Send(SetUndoSelectionHistory, 0);
            editor->Send(SetText, 0, reinterpret_cast<intptr_t>(item.before));
            editor->Send(SetSel, item.anchor, item.position);
            if (item.extraCaret >= 0) editor->Send(AddSelection, item.extraCaret, item.extraAnchor);
            editor->Send(SetOvertype, item.overtype);
            editor->Send(EmptyUndoBuffer);
            editor->Send(SetSavePoint);
            editor->Send(SetUndoSelectionHistory, 3);
            editor->Send(Colourise, 0, -1);
            editor->Send(SetReadOnly, item.readOnly);
            Character(editor, item.character);
            State(editor, item.after);
            for (const auto& state : item.undo) {
                editor->Send(Undo);
                State(editor, state);
            }
            for (const auto& state : item.redo) {
                editor->Send(Redo);
                State(editor, state);
            }
            DocumentSession document(*editor);
            const auto path = TestFiles::TemporaryDirectory("auto-insertion.txt");
            if (document.SaveCopy(TestFiles::Filename(path)).status != SaveStatus::Saved ||
                TestFiles::Bytes(path) != item.saved)
                throw std::runtime_error("saved bytes reproduce independently captured original file");
            if (next_ < AutoInsertionReference::cases.size()) {
                // A native timer returns to the GUI loop between independent
                // cases so painting, input and accessibility requests can run.
                if (!timer_.StartOnce(1)) throw std::runtime_error("schedule the next automatic-insertion case");
                return;
            }
            editor->Send(SetReadOnly, 0);
            std::puts("PASS: original character notifications, styles, read-only state and Undo/Redo");
            result_ = 0;
        } catch (const std::exception& error) {
            DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
        }
        timer_.Stop();
        frame_->Destroy();
        ExitMainLoop();
    }
    EditorFrame* frame_ = nullptr;
    wxTimer timer_;
    size_t next_ = 0;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(AutoInsertionApp);
