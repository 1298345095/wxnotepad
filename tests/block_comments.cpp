#include "app/editor_frame.h"
#include "native_search_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "syntax/syntax_scheme.h"
#include <SciLexer.h>
#include <wx/modalhook.h>
#include <wx/app.h>
#include <wx/menu.h>
#include <wx/ffile.h>
#include <chrono>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Text(EditorControl* editor, const char* expected, const char* message) {
    const auto actual = TestEditor::Text(editor);
    if (actual != expected) std::fprintf(stderr, "expected=<%s> actual=<%s>\n", expected, actual.c_str());
    Require(actual == expected, message);
}
void Choose(EditorFrame* frame, const char* language) {
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language));
}
void Toggle(EditorFrame* frame) {
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment"));
}
void Cases(EditorFrame* frame) {
    using Scintilla::Message;
    auto* editor = frame->Editor();
    struct Case {
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        const char* after;
        intptr_t newAnchor;
        intptr_t newCaret;
    };
    const Case cases[] = {
        {"", 0, 0, "/**/", 2, 2},
        {"alpha", 5, 0, "/*alpha*/", 7, 2},
        {"中文", 0, 6, "/*中文*/", 2, 8},
        {"/*alpha*/", 2, 7, "alpha", 0, 5},
        {"/*alpha*/ tail", 3, 5, "alpha tail", 1, 3},
        {"/*alpha*/ tail", 5, 3, "alpha tail", 3, 1},
        {"/*alpha*/ tail", 0, 14, "/*/*alpha*/ tail*/", 2, 16},
        {"/*alpha*/", 9, 9, "/*alpha*//**/", 11, 11},
        {"/*outer /*inner*/ rest*/", 12, 12, "/*outer inner rest*/", 10, 10},
        {"/*outer /*inner*/ rest*/", 4, 12, "/*outer inner rest*/", 4, 10},
        {"/*alpha", 4, 4, "/*al/**/pha", 6, 6},
        {"// /*alpha*/\nnext", 7, 7, "// alpha\nnext", 5, 5},
        {"/** @param x */", 6, 6, "* @param x ", 4, 4},
        {"/*a*//**b*/", 2, 9, "/*/*a*//**b*/*/", 4, 11},
        {"//alpha", 3, 3, "//a/**/lpha", 5, 5},
    };
    for (const char* language : {"C/C++", "JSON"}) {
        Choose(frame, language);
        for (const auto& item : cases) {
            TestEditor::ResetStream(editor, item.before, item.anchor, item.caret);
            editor->Send(Message::Colourise, 0, -1);
            Toggle(frame);
            Text(editor, item.after, "original style-dependent insertion or innermost delimiter removal");
            TestEditor::RequireSelection(editor, item.newAnchor, item.newCaret, "original block-comment selection");
            Require(editor->Send(Message::CanUndo) && editor->Send(Message::GetModify), "comment edit creates history");
            editor->Send(Message::Undo);
            Text(editor, item.before, "one Undo restores whole original comment operation");
            Require(!editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify), "Undo restores save point");
            editor->Send(Message::Redo);
            Text(editor, item.after, "one Redo restores whole comment operation");
        }
    }
    std::puts("PASS: empty/reversed/UTF-8 selections, partial/outside ranges, caret/EOF, line/doc styles, innermost "
              "and crossing delimiters, grouped Undo/Redo");
}
void PausedStyles(EditorFrame* frame) {
    using Scintilla::Message;
    auto* editor = frame->Editor();
    Choose(frame, "C/C++");
    const auto& syntax = SyntaxFor(SyntaxLanguage::Cpp);
    TestEditor::ResetStream(editor, "/*alpha*/", 4, 4);
    editor->Send(Message::Colourise, 0, -1);
    editor->Send(Message::SetLexer, SCLEX_CONTAINER);
    Require(editor->Send(Message::GetStyleIndexAt, 4) != 0, "pausing lexer retains existing comment styles");
    Require(editor->ToggleBlockComments(syntax.blockCommentOpen, syntax.blockCommentClose, syntax.commentStyleLast),
            "public block-comment operation accepts retained styles");
    Text(editor, "alpha", "paused lexer still removes delimiters when current styles classify comment");
    TestEditor::ResetStream(editor, "/*alpha*/", 4, 4);
    editor->Send(Message::ClearDocumentStyle);
    Require(editor->Send(Message::GetStyleIndexAt, 4) == 0, "paused unstyled fixture has default style");
    Require(editor->ToggleBlockComments(syntax.blockCommentOpen, syntax.blockCommentClose, syntax.commentStyleLast),
            "public block-comment operation accepts pending styles");
    Text(editor, "/*al/**/pha*/", "paused unstyled comment inserts delimiters without forcing highlighting");
    TestEditor::RequireSelection(editor, 6, 6, "paused insertion uses original enclosure selection");
    ApplySyntaxLanguage(*editor, SyntaxLanguage::Cpp);
    std::puts("PASS: public command uses retained or pending styles while highlighting is paused");
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
    using Scintilla::Message;
    auto* editor = frame->Editor();
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment");
    for (const char* language : {"Python", "Plain Text"}) {
        Choose(frame, language);
        TestEditor::ResetStream(editor, "alpha", 0, 5);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(!frame->GetMenuBar()->IsEnabled(id), "original NoBlockComment disables native menu");
        wxCommandEvent command(wxEVT_MENU, id);
        frame->GetEventHandler()->ProcessEvent(command);
        Text(editor, "alpha", "unavailable block command preserves bytes");
        TestEditor::RequireSelection(editor, 0, 5, "unavailable block command preserves selection");
        Require(!editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                "unavailable command preserves history");
    }
    Choose(frame, "C/C++");
    TestEditor::ResetStream(editor, "alpha", 0, 5);
    Toggle(frame);
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 0, 5);
    editor->Send(Message::SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(id), "original block-comment menu remains available in read-only mode");
    Toggle(frame);
    Text(editor, "alpha", "core protects read-only insertion bytes");
    TestEditor::RequireSelection(editor, 2, 5, "read-only enclosure retains original selection repair");
    Require(!editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
            "read-only mode suppresses undo/redo availability and retains save point");
    Require(editor->Send(Message::GetStatus) == static_cast<intptr_t>(Scintilla::Status::OutsideDocument),
            "original host reports rejected out-of-range suffix through message status");
    editor->Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
    editor->Send(Message::SetReadOnly, 0);
    Require(editor->Send(Message::CanRedo), "read-only insertion preserves pending redo after unlocking");
    TestEditor::ResetStream(editor, "/*alpha*/", 4, 4);
    editor->Send(Message::Colourise, 0, -1);
    editor->Send(Message::SetReadOnly, 1);
    Toggle(frame);
    Text(editor, "/*alpha*/", "core protects read-only removal bytes");
    TestEditor::RequireSelection(editor, 4, 4, "read-only removal retains core selection");
    Require(!editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
            "read-only removal preserves history");
    TestEditor::ResetStream(editor, "alpha\nbeta", 0, 5);
    Toggle(frame);
    editor->Send(Message::Undo);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 0);
    editor->Send(Message::SetRectangularSelectionCaret, 7);
    RectangleFeedback feedback;
    Toggle(frame);
    Require(feedback.count == 1, "rectangle insertion reports original unsupported enclosure");
    Text(editor, "alpha\nbeta", "rectangle rejection preserves bytes");
    Require(!editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify) &&
                editor->Send(Message::GetRectangularSelectionAnchor) == 0 &&
                editor->Send(Message::GetRectangularSelectionCaret) == 7,
            "rectangle rejection preserves history, save point, pending redo and selection");
    TestEditor::ResetStream(editor, "/*alpha\nbeta*/");
    editor->Send(Message::Colourise, 0, -1);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 2);
    editor->Send(Message::SetRectangularSelectionCaret, 10);
    Toggle(frame);
    Text(editor, "alpha\nbeta", "original uncomment runs before rectangular enclosure rejection");
    Require(feedback.count == 1 && editor->Send(Message::SelectionIsRectangle),
            "rectangle removal keeps original mode without feedback");
    std::puts(
        "PASS: original language availability, read-only selection/history and rectangle insertion/removal behavior");
}
void MultipleSelections(EditorFrame* frame) {
    using Scintilla::Message;
    auto* editor = frame->Editor();
    Choose(frame, "C/C++");
    TestEditor::ResetStream(editor, "alpha beta", 0, 5);
    editor->Send(Message::AddSelection, 10, 6);
    editor->Send(Message::SetMainSelection, 0);
    Toggle(frame);
    Text(editor, "/*alpha*/ beta", "enclosure edits main selection");
    Require(editor->Send(Message::GetSelections) == 1, "original enclosure resets to one selection");
    TestEditor::RequireSelection(editor, 2, 7, "main enclosure selection");
    TestEditor::ResetStream(editor, "/*alpha*/ beta", 4, 4);
    editor->Send(Message::AddSelection, 14, 10);
    editor->Send(Message::SetMainSelection, 0);
    editor->Send(Message::Colourise, 0, -1);
    Toggle(frame);
    Text(editor, "alpha beta", "uncomment removes main enclosing comment");
    Require(editor->Send(Message::GetSelections) == 2 && editor->Send(Message::GetSelectionNAnchor, 1) == 6 &&
                editor->Send(Message::GetSelectionNCaret, 1) == 10,
            "original deletion preserves and adjusts additional selections");
    TestEditor::RequireSelection(editor, 2, 2, "uncomment main caret adjustment");
    std::puts("PASS: original operation-specific multiple-selection behavior");
}
void SavedBytes(EditorFrame* frame) {
    using Scintilla::Message;
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-block-comment");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string after;
    };
    const Fixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中文\r\nB\nlast", "\xef\xbb\xbf/*中文\r\nB\nlast*/"},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\xce\xc4\rB\r\nlast\n", "/*\xd6\xd0\xce\xc4\rB\r\nlast\n*/"},
        {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x87\x65\x0d\0\x0a\0\x42\0", 12),
         std::string("\xff\xfe/\0*\0\x2d\x4e\x87\x65\x0d\0\x0a\0\x42\0*\0/\0", 20)},
    };
    for (const char* extension : {"cpp", "json"}) {
        for (const auto& fixture : fixtures) {
            const auto path = directory / (std::string(fixture.name) + "." + extension);
            TestFiles::WriteBytes(path, fixture.before);
            editor->Send(Message::SetSavePoint);
            Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                    "open block-comment fixture through application");
            editor->Send(Message::SelectAll);
            Toggle(frame);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                    "block insertion Save preserves independent encoded/BOM/mixed-EOL bytes");
            editor->Send(Message::Undo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                    "one insertion Undo saves original bytes");
            editor->Send(Message::Redo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                    "one insertion Redo saves commented bytes");
            editor->Send(Message::Colourise, 0, -1);
            Toggle(frame);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                    "styled block removal Save restores original independent bytes");
            editor->Send(Message::Undo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                    "one removal Undo saves commented bytes");
            editor->Send(Message::Redo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                    "one removal Redo saves original bytes");
        }
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: C++/JSON insertion/removal and Undo/Redo save independent UTF-8 BOM, GBK and UTF-16LE BOM bytes "
              "with mixed EOLs and final newline");
}
void NativeKey(EditorControl* editor, bool shift = false, bool alt = false) {
#ifdef __WXMSW__
    const bool handled = NativeSearch::WindowsShortcut(
        editor, 'Q', {{VK_CONTROL, 0x80}, {VK_SHIFT, shift ? 0x80 : 0}, {VK_MENU, alt ? 0x80 : 0}});
    Require(handled || shift || alt, "native Ctrl+Q accelerator is handled");
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys use private display helper");
    bool received = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&received](wxKeyEvent& event) {
        if (event.GetKeyCode() == 'Q') received = true;
        event.Skip();
    });
    editor->PushEventHandler(&observer);
    {
        wxFFile request(focusFile, "wb");
        request.Write(wxString("key:ctrl+") + (shift ? "shift+" : "") + (alt ? "alt+" : "") + "q");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!received && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "native private-display Q release reaches editor");
#endif
    wxYield();
}
void Keys(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const char* language : {"C/C++", "JSON"}) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language));
        TestEditor::ResetStream(editor, "alpha", 0, 5);
        NativeKey(editor);
        Require(TestEditor::Text(editor) == "/*alpha*/", "native Ctrl+Q uses language block delimiters");
        editor->Send(Scintilla::Message::Colourise, 0, -1);
        NativeKey(editor);
        Require(TestEditor::Text(editor) == "alpha", "native Ctrl+Q removes styled comment");
        NativeKey(editor, true);
        NativeKey(editor, false, true);
        Require(TestEditor::Text(editor) == "alpha", "Shift or Alt added to Ctrl+Q leaves comments unchanged");
    }
    std::puts("PASS: native Ctrl+Q and exact Shift/Alt modifiers for C++ and JSON");
}
class BlockCommentsApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                auto* editor = frame->Editor();
                TestEditor::ResetStream(editor, "alpha", 0, 5);
                const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Block Comment");
                Require(id != wxNOT_FOUND, "Edit menu exposes Toggle Block Comment");
                NativeSearch::Command(frame, id);
                Require(TestEditor::Text(editor) == "/*alpha*/", "C++ selection uses original block delimiters");
                TestEditor::RequireSelection(editor, 2, 7, "selected content remains selected");
                TestEditor::ResetStream(editor, "/*alpha*/", 4, 4);
                editor->Send(Scintilla::Message::Colourise, 0, -1);
                NativeSearch::Command(frame, id);
                Require(TestEditor::Text(editor) == "alpha",
                        "caret inside styled comment removes enclosing delimiters");
                TestEditor::RequireSelection(editor, 2, 2, "uncomment uses core selection adjustment");
                Keys(frame);
                Cases(frame);
                PausedStyles(frame);
                Protection(frame);
                MultipleSelections(frame);
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
wxIMPLEMENT_APP(BlockCommentsApp);
