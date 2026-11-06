#include "native_search_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <wx/ffile.h>
#include <wx/stopwatch.h>
#include <fstream>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Reset(EditorControl* editor, const char* text, intptr_t anchor = 0, intptr_t caret = 0) {
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::SetSel, anchor, caret);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
int CommandId(EditorFrame* frame) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Compress Whitespace");
    Require(id != wxNOT_FOUND, "native Edit menu exposes Compress Whitespace");
    return id;
}
void Compress(EditorFrame* frame) {
    NativeSearch::Command(frame, CommandId(frame));
}
void Range(EditorControl* editor, intptr_t anchor, intptr_t caret) {
    if (editor->Send(Message::GetAnchor) != anchor || editor->Send(Message::GetCurrentPos) != caret)
        std::fprintf(stderr, "Selection: %lld/%lld; expected %lld/%lld\n",
                     static_cast<long long>(editor->Send(Message::GetAnchor)),
                     static_cast<long long>(editor->Send(Message::GetCurrentPos)), static_cast<long long>(anchor),
                     static_cast<long long>(caret));
    Require(editor->Send(Message::GetAnchor) == anchor && editor->Send(Message::GetCurrentPos) == caret,
            "original replacement selection preserves ordered extent and direction");
}
void Fixtures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* name;
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        const char* after;
        intptr_t resultAnchor;
        intptr_t resultCaret;
        bool changed = true;
        intptr_t redoPosition = 0;
    };
    // Literal byte outcomes follow pinned EditCompressSpaces/EditReplaceRange,
    // including bytes immediately outside the selected range.
    const Fixture fixtures[] = {
        {"whole document from caret", " \tA  B\tC \r\n\tD\t \r E \n \t", 4, 4, "A B C\r\nD\rE\n", 0, 11},
        {"whole unterminated document", "A  B \t", 2, 2, "A B", 0, 3},
        {"whole whitespace-only document", " \t  \r\n\t \n \t\r ", 1, 1, "\r\n\n\r", 0, 4},
        {"partial backward selection", "xxA  B yy", 5, 3, "xxA B yy", 4, 3, true, 3},
        {"partial selection at document start", " \tA  BEND", 0, 6, "A BEND", 0, 3},
        {"partial selection at document end", "headA  B \t", 4, 10, "headA B", 4, 7, true, 4},
        {"both boundaries inside run", "A    B", 2, 4, "A   B", 2, 3, true, 2},
        {"next outside space survives", "A   B", 0, 3, "A  B", 0, 2},
        {"previous LF strips leading portion", "A\n \t B", 2, 4, "A\n B", 2, 2, true, 2},
        {"previous CR strips leading portion", "A\r \tB", 4, 2, "A\rB", 2, 2, true, 2},
        {"next LF strips trailing run", "A \t\nB", 1, 3, "A\nB", 1, 1, true, 1},
        {"next CR strips trailing run", "A \t\rB", 3, 1, "A\rB", 1, 1, true, 1},
        {"outside preceding space survives", "A  \r\nB", 2, 3, "A \r\nB", 2, 2, true, 2},
        {"one Tab changes equal byte count", "A\tB", 2, 1, "A B", 2, 1, true, 1},
        {"space then Tab run", "A \tB", 1, 3, "A B", 1, 2, true, 1},
        {"single space before outside space is unchanged", "A  \r\nB", 1, 2, "A  \r\nB", 1, 2, false},
        {"single interior space is unchanged", "A B", 2, 1, "A B", 2, 1, false},
        {"unchanged whole document retains caret", "A B", 1, 1, "A B", 1, 1, false},
        {"empty document", "", 0, 0, "", 0, 0, false},
        {"Unicode whitespace and control bytes are ordinary text", " \tA\xc2\xa0\t\tB\xe3\x80\x80  C\v\tD\f  E ", 0, 0,
         "A\xc2\xa0 B\xe3\x80\x80 C\v D\f E", 0, 16},
    };
    for (const auto& fixture : fixtures) {
        std::printf("Fixture: %s\n", fixture.name);
        Reset(editor, fixture.before, fixture.anchor, fixture.caret);
        Compress(frame);
        if (TestEditor::Text(editor) != fixture.after) std::fprintf(stderr, "Fixture: %s\n", fixture.name);
        Require(TestEditor::Text(editor) == fixture.after, "literal original compressed bytes");
        Range(editor, fixture.resultAnchor, fixture.resultCaret);
        Require(static_cast<bool>(editor->Send(Message::CanUndo)) == fixture.changed &&
                    static_cast<bool>(editor->Send(Message::GetModify)) == fixture.changed,
                "only original change predicate creates history and leaves save point");
        if (fixture.changed) {
            editor->Send(Message::Undo);
            Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::GetModify),
                    "one Undo restores original bytes and save point");
            Range(editor, fixture.anchor, fixture.caret);
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == fixture.after && !editor->Send(Message::CanRedo),
                    "one Redo restores the complete transformation");
            // The core records Redo at the end of ReplaceTarget, before the
            // original host's subsequent SetSel repairs the result range.
            Range(editor, fixture.redoPosition, fixture.redoPosition);
        }
    }
    std::puts(
        "PASS: literal partial/whole scopes, boundary sentinels, mixed EOLs, ASCII/Unicode whitespace and history");
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
void State(EditorFrame* frame) {
    auto* editor = frame->Editor();
    Reset(editor, "A B", 1, 1);
    editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("x"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 2, 1);
    Compress(frame);
    Require(TestEditor::Text(editor) == "A B" && !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                !editor->Send(Message::GetModify),
            "true no-op preserves pending redo and save point");
    Range(editor, 2, 1);
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "Ax B", "pending redo remains usable after no-op");
    for (bool empty : {false, true}) {
        Reset(editor, "A  B\nC  D", 0, 0);
        editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("x"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, 1);
        editor->Send(Message::SetRectangularSelectionCaret, empty ? 1 : 7);
        const auto anchor = editor->Send(Message::GetRectangularSelectionAnchor);
        const auto caret = editor->Send(Message::GetRectangularSelectionCaret);
        RectangleFeedback feedback;
        Compress(frame);
        Require(feedback.count == 1, "rectangle guard warns before empty-selection fallback");
        Require(TestEditor::Text(editor) == "A  B\nC  D" && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify) &&
                    editor->Send(Message::SelectionIsRectangle) &&
                    editor->Send(Message::GetRectangularSelectionAnchor) == anchor &&
                    editor->Send(Message::GetRectangularSelectionCaret) == caret,
                "unsupported rectangle retains bytes, selection, history, save point and redo");
    }
    for (bool backwards : {false, true}) {
        Reset(editor, "A  B", backwards ? 4 : 2, backwards ? 0 : 2);
        editor->Send(Message::SetReadOnly, 1);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(CommandId(frame)), "original read-only menu remains available");
        Compress(frame);
        Require(TestEditor::Text(editor) == "A  B" && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "read-only core rejects replacement without history or dirty state");
        Range(editor, backwards ? 3 : 0, backwards ? 0 : 3);
        editor->Send(Message::SetReadOnly, 0);
    }
    Reset(editor, "");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(CommandId(frame)), "empty document retains original menu availability");
    std::puts("PASS: no-op pending redo, empty/nonempty rectangle feedback and original read-only selection repair");
}
void NativeAlt(EditorFrame* frame, bool shift = false, bool control = false) {
#ifdef __WXMSW__
    const bool handled = NativeSearch::WindowsShortcut(frame->Editor(), 'W',
                                                       {{VK_MENU, 0x80},
                                                        {VK_CONTROL, static_cast<BYTE>(control ? 0x80 : 0)},
                                                        {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)}});
    Require(handled || shift || control, "native Alt+W accelerator is handled");
#else
    frame->Editor()->SetFocus();
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys require a private display");
    {
        wxFFile request(focusFile, "wb");
        request.Write("focus:wxnotepad");
    }
    wxStopWatch focusWait;
    while (focusWait.Time() < 3000) {
        wxString pending;
        {
            wxFFile request(focusFile, "rb");
            request.ReadAll(&pending);
        }
        if (pending.empty()) break;
        wxYield();
        wxMilliSleep(10);
    }
    bool received = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&received](wxKeyEvent& event) {
        if (event.GetKeyCode() == 'W' || event.GetKeyCode() == 'w') received = true;
        event.Skip();
    });
    frame->Editor()->PushEventHandler(&observer);
    {
        wxFFile request(focusFile, "wb");
        request.Write(wxString("key:alt+") + (shift ? "shift+" : "") + (control ? "ctrl+" : "") + "w");
    }
    wxStopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    frame->Editor()->PopEventHandler(false);
    Require(received, "actual private-display Alt+W key release reaches editor");
#endif
    wxYield();
}
void Keys(EditorFrame* frame) {
    auto* editor = frame->Editor();
    Reset(editor, "A  B");
    NativeAlt(frame);
    Require(TestEditor::Text(editor) == "A B", "actual native Alt+W routes to compression");
    for (const auto& modifiers : {std::pair{true, false}, std::pair{false, true}, std::pair{true, true}}) {
        Reset(editor, "A  B");
        NativeAlt(frame, modifiers.first, modifiers.second);
        Require(TestEditor::Text(editor) == "A  B" && !editor->Send(Message::CanUndo),
                "extra Shift/Control modifiers do not compress whitespace");
    }
    Reset(editor, "A  B");
    editor->Send(Message::SetReadOnly, 1);
    NativeAlt(frame);
    Require(TestEditor::Text(editor) == "A  B" && !editor->Send(Message::CanUndo),
            "read-only native Alt+W retains bytes and history");
    Range(editor, 0, 3);
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: actual native Alt+W, exact extra modifiers and read-only command routing");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-compress-whitespace");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string after;
    };
    const Fixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf \t中文  B\r\n\tC \rD\t \n ", "\xef\xbb\xbf中文 B\r\nC\rD\n"},
        {"gbk", FileEncoding::Gbk, "\t\xd6\xd0\xce\xc4  B\rC\tD \n ", "\xd6\xd0\xce\xc4 B\rC D\n"},
        {"utf16", FileEncoding::Utf16Le,
         std::string("\xff\xfe"
                     " \0\t\0A\0 \0 \0B\0\r\0\n\0\t\0C\0 \0",
                     24),
         std::string("\xff\xfe"
                     "A\0 \0B\0\r\0\n\0C\0",
                     14)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        {
            std::ofstream file(path, std::ios::binary);
            file.write(fixture.before.data(), static_cast<std::streamsize>(fixture.before.size()));
        }
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "open encoded fixture through application");
        Compress(frame);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "Save independently preserves exact compressed encoding/BOM/EOL bytes");
        editor->Send(Message::Undo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "one Undo restores independently read original encoded bytes");
        editor->Send(Message::Redo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "one Redo restores independently read compressed encoded bytes");
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: independent UTF-8 BOM, GBK and UTF-16LE BOM disk bytes, mixed EOLs and Undo/Redo");
}
class CompressWhitespaceApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                Fixtures(frame);
                State(frame);
                Keys(frame);
                SavedBytes(frame);
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(CompressWhitespaceApp);
