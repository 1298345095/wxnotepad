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
void Menu(EditorFrame* frame, bool merge) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", merge ? "Merge Blank Lines" : "Remove Blank Lines");
    Require(id != wxNOT_FOUND, "Edit menu exposes Merge/Remove Blank Lines");
    NativeSearch::Command(frame, id);
}
void Range(EditorControl* editor, intptr_t anchor, intptr_t caret) {
    Require(editor->Send(Message::GetAnchor) == anchor && editor->Send(Message::GetCurrentPos) == caret,
            "blank-line deletion uses original core anchor/caret movement");
}
void Fixtures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* name;
        const char* before;
        bool merge;
        intptr_t anchor;
        intptr_t caret;
        const char* after;
        intptr_t resultAnchor;
        intptr_t resultCaret;
    };
    // Literal outcomes of pinned EditRemoveBlankLines, including its final-line exception.
    const Fixture fixtures[] = {
        {"merge keeps final blank whitespace and EOL", "A\n \r\n\t\rB", true, 4, 4, "A\n\t\rB", 2, 2},
        {"remove whole document from caret", "A\n \r\n\t\rB", false, 4, 4, "A\nB", 2, 2},
        {"merge leading, interior, trailing runs", " \n\t\nA\n\n \r\nB\n \n\t", true, 0, 0, "\t\nA\n \r\nB\n\t", 0, 0},
        {"remove leading, interior, trailing runs", " \n\t\nA\n\n \r\nB\n \n\t", false, 0, 0, "A\nB\n", 0, 0},
        {"partial first line is excluded, partial last included", " \t\n\nA\n \t\n\nZ", false, 1, 7, " \t\nA\n\nZ", 1,
         5},
        {"reverse stream selection tracks the same deletions", " \t\n\nA\n \t\n\nZ", false, 7, 1, " \t\nA\n\nZ", 5, 1},
        {"selection ending at nonfinal line start excludes it", "A\n \n\t\nZ", false, 2, 4, "A\n\t\nZ", 2, 2},
        {"selection ending at final line start includes it", "A\n \n\t", false, 2, 4, "A\n", 2, 2},
        {"merge final-line endpoint keeps unterminated whitespace", "A\n \n\t", true, 2, 4, "A\n\t", 2, 2},
        {"merge keeps final zero-length logical line", "A\n\n", true, 0, 0, "A\n", 0, 0},
        {"remove final zero-length logical line preserves preceding EOL", "A\n\n", false, 0, 0, "A\n", 0, 0},
        {"merge entire blank document", "\n\n", true, 0, 0, "", 0, 0},
        {"remove entire whitespace document", " \r\n\t", false, 0, 0, "", 0, 0},
        {"other whitespace bytes are ordinary text", "\v\n\f\n\xc2\xa0\n \t\n\nZ", false, 0, 0, "\v\n\f\n\xc2\xa0\nZ",
         0, 0},
    };
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(editor, fixture.before, fixture.anchor, fixture.caret);
        const auto eol = editor->Send(Message::GetEOLMode);
        const auto encoding = frame->DocumentEncoding();
        Menu(frame, fixture.merge);
        if (TestEditor::Text(editor) != fixture.after) std::fprintf(stderr, "Fixture: %s\n", fixture.name);
        Require(TestEditor::Text(editor) == fixture.after, fixture.name);
        Range(editor, fixture.resultAnchor, fixture.resultCaret);
        Require(editor->Send(Message::CanUndo) && editor->Send(Message::GetModify),
                "blank-line changes have undo history and leave save point");
        Require(editor->Send(Message::GetEOLMode) == eol && frame->DocumentEncoding() == encoding,
                "blank-line edits preserve current EOL mode and encoding");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "one Undo restores all blank runs and the original save point");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == fixture.after && editor->Send(Message::GetModify),
                "one Redo restores the complete blank-line operation");
        std::printf("PASS: %s, selection, one-step Undo/Redo and save point\n", fixture.name);
    }
}
void NoChange(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* before;
        bool merge;
        intptr_t anchor;
        intptr_t caret;
    };
    const Fixture fixtures[] = {
        {"", true, 0, 0},        {"", false, 0, 0},           {"A\n", true, 0, 0},
        {"A\n", false, 0, 0},    {"A\n \nB\n\t", true, 3, 3}, {"A\nB", false, 2, 0},
        {"\t \nA", false, 1, 2}, {"A\n \n\t\nZ", true, 2, 4},
    };
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(editor, fixture.before, fixture.anchor, fixture.caret);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, fixture.anchor, fixture.caret);
        Menu(frame, fixture.merge);
        Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                "unchanged blank-line operations retain clean history and pending redo");
        Range(editor, fixture.anchor, fixture.caret);
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == std::string(fixture.before) + "!", "no-change retains usable redo");
    }
    std::puts("PASS: empty document, singleton merge, final logical line and excluded selection preserve redo");
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
void Protection(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool merge : {true, false}) {
        TestEditor::ResetStream(editor, "A\n\n\nB", 4, 1);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, 4, 1);
        editor->Send(Message::SetReadOnly, 1);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", merge ? "Merge Blank Lines" : "Remove Blank Lines");
        Require(frame->GetMenuBar()->IsEnabled(id), "original blank-line menu stays enabled for read-only text");
        Menu(frame, merge);
        Require(TestEditor::Text(editor) == "A\n\n\nB" && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                "core rejects read-only blank-line deletions without changing history");
        Range(editor, 4, 1);
        for (const bool empty : {false, true}) {
            editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
            editor->Send(Message::SetRectangularSelectionAnchor, 0);
            editor->Send(Message::SetRectangularSelectionCaret, empty ? 0 : 4);
            RectangleFeedback feedback;
            Menu(frame, merge);
            Require(feedback.count == 1, "rectangle feedback precedes empty-selection and read-only handling");
            Require(TestEditor::Text(editor) == "A\n\n\nB" && !editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify) &&
                        editor->Send(Message::GetRectangularSelectionAnchor) == 0 &&
                        editor->Send(Message::GetRectangularSelectionCaret) == (empty ? 0 : 4),
                    "rectangle retains bytes, endpoints, history, redo and save point");
        }
        // Pinned CanRedo reports availability, which is false while read-only.
        editor->Send(Message::SetReadOnly, 0);
        Require(editor->Send(Message::CanRedo), "read-only command and rectangle feedback preserve pending redo");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == "A\n\n\nB!", "pending redo remains usable after read-only protection");
    }
    TestEditor::ResetStream(editor, "");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    for (const char* label : {"Merge Blank Lines", "Remove Blank Lines"})
        Require(frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", label)),
                "original menu has no document-length or selection gate");
    std::puts("PASS: original menu gates, read-only core protection and both rectangular-selection warnings");
}
void NativeAlt(EditorFrame* frame, bool merge, bool control = false, bool shift = false) {
    auto* editor = frame->Editor();
    editor->SetFocus();
    wxYield();
#ifdef __WXMSW__
    const bool handled = NativeSearch::WindowsShortcut(editor, merge ? 'B' : 'R',
                                                       {{VK_MENU, 0x80},
                                                        {VK_CONTROL, static_cast<BYTE>(control ? 0x80 : 0)},
                                                        {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)}});
    Require(handled || control || shift, "native Alt+B/Alt+R accelerator is handled");
#else
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
    observer.Bind(wxEVT_KEY_UP, [&received, merge](wxKeyEvent& event) {
        if (event.GetKeyCode() == (merge ? 'B' : 'R')) received = true;
        event.Skip();
    });
    editor->PushEventHandler(&observer);
    {
        wxFFile request(focusFile, "wb");
        request.Write(wxString("key:alt+") + (control ? "ctrl+" : "") + (shift ? "shift+" : "") + (merge ? "b" : "r"));
    }
    wxStopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "actual private-display blank-line shortcut release reaches editor");
#endif
    wxYield();
}
void Shortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool merge : {true, false}) {
        TestEditor::ResetStream(editor, "A\n \r\n\t\rB");
        NativeAlt(frame, merge);
        Require(TestEditor::Text(editor) == (merge ? "A\n\t\rB" : "A\nB"),
                "actual Alt+B/Alt+R routes to the matching blank-line mode");
        for (const auto [control, shift] : {std::pair{true, false}, std::pair{false, true}, std::pair{true, true}}) {
            TestEditor::ResetStream(editor, "A\n \r\n\t\rB");
            NativeAlt(frame, merge, control, shift);
            Require(TestEditor::Text(editor) == "A\n \r\n\t\rB" && !editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::GetModify),
                    "additional Ctrl/Shift modifiers do not run blank-line operations");
        }
    }
    std::puts("PASS: native Alt+B/Alt+R and all additional Ctrl/Shift modifier combinations");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-blank-lines");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string merged;
        std::string removed;
    };
    const Fixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf \r\n\t\n中文\r\n \t\r\n\nZ\n \t", "\xef\xbb\xbf\t\n中文\r\n\nZ\n \t",
         "\xef\xbb\xbf中文\r\nZ\n"},
        {"gbk", FileEncoding::Gbk, " \r\n\t\n\xd6\xd0\xce\xc4\r\n \t\r\n\nZ\n \t", "\t\n\xd6\xd0\xce\xc4\r\n\nZ\n \t",
         "\xd6\xd0\xce\xc4\r\nZ\n"},
        {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe \0\n\0\t\0\r\0\n\0A\0\n\0 \0\t\0", 20),
         std::string("\xff\xfe\t\0\r\0\n\0A\0\n\0 \0\t\0", 16), std::string("\xff\xfe\x41\0\n\0", 6)},
    };
    for (const auto& fixture : fixtures) {
        for (const bool merge : {true, false}) {
            const auto path = directory / (std::string(fixture.name) + (merge ? "-merge.txt" : "-remove.txt"));
            TestFiles::WriteBytes(path, fixture.before);
            editor->Send(Message::SetSavePoint);
            Require(frame->Open(wxString::FromUTF8(path.string()), fixture.encoding).status == OpenStatus::Opened,
                    "open independent blank-line encoding fixture through application");
            Menu(frame, merge);
            const auto& expected = merge ? fixture.merged : fixture.removed;
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == expected,
                    "blank-line Save preserves independent literal encoding, BOM and surviving EOL bytes");
            editor->Send(Message::Undo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                    "one Undo and Save restore all independent original bytes");
            editor->Send(Message::Redo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == expected,
                    "one Redo and Save restore all independent transformed bytes");
        }
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: independent UTF-8 BOM, GBK, UTF-16LE BOM saved bytes and Undo/Redo in both modes");
}
class BlankLinesApp final : public wxApp {
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
                NoChange(frame);
                Protection(frame);
                Shortcuts(frame);
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
wxIMPLEMENT_APP(BlankLinesApp);
