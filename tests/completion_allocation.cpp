// Include the production implementation so this fixture can also exercise its
// private candidate storage. The executable supplies CompleteWord to app_shell.
#include "editor/word_completion.cpp"
#include "native_completion_helpers.h"

namespace CompletionAllocationFault {
thread_local bool armed = false;
thread_local bool injected = false;
thread_local size_t targetSize = 0;

class ScopedFailure final {
public:
    explicit ScopedFailure(size_t size = 0) {
        targetSize = size;
        injected = false;
        armed = true;
    }
    ~ScopedFailure() { armed = false; }
    bool Injected() const { return injected; }
};

void* Allocate(size_t size) {
    if (armed && (targetSize == 0 || size == targetSize)) {
        armed = false; // Cleanup and diagnostics must be able to allocate.
        injected = true;
        throw std::bad_alloc();
    }
    for (;;) {
        if (void* allocation = std::malloc(size ? size : 1)) return allocation;
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}
} // namespace CompletionAllocationFault

void* operator new(size_t size) {
    return CompletionAllocationFault::Allocate(size);
}
void* operator new[](size_t size) {
    return CompletionAllocationFault::Allocate(size);
}
void operator delete(void* allocation) noexcept {
    std::free(allocation);
}
void operator delete[](void* allocation) noexcept {
    std::free(allocation);
}
void operator delete(void* allocation, size_t) noexcept {
    std::free(allocation);
}
void operator delete[](void* allocation, size_t) noexcept {
    std::free(allocation);
}

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}

template <class Action> void RequireAllocationFailure(size_t size, Action action) {
    bool caught = false;
    bool injected = false;
    {
        const CompletionAllocationFault::ScopedFailure failure(size);
        try {
            action();
        } catch (const std::bad_alloc&) {
            caught = true;
        }
        injected = failure.Injected();
    }
    Require(injected && caught, "candidate storage propagates its injected allocation failure");
}

void CandidateAllocationFailures() {
    // Production roots use CharBuffer's padded storage for the cached sort key.
    constexpr std::array<char, NP2_AUTOC_SORT_KEY_LENGTH> root{'a'};
    RequireAllocationFailure(0, [] { const CharBuffer<> buffer(33); });
    RequireAllocationFailure(0, [&root] {
        WordList words;
        words.Init(root.data(), 1, false);
    });
    RequireAllocationFailure(NP2_AUTOC_INIT_BUFFER_SIZE, [&root] {
        WordList words;
        words.Init(root.data(), 1, false);
    });

    WordList words;
    words.Init(root.data(), 1, false);
    words.AddWord("a00000", 6);
    unsigned failedIndex = 0;
    for (unsigned index = 1; index < 256; ++index) {
        char word[16];
        std::snprintf(word, sizeof(word), "a%05u", index);
        const auto capacity = words.capacity;
        const auto offset = words.offset;
        const auto* buffer = words.buffer;
        bool caught = false;
        {
            const CompletionAllocationFault::ScopedFailure failure;
            try {
                words.AddWord(word, 6);
            } catch (const std::bad_alloc&) {
                caught = true;
            }
        }
        if (!caught) continue;
        Require(words.capacity == capacity && words.offset == offset && words.buffer == buffer &&
                    words.nWordCount == index,
                "failed candidate growth retains the previous buffer bounds and tree");
        words.AddWord(word, 6);
        failedIndex = index;
        break;
    }
    Require(failedIndex != 0 && words.nWordCount == failedIndex + 1,
            "candidate insertion succeeds after a failed buffer expansion");
    std::string expected;
    for (unsigned index = 0; index <= failedIndex; ++index) {
        char word[16];
        std::snprintf(word, sizeof(word), "a%05u", index);
        if (index) expected += '\n';
        expected += word;
    }
    Require(expected == words.GetList().get(), "failed growth and retry preserve every sorted candidate");
    RequireAllocationFailure(words.nTotalLen + 1, [&words] { const auto list = words.GetList(); });
    Require(expected == words.GetList().get(), "failed list serialization preserves reusable candidate storage");
    std::puts("PASS: completion allocation failures unwind and retain reusable candidate buffers");
}

constexpr size_t RootLength = 33;
constexpr unsigned CandidateCount = 200;
constexpr size_t CandidateLength = RootLength + 5;

std::string KeywordFixture(const std::string& root) {
    std::string words;
    for (unsigned index = 0; index < CandidateCount; ++index) {
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), "%05u ", index);
        words += root;
        words += suffix;
    }
    return words;
}

struct FailurePoint {
    const char* name;
    size_t size;
    bool document = false;
    bool existingPopup = false;
};

void NativeAllocationFailure(EditorFrame* frame, const FailurePoint& point) {
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
    editor->Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
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
    Require(status == static_cast<intptr_t>(Scintilla::Status::BadAlloc) && !active && previousItemCount == 0,
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
    CandidateAllocationFailures();
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    constexpr std::array points{
        FailurePoint{"root buffer", RootLength + 1},
        FailurePoint{"insertion path", InitialTreePathCapacity * sizeof(WordNode*)},
        FailurePoint{"initial candidate buffer", NP2_AUTOC_INIT_BUFFER_SIZE},
        FailurePoint{"candidate buffer growth", 2 * NP2_AUTOC_INIT_BUFFER_SIZE},
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
