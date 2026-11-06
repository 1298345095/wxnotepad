#include "app/editor_frame.h"
#include "native_search_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <wx/app.h>
#include <wx/menu.h>
#include <wx/ffile.h>
#include <wx/stopwatch.h>
#include <wx/modalhook.h>
#include <cstdio>
#include <memory>
#include <fstream>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Join(EditorFrame* frame, bool paragraphs) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", paragraphs ? "Join Paragraphs" : "Join Lines");
    Require(id != wxNOT_FOUND, "native Edit menu exposes the original join command");
    NativeSearch::Command(frame, id);
}
void Set(EditorControl* editor, const char* text, intptr_t anchor, intptr_t caret,
         Scintilla::EndOfLine mode = Scintilla::EndOfLine::Lf) {
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(mode));
    editor->Send(Message::SetSel, anchor, caret);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
void Range(EditorControl* editor, intptr_t anchor, intptr_t caret) {
    if (editor->Send(Message::GetAnchor) != anchor || editor->Send(Message::GetCurrentPos) != caret)
        std::fprintf(stderr, "anchor/caret: %lld/%lld; expected %lld/%lld\n",
                     static_cast<long long>(editor->Send(Message::GetAnchor)),
                     static_cast<long long>(editor->Send(Message::GetCurrentPos)), static_cast<long long>(anchor),
                     static_cast<long long>(caret));
    Require(editor->Send(Message::GetAnchor) == anchor && editor->Send(Message::GetCurrentPos) == caret,
            "original selection endpoints and direction");
}
void Fixtures(EditorFrame* frame) {
    // Literal expectations from pinned Editor::LinesJoin / EditJoinLinesEx.
    struct Fixture {
        bool paragraphs;
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        const char* after;
        intptr_t afterAnchor;
        intptr_t afterCaret;
        bool modified = true;
        Scintilla::EndOfLine mode = Scintilla::EndOfLine::Lf;
    };
    const Fixture fixtures[] = {
        {false, "a\r\n  b\nc", 1, 8, "a   b c", 2, 7},
        {false, "ab\ncd\nef", 1, 4, "ab cd\nef", 1, 4},
        {false, "ab\ncd\nef", 4, 1, "ab cd\nef", 4, 1},
        {false, "ab\ncd", 0, 3, "ab cd", 0, 2},
        {false, "a \nb\t\nc", 0, 7, "a b\t c", 0, 6},
        {false, "a\t\nb", 0, 4, "a\t b", 0, 4},
        {false, "a\n", 0, 2, "a ", 0, 1},
        {false, "abc", 1, 3, "abc", 1, 3, false},
        {false, "A\nB", 1, 1, "A\nB", 1, 1, false},
        {false, "中\r\n😀\r尾\n", 0, 14, "中 😀 尾 ", 0, 12},
        {true, "  A\nB\nTAIL", 2, 5, "  A B\nTAIL", 0, 5},
        {true, "  A\nB\nTAIL", 5, 2, "  A B\nTAIL", 5, 0},
        {true, "A\nB\nC", 0, 4, "A BC", 0, 3},
        {true, "A\n", 0, 2, "A\n", 0, 2, false},
        {true, "A\r\n", 0, 3, "A\r\n", 0, 3, false},
        {true, "A\n\n", 0, 3, "A", 0, 1},
        {true, "A\n\nB", 4, 0, "A\n\nB", 4, 0},
        {true, "\n\nA", 0, 3, "\nA", 0, 2},
        {true, "A\n \nB", 0, 5, "A   B", 0, 5},
        {true, "中\r\n😀\n\n尾", 0, 14, "中 😀\r\n\r\n尾", 0, 15, true, Scintilla::EndOfLine::CrLf},
        {true, "A\n\nB\r\nC", 0, 7, "A\r\rB C", 0, 6, true, Scintilla::EndOfLine::Cr},
        {true, "A\nB", 0, 3, "A B", 0, 3},
        {true, "A\nB", 1, 1, "A\nB", 1, 1, false},
    };
    auto* editor = frame->Editor();
    for (const auto& fixture : fixtures) {
        Set(editor, fixture.before, fixture.anchor, fixture.caret, fixture.mode);
        if (fixture.anchor == fixture.caret) {
            if (fixture.paragraphs)
                Require(editor->JoinParagraphs(), "empty paragraph command is supported");
            else
                editor->JoinLines();
        } else {
            Join(frame, fixture.paragraphs);
        }
        if (TestEditor::Text(editor) != fixture.after)
            std::fprintf(stderr, "join paragraphs=%d before=<%s> actual=<%s> expected=<%s>\n", fixture.paragraphs,
                         fixture.before, TestEditor::Text(editor).c_str(), fixture.after);
        Require(TestEditor::Text(editor) == fixture.after, "original literal join bytes");
        Range(editor, fixture.afterAnchor, fixture.afterCaret);
        Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(fixture.mode), "join retains EOL mode");
        Require(!!editor->Send(Message::CanUndo) == fixture.modified &&
                    !!editor->Send(Message::GetModify) == fixture.modified,
                "original unchanged-result history and save point");
        if (fixture.modified) {
            editor->Send(Message::Undo);
            Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::GetModify),
                    "one Undo restores bytes and save point");
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == fixture.after && !editor->Send(Message::CanRedo) &&
                        editor->Send(Message::GetModify),
                    "one Redo restores whole join");
        }
    }
    std::puts(
        "PASS: partial/reversed/end-at-line-start selections, mixed EOLs, Unicode, paragraph runs, trailing EOLs, "
        "unterminated lines, unchanged and identical-replacement history, one-step Undo/Redo");
}
void NativeKey(EditorControl* editor, bool shift, bool alt = false) {
#ifdef __WXMSW__
    const bool handled = NativeSearch::WindowsShortcut(
        editor, 'J', {{VK_CONTROL, 0x80}, {VK_SHIFT, shift ? 0x80 : 0}, {VK_MENU, alt ? 0x80 : 0}});
    Require(handled || alt, "native original Ctrl+J shortcut is handled");
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys use private display helper");
    bool received = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&received](wxKeyEvent& event) {
        if (event.GetKeyCode() == 'J') received = true;
        event.Skip();
    });
    editor->PushEventHandler(&observer);
    {
        wxFFile request(focusFile, "wb");
        request.Write(wxString("key:ctrl+") + (shift ? "shift+" : "") + (alt ? "alt+" : "") + "j");
    }
    wxStopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "native private-display J release reaches editor");
#endif
    wxYield();
}
void Shortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    Set(editor, "A\n\nB", 0, 4);
    NativeKey(editor, false);
    Require(TestEditor::Text(editor) == "A B", "native Ctrl+J invokes core join");
    Set(editor, "A\n\nB", 0, 4);
    NativeKey(editor, true);
    Require(TestEditor::Text(editor) == "A\n\nB" && editor->Send(Message::GetModify),
            "native Ctrl+Shift+J invokes paragraph join identical-byte replacement");
    for (bool shift : {false, true}) {
        Set(editor, "A\n\nB", 0, 4);
        NativeKey(editor, shift, true);
        Require(TestEditor::Text(editor) == "A\n\nB" && !editor->Send(Message::CanUndo),
                "additional Alt modifier does not route either join command");
    }
    std::puts("PASS: native Ctrl+J, Ctrl+Shift+J and exact extra-modifier routing");
}
class RectangleFeedback final : public wxModalDialogHook {
public:
    RectangleFeedback() { Register(); }
    int Enter(wxDialog*) override {
        ++count;
        return wxID_OK;
    }
    int count = 0;
};
void PendingRedo(EditorControl* editor) {
    editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    Require(!editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
            "fixture has pending redo at original save point");
}
void Protection(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const int lineId = frame->GetMenuBar()->FindMenuItem("Edit", "Join Lines");
    const int paragraphId = frame->GetMenuBar()->FindMenuItem("Edit", "Join Paragraphs");
    Set(editor, "A\nB", 1, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(lineId) && !frame->GetMenuBar()->IsEnabled(paragraphId),
            "both original join menus require a nonempty selection");
    for (bool paragraphs : {false, true}) {
        Set(editor, paragraphs ? "A\n" : "ABC", 0, 2);
        PendingRedo(editor);
        editor->Send(Message::SetSel, 0, 2);
        const auto before = TestEditor::Text(editor);
        Join(frame, paragraphs);
        Require(TestEditor::Text(editor) == before && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                "true no-op join preserves pending redo and save point");
        Range(editor, 0, 2);
    }
    for (bool backwards : {false, true}) {
        Set(editor, "aa\nbb\ncc", 0, 0);
        PendingRedo(editor);
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, backwards ? 8 : 0);
        editor->Send(Message::SetRectangularSelectionCaret, backwards ? 0 : 8);
        const auto anchor = editor->Send(Message::GetRectangularSelectionAnchor);
        const auto caret = editor->Send(Message::GetRectangularSelectionCaret);
        RectangleFeedback feedback;
        Join(frame, false);
        Require(feedback.count == 0 && TestEditor::Text(editor) == "aa\nbb\ncc" &&
                    editor->Send(Message::GetTargetStart) == (backwards ? 0 : 6) &&
                    editor->Send(Message::GetTargetEnd) == (backwards ? 2 : 8),
                "rectangular line join uses the main line range, without warning or whole-rectangle expansion");
        Join(frame, true);
        Require(feedback.count == 1 && TestEditor::Text(editor) == "aa\nbb\ncc" && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify) &&
                    editor->Send(Message::GetRectangularSelectionAnchor) == anchor &&
                    editor->Send(Message::GetRectangularSelectionCaret) == caret,
                "paragraph rectangle warning preserves direction, text, save point and pending redo");
        editor->Send(Message::SetRectangularSelectionAnchor, 0);
        editor->Send(Message::SetRectangularSelectionCaret, 6);
        Require(editor->JoinParagraphs() && feedback.count == 1,
                "empty rectangular paragraph selection returns before unsupported-rectangle feedback");
    }
    for (bool paragraphs : {false, true}) {
        Set(editor, "  A\nB", 2, 5);
        editor->Send(Message::SetReadOnly, 1);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(lineId) && frame->GetMenuBar()->IsEnabled(paragraphId),
                "original join menus retain selection-only availability on read-only text");
        Join(frame, paragraphs);
        Require(TestEditor::Text(editor) == "  A\nB" && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "core protects read-only bytes and history");
        Range(editor, paragraphs ? 0 : 2, 5);
    }
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: original selection-only availability, read-only protection/endpoint repair, main-range rectangle "
              "line join, paragraph warning ordering and pending redo");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-join");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string lines;
        std::string paragraphs;
    };
    const Fixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中\r\n\n尾", "\xef\xbb\xbf中 尾", "\xef\xbb\xbf中\n\n尾"},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\r\n\xce\xc4", "\xd6\xd0 \xce\xc4", "\xd6\xd0 \xce\xc4"},
        {"utf16", FileEncoding::Utf16Le,
         std::string("\xff\xfe"
                     "A\0\r\0\n\0\r\0\n\0B\0",
                     14),
         std::string("\xff\xfe"
                     "A\0 \0B\0",
                     8),
         std::string("\xff\xfe"
                     "A\0\n\0\n\0B\0",
                     10)},
    };
    for (const auto& fixture : fixtures) {
        for (bool paragraphs : {false, true}) {
            const auto path = directory / (std::string(fixture.name) + (paragraphs ? "-paragraphs.txt" : "-lines.txt"));
            {
                std::ofstream file(path, std::ios::binary);
                file.write(fixture.before.data(), static_cast<std::streamsize>(fixture.before.size()));
            }
            editor->Send(Message::SetSavePoint);
            Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                    "open encoded join fixture through application");
            editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
            editor->Send(Message::SelectAll);
            Join(frame, paragraphs);
            const auto& after = paragraphs ? fixture.paragraphs : fixture.lines;
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == after,
                    "independent saved join bytes retain encoding, BOM and exact generated EOLs");
            editor->Send(Message::Undo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                    "one Undo saves exact independent original bytes");
            editor->Send(Message::Redo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == after,
                    "one Redo saves exact independent joined bytes");
        }
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: independent UTF-8 BOM, GBK and UTF-16LE BOM saved bytes and saved Undo/Redo");
}
class JoinLinesApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("a\r\n  b\nc"));
                editor->Send(Message::SetSel, 1, 8);
                Join(frame, false);
                Require(TestEditor::Text(editor) == "a   b c", "target-based join retains next-line indentation");
                std::puts("PASS: original native Join Lines command");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("  a\r\n b\n\nc\r\r\nlast"));
                editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
                editor->Send(Message::SetSel, 2, 17);
                Join(frame, true);
                Require(TestEditor::Text(editor) == "  a  b\n\nc\n\nlast",
                        "paragraph join expands first line and emits current-mode paragraph separators");
                std::puts("PASS: original native Join Paragraphs command");
                Fixtures(frame);
                Shortcuts(frame);
                Protection(frame);
                SavedBytes(frame);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(JoinLinesApp);
