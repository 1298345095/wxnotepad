#include "editor_test_helpers.h"
#include "native_search_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <wx/ffile.h>
#include <chrono>
#include <memory>
#include <tuple>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Mapping(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "Straße ﬃ ΐ", 0, 14);
    editor->Send(Message::CustomCaseMapping, 1);
    Require(TestEditor::Text(editor) == "STRASSE FFI Ϊ́", "pinned Unicode mapping expands sharp s, ligature and Greek");
    TestEditor::RequireSelection(editor, 0, 18, "expansion preserves full selection");
}
void Menus(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const int upper = frame->GetMenuBar()->FindMenuItem("Edit", "Upper Case");
    const int lower = frame->GetMenuBar()->FindMenuItem("Edit", "Lower Case");
    Require(upper != wxNOT_FOUND && lower != wxNOT_FOUND, "native Edit menu exposes Upper Case and Lower Case");
    TestEditor::ResetStream(editor, "é σς 𐐨 é中😀\r\n", 0, 25);
    NativeSearch::Command(frame, upper);
    Require(TestEditor::Text(editor) == "É ΣΣ 𐐀 É中😀\r\n",
            "native upper maps accents, Greek and supplementary letters");
    TestEditor::RequireSelection(editor, 0, 25, "equal-byte upper preserves selection");
    TestEditor::ResetStream(editor, "É İ K ΣΩ 𐐀 Á中😀\r\n", 32, 0);
    NativeSearch::Command(frame, lower);
    Require(TestEditor::Text(editor) == "é i̇ k σω 𐐨 á中😀\r\n", "native lower uses pinned expansion and contraction");
    TestEditor::RequireSelection(editor, 31, 0, "reversed lower selection tracks changed UTF-8 byte length");
}
void NativeKey(EditorControl* editor, bool shift, bool alt = false) {
#ifdef __WXMSW__
    NativeSearch::WindowsShortcut(editor, 'U',
                                  {{VK_CONTROL, 0x80},
                                   {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)},
                                   {VK_MENU, static_cast<BYTE>(alt ? 0x80 : 0)}});
#else
    editor->SetFocus();
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native shortcuts require private display");
    {
        wxFFile request(focusFile, "wb");
        request.Write("focus:wxnotepad");
    }
    bool focused = false;
    const auto focusDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!focused && std::chrono::steady_clock::now() < focusDeadline) {
        wxYield();
        wxFFile request(focusFile, "rb");
        wxString pending;
        if (request.IsOpened()) request.ReadAll(&pending);
        focused = pending.empty();
        if (!focused) wxMilliSleep(10);
    }
    Require(focused, "private display acknowledges native focus");
    bool received = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&received](wxKeyEvent& event) {
        if (event.GetKeyCode() == 'U') received = true;
        event.Skip();
    });
    editor->PushEventHandler(&observer);
    {
        wxFFile request(focusFile, "wb");
        request.Write(wxString("key:ctrl+") + (shift ? "shift+" : "") + (alt ? "alt+" : "") + "u");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!received && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "native U release reaches owned editor");
#endif
    wxYield();
}
void Shortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "Straße ﬃ ΐ", 0, 14);
    NativeKey(editor, true);
    Require(TestEditor::Text(editor) == "STRASSE FFI Ϊ́", "native Ctrl+Shift+U invokes upper");
    TestEditor::ResetStream(editor, "É İ K ΣΩ 𐐀 Á中😀\r\n", 0, 32);
    NativeKey(editor, false);
    Require(TestEditor::Text(editor) == "é i̇ k σω 𐐨 á中😀\r\n", "native Ctrl+U invokes lower");
    for (const bool shift : {false, true}) {
        TestEditor::ResetStream(editor, "aB", 0, 2);
        NativeKey(editor, shift, true);
        Require(TestEditor::Text(editor) == "aB" && !editor->Send(Message::CanUndo),
                "additional Alt modifier does not invoke upper or lower");
    }
    std::puts("PASS: native Ctrl+Shift+U, Ctrl+U and exact additional-Alt routing");
}
void History(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const auto& [label, before, after] : {std::tuple{"Upper Case", "Straße ﬃ ΐ", "STRASSE FFI Ϊ́"},
                                               std::tuple{"Lower Case", "STRASSE FFI Ϊ́", "strasse ffi ΐ"}}) {
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
        TestEditor::ResetStream(editor, before);
        editor->Send(Message::SelectAll);
        const auto eol = editor->Send(Message::GetEOLMode);
        const auto encoding = frame->DocumentEncoding();
        NativeSearch::Command(frame, id);
        Require(TestEditor::Text(editor) == after && editor->Send(Message::CanUndo) && editor->Send(Message::GetModify),
                "case change leaves save point with undo history");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == before && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "one Undo restores entire original case and save point");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == after && editor->Send(Message::GetModify) &&
                    editor->Send(Message::GetEOLMode) == eol && frame->DocumentEncoding() == encoding,
                "one Redo restores case change and preserves EOL and external encoding");
    }
    for (const int mapping : {1, 2}) {
        TestEditor::ResetStream(editor, "中😀\r\n", 0, 9);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, 0, 9);
        editor->Send(Message::CustomCaseMapping, mapping);
        Require(TestEditor::Text(editor) == "中😀\r\n" && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                "uncased selection preserves clean history and pending redo");
        TestEditor::RequireSelection(editor, 0, 9, "unchanged mapping retains selection");
        editor->Send(Message::SetSel, 3, 3);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(!frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", "Upper Case")) &&
                    !frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", "Lower Case")),
                "original case menus require a nonempty selection");
        editor->Send(Message::CustomCaseMapping, mapping);
        TestEditor::RequireSelection(editor, 3, 3, "empty selection maps no text and keeps caret");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == "中😀\r\n!", "unchanged and empty commands retain executable redo");
    }
    TestEditor::ResetStream(editor, "aB", 0, 2);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 0, 2);
    editor->Send(Message::SetReadOnly, 1);
    const int upper = frame->GetMenuBar()->FindMenuItem("Edit", "Upper Case");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(upper), "original case menu remains enabled for read-only selection");
    NativeSearch::Command(frame, upper);
    Require(TestEditor::Text(editor) == "aB" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
            "read-only core retains text, save point and undo availability");
    // The pinned core repairs the range using the rejected insertion's zero length.
    TestEditor::RequireSelection(editor, 0, 1, "read-only case retains original core selection repair");
    editor->Send(Message::SetReadOnly, 0);
    Require(editor->Send(Message::CanRedo), "read-only conversion retains pending redo");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "aB!", "read-only conversion retains executable redo");
    std::puts("PASS: grouped Undo/Redo, save point, unchanged and empty selections, original read-only range repair");
}
void SelectionShapes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const int upper = frame->GetMenuBar()->FindMenuItem("Edit", "Upper Case");
    // CaseConvertString's pinned capacity check rejects a result exactly three times
    // the input size. The original host passes that empty result to the live core.
    TestEditor::ResetStream(editor, "ΐ", 0, 2);
    NativeSearch::Command(frame, upper);
    Require(TestEditor::Text(editor).empty() && editor->Send(Message::GetModify),
            "exact maximum-expansion span retains original empty conversion result");
    TestEditor::RequireSelection(editor, 0, 0, "empty maximum-expansion result repairs selection");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "ΐ" && !editor->Send(Message::GetModify),
            "original maximum-expansion result is undoable");
    TestEditor::ResetStream(editor, "ΐa x ΐb", 0, 3);
    editor->Send(Message::SetMultipleSelection, 1);
    editor->Send(Message::AddSelection, 6, 9);
    editor->Send(Message::AddSelection, 4, 4);
    editor->Send(Message::SetMainSelection, 0);
    NativeSearch::Command(frame, upper);
    Require(TestEditor::Text(editor) == "Ϊ́A x Ϊ́B" && editor->Send(Message::GetSelections) == 3 &&
                editor->Send(Message::GetMainSelection) == 0,
            "multiple selections convert their own spans and retain empty range and main selection");
    Require(editor->Send(Message::GetSelectionNAnchor, 0) == 0 && editor->Send(Message::GetSelectionNCaret, 0) == 7 &&
                editor->Send(Message::GetSelectionNAnchor, 1) == 17 &&
                editor->Send(Message::GetSelectionNCaret, 1) == 10 &&
                editor->Send(Message::GetSelectionNAnchor, 2) == 8 && editor->Send(Message::GetSelectionNCaret, 2) == 8,
            "expansion moves forward, reversed and empty multiple-selection byte endpoints");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "ΐa x ΐb" && !editor->Send(Message::GetModify),
            "one Undo restores all multiple-selection case conversions");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "Ϊ́A x Ϊ́B", "one Redo restores all multiple selections");
    for (const bool reversed : {false, true}) {
        TestEditor::ResetStream(editor, "aß\nbé");
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, reversed ? 7 : 0);
        editor->Send(Message::SetRectangularSelectionCaret, reversed ? 0 : 7);
        NativeSearch::Command(frame, upper);
        Require(TestEditor::Text(editor) == "ASS\nBÉ" && editor->Send(Message::GetSelections) == 2 &&
                    editor->Send(Message::GetSelectionMode) ==
                        static_cast<intptr_t>(Scintilla::SelectionMode::Rectangle),
                "forward and reverse rectangles convert each row with original shape");
        // ChangeCaseOfSelection restores each row, while the rectangular endpoints
        // retain Selection::MovePositions' separate deletion/insertion movement.
        TestEditor::RequireSelection(editor, reversed ? 4 : 3, reversed ? 3 : 4,
                                     "rectangle keeps original automatic endpoint movement");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "aß\nbé" && !editor->Send(Message::GetModify),
                "rectangle conversion is one undo group");
    }
    TestEditor::ResetStream(editor, "a\nx");
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 3);
    editor->Send(Message::SetRectangularSelectionAnchorVirtualSpace, 1);
    editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, 3);
    NativeSearch::Command(frame, upper);
    Require(TestEditor::Text(editor) == "a\nx" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify) &&
                editor->Send(Message::GetRectangularSelectionAnchorVirtualSpace) == 1 &&
                editor->Send(Message::GetRectangularSelectionCaretVirtualSpace) == 3,
            "virtual-only rectangle keeps empty byte ranges, virtual columns and history");
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    editor->Send(Message::SetMultipleSelection, 0);
    std::puts("PASS: multiple forward/reversed/empty selections, expansion endpoints, rectangles and virtual space");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        TestFiles::EncodedFixture file;
        bool upper;
    };
    const Fixture fixtures[] = {
        {{"utf8-upper", FileEncoding::Utf8, "\xef\xbb\xbfStraße ﬃ ΐ\r\n", "\xef\xbb\xbfSTRASSE FFI Ϊ́\r\n"}, true},
        {{"gbk-upper", FileEncoding::Gbk,
          "\xd6\xd0"
          "a\r\nB",
          "\xd6\xd0"
          "A\r\nB"},
         true},
        {{"gbk-lower", FileEncoding::Gbk,
          "\xd6\xd0"
          "a\r\nB",
          "\xd6\xd0"
          "a\r\nb"},
         false},
        {{"utf16-upper", FileEncoding::Utf16Le, std::string("\xff\xfe\x61\0\xe9\0\x01\xd8\x28\xdc\r\0\n\0", 14),
          std::string("\xff\xfe\x41\0\xc9\0\x01\xd8\0\xdc\r\0\n\0", 14)},
         true},
        {{"utf16-lower", FileEncoding::Utf16Le, std::string("\xff\xfe\x41\0\x30\x01\x2a\x21\x01\xd8\0\xdc\r\0\n\0", 16),
          std::string("\xff\xfe\x61\0\x69\0\x07\x03\x6b\0\x01\xd8\x28\xdc\r\0\n\0", 18)},
         false},
    };
    const auto directory = TestFiles::TemporaryDirectory("case-conversion");
    std::filesystem::create_directory(directory);
    for (const auto& fixture : fixtures) {
        const auto path = directory / fixture.file.name;
        TestFiles::WriteBytes(path, fixture.file.before);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.file.encoding).status == OpenStatus::Opened,
                "open case fixture through application file operation");
        const auto bom = frame->DocumentHasBom();
        const auto eol = editor->Send(Message::GetEOLMode);
        editor->Send(Message::SelectAll);
        NativeSearch::Command(frame,
                              frame->GetMenuBar()->FindMenuItem("Edit", fixture.upper ? "Upper Case" : "Lower Case"));
        TestFiles::RequireSavedUndoRedo(frame, path, fixture.file,
                                        {"case Save matches independently reopened encoded bytes",
                                         "one Undo restores independently reopened original bytes",
                                         "one Redo restores independently reopened converted bytes"});
        Require(frame->DocumentEncoding() == fixture.file.encoding && frame->DocumentHasBom() == bom &&
                    editor->Send(Message::GetEOLMode) == eol && !editor->Send(Message::GetModify),
                "case Save retains external encoding, BOM, EOL mode and final save point");
    }
    const auto path = directory / "gbk-unrepresentable";
    TestFiles::WriteBytes(path, "\xa8\xa6"); // CP936 é has no representable uppercase É.
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
            "open GBK letter with unrepresentable uppercase");
    editor->Send(Message::SelectAll);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Upper Case"));
    Require(TestEditor::Text(editor) == "É", "case mapping remains UTF-8 despite external GBK codec");
    bool asked = false;
    const auto cancelled = frame->Save([&asked] {
        asked = true;
        return GbkLossChoice::Cancel;
    });
    Require(asked && cancelled.status == SaveStatus::Cancelled && TestFiles::Bytes(path) == "\xa8\xa6" &&
                editor->Send(Message::GetModify) && frame->DocumentEncoding() == FileEncoding::Gbk,
            "existing GBK loss cancellation preserves disk bytes and unsaved Unicode result");
    Require(frame->Save([] { return GbkLossChoice::Replace; }).status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == "?" && TestEditor::Text(editor) == "É" && !editor->Send(Message::GetModify),
            "existing explicit GBK replacement saves literal replacement byte and retains Unicode editor text");
    std::puts("PASS: exact UTF-8 BOM, GBK, UTF-16LE BOM bytes, Undo/Redo and unrepresentable-GBK decisions");
}
class CaseConversionApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                Mapping(frame);
                Menus(frame);
                Shortcuts(frame);
                History(frame);
                SelectionShapes(frame);
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
wxIMPLEMENT_APP(CaseConversionApp);
