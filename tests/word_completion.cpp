#include "native_completion_helpers.h"
#include <print>
#include <wx/vlbox.h>
#include "completion_reference.h"
#include "completion_behavior_reference.h"
#include "settings/settings_store.h"
#include "syntax/syntax_catalog.h"
#include "SciLexer.h"
#include <array>
#ifdef __WXGTK__
#include <gdk/gdkkeysyms.h>
#endif
namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw TestEditor::Failure(message);
}
std::string CurrentCandidate(EditorControl* editor) {
    const auto size = editor->Send(Message::AutoCGetCurrentText);
    std::string word(size + 1, 0);
    editor->Send(Message::AutoCGetCurrentText, 0, reinterpret_cast<intptr_t>(word.data()));
    word.pop_back();
    return word;
}
std::vector<std::string> Candidates(EditorControl* editor) {
    std::vector<std::string> words;
    intptr_t previous = -1;
    while (editor->Send(Message::AutoCActive)) {
        const auto current = editor->Send(Message::AutoCGetCurrent);
        if (current == previous) break;
        previous = current;
        words.push_back(CurrentCandidate(editor));
#ifdef __WXMSW__
        NativeCompletion::Key(editor, VK_DOWN);
#else
        NativeCompletion::Key(editor, GDK_KEY_Down);
#endif
    }
    return words;
}
void FrozenCompletion(EditorFrame* frame, int id) {
    auto* editor = frame->Editor();
    for (const auto& fixture : completionReference) {
        editor->Send(Message::AutoCCancel);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem(
                                         "Scheme", std::string_view(fixture.language) == "HTML"  ? "Web Source Code"
                                                   : std::string_view(fixture.language) == "XML" ? "XML Document"
                                                                                                 : fixture.language));
        const auto length = std::strlen(fixture.before);
        TestEditor::ResetStream(editor, fixture.before, length, length);
        editor->Send(Message::Colourise, 0, -1);
        NativeSearch::Command(frame, id);
        const auto actual = Candidates(editor);
        if (TestEditor::Text(editor) != fixture.after || actual != fixture.items) {
            std::println(stderr, "Fixture {}: {}\nActual text: {}", fixture.language, fixture.before,
                         TestEditor::Text(editor).c_str());
            for (const auto& word : actual)
                std::print(stderr, "[{}] ", word.c_str());
            std::println(stderr, "");
        }
        Require(TestEditor::Text(editor) == fixture.after, "original command bytes");
        Require(actual == fixture.items, "original candidate list and native Down order");
        TestEditor::RequireSelection(editor, fixture.caret, fixture.caret, "original command caret");
        if (fixture.confirmed) {
            editor->Send(Message::AutoCSelect, 0, reinterpret_cast<intptr_t>(fixture.items.front().c_str()));
#ifdef __WXMSW__
            NativeCompletion::Key(editor, VK_RETURN);
#else
            NativeCompletion::Key(editor, GDK_KEY_Return);
#endif
            Require(TestEditor::Text(editor) == fixture.confirmed, "original native Enter confirmation bytes");
            TestEditor::RequireSelection(editor, fixture.confirmedCaret, fixture.confirmedCaret,
                                         "original Enter confirmation caret");
        }
    }
    std::puts("PASS: frozen original candidate lists, case, numbers, CJK, keywords and prefix fallbacks");
}
void RequireDocumentState(EditorControl* editor, const CompletionDocumentState& state) {
    using enum Scintilla::Message;
    Require(TestEditor::Text(editor) == state.text, "original completion document bytes");
    TestEditor::RequireSelection(editor, state.anchor, state.caret, "original completion selection");
    Require(static_cast<bool>(editor->Send(GetModify)) == state.modified &&
                static_cast<bool>(editor->Send(CanUndo)) == state.undo &&
                static_cast<bool>(editor->Send(CanRedo)) == state.redo,
            "original completion save point and history state");
}
void FrozenBehaviors(EditorFrame* frame, SettingsStore& store, int command) {
    auto* editor = frame->Editor();
    for (const auto& ref : completionBehaviors) {
        std::println(stderr, "Reference behavior {}: {}", ref.scheme, ref.before);
        editor->Send(Message::AutoCCancel);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", ref.scheme));
        auto& settings = store.values.completion;
        settings = WordCompletionSettings{};
        settings.completeOption = ref.options;
        settings.ignoreCase = ref.ignoreCase;
        settings.latexInputMethod = ref.latex;
        settings.completeScope = ref.scopes & WordCompletionLimits::ScopeMask;
        settings.scanWordScope = ref.scopes >> WordCompletionLimits::ScanScopeShift;
        TestEditor::ResetStream(editor, ref.before, ref.initialCaret, ref.initialCaret);
        editor->Send(Message::Colourise, 0, -1);
        const auto* focus = wxWindow::FindFocus();
        NativeSearch::Command(frame, command);
        RequireDocumentState(editor, ref.command);
        const auto actual = Candidates(editor);
        if (actual != ref.items) {
            for (const auto& item : actual)
                std::print(stderr, "[{}] ", item.c_str());
            std::println(stderr, "");
        }
        Require(actual == ref.items, "original document and language candidate order");
        if (!ref.items.empty()) {
            editor->Send(Message::AutoCSelect, 0, reinterpret_cast<intptr_t>(ref.items.front().c_str()));
#ifdef __WXMSW__
            NativeCompletion::Key(editor, VK_RETURN);
#else
            NativeCompletion::Key(editor, GDK_KEY_Return);
#endif
        }
        RequireDocumentState(editor, ref.confirmed);
        Require(wxWindow::FindFocus() == focus, "native completion preserves editor keyboard focus");
        editor->Send(Message::AutoCCancel);
        editor->Send(Message::Undo);
        RequireDocumentState(editor, ref.undo);
        editor->Send(Message::Redo);
        RequireDocumentState(editor, ref.redo);
    }
    store.values.completion = WordCompletionSettings{};
    std::puts("PASS: original document candidates, language prefixes, bytes, caret, save point, Undo/Redo and focus");
}
void EscapePrefixCompletion(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* scheme;
        SyntaxLanguage language;
        const char* text;
        int escapeStyle;
        bool completes;
    };
    const std::array fixtures = {
        Fixture{"Web Source Code", SyntaxLanguage::Html,
                "<script>const name = 'alpha'; const suffix = '\\nal';</script>", js_style(SCE_JS_ESCAPECHAR), true},
        Fixture{"JavaScript", SyntaxLanguage::JavaScript, "const name = 'alpha'; const suffix = '\\nal';",
                SCE_JS_ESCAPECHAR, true},
        Fixture{"Web Source Code", SyntaxLanguage::Html,
                R"(<script>const name = 'alpha'; const suffix = '\\nal';</script>)", js_style(SCE_JS_ESCAPECHAR),
                false},
    };
    WordCompletionSettings settings;
    settings.completeOption = WordCompletionOption::CompleteWord | WordCompletionOption::ScanWordsInDocument |
                              WordCompletionOption::OnlyWordsInDocument;
    for (const auto& fixture : fixtures) {
        using enum Scintilla::Message;
        editor->Send(AutoCCancel);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", fixture.scheme));
        const std::string before = fixture.text;
        const auto escape = before.find("\\nal");
        Require(escape != std::string::npos, "escape completion fixture contains its explicit caret marker");
        const auto caret = escape + 4;
        TestEditor::ResetStream(editor, before.c_str(), caret, caret);
        editor->Send(Colourise, 0, -1);
        Require(editor->Send(GetStyleIndexAt, escape) == fixture.escapeStyle,
                "native lexer marks the escape prefix in its embedded or standalone style");
        CompleteWord(*editor, SyntaxFor(fixture.language), settings, false);
        if (!fixture.completes) {
            Require(!editor->Send(AutoCActive) && TestEditor::Text(editor) == before && !editor->Send(CanUndo),
                    "an escaped backslash keeps the following n in the completion root");
            continue;
        }
        Require(Candidates(editor) == std::vector<std::string>{"alpha"},
                "completion searches after the escape character and finds the document word");
#ifdef __WXMSW__
        NativeCompletion::Key(editor, VK_RETURN);
#else
        NativeCompletion::Key(editor, GDK_KEY_Return);
#endif
        auto after = before;
        after.replace(escape + 2, 2, "alpha");
        Require(TestEditor::Text(editor) == after, "completion retains the escape prefix and surrounding source");
        TestEditor::RequireSelection(editor, escape + 7, escape + 7, "escape completion caret");
        editor->Send(Undo);
        Require(TestEditor::Text(editor) == before && !editor->Send(CanUndo),
                "escape completion remains one Undo action");
    }
    std::puts("PASS: embedded and standalone JavaScript completion preserves escape prefixes");
}
void LargeKeywordList(EditorFrame* frame) {
    using enum Scintilla::Message;
    constexpr int CandidateCount = 200000;
    std::string words;
    words.reserve(CandidateCount * 10);
    for (int index = 0; index < CandidateCount; ++index) {
        const auto digits = std::to_string(index);
        words += 'a';
        words.append(8 - digits.size(), '0');
        words += digits;
        words += ' ';
    }
    const std::array keywords = {SyntaxKeywords{words.c_str(), 0}};
    auto definition = SyntaxFor(SyntaxLanguage::Cpp);
    definition.keywords = keywords;
    WordCompletionSettings settings;
    settings.completeOption = WordCompletionOption::CompleteWord;
    auto* editor = frame->Editor();
    editor->Send(AutoCCancel);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    TestEditor::ResetStream(editor, "a", 1, 1);
    CompleteWord(*editor, definition, settings, false);
    Require(editor->Send(AutoCActive) && editor->Send(AutoCGetCurrent) == 0 && CurrentCandidate(editor) == "a00000000",
            "large sorted keyword lists retain the first completion candidate");
    editor->Send(AutoCSelect, 0, reinterpret_cast<intptr_t>("a00199999"));
    Require(editor->Send(AutoCGetCurrent) == CandidateCount - 1 && CurrentCandidate(editor) == "a00199999",
            "large keyword insertion and traversal preserve every candidate in sorted order");
    editor->Send(AutoCCancel);
    Require(TestEditor::Text(editor) == "a" && !editor->Send(CanUndo),
            "constructing and canceling a large completion list leaves document history unchanged");
    std::puts("PASS: 200000 sorted completion candidates exceed the former tree traversal stack bound");
}
class WordCompletionApp final : public wxApp {
public:
    bool OnInit() override {
        settings_.values = OriginalSettings{};
        auto* frame = new EditorFrame(&settings_);
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
                FrozenCompletion(frame, id);
                FrozenBehaviors(frame, settings_, id);
                EscapePrefixCompletion(frame);
                LargeKeywordList(frame);
                editor->Send(Message::AutoCCancel);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
                const std::string longestKeyword(124, 'x');
                const auto keywordList = longestKeyword + "^y(z";
                const std::array keywords = {SyntaxKeywords{keywordList.c_str(), 0}};
                auto definition = SyntaxFor(SyntaxLanguage::Cpp);
                definition.keywords = keywords;
                WordCompletionSettings boundarySettings;
                boundarySettings.completeOption = WordCompletionOption::CompleteWord;
                TestEditor::ResetStream(editor, "x", 1, 1);
                CompleteWord(*editor, definition, boundarySettings, false);
                Require(Candidates(editor) == std::vector<std::string>{longestKeyword, longestKeyword + "()"},
                        "keyword prefix markers at the original word limit preserve bounded completion suffixes");
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
                std::println(stderr, "FAIL: {}", error.what());
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
    SettingsStore settings_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(WordCompletionApp);
