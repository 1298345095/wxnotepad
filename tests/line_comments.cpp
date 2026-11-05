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
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

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
void Set(EditorControl* editor, const char* text, intptr_t anchor = 0, intptr_t caret = 0) {
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::SetSel, anchor, caret);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
void Choose(EditorFrame* frame, const char* language) {
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language));
}
void Range(EditorControl* editor, intptr_t anchor, intptr_t caret) {
    if (editor->Send(Message::GetAnchor) != anchor || editor->Send(Message::GetCurrentPos) != caret)
        std::fprintf(stderr, "Selection anchor/caret: %lld/%lld; expected %lld/%lld\n",
                     static_cast<long long>(editor->Send(Message::GetAnchor)),
                     static_cast<long long>(editor->Send(Message::GetCurrentPos)), static_cast<long long>(anchor),
                     static_cast<long long>(caret));
    Require(editor->Send(Message::GetAnchor) == anchor && editor->Send(Message::GetCurrentPos) == caret,
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
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys use private display helper");
    bool received = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&received](wxKeyEvent& event) {
        received = true;
        event.Skip();
    });
    editor->PushEventHandler(&observer);
    {
        wxFFile request(focusFile, "wb");
        request.Write(wxString("key:ctrl+") + (shift ? "shift+" : "") + (alt ? "alt+" : "") +
                      (divide ? "KP_Divide" : "slash"));
    }
    wxStopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "native private-display comment key reaches editor");
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
    auto* editor = frame->Editor();
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Toggle Line Comment");
    Set(editor, "alpha\nbeta", 0, 5);
    Choose(frame, "Plain Text");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(id), "Plain Text has original no-line-comment capability");
    wxCommandEvent plainCommand(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(plainCommand);
    Require(TestEditor::Text(editor) == "alpha\nbeta" && !editor->Send(Message::CanUndo),
            "Plain Text command retains bytes and history");
    Choose(frame, "C/C++");
    editor->Send(Message::SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(id), "read-only state disables line-comment menu");
    wxCommandEvent readOnlyCommand(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(readOnlyCommand);
    Require(TestEditor::Text(editor) == "alpha\nbeta" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "read-only command retains bytes, save point and history");
    editor->Send(Message::SetReadOnly, 0);
    Toggle(frame);
    editor->Send(Message::Undo);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 0);
    editor->Send(Message::SetRectangularSelectionCaret, 7);
    const auto anchor = editor->Send(Message::GetRectangularSelectionAnchor);
    const auto caret = editor->Send(Message::GetRectangularSelectionCaret);
    RectangleFeedback feedback;
    Toggle(frame);
    Require(feedback.count == 1, "rectangular selection reports original unsupported operation");
    Require(TestEditor::Text(editor) == "alpha\nbeta" && !editor->Send(Message::CanUndo) &&
                editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify) &&
                editor->Send(Message::GetRectangularSelectionAnchor) == anchor &&
                editor->Send(Message::GetRectangularSelectionCaret) == caret,
            "unsupported rectangle retains bytes, selection, save point, undo and pending redo");
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
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
    const Fixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中文\r\nB\nlast", "\xef\xbb\xbf// 中文\r\n// B\n// last"},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\xce\xc4\rB\r\nlast\n", "// \xd6\xd0\xce\xc4\r// B\r\n// last\n"},
        {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x87\x65\x0d\0\x0a\0\x42\0", 12),
         std::string("\xff\xfe/\0/\0 \0\x2d\x4e\x87\x65\x0d\0\x0a\0/\0/\0 \0\x42\0", 24)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".cpp");
        {
            std::ofstream file(path, std::ios::binary);
            file.write(fixture.before.data(), static_cast<std::streamsize>(fixture.before.size()));
        }
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(wxString::FromUTF8(path.string()), fixture.encoding).status == OpenStatus::Opened,
                "open encoded fixture through application");
        editor->Send(Message::SelectAll);
        Toggle(frame);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "comment Save preserves exact encoding, BOM, EOL bytes and final newline");
        Require(frame->GetStatusBar()->GetStatusText().Contains("C/C++"), "comment retains document language");
        editor->Send(Message::Undo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "one Undo restores independent original disk bytes");
        editor->Send(Message::Redo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "one Redo restores independent commented disk bytes");
    }
    std::filesystem::remove_all(directory);
    std::puts(
        "PASS: independent UTF-8 BOM, GBK, UTF-16LE BOM disk bytes, mixed EOLs, final newline, language and Undo/Redo");
}
void Selections(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetUseTabs, 0);
    const char* original = "    alpha\r\n  beta\n\n    // kept\rnext";
    const char* commented = "  //   alpha\r\n  // beta\n  //\n  //   // kept\rnext";
    Set(editor, original);
    editor->Send(Message::SetSel, 3, editor->Send(Message::PositionFromLine, 4));
    Toggle(frame);
    Require(TestEditor::Text(editor) == commented, "common column, blank line and deeper comment match original");
    Range(editor, 0, editor->Send(Message::PositionFromLine, 4));
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == original && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "one Undo restores exact mixed EOL bytes and save point");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == commented, "one Redo restores whole comment group");
    editor->Send(Message::SetSel, editor->Send(Message::PositionFromLine, 4), 3);
    Toggle(frame);
    Require(TestEditor::Text(editor) == original, "uncomment reverses whole original multiline edit");
    Range(editor, editor->Send(Message::PositionFromLine, 4), 0);
    Set(editor, "  // alpha\n  plain\n    //  deep\n  //\nend");
    editor->Send(Message::SetSel, 0, editor->Send(Message::PositionFromLine, 4));
    Toggle(frame);
    Require(TestEditor::Text(editor) == "  alpha\n  plain\n     deep\n\nend",
            "first commented line deletes only comments and one pad");
    Set(editor, "  plain\n  // existing\n");
    editor->Send(Message::SelectAll);
    Toggle(frame);
    Require(TestEditor::Text(editor) == "  // plain\n  // existing\n",
            "add action leaves already-commented line at common column alone");
    Set(editor, "\talpha\n    beta\n");
    editor->Send(Message::SetTabWidth, 4);
    editor->Send(Message::SelectAll);
    Toggle(frame);
    Require(TestEditor::Text(editor) == "\t// alpha\n    // beta\n", "common column uses visual tab indentation");
    Set(editor, "   ", 3, 3);
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
        CallAfter([this, frame] {
            try {
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
                    Set(editor, "  value\nnext", 4, 4);
                    NativeKey(editor, false);
                    const std::string expected = std::string(language) == "Python"
                                                     ? (spaced ? "  # value\nnext" : "  #value\nnext")
                                                     : (spaced ? "  // value\nnext" : "  //value\nnext");
                    Require(TestEditor::Text(editor) == expected,
                            "actual Ctrl+/ uses scheme prefix and settings spacing");
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
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Save Settings Now"));
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
