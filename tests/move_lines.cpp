#include "native_search_helpers.h"
#include "file_helpers.h"
#include <wx/ffile.h>
#include "steady_stopwatch.h"
#include <fstream>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    Require(id != wxNOT_FOUND, "native Edit menu exposes Move Up/Down");
    NativeSearch::Command(frame, id);
}
void NativeAlt(EditorFrame* frame, bool upwards, bool shift = false) {
    frame->Editor()->SetFocus();
    wxYield();
#ifdef __WXMSW__
    const bool handled = NativeSearch::WindowsShortcut(
        frame->Editor(), upwards ? VK_UP : VK_DOWN,
        {{VK_MENU, 0x80}, {VK_CONTROL, 0}, {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)}});
    Require(handled || shift, "native Alt+Up/Down accelerator is handled");
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys require a private display");
    {
        wxFFile file(focusFile, "wb");
        file.Write("focus:wxnotepad");
    }
    TestClock::StopWatch focusWait;
    while (focusWait.Time() < 3000) {
        wxString pending;
        {
            wxFFile file(focusFile, "rb");
            file.ReadAll(&pending);
        }
        if (pending.empty()) break;
        wxYield();
        wxMilliSleep(10);
    }
    bool received = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&received, upwards](wxKeyEvent& event) {
        if (event.GetKeyCode() == (upwards ? WXK_UP : WXK_DOWN)) received = true;
        event.Skip();
    });
    frame->Editor()->PushEventHandler(&observer);
    {
        wxFFile file(focusFile, "wb");
        file.Write(wxString("key:alt+") + (shift ? "shift+" : "") + (upwards ? "Up" : "Down"));
    }
    TestClock::StopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    frame->Editor()->PopEventHandler(false);
    Require(received, "actual private-display Alt arrow reaches editor");
#endif
    wxYield();
}
void Reset(EditorControl* editor, const char* text, intptr_t anchor, intptr_t caret) {
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::SetSel, anchor, caret);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
void Fixtures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* name;
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        bool upwards;
        const char* after;
        intptr_t movedAnchor;
        intptr_t movedCaret;
    };
    // Literal outcomes follow pinned EditMoveUp/EditMoveDown and the pinned
    // core's LineTranspose/MoveSelectedLines, including their EOL placement.
    const Fixture fixtures[] = {
        {"caret up from unterminated last line", "A\nlast", 4, 4, true, "last\nA", 2, 2},
        {"same-line down into unterminated last line", "AA\nlast", 2, 1, false, "last\nAA", 7, 6},
        {"CRLF same-line offsets", "A\r\n\t中文\r\nZ", 4, 10, true, "\t中文\r\nA\r\nZ", 1, 7},
        {"CR same-line offsets", "A\rBB\rZ", 3, 4, false, "A\rZ\rBB", 5, 6},
        {"mixed EOL transpose", "A\r\nBB\nC\rD", 3, 5, true, "BB\r\nA\nC\rD", 0, 2},
        {"reverse endpoint up", "A\nbb\nccc\nD\n", 5, 2, true, "bb\nA\nccc\nD\n", 3, 0},
        {"partial reverse multiline up", "A\nbb\nccc\nD\n", 7, 3, true, "bb\nccc\nA\nD\n", 7, 0},
        {"partial reverse multiline down", "A\nbb\nccc\nD\n", 7, 3, false, "A\nD\nbb\nccc\n", 11, 4},
        {"partial forward multiline up", "A\nbb\nccc\nD\n", 3, 7, true, "bb\nccc\nA\nD\n", 0, 7},
        {"CRLF multiline down", "A\r\nB\r\nC\r\nD", 3, 6, false, "A\r\nC\r\nB\r\nD", 6, 9},
        {"CR multiline up", "A\rB\rC\rD", 2, 4, true, "B\rA\rC\rD", 0, 2},
        {"unterminated multiline up uses current EOL", "A\nbb\nlast", 3, 7, true, "bb\nlast\r\nA", 0, 9},
    };
    for (const auto& fixture : fixtures) {
        Reset(editor, fixture.before, fixture.anchor, fixture.caret);
        const auto eol = editor->Send(Message::GetEOLMode);
        const auto encoding = frame->DocumentEncoding();
        Menu(frame, fixture.upwards ? "Move Up" : "Move Down");
        Require(NativeSearch::Text(editor) == fixture.after, fixture.name);
        Require(editor->Send(Message::GetAnchor) == fixture.movedAnchor &&
                    editor->Send(Message::GetCurrentPos) == fixture.movedCaret,
                fixture.name);
        Require(editor->Send(Message::GetModify) && editor->Send(Message::CanUndo), "move modifies with undo");
        Require(editor->Send(Message::GetEOLMode) == eol && frame->DocumentEncoding() == encoding,
                "move preserves EOL mode and encoding");
        Menu(frame, "Undo");
        Require(NativeSearch::Text(editor) == fixture.before && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "one Undo restores bytes and save point");
        Menu(frame, "Redo");
        Require(NativeSearch::Text(editor) == fixture.after && editor->Send(Message::GetModify),
                "one Redo restores moved bytes");
        std::printf("PASS: %s, anchor/caret, one-step Undo/Redo, mode preservation\n", fixture.name);
    }
}
void NoOps(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool upwards : {true, false}) {
        const char* before = "A\nBB\nlast";
        const intptr_t position = upwards ? 1 : 8;
        Reset(editor, before, position, position);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, position, position);
        Menu(frame, upwards ? "Move Up" : "Move Down");
        Require(NativeSearch::Text(editor) == before && editor->Send(Message::GetAnchor) == position &&
                    editor->Send(Message::GetCurrentPos) == position && !editor->Send(Message::GetModify) &&
                    !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                "boundary no-op preserves selection, clean state and pending redo");
        Menu(frame, "Redo");
        Require(NativeSearch::Text(editor) == "A\nBB\nlast!", "pending redo survives boundary no-op");
    }
    Reset(editor, "", 0, 0);
    Menu(frame, "Move Up");
    Menu(frame, "Move Down");
    Require(!editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo), "empty moves are no-ops");
    Reset(editor, "A\nBB\nlast", 0, 5);
    Menu(frame, "Move Up");
    Require(NativeSearch::Text(editor) == "A\nBB\nlast" && editor->Send(Message::GetCurrentPos) == 5 &&
                !editor->Send(Message::GetModify),
            "first multiline block cannot move up");
    editor->Send(Message::SetSel, 3, 8);
    Menu(frame, "Move Down");
    Require(NativeSearch::Text(editor) == "A\nBB\nlast" && editor->Send(Message::GetAnchor) == 3 &&
                editor->Send(Message::GetCurrentPos) == 8 && !editor->Send(Message::GetModify),
            "last multiline block cannot move down");
    std::puts("PASS: first/last-line, multiline and empty boundaries preserve history and selection");
}
void SavedBytes(EditorFrame* frame) {
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-move-lines");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string after;
    };
    const Fixture fixtures[] = {
        {"utf8-bom", FileEncoding::Utf8,
         "\xef\xbb\xbf"
         "A\r\n中文",
         "\xef\xbb\xbf"
         "中文\r\nA"},
        {"gbk", FileEncoding::Gbk, "A\r\n\xd6\xd0\xce\xc4", "\xd6\xd0\xce\xc4\r\nA"},
        {"utf16-bom", FileEncoding::Utf16Le, std::string("\xff\xfe\x41\0\x0d\0\x0a\0\x2d\x4e\x87\x65", 12),
         std::string("\xff\xfe\x2d\x4e\x87\x65\x0d\0\x0a\0\x41\0", 12)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        {
            std::ofstream file(path, std::ios::binary);
            file.write(fixture.before.data(), static_cast<std::streamsize>(fixture.before.size()));
        }
        frame->Editor()->Send(Message::SetSavePoint);
        Require(frame->Open(wxString::FromUTF8(path.string()), fixture.encoding).status == OpenStatus::Opened,
                "saved-byte fixture opens through application");
        frame->Editor()->Send(Message::GotoPos, 3);
        Menu(frame, "Move Up");
        Require(frame->DocumentEncoding() == fixture.encoding && frame->Save().status == SaveStatus::Saved &&
                    TestFiles::Bytes(path) == fixture.after,
                "move preserves encoding, BOM and unterminated final line");
        Menu(frame, "Undo");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "Undo and Save restore independently read original bytes");
        Menu(frame, "Redo");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "Redo and Save restore independently read moved bytes");
        std::printf("PASS: %s independent saved bytes and encoding/BOM preservation\n", fixture.name);
    }
    std::filesystem::remove_all(directory);
}
class MoveLinesApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("first\n\t中文 tail\nlast"));
                editor->Send(Message::SetSel, 13, 7);
                Menu(frame, "Move Up");
                Require(NativeSearch::Text(editor) == "\t中文 tail\nfirst\nlast", "Move Up transposes a logical line");
                Require(editor->Send(Message::GetAnchor) == 7 && editor->Send(Message::GetCurrentPos) == 1,
                        "same-line reverse selection keeps byte-relative anchor and caret");
                std::puts("PASS: native Move Up preserves original same-line selection");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("A\nbb\nccc\nD\n"));
                editor->Send(Message::SetSel, 2, 5);
                Menu(frame, "Move Down");
                Require(NativeSearch::Text(editor) == "A\nccc\nbb\nD\n", "Move Down excludes next-line-start endpoint");
                Require(editor->Send(Message::GetAnchor) == 6 && editor->Send(Message::GetCurrentPos) == 9,
                        "multiline movement selects complete moved lines with original orientation");
                std::puts("PASS: native Move Down excludes next-line-start endpoint");
                Fixtures(frame);
                NoOps(frame);
                Reset(editor, "A\nBB\nlast", 3, 4);
                NativeAlt(frame, true);
                Require(NativeSearch::Text(editor) == "BB\nA\nlast" && editor->Send(Message::GetAnchor) == 1 &&
                            editor->Send(Message::GetCurrentPos) == 2,
                        "native Alt+Up uses original helper");
                NativeAlt(frame, false);
                Require(NativeSearch::Text(editor) == "A\nBB\nlast" && editor->Send(Message::GetAnchor) == 3 &&
                            editor->Send(Message::GetCurrentPos) == 4,
                        "native Alt+Down uses original helper");
                NativeAlt(frame, true, true);
                Require(NativeSearch::Text(editor) == "A\nBB\nlast", "Alt+Shift+Up does not move lines");
                std::puts("PASS: actual native Alt+Up/Down and exact modifiers");
                TestEditor::ResetStream(editor, "alpha\r\nbeta\r\n", 4, 1);
                editor->Send(Message::SetReadOnly, 1);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                for (const char* label : {"Move Up", "Move Down"}) {
                    Require(frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", label)),
                            "original read-only move menus remain enabled");
                }
                editor->MoveLines(false);
                TestEditor::RequireSelection(editor, 11, 8,
                                             "original protected Move Down repairs the destination selection");
                editor->Send(Message::SetSel, 4, 1);
                NativeAlt(frame, false);
                Require(NativeSearch::Text(editor) == "alpha\r\nbeta\r\n" && editor->Send(Message::GetAnchor) == 11 &&
                            editor->Send(Message::GetCurrentPos) == 8 && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "read-only native Move Down retains original selection repair without changing bytes/history");
                editor->Send(Message::SetReadOnly, 0);
                std::puts("PASS: original read-only menu/keyboard availability and selection effects");
                const std::string longLine = "first " + std::string(300, 'x');
                Reset(editor, (longLine + "\nB\nlast").c_str(), 2, 2);
                frame->SetClientSize(wxSize(500, 300));
                editor->ToggleWordWrap();
                editor->Refresh();
                editor->Update();
                wxYield();
                Require(editor->Send(Message::WrapCount, 0) > 1, "fixture has wrapped display rows");
                NativeAlt(frame, false);
                Require(NativeSearch::Text(editor) == "B\n" + longLine + "\nlast" &&
                            editor->Send(Message::GetCurrentPos) == 4,
                        "wrapped rows move as one logical line");
                editor->ToggleWordWrap();
                Reset(editor, "before\nvoid f() {\n  call();\n}\nafter();\n", 7, 29);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                editor->Refresh();
                editor->Update();
                wxYield();
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
                Require(!editor->Send(Message::GetLineVisible, 2), "fixture includes folded content");
                editor->Send(Message::SetSel, 7, 30);
                NativeAlt(frame, true);
                Require(NativeSearch::Text(editor) == "void f() {\n  call();\n}\nbefore\nafter();\n" &&
                            editor->Send(Message::GetAnchor) == 0 && editor->Send(Message::GetCurrentPos) == 23,
                        "folded selection moves its hidden logical lines with original bounds");
                Require(frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Language", "C/C++")),
                        "move preserves selected language");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Expand All"));
                std::puts("PASS: wrapped and folded logical lines, language preservation");
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(MoveLinesApp);
