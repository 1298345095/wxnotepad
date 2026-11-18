#include "app/editor_frame.h"
#include <array>
#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <wx/app.h>
#include <wx/menu.h>
#include <wx/ffile.h>
#include <chrono>
#include <wx/modalhook.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Toggle(EditorFrame* frame) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment");
    Require(id != wxNOT_FOUND, "Edit menu exposes Toggle Line Comment");
    NativeSearch::Command(frame, id);
}
void Choose(EditorFrame* frame, const char* language) {
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", language));
}
void Range(EditorControl* editor, intptr_t anchor, intptr_t caret) {
    TestEditor::RequireSelection(editor, anchor, caret,
                                 "original anchor/caret adjustment preserves selection direction");
}
void NativeKey(EditorControl* editor, bool divide, bool shift = false, bool alt = false) {
#ifdef __WXMSW__
    const bool handled =
        NativeSearch::WindowsShortcut(editor, divide ? VK_DIVIDE : VK_OEM_2,
                                      {{VK_CONTROL, 0x80}, {VK_SHIFT, shift ? 0x80 : 0}, {VK_MENU, alt ? 0x80 : 0}});
    Require(handled || shift || alt,
            divide ? "native Ctrl+Divide accelerator is handled" : "native Ctrl+/ accelerator is handled");
#else
    wxString focusFile;
    NativeKeys::FocusEditor(static_cast<EditorFrame*>(wxGetTopLevelParent(editor)), "wxnotepad", focusFile);
    bool received = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&received, divide](wxKeyEvent& event) {
        // Previous native input can still release modifiers after this request.
        const int key = event.GetKeyCode();
        if (divide ? key == WXK_NUMPAD_DIVIDE || key == WXK_DIVIDE : key == '/' || key == '?') received = true;
        event.Skip();
    });
    editor->PushEventHandler(&observer);
    {
        wxFFile request(focusFile, "wb");
        request.Write(wxString("key:ctrl+") + (shift ? "shift+" : "") + (alt ? "alt+" : "") +
                      (divide ? "KP_Divide" : "slash"));
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!received && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, divide ? "native private-display Divide release reaches editor"
                             : "native private-display slash release reaches editor");
#endif
    wxYield();
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
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment");
    TestEditor::ResetStream(editor, "alpha\nbeta", 0, 5);
    Choose(frame, "Plain Text");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(id), "Plain Text has original no-line-comment capability");
    wxCommandEvent plainCommand(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(plainCommand);
    Require(TestEditor::Text(editor) == "alpha\nbeta" && !editor->Send(CanUndo),
            "Plain Text command retains bytes and history");
    Choose(frame, "C/C++");
    editor->Send(SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(id),
            "original read-only line-comment menu retains its lexer capability gate");
    wxCommandEvent readOnlyCommand(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(readOnlyCommand);
    Require(TestEditor::Text(editor) == "alpha\nbeta" && !editor->Send(CanUndo) && !editor->Send(GetModify),
            "read-only command retains bytes, save point and history");
    editor->Send(SetReadOnly, 0);
    Toggle(frame);
    editor->Send(Undo);
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Rectangle)));
    editor->Send(SetRectangularSelectionAnchor, 0);
    editor->Send(SetRectangularSelectionCaret, 7);
    const auto anchor = editor->Send(GetRectangularSelectionAnchor);
    const auto caret = editor->Send(GetRectangularSelectionCaret);
    RectangleFeedback feedback;
    Toggle(frame);
    Require(feedback.count == 1, "rectangular selection reports original unsupported operation");
    Require(TestEditor::Text(editor) == "alpha\nbeta" && !editor->Send(CanUndo) && editor->Send(CanRedo) &&
                !editor->Send(GetModify) && editor->Send(GetRectangularSelectionAnchor) == anchor &&
                editor->Send(GetRectangularSelectionCaret) == caret,
            "unsupported rectangle retains bytes, selection, save point, undo and pending redo");
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream)));
    std::puts("PASS: Plain Text, read-only and original rectangular-selection warning preserve document/history");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-line-comment");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string after;
    };
    const std::array<Fixture, 3> fixtures = {{
        {"utf8", FileEncoding::Utf8, "\x{ef}\x{bb}\x{bf}中文\r\nB\nlast", "\x{ef}\x{bb}\x{bf}// 中文\r\n// B\n// last"},
        {"gbk", FileEncoding::Gbk, "\x{d6}\x{d0}\x{ce}\x{c4}\rB\r\nlast\n",
         "// \x{d6}\x{d0}\x{ce}\x{c4}\r// B\r\n// last\n"},
        {"utf16", FileEncoding::Utf16Le,
         std::string("\x{ff}\x{fe}\x{2d}\x{4e}\x{87}\x{65}\x{0d}\0\x{0a}\0\x{42}\0", 12),
         std::string("\x{ff}\x{fe}/\0/\0 \0\x{2d}\x{4e}\x{87}\x{65}\x{0d}\0\x{0a}\0/\0/\0 \0\x{42}\0", 24)},
    }};
    for (const auto& fixture : fixtures) {
        using enum Scintilla::Message;
        const auto path = directory / (std::string(fixture.name) + ".cpp");
        {
            std::ofstream file(path, std::ios::binary);
            file.write(fixture.before.data(), static_cast<std::streamsize>(fixture.before.size()));
        }
        editor->Send(SetSavePoint);
        Require(frame->Open(wxString::FromUTF8(path.string()), fixture.encoding).status == OpenStatus::Opened,
                "open encoded fixture through application");
        editor->Send(SelectAll);
        Toggle(frame);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "comment Save preserves exact encoding, BOM, EOL bytes and final newline");
        Require(frame->GetStatusBar()->GetStatusText().Contains("C/C++"), "comment retains document language");
        editor->Send(Undo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "one Undo restores independent original disk bytes");
        editor->Send(Redo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "one Redo restores independent commented disk bytes");
    }
    std::filesystem::remove_all(directory);
    std::puts(
        "PASS: independent UTF-8 BOM, GBK, UTF-16LE BOM disk bytes, mixed EOLs, final newline, language and Undo/Redo");
}
void Selections(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    editor->Send(SetUseTabs, 0);
    const char* original = "    alpha\r\n  beta\n\n    // kept\rnext";
    const char* commented = "  //   alpha\r\n  // beta\n  //\n  //   // kept\rnext";
    TestEditor::ResetStream(editor, original);
    editor->Send(SetSel, 3, editor->Send(PositionFromLine, 4));
    Toggle(frame);
    Require(TestEditor::Text(editor) == commented, "common column, blank line and deeper comment match original");
    Range(editor, 0, editor->Send(PositionFromLine, 4));
    editor->Send(Undo);
    Require(TestEditor::Text(editor) == original && !editor->Send(CanUndo) && !editor->Send(GetModify),
            "one Undo restores exact mixed EOL bytes and save point");
    editor->Send(Redo);
    Require(TestEditor::Text(editor) == commented, "one Redo restores whole comment group");
    editor->Send(SetSel, editor->Send(PositionFromLine, 4), 3);
    Toggle(frame);
    Require(TestEditor::Text(editor) == original, "uncomment reverses whole original multiline edit");
    Range(editor, editor->Send(PositionFromLine, 4), 0);
    TestEditor::ResetStream(editor, "  // alpha\n  plain\n    //  deep\n  //\nend");
    editor->Send(SetSel, 0, editor->Send(PositionFromLine, 4));
    Toggle(frame);
    Require(TestEditor::Text(editor) == "  alpha\n  plain\n     deep\n\nend",
            "first commented line deletes only comments and one pad");
    TestEditor::ResetStream(editor, "  plain\n  // existing\n");
    editor->Send(SelectAll);
    Toggle(frame);
    Require(TestEditor::Text(editor) == "  // plain\n  // existing\n",
            "add action leaves already-commented line at common column alone");
    TestEditor::ResetStream(editor, "\talpha\n    beta\n");
    editor->Send(SetTabWidth, 4);
    editor->Send(SelectAll);
    Toggle(frame);
    Require(TestEditor::Text(editor) == "\t// alpha\n    // beta\n", "common column uses visual tab indentation");
    TestEditor::ResetStream(editor, "   ", 3, 3);
    Toggle(frame);
    Require(TestEditor::Text(editor) == "   //", "single blank line receives prefix without trailing pad");
    Range(editor, 3, 3);
    std::puts("PASS: common indentation, mixed/comment-only/blank lines, reverse selections, excluded endpoint, caret "
              "and one-step Undo/Redo");
}
class LineCommentsApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native line-comment view is presented before menu and keyboard actions");
                auto* editor = frame->Editor();
                wxString configured;
                const bool spaced = !wxGetEnv("WXNOTEPAD_COMMENT_SPACED", &configured) || configured == "1";
                Choose(frame, "C/C++");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("  call();\r\nnext();"));
                editor->Send(Message::GotoPos, 4);
                Toggle(frame);
                Require(TestEditor::Text(editor) == (spaced ? "  // call();\r\nnext();" : "  //call();\r\nnext();"),
                        "current C++ line uses configured original prefix spacing");
                std::puts("PASS: native Toggle Line Comment menu uses original C++ prefix and default spacing");
                if (spaced) Selections(frame);
                Protection(frame);
                for (const auto* language : {"C/C++", "Python", "JSON"}) {
                    Choose(frame, language);
                    TestEditor::ResetStream(editor, "  value\nnext", 4, 4);
                    NativeKey(editor, false);
                    const std::string expected = std::string(language) == "Python"
                                                     ? (spaced ? "  # value\nnext" : "  #value\nnext")
                                                     : (spaced ? "  // value\nnext" : "  //value\nnext");
                    const auto actual = TestEditor::Text(editor);
                    if (actual != expected)
                        std::fprintf(stderr, "Ctrl+/ language=%s status=%s expected=<%s> actual=<%s> focusEditor=%d\n",
                                     language, frame->GetStatusBar()->GetStatusText().utf8_str().data(),
                                     expected.c_str(), actual.c_str(), wxWindow::FindFocus() == editor);
                    Require(actual == expected, "actual Ctrl+/ uses scheme prefix and settings spacing");
                    NativeKey(editor, true);
                    Require(TestEditor::Text(editor) == "  value\nnext",
                            "actual Ctrl+Divide toggles same shared command");
                    NativeKey(editor, false, true);
                    NativeKey(editor, true, false, true);
                    Require(TestEditor::Text(editor) == "  value\nnext",
                            "Shift or Alt added to shortcut does not toggle comments");
                }
                std::puts("PASS: actual native Ctrl+/, Ctrl+Divide and exact modifiers for C/C++, Python and JSON");
                if (spaced) SavedBytes(frame);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
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
wxIMPLEMENT_APP(LineCommentsApp);
