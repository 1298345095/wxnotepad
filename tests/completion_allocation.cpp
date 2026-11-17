#include "editor/word_completion.h"
#include "syntax/syntax_catalog.h"
#include "completion_allocation_fault.h"
#include "native_completion_helpers.h"
#include <array>
#include <cstdio>
#include <exception>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace {
using Scintilla::Message;

void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}

size_t CandidateBufferAllocationSize(size_t size) {
    const auto requested = CompletionAllocationFault::ByteVectorAllocationSize(size);
    Require(requested >= size, "candidate buffer probe observes the allocator's complete backing-storage request");
    return requested;
}

constexpr size_t RootLength = 33;
constexpr unsigned CandidateCount = 200;
constexpr size_t CandidateLength = RootLength + 5;

std::string KeywordFixture(std::string_view root) {
    std::string words;
    for (unsigned index = 0; index < CandidateCount; ++index) {
        words += root;
        std::format_to(std::back_inserter(words), "{:05} ", index);
    }
    return words;
}

struct FailurePoint {
    const char* name;
    size_t size;
    bool document = false;
    bool existingPopup = false;
};

void NativeAllocationFailure(const EditorFrame* frame, const FailurePoint& point) {
    auto* editor = frame->Editor();
    const std::string root(RootLength, 'q');
    const auto keywords = KeywordFixture(root);
    const std::array keywordSets{SyntaxKeywords{keywords.c_str(), 0}};
    auto definition = SyntaxFor(SyntaxLanguage::Cpp);
    definition.keywords = keywordSets;
    WordCompletionSettings settings;
    settings.completeOption = WordCompletionOption::CompleteWord;
    if (point.document)
        settings.completeOption |=
            WordCompletionOption::ScanWordsInDocument | WordCompletionOption::OnlyWordsInDocument;
    const auto before = (point.document ? keywords : std::string{}) + root.substr(0, root.size() - 1);
    const auto after = before + 'q';
    editor->SetCompletionSettings(settings);
    TestEditor::ResetStream(editor, before.c_str(), before.size(), before.size());
    editor->Send(Message::SetStatus, static_cast<uintptr_t>(std::to_underlying(Scintilla::Status::Ok)));
    bool called = false;
    bool injected = false;
    editor->SetWordCompletionHandler(
        [editor, &definition, &settings, &point, &called, &injected](WordCompletionCondition condition) {
            called = true;
            const CompletionAllocationFault::ScopedFailure failure(point.size);
            CompleteWord(*editor, definition, settings, false, condition);
            injected = failure.Injected();
        });
    if (point.existingPopup) {
        CompleteWord(*editor, definition, settings, false);
        Require(editor->Send(Message::AutoCActive), "the failure fixture starts with an existing completion popup");
    }
    wxKeyEvent event(wxEVT_CHAR);
    event.m_keyCode = 'q';
    event.m_uniChar = 'q';
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
    const auto status = editor->Send(Message::GetStatus);
    const bool active = editor->Send(Message::AutoCActive) != 0;
    const auto previousItemCount = editor->CompletionState().previousItemCount;
    editor->SetWordCompletionHandler({});
    Require(called && injected, "the typed-character notification reaches the selected completion allocation");
    Require(status == static_cast<intptr_t>(std::to_underlying(Scintilla::Status::BadAlloc)) && !active &&
                previousItemCount == 0,
            "allocation failure reports BadAlloc and cancels completion before leaving the native notification");
    Require(TestEditor::Text(editor) == after && editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo) &&
                editor->Send(Message::GetModify),
            "failed completion preserves the typed character and its modified history state");
    TestEditor::RequireSelection(editor, after.size(), after.size(), "failed completion retains the typed caret");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == before && !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                !editor->Send(Message::GetModify),
            "one Undo restores the original text and save point after failed completion");
    TestEditor::RequireSelection(editor, before.size(), before.size(), "Undo restores the pre-typing caret");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == after, "Redo restores the typed character after failed completion");
    CompleteWord(*editor, definition, settings, false);
    Require(editor->Send(Message::AutoCActive) && editor->CompletionState().previousItemCount == CandidateCount,
            "completion can rebuild its full candidate list after an allocation failure");
    editor->Send(Message::AutoCCancel);
    Require(TestEditor::Text(editor) == after && editor->Send(Message::CanUndo),
            "retrying and canceling completion retain the document and its Undo step");
    std::printf("PASS: native completion recovers from %s allocation failure\n", point.name);
}

void CheckAllocationFailures(EditorFrame* frame, bool presented) {
    Require(presented, "native compositor presents the completion allocation fixture");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    // Keep the original seven native failure stages. Their allocation sizes
    // select known candidate work before the popup's noexcept native callbacks.
    const std::array points{
        FailurePoint{"root buffer", RootLength + 1},
        FailurePoint{"insertion path", 32 * sizeof(void*)},
        FailurePoint{"initial candidate buffer", CandidateBufferAllocationSize(4096)},
        FailurePoint{"candidate buffer growth", CandidateBufferAllocationSize(8192)},
        FailurePoint{"serialized candidates", CandidateCount * (CandidateLength + 1) + 1},
        FailurePoint{"document search buffer", RootLength + 2, true},
        FailurePoint{"active popup root buffer", RootLength + 1, false, true},
    };
    for (const auto& point : points)
        NativeAllocationFailure(frame, point);
}

class CompletionAllocationApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                CheckAllocationFailures(frame, presented);
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
wxIMPLEMENT_APP(CompletionAllocationApp);
