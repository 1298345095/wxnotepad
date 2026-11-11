#include "native_completion_helpers.h"
#include <wx/vlbox.h>
#include "completion_reference.h"
#ifdef __WXGTK__
#include <gdk/gdkkeysyms.h>
#endif
namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw TestEditor::Failure(message);
}
std::vector<std::string> Candidates(EditorControl* editor) {
    std::vector<std::string> words;
    intptr_t previous = -1;
    while (editor->Send(Message::AutoCActive)) {
        const auto current = editor->Send(Message::AutoCGetCurrent);
        if (current == previous) break;
        previous = current;
        const auto size = editor->Send(Message::AutoCGetCurrentText);
        std::string word(size + 1, 0);
        editor->Send(Message::AutoCGetCurrentText, 0, reinterpret_cast<intptr_t>(word.data()));
        word.pop_back();
        words.push_back(word);
#ifdef __WXMSW__
        NativeCompletion::Key(editor, VK_DOWN);
#else
        NativeCompletion::Key(editor, GDK_KEY_Down);
#endif
    }
    return words;
}
class WordCompletionApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
#ifdef __WXMSW__
        frame->ShowWithoutActivating();
#else
        frame->Show();
#endif
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native compositor presents completion test editor");
                auto* editor = frame->Editor();
                TestEditor::ResetStream(editor, "alpha al", 8, 8);
                const auto id = frame->GetMenuBar()->FindMenuItem("Edit", "Complete Word");
                Require(id != wxNOT_FOUND, "Edit > Special > Complete Word exists");
                TestEditor::ResetStream(editor, "", 0, 0);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetMenuBar()->IsEnabled(id), "empty document disables original Complete Word");
                TestEditor::ResetStream(editor, "alpha al", 8, 8);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsEnabled(id), "nonempty document enables original Complete Word");
                NativeSearch::Command(frame, id);
                Require(TestEditor::Text(editor) == "alpha alpha", "manual completion inserts original sole candidate");
                TestEditor::RequireSelection(editor, 11, 11, "single candidate caret");
                Require(!editor->Send(Message::AutoCActive), "single candidate has no popup");
                editor->Send(Message::Undo);
                Require(TestEditor::Text(editor) == "alpha al" && !editor->Send(Message::CanUndo),
                        "one Undo restores completion");
                std::puts("PASS: original Complete Word single-candidate command and Undo");
#ifdef __WXMSW__
                for (const int key : {VK_OEM_2, VK_DIVIDE}) {
                    TestEditor::ResetStream(editor, "alpha al", 8, 8);
                    Require(
                        NativeSearch::WindowsShortcut(editor, key, {{VK_CONTROL, 0}, {VK_SHIFT, 0}, {VK_MENU, 0x80}}),
                        "native Alt+/ accelerator is handled");
                    Require(TestEditor::Text(editor) == "alpha alpha",
                            "native Alt+/ and keypad Divide invoke completion");
                }
#else
                for(const int key:{static_cast<int>('/'),static_cast<int>(WXK_NUMPAD_DIVIDE)}) {
                    TestEditor::ResetStream(editor,"alpha al",8,8);
                    NativeKeys::Shortcut(frame,key,false,true);
                    Require(TestEditor::Text(editor)=="alpha alpha","physical native Alt+/ and keypad Divide invoke completion");
                }
#endif
                for (const auto& fixture : completionReference) {
                    editor->Send(Message::AutoCCancel);
                    NativeSearch::Command(
                        frame, frame->GetMenuBar()->FindMenuItem(
                                   "Scheme", std::string_view(fixture.language) == "HTML"  ? "Web Source Code"
                                             : std::string_view(fixture.language) == "XML" ? "XML Document"
                                                                                           : fixture.language));
                    const auto length = std::strlen(fixture.before);
                    TestEditor::ResetStream(editor, fixture.before, length, length);
                    editor->Send(Message::Colourise, 0, -1);
                    NativeSearch::Command(frame, id);
                    const auto actual = Candidates(editor);
                    if (TestEditor::Text(editor) != fixture.after || actual != fixture.items) {
                        std::fprintf(stderr, "Fixture %s: %s\nActual text: %s\n", fixture.language, fixture.before,
                                     TestEditor::Text(editor).c_str());
                        for (const auto& word : actual)
                            std::fprintf(stderr, "[%s] ", word.c_str());
                        std::fprintf(stderr, "\n");
                    }
                    Require(TestEditor::Text(editor) == fixture.after, "original command bytes");
                    Require(actual == fixture.items, "original candidate list and native Down order");
                    TestEditor::RequireSelection(editor, fixture.caret, fixture.caret, "original command caret");
                    if (fixture.confirmed) {
                        editor->Send(Message::AutoCSelect, 0,
                                     reinterpret_cast<intptr_t>(fixture.items.front().c_str()));
#ifdef __WXMSW__
                        NativeCompletion::Key(editor, VK_RETURN);
#else
                        NativeCompletion::Key(editor, GDK_KEY_Return);
#endif
                        Require(TestEditor::Text(editor) == fixture.confirmed,
                                "original native Enter confirmation bytes");
                        TestEditor::RequireSelection(editor, fixture.confirmedCaret, fixture.confirmedCaret,
                                                     "original Enter confirmation caret");
                    }
                }
                std::puts("PASS: frozen original candidate lists, case, numbers, CJK, keywords and prefix fallbacks");
                editor->Send(Message::AutoCCancel);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
                TestEditor::ResetStream(editor, "pri", 3, 3);
                NativeSearch::Command(frame, id);
#ifdef __WXMSW__
                NativeCompletion::Key(editor, VK_RETURN);
#else
                NativeCompletion::Key(editor, GDK_KEY_Return);
#endif
                Require(TestEditor::Text(editor) == "printf()", "native Enter confirms original function candidate");
                TestEditor::RequireSelection(editor, 7, 7, "native confirmation leaves caret inside parentheses");
                editor->Send(Message::Undo);
                Require(TestEditor::Text(editor) == "pri" && !editor->Send(Message::CanUndo),
                        "confirmation is one Undo step");
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
wxIMPLEMENT_APP(WordCompletionApp);
