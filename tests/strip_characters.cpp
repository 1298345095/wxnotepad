#include "native_key_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Strip(EditorFrame* frame, bool first) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", first ? "Strip First Character" : "Strip Last Character");
    Require(id != wxNOT_FOUND, "Edit menu exposes original Strip First/Last Character commands");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(id),
            "original strip menus remain available for every document and selection state");
    NativeSearch::Command(frame, id);
}
void NativeAlt(EditorFrame* frame, bool first, bool shift = false, bool control = false) {
#ifdef __WXMSW__
    auto* editor = frame->Editor();
    editor->SetFocus();
    wxYield();
    const bool handled = NativeSearch::WindowsShortcut(editor, first ? 'Z' : 'L',
                                                       {{VK_MENU, 0x80},
                                                        {VK_CONTROL, static_cast<BYTE>(control ? 0x80 : 0)},
                                                        {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)}});
    Require(handled || shift || control, "native Alt+Z/Alt+L accelerator is handled");
#else
    bool received = false;
    NativeKeys::Shortcut(frame, first ? 'Z' : 'L', control, true, shift, &received);
    Require(received, "actual private-display Alt+Z/Alt+L release reaches editor");
#endif
    wxYield();
}
void Shortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool first : {true, false}) {
        TestEditor::ResetStream(editor, "abc\ndef", 1, 1);
        NativeAlt(frame, first);
        Require(TestEditor::Text(editor) == (first ? "bc\ndef" : "ab\ndef"),
                "exact native Alt+Z/Alt+L strips character");
        for (const auto& modifiers : {std::pair{true, false}, std::pair{false, true}, std::pair{true, true}}) {
            TestEditor::ResetStream(editor, "abc\ndef", 1, 1);
            NativeAlt(frame, first, modifiers.first, modifiers.second);
            Require(TestEditor::Text(editor) == "abc\ndef" && !editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::GetModify),
                    "extra Shift/Control modifiers preserve text and history");
        }
    }
    std::puts("PASS: native Alt+Z/Alt+L and exact Shift/Control modifier routing");
}
void Range(EditorControl* editor, intptr_t anchor, intptr_t caret) {
    TestEditor::RequireSelection(editor, anchor, caret,
                                 "original core adjusts anchor/caret without explicit reselection");
}
void Fixtures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* name;
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        bool first;
        const char* after;
        intptr_t resultAnchor;
        intptr_t resultCaret;
    };
    // Literal outcomes follow Notepad4 89b8b36 Edit.cpp:3548-3617 and its
    // PositionAfter/PositionBefore operations, including independent combining marks.
    const Fixture fixtures[] = {
        {"first caret uses current line", "abc\ndef", 2, 2, true, "bc\ndef", 1, 1},
        {"last caret uses current line", "abc\ndef", 1, 1, false, "ab\ndef", 1, 1},
        {"first single-line reversed selection", "abc\ndef", 3, 1, true, "bc\ndef", 2, 0},
        {"last single-line reversed selection", "abc\ndef", 3, 1, false, "ab\ndef", 2, 1},
        {"first partial multiline boundaries", "abc\ndef\nxyz", 1, 9, true, "abc\nef\nyz", 1, 7},
        {"last partial multiline boundaries", "abc\ndef\nxyz", 1, 9, false, "ab\nde\nxyz", 1, 7},
        {"first reversed partial boundaries", "abc\ndef\nxyz", 9, 1, true, "abc\nef\nyz", 7, 1},
        {"last reversed partial boundaries", "abc\ndef\nxyz", 9, 1, false, "ab\nde\nxyz", 7, 1},
        {"first excludes end at line start", "abc\ndef\nxyz", 0, 8, true, "bc\nef\nxyz", 0, 6},
        {"last excludes end before text end", "abc\ndef\nxyz", 0, 8, false, "ab\nde\nxyz", 0, 6},
        {"first excludes start at previous text end", "abc\ndef\nxyz", 3, 7, true, "abc\nef\nxyz", 3, 6},
        {"last excludes start at previous text end", "abc\ndef\nxyz", 3, 7, false, "abc\nde\nxyz", 3, 6},
        {"first includes end at next text end", "abc\ndef\nxyz", 1, 7, true, "abc\nef\nxyz", 1, 6},
        {"last includes end at next text end", "abc\ndef\nxyz", 1, 7, false, "ab\nde\nxyz", 1, 5},
        {"first preserves mixed EOLs", "abc\r\ndef\nxyz\rtail", 1, 10, true, "abc\r\nef\nyz\rtail", 1, 8},
        {"last preserves mixed EOLs", "abc\r\ndef\nxyz\rtail", 1, 10, false, "ab\r\nde\nxyz\rtail", 1, 8},
        {"first preserves empty lines and final newline", "\r\nA\n\rB\r\n", 0, 9, true, "\r\n\n\r\r\n", 0, 6},
        {"last preserves empty lines and final newline", "\r\nA\n\rB\r\n", 0, 9, false, "\r\n\n\r\r\n", 0, 6},
        {"first removes complete CJK and supplementary scalars", "中A\n😀B\n", 0, 11, true, "A\nB\n", 0, 4},
        {"last removes complete CJK and supplementary scalars", "A中\nB😀", 0, 10, false, "A\nB", 0, 3},
        {"first retains independent combining mark", "e\u0301x\n", 0, 5, true, "\u0301x\n", 0, 4},
        {"last removes independent combining mark", "xe\u0301\n", 0, 5, false, "xe\n", 0, 3},
        {"first current line with supplementary caret", "😀x", 4, 4, true, "x", 0, 0},
        {"last current line with supplementary caret", "x😀", 5, 5, false, "x", 1, 1},
    };
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(editor, fixture.before, fixture.anchor, fixture.caret);
        const auto eol = editor->Send(Message::GetEOLMode);
        const auto encoding = frame->DocumentEncoding();
        Strip(frame, fixture.first);
        Require(TestEditor::Text(editor) == fixture.after, fixture.name);
        Range(editor, fixture.resultAnchor, fixture.resultCaret);
        Require(editor->Send(Message::GetModify) && editor->Send(Message::CanUndo), "strip creates text history");
        Require(editor->Send(Message::GetEOLMode) == eol && frame->DocumentEncoding() == encoding,
                "strip preserves EOL mode and encoding");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify) && editor->Send(Message::CanRedo),
                "one Undo restores complete original bytes and save point");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == fixture.after && editor->Send(Message::GetModify),
                "one Redo restores complete strip group");
    }
    std::puts(
        "PASS: 24 literal current-line/boundary/direction/EOL/Unicode fixtures with one-step Undo/Redo and save point");
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
    for (const bool first : {true, false}) {
        const int id =
            frame->GetMenuBar()->FindMenuItem("Edit", first ? "Strip First Character" : "Strip Last Character");
        for (const auto& fixture : {std::pair{"", 0}, std::pair{"\r\n\n\r", 0}, std::pair{"abc\ndef", 3}}) {
            TestEditor::ResetStream(editor, fixture.first, fixture.second, fixture.second == 3 ? 4 : fixture.second);
            const auto anchor = editor->Send(Message::GetAnchor);
            const auto caret = editor->Send(Message::GetCurrentPos);
            editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("!"));
            editor->Send(Message::Undo);
            // Insertion/Undo use the core's normal selection-history setting.
            editor->Send(Message::SetSel, anchor, caret);
            Strip(frame, first);
            Require(TestEditor::Text(editor) == fixture.first && !editor->Send(Message::CanUndo) &&
                        editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                    "no eligible nonempty line preserves undo, pending redo and save point");
            Range(editor, anchor, caret);
        }
        TestEditor::ResetStream(editor, "abc\ndef", 1, 6);
        editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, 1, 6);
        editor->Send(Message::SetReadOnly, 1);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(id), "original strip menu remains available in read-only text");
        Strip(frame, first);
        Require(TestEditor::Text(editor) == "abc\ndef" && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                "read-only core protects bytes and save point and disables undo/redo availability");
        Range(editor, 1, 6);
        for (const bool readOnly : {false, true})
            for (const bool empty : {false, true}) {
                editor->Send(Message::SetReadOnly, readOnly);
                editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
                editor->Send(Message::SetRectangularSelectionAnchor, 0);
                editor->Send(Message::SetRectangularSelectionCaret, empty ? 0 : 6);
                const auto anchor = editor->Send(Message::GetRectangularSelectionAnchor);
                const auto caret = editor->Send(Message::GetRectangularSelectionCaret);
                RectangleFeedback feedback;
                Strip(frame, first);
                Require(feedback.count == 1, "rectangle warns before empty-scope and read-only handling");
                Require(TestEditor::Text(editor) == "abc\ndef" && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo) == !readOnly && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::GetRectangularSelectionAnchor) == anchor &&
                            editor->Send(Message::GetRectangularSelectionCaret) == caret,
                        "rectangle retains text, selection, save point, undo and pending redo");
                Require(!(first ? editor->StripFirstCharacter() : editor->StripLastCharacter()),
                        "public editor command reports unsupported rectangle");
            }
        editor->Send(Message::SetReadOnly, 0);
        Require(editor->Send(Message::CanRedo), "read-only and rectangle commands preserve pending redo");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == "!abc\ndef", "pending redo remains executable after removing read-only");
    }
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    std::puts(
        "PASS: empty/no-eligible scopes, enabled read-only menus, rectangle-first feedback and preserved redo/history");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-strip-characters");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string first;
        std::string last;
    };
    const Fixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中A\r\n😀B\nxe\u0301",
         "\xef\xbb\xbf"
         "A\r\nB\ne\u0301",
         "\xef\xbb\xbf中\r\n😀\nxe"},
        {"gbk", FileEncoding::Gbk,
         "\xd6\xd0\xce\xc4"
         "A\rB"
         "\xd6\xd0\xce\xc4\n",
         "\xce\xc4"
         "A\r"
         "\xd6\xd0\xce\xc4\n",
         "\xd6\xd0\xce\xc4\rB"
         "\xd6\xd0\n"},
        {"utf16", FileEncoding::Utf16Le,
         std::string("\xff\xfe\x2d\x4e\x41\0\x0d\0\x0a\0\x3d\xd8\0\xde\x42\0\x0a\0\x78\0\x65\0\x01\x03", 24),
         std::string("\xff\xfe\x41\0\x0d\0\x0a\0\x42\0\x0a\0\x65\0\x01\x03", 16),
         std::string("\xff\xfe\x2d\x4e\x0d\0\x0a\0\x3d\xd8\0\xde\x0a\0\x78\0\x65\0", 18)},
    };
    for (const auto& fixture : fixtures)
        for (const bool first : {true, false}) {
            const auto path = directory / (std::string(fixture.name) + (first ? "-first.cpp" : "-last.cpp"));
            TestFiles::WriteBytes(path, fixture.before);
            editor->Send(Message::SetSavePoint);
            Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                    "open encoded fixture through application");
            const auto encoding = frame->DocumentEncoding();
            const auto bom = frame->DocumentHasBom();
            const auto eol = editor->Send(Message::GetEOLMode);
            editor->Send(Message::SelectAll);
            Strip(frame, first);
            const auto& after = first ? fixture.first : fixture.last;
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == after,
                    "strip Save preserves independent encoding, BOM, mixed EOL and final-newline bytes");
            Require(frame->DocumentEncoding() == encoding && frame->DocumentHasBom() == bom &&
                        editor->Send(Message::GetEOLMode) == eol && !editor->Send(Message::GetModify),
                    "Save retains encoding, BOM and EOL mode and establishes a save point");
            editor->Send(Message::Undo);
            Require(editor->Send(Message::GetModify) && frame->Save().status == SaveStatus::Saved &&
                        TestFiles::Bytes(path) == fixture.before,
                    "one Undo restores independently read original disk bytes");
            editor->Send(Message::Redo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == after,
                    "one Redo restores independently read stripped disk bytes");
        }
    std::filesystem::remove_all(directory);
    std::puts("PASS: both commands save exact UTF-8 BOM, GBK and UTF-16LE BOM bytes with Undo/Redo and save points");
}
class StripCharactersApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("alpha\r\nbeta"));
                editor->Send(Message::SetSel, 3, 1);
                Strip(frame, true);
                Require(TestEditor::Text(editor) == "lpha\r\nbeta", "single-line selection strips entire current line");
                std::puts("PASS: native Strip First Character preserves original current-line scope");
                Strip(frame, false);
                Require(TestEditor::Text(editor) == "lph\r\nbeta",
                        "single-line selection strips entire current line end");
                std::puts("PASS: native Strip Last Character preserves original current-line scope");
                Shortcuts(frame);
                Fixtures(frame);
                Protection(frame);
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
wxIMPLEMENT_APP(StripCharactersApp);
