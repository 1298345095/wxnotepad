#include "native_completion_helpers.h"
#include "automatic_completion_reference.h"
#include <wx/vlbox.h>
namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}
void Type(EditorControl* editor, int ch) {
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(editor->GetHandle());
    const auto virtualKey = LOBYTE(VkKeyScanW(static_cast<wchar_t>(ch)));
    SendMessageW(handle, WM_KEYDOWN, virtualKey, 0);
    SendMessageW(handle, WM_CHAR, ch, 0);
    SendMessageW(handle, WM_KEYUP, virtualKey, 0);
#else
    auto* frame = wxDynamicCast(editor->GetParent(), EditorFrame);
    if (ch == '>')
        NativeKeys::Shortcut(frame, '.', false, false, true);
    else
        NativeKeys::Shortcut(frame, ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch, false);
#endif
    wxYield();
}
void Key(EditorControl* editor, int key) {
#ifdef __WXMSW__
    NativeCompletion::Key(editor, key);
#else
    if (key == 9) {
        NativeKeys::Shortcut(wxDynamicCast(editor->GetParent(), EditorFrame), WXK_TAB, false);
        return;
    }
    NativeCompletion::Key(editor, key == 8    ? GDK_KEY_BackSpace
                                  : key == 9  ? GDK_KEY_Tab
                                  : key == 13 ? GDK_KEY_Return
                                  : key == 27 ? GDK_KEY_Escape
                                              : GDK_KEY_Down);
#endif
}
std::vector<std::string> Candidates(EditorControl* editor) {
    std::vector<std::string> words;
    intptr_t previous = -1;
    while (editor->Send(Message::AutoCActive)) {
        const auto index = editor->Send(Message::AutoCGetCurrent);
        if (index == previous) break;
        previous = index;
        const auto length = editor->Send(Message::AutoCGetCurrentText);
        std::string text(length + 1, 0);
        editor->Send(Message::AutoCGetCurrentText, 0, reinterpret_cast<intptr_t>(text.data()));
        text.pop_back();
        words.push_back(text);
        Key(editor, 40);
    }
    return words;
}
void Frozen(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const auto& ref : automaticReference) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", ref.scheme));
        WordCompletionSettings settings;
        settings.fillUpMask = ref.fill;
        settings.latexInputMethod = ref.latex;
        editor->SetCompletionSettings(settings);
        const auto length = std::strlen(ref.before);
        TestEditor::ResetStream(editor, ref.before, length, length);
        editor->Send(Message::Colourise, 0, -1);
        if (ref.readOnly) editor->Send(Message::SetReadOnly, 1);
        if (*ref.popup) {
            constexpr int SuffixPrefixBytes = 2;
            editor->Send(Message::SetSel, SuffixPrefixBytes, SuffixPrefixBytes);
            editor->Send(Message::AutoCShow, SuffixPrefixBytes, reinterpret_cast<intptr_t>(ref.popup));
        }
        for (const auto& [key, ch] : ref.events) {
            if (key)
                Key(editor, ch);
            else
                Type(editor, ch);
        }
        if (TestEditor::Text(editor) != ref.after)
            std::fprintf(stderr, "before=[%s] expected=[%s] actual=[%s]", ref.before, ref.after,
                         TestEditor::Text(editor).c_str());
        Require(TestEditor::Text(editor) == ref.after, "frozen original typing/confirmation bytes");
        TestEditor::RequireSelection(editor, ref.caret, ref.caret, "frozen original typing caret");
        Require(static_cast<bool>(editor->Send(Message::AutoCActive)) == ref.active,
                "frozen original popup visibility");
        Require(Candidates(editor) == ref.items, "frozen original native candidate order");
        editor->Send(Message::AutoCCancel);
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == ref.undo, "frozen original confirmation undo boundary");
        TestEditor::RequireSelection(editor, ref.undoAnchor, ref.undoCaret, "frozen original Undo selection");
        editor->Send(Message::Redo);
        if (TestEditor::Text(editor) != ref.after)
            std::fprintf(stderr, "before=[%s] expected=[%s] actual=[%s]", ref.before, ref.after,
                         TestEditor::Text(editor).c_str());
        Require(TestEditor::Text(editor) == ref.after, "redo restores frozen completion bytes");
    }
    std::puts("PASS: frozen original automatic candidates, Enter/Tab/Space, suffix caret, Undo/Redo and deletion");
}

void Behavior(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto scheme = [&](const char* name) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", name));
    };
    const auto reset = [&](const char* text) {
        const auto length = std::strlen(text);
        TestEditor::ResetStream(editor, text, length, length);
        editor->Send(Message::Colourise, 0, -1);
    };
    WordCompletionSettings settings;
    scheme("Plain Text");
    reset("alpha alpine a");
    Type(editor, 'l');
    Key(editor, 40);
    Key(editor, 13);
    Require(TestEditor::Text(editor) == "alpha alpine alpine",
            "automatic native Down/Enter confirms selected document candidate");
    reset("alpha alpine a");
    Type(editor, 'l');
    auto* list = wxDynamicCast(wxWindow::FindWindowByName("Completion candidates"), wxVListBox);
    Require(list && list->IsShown(), "automatic popup exposes the same native mouse list");
    const auto focus = wxWindow::FindFocus();
    NativeCompletion::DoubleClick(list,
                                  list->GetItemRect(1).GetPosition() + wxPoint(8, list->GetItemRect(1).height / 2));
    Require(TestEditor::Text(editor) == "alpha alpine alpine" && wxWindow::FindFocus() == focus,
            "automatic mouse selection confirms without stealing editor focus");
    settings.minWordLength = 3;
    editor->SetCompletionSettings(settings);
    reset("alpha alpine a");
    Type(editor, 'l');
    Require(!editor->Send(Message::AutoCActive), "minimum word length suppresses early popup");
    Type(editor, 'p');
    Require(editor->Send(Message::AutoCActive), "minimum word length triggers at original boundary");
    Key(editor, 27);
    Require(!editor->Send(Message::AutoCActive), "native Escape cancels typing popup");
    Type(editor, 'h');
    Require(editor->Send(Message::AutoCActive), "typing after Escape can reopen original sole-candidate popup");
    settings.minWordLength = 1;
    settings.minNumberLength = 0;
    editor->SetCompletionSettings(settings);
    reset("12345 12399 12");
    Type(editor, '3');
    Require(!editor->Send(Message::AutoCActive), "zero minimum number length disables number completion");
    settings.minNumberLength = 3;
    editor->SetCompletionSettings(settings);
    reset("12345 12399 12");
    Type(editor, '3');
    Require(Candidates(editor) == std::vector<std::string>{"12345", "12399"},
            "number completion uses original minimum without inserting");
    settings.completeScope = 8;
    editor->SetCompletionSettings(settings);
    reset("alpha alpine a");
    Type(editor, 'l');
    Require(!editor->Send(Message::AutoCActive), "plain-text completion scope is honored");
    scheme("C/C++");
    reset("// pri");
    Type(editor, 'n');
    Require(!editor->Send(Message::AutoCActive), "comment completion scope is honored");
    reset("\"pri");
    Type(editor, 'n');
    Require(!editor->Send(Message::AutoCActive), "string completion scope is honored");
    settings.completeScope = 15;
    settings.scanWordScope = 8;
    settings.completeOption = WordCompletionOption::CompleteWord | WordCompletionOption::ScanWordsInDocument |
                              WordCompletionOption::OnlyWordsInDocument;
    editor->SetCompletionSettings(settings);
    reset("// alpha\r\nalpine\r\na");
    Type(editor, 'l');
    Require(Candidates(editor) == std::vector<std::string>{"alpine"},
            "scan scope excludes document comments from code candidates");
    settings = WordCompletionSettings{};
    editor->SetCompletionSettings(settings);
    reset("pr");
    Type(editor, 'i');
    Require(editor->Send(Message::AutoCActive), "C++ keywords populate automatic popup");
    scheme("Plain Text");
    Require(!editor->Send(Message::AutoCActive), "switching schemes cancels stale candidates");
    reset("pr");
    Type(editor, 'i');
    Require(!editor->Send(Message::AutoCActive), "plain-text scheme does not retain C++ keyword data");
    reset("alpha alpine a");
    Type(editor, 'l');
    Require(editor->Send(Message::AutoCActive), "document words populate popup");
    reset("other a");
    Require(!editor->Send(Message::AutoCActive), "opening another document cancels stale candidates");
    Type(editor, 'l');
    Require(!editor->Send(Message::AutoCActive), "new document has no previous document words");
    reset("alpha alpine a");
    editor->Send(Message::SetReadOnly, 1);
    Type(editor, 'l');
    Require(TestEditor::Text(editor) == "alpha alpine a" && editor->Send(Message::AutoCActive),
            "original read-only typing preserves text while still offering candidates");
    reset("alpha a\r\nalpine a");
    editor->Send(Message::SetSel, 7, 7);
    editor->Send(Message::SetMultipleSelection, 1);
    editor->Send(Message::AddSelection, 17, 17);
    Type(editor, 'l');
    Require(!editor->Send(Message::AutoCActive), "multiple-selection input does not open completion popup");
    editor->Send(Message::SetMultipleSelection, 0);
    settings.completeOption &= ~WordCompletionOption::CompleteWord;
    editor->SetCompletionSettings(settings);
    reset("alpha alpine a");
    Type(editor, 'l');
    Require(!editor->Send(Message::AutoCActive), "disabled automatic completion suppresses typing popup");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Complete Word"));
    Require(editor->Send(Message::AutoCActive),
            "manual command still uses shared collector when automatic mode is disabled");
    editor->Send(Message::AutoCCancel);
    settings = WordCompletionSettings{};
    settings.scanWordsTimeout = 50;
    editor->SetCompletionSettings(settings);
    std::string longDocument;
    longDocument.reserve(12'000'001);
    for (int i = 0; i < 2'000'000; ++i)
        longDocument += "alpha ";
    longDocument += 'a';
    reset(longDocument.c_str());
    const auto started = std::chrono::steady_clock::now();
    Type(editor, 'l');
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    Require(editor->Send(Message::AutoCActive), "long document scan retains candidates before timeout");
    std::printf(
        "PASS: 12 MB document scan with original 50 ms scan timeout; observed %.3f ms including native typing and UI\n",
        elapsed);
    std::puts("PASS: original minimum lengths, scopes, scan filtering, language/document changes, Escape, read-only "
              "and multiple selections");
}

class CompletionApp final : public wxApp {
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
                Require(presented, "native editor is presented");
                auto* editor = frame->Editor();
                TestEditor::ResetStream(editor, "alpha alpine a", 14, 14);
                Type(editor, 'l');
                Require(editor->Send(Message::AutoCActive), "typing opens original shared candidate popup");
                Require(TestEditor::Text(editor) == "alpha alpine al", "typing does not auto-insert sole candidate");
                std::puts("PASS: native typing opens original completion popup without inserting");
                Frozen(frame);
                Behavior(frame);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "FAIL: %s\n", e.what());
                result_ = 1;
            }
            frame->Editor()->Send(Message::SetSavePoint);
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
wxIMPLEMENT_APP(CompletionApp);
