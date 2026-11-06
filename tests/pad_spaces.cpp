#include "app/editor_frame.h"
#include "encoded_file_helpers.h"
#include "native_search_helpers.h"
#include "settings/settings_store.h"
#include <wx/app.h>
#include <wx/ffile.h>
#include <wx/menu.h>
#include <wx/stopwatch.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Pad With Spaces");
    Require(id != wxNOT_FOUND, "native Edit menu exposes Pad With Spaces");
    NativeSearch::Command(frame, id);
}
void Reset(EditorControl* editor, const char* text, intptr_t anchor, intptr_t caret) {
    editor->Send(Message::SetSelectionMode, 0);
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::SetSel, anchor, caret);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
void Selection(EditorControl* editor, intptr_t anchor, intptr_t caret, const char* message) {
    if (editor->Send(Message::GetAnchor) != anchor || editor->Send(Message::GetCurrentPos) != caret)
        std::fprintf(stderr, "Selection: %lld/%lld; expected %lld/%lld\n",
                     static_cast<long long>(editor->Send(Message::GetAnchor)),
                     static_cast<long long>(editor->Send(Message::GetCurrentPos)), static_cast<long long>(anchor),
                     static_cast<long long>(caret));
    Require(editor->Send(Message::GetAnchor) == anchor && editor->Send(Message::GetCurrentPos) == caret, message);
}
void History(EditorControl* editor, const char* before, const char* after, intptr_t anchor, intptr_t caret) {
    Require(editor->Send(Message::CanUndo) && editor->Send(Message::GetModify),
            "padding changes history and dirty state");
    editor->Send(Message::Undo);
    Require(NativeSearch::Text(editor) == before && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "one Undo restores original bytes and save point");
    Selection(editor, anchor, caret, "original core selection history restores input anchor/caret");
    editor->Send(Message::Redo);
    Require(NativeSearch::Text(editor) == after && editor->Send(Message::GetModify), "one Redo restores padding");
}
void StreamFixtures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* name;
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        const char* after;
        intptr_t resultAnchor;
        intptr_t resultCaret;
    };
    // Literal results from EditPadWithSpaces and the pinned core's column/selection operations.
    const Fixture fixtures[] = {
        {"single-line reverse selection pads whole document", "A\nlong\nx", 4, 3, "A   \nlong\nx   ", 7, 6},
        {"forward partial multiline selection", "AA\nbbbb\nx\nlastLONG", 1, 9, "AA  \nbbbb\nx   \nlastLONG", 0, 14},
        {"reverse partial multiline selection", "AA\nbbbb\nx\nlastLONG", 9, 1, "AA  \nbbbb\nx   \nlastLONG", 14, 0},
        {"next-line-start endpoint excludes final line", "A\nlong\nz", 1, 7, "A   \nlong\nz", 0, 10},
        {"reverse next-line-start endpoint", "A\nlong\nz", 7, 1, "A   \nlong\nz", 10, 0},
        {"original live-position reselection predicate", "a\n12345678", 0, 3, "a       \n12345678", 0, 10},
        {"Tabs Unicode mixed EOL and unterminated final line", "\tX\r\n中\n😀\rZ", 0, 0, "\tX\r\n中    \n😀    \rZ    ",
         0, 0},
        {"Unicode scalar columns and combining marks", "😀\nx\né", 0, 0, "😀 \nx \né", 0, 0},
        {"whitespace-only line participates", "abcd\n \n\t", 0, 0, "abcd\n    \n\t", 0, 0},
    };
    editor->SetIndentationWidths(4, 4);
    for (const auto& fixture : fixtures) {
        Reset(editor, fixture.before, fixture.anchor, fixture.caret);
        const auto mode = editor->Send(Message::GetEOLMode);
        Menu(frame);
        Require(NativeSearch::Text(editor) == fixture.after, fixture.name);
        Selection(editor, fixture.resultAnchor, fixture.resultCaret, fixture.name);
        Require(editor->Send(Message::GetEOLMode) == mode, "padding preserves EOL mode");
        History(editor, fixture.before, fixture.after, fixture.anchor, fixture.caret);
        std::printf("PASS: %s, literal selection and single Undo/Redo\n", fixture.name);
    }
    Reset(editor, "\tX\n123456\nq", 0, 0);
    editor->SetIndentationWidths(8, 8);
    Menu(frame);
    Require(NativeSearch::Text(editor) == "\tX\n123456   \nq        ", "effective tab width controls core columns");
    History(editor, "\tX\n123456\nq", "\tX\n123456   \nq        ", 0, 0);
    editor->SetIndentationWidths(4, 4);
    std::puts("PASS: effective tab width 8 preserves literal Tab bytes");
}
void Rectangle(EditorControl* editor, intptr_t anchor, intptr_t caret, intptr_t anchorVirtual = 0,
               intptr_t caretVirtual = 0) {
    editor->Send(Message::SetRectangularSelectionAnchor, anchor);
    editor->Send(Message::SetRectangularSelectionCaret, caret);
    editor->Send(Message::SetRectangularSelectionAnchorVirtualSpace, anchorVirtual);
    editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, caretVirtual);
}
void Rectangles(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const char* before = "LLLLLL\nabcd\nx\nabcde\nZ\nTAIL";
    const char* after = "LLLLLL\nabcd\nx   \nabcde\nZ   \nTAIL";
    for (const bool reverse : {false, true}) {
        Reset(editor, before, 0, 0);
        Rectangle(editor, reverse ? 21 : 8, reverse ? 8 : 21, reverse ? 3 : 0, reverse ? 0 : 3);
        Menu(frame);
        Require(NativeSearch::Text(editor) == after,
                "rectangle pads participating short lines, excludes outside and text after selected end");
        Require(editor->Send(Message::SelectionIsRectangle) &&
                    editor->Send(Message::GetRectangularSelectionAnchor) == (reverse ? 24 : 8) &&
                    editor->Send(Message::GetRectangularSelectionCaret) == (reverse ? 8 : 24) &&
                    editor->Send(Message::GetRectangularSelectionAnchorVirtualSpace) == 0 &&
                    editor->Send(Message::GetRectangularSelectionCaretVirtualSpace) == 0,
                "original FindColumn repair restores actual endpoint columns and clears virtual columns");
        editor->Send(Message::Undo);
        Require(NativeSearch::Text(editor) == before && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "rectangle padding has one Undo and restores save point");
        Require(editor->Send(Message::SelectionIsRectangle) &&
                    editor->Send(Message::GetRectangularSelectionAnchor) == (reverse ? 21 : 8) &&
                    editor->Send(Message::GetRectangularSelectionCaret) == (reverse ? 8 : 21) &&
                    editor->Send(Message::GetRectangularSelectionAnchorVirtualSpace) == (reverse ? 3 : 0) &&
                    editor->Send(Message::GetRectangularSelectionCaretVirtualSpace) == (reverse ? 0 : 3),
                "Undo restores original rectangle endpoints and virtual columns");
        editor->Send(Message::Redo);
        Require(NativeSearch::Text(editor) == after, "rectangle Redo restores literal padded text");
    }
    Reset(editor, "abcd\n\nx\n \nZ", 0, 0);
    Rectangle(editor, 0, 11, 0, 3);
    Menu(frame);
    Require(NativeSearch::Text(editor) == "abcd\n    \nx   \n    \nZ   ",
            "public rectangle command pads empty and whitespace-only selected lines");
    std::puts("PASS: forward/reverse rectangle participation, endpoint limits, virtual-column repair and history");
}
void Variants(EditorControl* editor) {
    Reset(editor, "abcd\n\nx\n \nZ", 0, 0);
    editor->PadWithSpaces(true, false);
    Require(NativeSearch::Text(editor) == "abcd\n\nx   \n    \nZ   ",
            "skip-empty variant preserves truly empty lines and pads whitespace-only lines");
    History(editor, "abcd\n\nx\n \nZ", "abcd\n\nx   \n    \nZ   ", 0, 0);
    Reset(editor, "a\nbbbb\nx", 0, 0);
    editor->PadWithSpaces(false, true);
    Require(NativeSearch::Text(editor) == "a   \nbbbb\nx   ", "caller-owned variant still pads all lines");
    editor->Send(Message::Undo);
    Require(NativeSearch::Text(editor) == "a   \nbbbb\nx" && editor->Send(Message::CanUndo),
            "noUndoGroup variant leaves individual target replacements separately undoable");
    editor->Send(Message::Undo);
    Require(NativeSearch::Text(editor) == "a\nbbbb\nx" && !editor->Send(Message::GetModify),
            "second Undo restores earlier independent padding");
    Reset(editor, "abcd\n\nx\n \nZ", 0, 0);
    Rectangle(editor, 0, 11, 0, 3);
    editor->Send(Message::BeginUndoAction);
    editor->PadWithSpaces(true, true);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::EndUndoAction);
    Require(NativeSearch::Text(editor) == "abcd\n\nx   \n    \nZ   !",
            "duplicate-line caller variant pads rectangle and skips empty lines");
    editor->Send(Message::Undo);
    Require(NativeSearch::Text(editor) == "abcd\n\nx\n \nZ" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "caller combines padding and subsequent edit into one Undo");
    std::puts("PASS: genuine skip-empty, caller-owned grouping and combined rectangle variants");
}
void NoChangeAndReadOnly(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const char* before : {"", "AA\nBB"}) {
        const bool empty = !*before;
        Reset(editor, before, empty ? 0 : 1, empty ? 0 : 4);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, empty ? 0 : 1, empty ? 0 : 4);
        const auto revision = editor->DocumentRevision();
        Menu(frame);
        Require(NativeSearch::Text(editor) == before && !editor->Send(Message::GetModify) &&
                    !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                    editor->DocumentRevision() == revision,
                "zero padding preserves bytes, dirty state, document revision and pending Redo");
        Selection(editor, 0, empty ? 0 : 5, "unchanged multiline input still receives original selection repair");
        editor->Send(Message::Redo);
        Require(NativeSearch::Text(editor) == std::string(before) + "!", "pending Redo survives padding no-op");
    }
    Reset(editor, "A\nlong\nz", 1, 7);
    editor->Send(Message::SetReadOnly, 1);
    const auto revision = editor->DocumentRevision();
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", "Pad With Spaces")),
            "original Pad With Spaces menu remains enabled for read-only text");
    Menu(frame);
    Require(NativeSearch::Text(editor) == "A\nlong\nz" && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo) && editor->DocumentRevision() == revision,
            "read-only core refuses padding without changing history or revision");
    Selection(editor, 0, 7, "read-only command retains original multiline selection repair");
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: empty/equal-column no-op, pending Redo and original read-only menu/selection behavior");
}
void NativeAltP(EditorFrame* frame, bool control, bool shift) {
    frame->Editor()->SetFocus();
    wxYield();
#ifdef __WXMSW__
    const bool handled = NativeSearch::WindowsShortcut(frame->Editor(), 'P',
                                                       {{VK_MENU, 0x80},
                                                        {VK_CONTROL, static_cast<BYTE>(control ? 0x80 : 0)},
                                                        {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)}});
    Require(handled || control || shift, "native Alt+P accelerator is handled");
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys require a private display");
    {
        wxFFile file(focusFile, "wb");
        file.Write("focus:wxnotepad");
    }
    wxStopWatch focusWait;
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
    observer.Bind(wxEVT_KEY_UP, [&received](wxKeyEvent& event) {
        if (event.GetKeyCode() == 'P') received = true;
        event.Skip();
    });
    frame->Editor()->PushEventHandler(&observer);
    {
        wxFFile file(focusFile, "wb");
        file.Write(wxString("key:alt+") + (control ? "ctrl+" : "") + (shift ? "shift+" : "") + "p");
    }
    wxStopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    frame->Editor()->PopEventHandler(false);
    Require(received, "actual private-display Alt+P release reaches owned editor");
#endif
    wxYield();
}
void Shortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool control : {false, true}) {
        for (const bool shift : {false, true}) {
            Reset(editor, "A\nlong", 0, 0);
            NativeAltP(frame, control, shift);
            Require(NativeSearch::Text(editor) == (control || shift ? "A\nlong" : "A   \nlong"),
                    "Alt+P requires exact modifiers");
            if (control || shift)
                Require(!editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                        "extra shortcut modifiers preserve history and save point");
        }
    }
    Reset(editor, "A\nlong\nz", 1, 7);
    editor->Send(Message::SetReadOnly, 1);
    NativeAltP(frame, false, false);
    Require(NativeSearch::Text(editor) == "A\nlong\nz" && !editor->Send(Message::CanUndo),
            "native read-only Alt+P leaves text protected");
    Selection(editor, 0, 7, "native read-only shortcut executes original selection repair");
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: actual native Alt+P, extra Ctrl/Shift exclusions and read-only routing");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-pad-spaces");
    std::filesystem::create_directory(directory);
    using Fixture = TestFiles::EncodedFixture;
    const Fixture fixtures[] = {
        {"utf8-bom", FileEncoding::Utf8, "\xef\xbb\xbf中文\r\nA\n😀\r", "\xef\xbb\xbf中文\r\nA \n😀 \r  "},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\xce\xc4\rA\r\nx\n", "\xd6\xd0\xce\xc4\rA \r\nx \n  "},
        {"utf16-bom", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x87\x65\x0d\0\x0a\0\x41\0", 12),
         std::string("\xff\xfe\x2d\x4e\x87\x65\x0d\0\x0a\0\x41\0\x20\0", 14)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(wxString::FromUTF8(path.string()), fixture.encoding).status == OpenStatus::Opened,
                "saved-byte fixture opens through application");
        editor->Send(Message::SetSel, 0, 0);
        Menu(frame);
        Require(frame->DocumentEncoding() == fixture.encoding && frame->Save().status == SaveStatus::Saved &&
                    TestFiles::Bytes(path) == fixture.after,
                "Save preserves encoding/BOM and mixed EOL bytes with literal original padding");
        wxString evidence;
        if (wxGetEnv("WXNOTEPAD_PAD_EVIDENCE_DIR", &evidence))
            std::filesystem::copy_file(
                path, std::filesystem::path(evidence.ToStdString()) / (std::string(fixture.name) + ".saved"),
                std::filesystem::copy_options::overwrite_existing);
        editor->Send(Message::Undo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "single Undo and Save restore independently read original bytes");
        editor->Send(Message::Redo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "single Redo and Save restore independently read padded bytes");
        std::printf("PASS: %s independent saved bytes, encoding/BOM/EOL preservation and one Undo/Redo\n",
                    fixture.name);
    }
    std::filesystem::remove_all(directory);
}
class PadSpacesApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        settings_->values.warnLineEndings = false;
        settings_->values.fixLineEndings = false;
        settings_->values.fixTrailingBlanks = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                Reset(editor, "A\nlong\n", 0, 0);
                Menu(frame);
                Require(NativeSearch::Text(editor) == "A   \nlong\n    ",
                        "caret pads whole document including empty final logical line");
                Require(editor->Send(Message::GetAnchor) == 0 && editor->Send(Message::GetCurrentPos) == 0,
                        "caret keeps its original position");
                std::puts("PASS: native Pad With Spaces pads whole document including empty lines");
                StreamFixtures(frame);
                Rectangles(frame);
                Variants(editor);
                NoChangeAndReadOnly(frame);
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
wxIMPLEMENT_APP(PadSpacesApp);
