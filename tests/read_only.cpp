#include "native_key_helpers.h"
#include "encoded_file_helpers.h"
#include "file_command_driver.h"
#include <wx/wx.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <tuple>
#include <wx/textctrl.h>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int Mode(EditorFrame* frame) {
    const int id = frame->GetMenuBar()->FindMenuItem("File", "Read Only Mode");
    Require(id != wxNOT_FOUND, "File / File Mode must expose original Read Only Mode");
    return id;
}
void Toggle(EditorFrame* frame) {
    NativeSearch::Command(frame, Mode(frame));
}
void TogglePreservesDocument(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "alpha\r\nbeta\r\n", 4, 1);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 4, 1);
    const auto actions = editor->Send(Message::GetUndoActions);
    const auto current = editor->Send(Message::GetUndoCurrent);
    const auto savePoint = editor->Send(Message::GetUndoSavePoint);
    const auto revision = editor->DocumentRevision();
    Toggle(frame);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(editor->Send(Message::GetReadOnly) && frame->GetMenuBar()->IsChecked(Mode(frame)) &&
                frame->GetTitle() == "Untitled (Read Only Mode) - wxnotepad",
            "native command checks live protection and original title suffix");
    Require(TestEditor::Text(editor) == "alpha\r\nbeta\r\n" && !editor->Send(Message::GetModify) &&
                editor->DocumentRevision() == revision && editor->Send(Message::GetAnchor) == 4 &&
                editor->Send(Message::GetCurrentPos) == 1 && editor->Send(Message::GetUndoActions) == actions &&
                editor->Send(Message::GetUndoCurrent) == current &&
                editor->Send(Message::GetUndoSavePoint) == savePoint,
            "toggling preserves bytes, selection, revision, save point and pre-existing history");
    Require(!editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo),
            "original public Undo/Redo availability is disabled during protection");
    editor->Send(Message::Undo);
    editor->Send(Message::Redo);
    Require(editor->Send(Message::GetUndoCurrent) == current, "protected Undo/Redo retain the history cursor");
    NativeKeys::Shortcut(frame, WXK_F10, false);
    Require(!editor->Send(Message::GetReadOnly) && editor->Send(Message::CanRedo) &&
                frame->GetTitle() == "Untitled - wxnotepad",
            "native F10 releases protection with original history and title");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "alpha\r\nbeta\r\n!" && editor->Send(Message::GetModify),
            "pending Redo remains executable after toggling off");
    Toggle(frame);
    Require(editor->Send(Message::GetModify) && frame->GetTitle() == "*Untitled (Read Only Mode) - wxnotepad",
            "dirty untitled mode preserves modification state and its title feedback");
    Toggle(frame);
    std::puts("PASS: native menu/F10, title and unchanged text, selection, save point and history");
}
void DocumentTransitions(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "old", 2, 1);
    editor->Send(Message::SetReadOnly, 1);
    Require(frame->New() && !editor->Send(Message::GetReadOnly) && TestEditor::Text(editor).empty(),
            "New clears editor protection before replacing the previous document");
    const auto source = TestFiles::TemporaryDirectory() / "read-only-中文.cpp";
    const auto other = TestFiles::TemporaryDirectory() / "other-中文.txt";
    TestFiles::WriteBytes(source, "alpha\r\nbeta\r\n");
    TestFiles::WriteBytes(other, "other\n");
    Require(frame->Open(TestFiles::Filename(source)).status == OpenStatus::Opened, "open the named original fixture");
    editor->Send(Message::SetSel, 3, 9);
    Toggle(frame);
    Require(frame->Open(TestFiles::Filename(source)).status == OpenStatus::Opened &&
                editor->Send(Message::GetReadOnly) && TestEditor::Text(editor) == "alpha\r\nbeta\r\n" &&
                editor->Send(Message::GetAnchor) == 3 && editor->Send(Message::GetCurrentPos) == 9 &&
                !editor->Send(Message::GetModify) && editor->Send(Message::GetUndoActions) == 0,
            "same-path reopen reapplies original protection after replacement and restores view");
    Require(frame->Open(TestFiles::Filename(other)).status == OpenStatus::Opened &&
                !editor->Send(Message::GetReadOnly) && TestEditor::Text(editor) == "other\n",
            "a writable different file does not inherit manually enabled editor mode");
    Toggle(frame);
    Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled && editor->Send(Message::GetReadOnly) &&
                frame->Open(TestFiles::Filename(other) + ".missing").status == OpenStatus::Failed &&
                editor->Send(Message::GetReadOnly) && TestEditor::Text(editor) == "other\n",
            "cancelled and failed Open retain mode and original bytes");
    Toggle(frame);
    std::puts("PASS: New, same/different-path Open and failed/cancelled document transitions");
}
void FileProtection(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto source = TestFiles::TemporaryDirectory() / "attribute-中文.txt";
    const auto saved = TestFiles::TemporaryDirectory() / "attribute-saved-中文.txt";
    TestFiles::WriteBytes(source, "readonly\r\n");
    const auto permissions = std::filesystem::status(source).permissions();
    struct RestorePermissions {
        const std::filesystem::path& path;
        std::filesystem::perms permissions;
        bool pending = true;
        void Restore() {
            std::filesystem::permissions(path, permissions);
            pending = false;
        }
        ~RestorePermissions() {
            if (!pending) return;
            std::error_code error;
            std::filesystem::permissions(path, permissions, error);
            if (error)
                std::fprintf(stderr, "FAIL: restore native read-only fixture permissions (%s: %d)\n",
                             error.category().name(), error.value());
        }
    } restore{source, permissions};
    std::filesystem::permissions(source, std::filesystem::perms::owner_read);
    Require(frame->Open(TestFiles::Filename(source)).status == OpenStatus::Opened &&
                editor->Send(Message::GetReadOnly) && frame->GetTitle().EndsWith(" (Read Only File) - wxnotepad"),
            "opening a native read-only file enables editor protection with original file-title priority");
    Toggle(frame);
    Require(!editor->Send(Message::GetReadOnly) && frame->GetTitle().EndsWith(" (Read Only File) - wxnotepad"),
            "F10 releases editor protection while file attributes retain their title priority");
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Save, wxID_NO);
        Require(frame->Save().status == SaveStatus::Cancelled && driver.prompts == 1 && driver.choosers == 0 &&
                    TestFiles::Bytes(source) == "readonly\r\n" && editor->Send(Message::GetModify),
                "declining original read-only Save redirection retains disk, dirty state and document identity");
    }
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Save, wxID_YES,
                                                TestFiles::Filename(saved));
        Require(frame->Save().status == SaveStatus::Saved && driver.prompts == 1 && driver.choosers == 1 &&
                    TestFiles::Bytes(source) == "readonly\r\n" && TestFiles::Bytes(saved) == "readonly\r\n!" &&
                    frame->DocumentPath() == TestFiles::Filename(saved) && !editor->Send(Message::GetModify) &&
                    !editor->Send(Message::GetReadOnly) && !frame->GetTitle().Contains("Read Only"),
                "accepting read-only Save redirects through native Save As to writable independent disk bytes");
    }
    restore.Restore();
    std::puts("PASS: native file attributes, title priority and original Save / Save As distinction");
}
void CommandOutcomes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
    // Independent original Windows results: 139/original-nonclipboard-expanded.json.
    std::ifstream fixtures{std::filesystem::path(READ_ONLY_REFERENCE_FIXTURES)};
    Require(fixtures.is_open(), "open readable independent original command fixtures");
    std::string row;
    while (std::getline(fixtures, row)) {
        if (row.empty() || row.front() == '#') continue;
        std::istringstream values(row);
        std::string label;
        std::getline(values, label, '\t');
        intptr_t anchor = 0, caret = 0, eol = 0;
        bool enabled = false;
        Require(static_cast<bool>(values >> anchor >> caret >> eol >> enabled), "read original command expectation");
        TestEditor::ResetStream(editor, "alpha\r\nbeta\r\n", 4, 1);
        editor->Send(Message::SetEOLMode, 0);
        editor->Send(Message::SetReadOnly, 1);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", wxString::FromUTF8(label));
        Require(id != wxNOT_FOUND && frame->GetMenuBar()->IsEnabled(id) == enabled,
                "original nonempty/capability-based editing menu remains enabled during protection");
        Require(frame->GetMenuBar()->IsEnabled(wxID_CUT) && !frame->GetMenuBar()->IsEnabled(wxID_UNDO) &&
                    !frame->GetMenuBar()->IsEnabled(wxID_REDO) && !frame->GetMenuBar()->IsEnabled(wxID_PASTE) &&
                    !frame->GetMenuBar()->IsEnabled(wxID_SAVE),
                "original Cut, Undo/Redo, Paste and clean Save availability remain distinct");
        NativeSearch::Command(frame, id);
        Require(TestEditor::Text(editor) == "alpha\r\nbeta\r\n" && !editor->Send(Message::GetModify) &&
                    editor->Send(Message::GetUndoActions) == 0 && editor->Send(Message::GetEOLMode) == eol,
                "protected native commands preserve original bytes, save point and history");
        TestEditor::RequireSelection(editor, anchor, caret, ("original protected selection: " + label).c_str());
    }
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: original native command availability and command-specific protected selection effects");
}
std::vector<intptr_t> Selections(EditorControl* editor) {
    std::vector<intptr_t> state;
    for (const auto message :
         {Message::GetSelectionMode, Message::GetSelections, Message::GetMainSelection,
          Message::GetRectangularSelectionAnchor, Message::GetRectangularSelectionCaret,
          Message::GetRectangularSelectionAnchorVirtualSpace, Message::GetRectangularSelectionCaretVirtualSpace})
        state.push_back(editor->Send(message));
    for (intptr_t index = 0; index < editor->Send(Message::GetSelections); ++index)
        for (const auto message : {Message::GetSelectionNAnchor, Message::GetSelectionNCaret,
                                   Message::GetSelectionNAnchorVirtualSpace, Message::GetSelectionNCaretVirtualSpace})
            state.push_back(editor->Send(message, index));
    return state;
}
void SelectionShapes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (bool rectangle : {true, false}) {
        TestEditor::ResetStream(editor, "alpha\r\nbeta\r\n", 4, 1);
        if (rectangle) {
            editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
            editor->Send(Message::SetRectangularSelectionAnchor, 1);
            editor->Send(Message::SetRectangularSelectionCaret, 9);
            editor->Send(Message::SetRectangularSelectionAnchorVirtualSpace, 1);
            editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, 3);
        } else {
            editor->Send(Message::SetMultipleSelection, 1);
            editor->Send(Message::AddSelection, 10, 8);
        }
        const auto before = Selections(editor);
        Toggle(frame);
        Require(Selections(editor) == before, "original mode-on preserves every rectangular/multiple selection field");
        Toggle(frame);
        Require(Selections(editor) == before, "original mode-off preserves every rectangular/multiple selection field");
        editor->Send(Message::SetReadOnly, 1);
        editor->Send(Message::Clear);
        Require(TestEditor::Text(editor) == "alpha\r\nbeta\r\n" && !editor->Send(Message::GetModify) &&
                    editor->Send(Message::GetUndoActions) == 0,
                "independent editor-message protection rejects rectangular/multiple selection deletion");
        editor->Send(Message::SetReadOnly, 0);
    }
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    editor->Send(Message::ClearSelections);
    for (const int modifiers : std::array<int, 4>{wxMOD_CONTROL, wxMOD_ALT, wxMOD_SHIFT, wxMOD_CONTROL | wxMOD_SHIFT}) {
        NativeKeys::Shortcut(frame, WXK_F10, (modifiers & wxMOD_CONTROL) != 0, (modifiers & wxMOD_ALT) != 0,
                             (modifiers & wxMOD_SHIFT) != 0);
        Require(!editor->Send(Message::GetReadOnly), "modified F10 does not alias the original unmodified accelerator");
    }
    std::puts("PASS: rectangle/multiple-selection mode transitions and exact F10 modifiers");
}
void SavedFormats(EditorFrame* frame) {
    using namespace std::string_literals;
    const TestFiles::EncodedFixture fixtures[] = {
        {"utf8", FileEncoding::Utf8,
         "\xef\xbb\xbf"
         "a \r\n中\t",
         "\xef\xbb\xbf"
         "a \r\n中\t!"},
        {"utf16", FileEncoding::Utf16Le,
         "\xff\xfe"
         "a\0 \0\r\0\n\0\x2d\x4e\t\0"s,
         "\xff\xfe"
         "a\0 \0\r\0\n\0\x2d\x4e\t\0!\0"s},
        {"gbk", FileEncoding::Gbk, "a \r\n\xd6\xd0\t", "a \r\n\xd6\xd0\t!"},
    };
    auto* editor = frame->Editor();
    for (const auto& fixture : fixtures) {
        const auto path = TestFiles::TemporaryDirectory() / (std::string(fixture.name) + "-source.txt");
        const auto saved = TestFiles::TemporaryDirectory() / (std::string(fixture.name) + "-saved.txt");
        TestFiles::WriteBytes(path, fixture.before);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "open explicitly encoded fixture through the delivered document flow");
        Require(!editor->Send(Message::GetReadOnly), "different-path explicit decoding starts writable");
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        const auto eol = editor->Send(Message::GetEOLMode);
        const auto bom = frame->DocumentHasBom();
        Toggle(frame);
        Require(editor->Send(Message::GetModify) && frame->GetTitle().StartsWith("*") &&
                    frame->DocumentEncoding() == fixture.encoding && frame->DocumentHasBom() == bom &&
                    editor->Send(Message::GetEOLMode) == eol,
                "dirty named mode transition preserves encoding/BOM/EOL and modification feedback");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after &&
                    editor->Send(Message::GetReadOnly),
                "editor-only protection permits Save with independent encoded bytes and retains mode");
        Require(frame->SaveAs(TestFiles::Filename(saved)).status == SaveStatus::Saved &&
                    TestFiles::Bytes(saved) == fixture.after && editor->Send(Message::GetReadOnly),
                "Save As changes identity and preserves editor protection and encoded disk bytes");
        Toggle(frame);
        TestFiles::RequireSavedUndoRedo(frame, saved, fixture,
                                        {"saved edit remains", "Undo restores original encoded bytes",
                                         "Redo restores encoded edit after mode and Save As"});
        Toggle(frame);
        Require(frame->Open(TestFiles::Filename(saved), fixture.encoding).status == OpenStatus::Opened &&
                    editor->Send(Message::GetReadOnly) && frame->DocumentEncoding() == fixture.encoding &&
                    frame->DocumentHasBom() == bom && editor->Send(Message::GetUndoActions) == 0 &&
                    TestFiles::Bytes(saved) == fixture.after,
                "same-path explicit encoding reload reapplies protection and commits the original fresh history");
    }
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: dirty named UTF-8/UTF-16LE/GBK Save, Save As and independently read Undo/Redo bytes");
}
void NativeEditingKey(EditorFrame* frame, int key, const char* name) {
    auto* editor = frame->Editor();
    editor->SetFocus();
    wxYield();
#ifdef __WXMSW__
    const int native = key == WXK_DELETE ? VK_DELETE : key == WXK_RIGHT ? VK_RIGHT : key;
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYDOWN, native, 0);
    if (key == 'X') SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_CHAR, 'x', 0);
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYUP, native, 0);
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native editing keys require a private display");
    bool received = false;
    NativeKeys::KeyRelease release(key, received, nullptr);
    {
        wxFFile request(focusFile, "wb");
        Require(request.Write(wxString("key:") + name), "request actual native editing key");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!received && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(received, "actual protected editing-key release reaches the owned editor");
#endif
    wxYield();
}
void NavigationAndEditing(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const std::tuple<int, const char*, int, int> keys[] = {
        {WXK_DELETE, "Delete", 1, 1}, {WXK_BACK, "BackSpace", 1, 1}, {'X', "x", 4, 1}, {WXK_RIGHT, "Right", 4, 4}};
    for (const auto& [key, name, anchor, caret] : keys) {
        TestEditor::ResetStream(editor, "alpha\r\nbeta\r\n", 4, 1);
        editor->Send(Message::SetReadOnly, 1);
        NativeEditingKey(frame, key, name);
        Require(TestEditor::Text(editor) == "alpha\r\nbeta\r\n" && !editor->Send(Message::GetModify) &&
                    editor->Send(Message::GetUndoActions) == 0,
                "native typing, deletion and navigation preserve protected bytes/save point/history");
        TestEditor::RequireSelection(editor, anchor, caret, "original native protected editing/navigation selection");
    }
    SearchSession search(*editor);
    search.Configure({"beta", "replacement", {}, false});
    const auto found = search.Find(SearchDirection::Forward);
    Require(found.status == FindStatus::Found && found.start == 7 && found.end == 11,
            "read-only search still selects the real original-core match");
    const auto wrap = editor->Send(Message::GetWrapMode);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Word Wrap"));
    Require(editor->Send(Message::GetWrapMode) != wrap && editor->Send(Message::GetReadOnly),
            "native view commands remain available during editor protection");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Word Wrap"));
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: actual native typing/Delete/Backspace, navigation, search and view during protection");
}
class AcceptTransformation final : public wxModalDialogHook {
public:
    explicit AcceptTransformation(const char* title, bool* released = nullptr) : title_(title), released_(released) {
        Register();
    }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() != title_) return wxID_NONE;
        ++shown;
        wxTheApp->CallAfter([this, dialog] {
#ifndef __WXMSW__
            // Closing a shortcut-opened modal before its physical release can
            // withdraw Wayland keyboard focus while that release is in flight.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (released_ && !*released_ && std::chrono::steady_clock::now() < deadline) {
                wxYield();
                wxMilliSleep(10);
            }
            Require(!released_ || *released_, "opening shortcut releases before the protected dialog responds");
#endif
            for (const auto* name : {"Insert before selection", "Prefix text to lines"})
                if (auto* input = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxTextCtrl))
                    input->SetValue("[");
            for (const auto* name : {"Insert after selection", "Append text to lines"})
                if (auto* input = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxTextCtrl))
                    input->SetValue("]");
            if (auto* input = wxDynamicCast(wxWindow::FindWindowByName("Boundary", dialog), wxTextCtrl))
                input->SetValue("4");
            NativeSearch::Click(dialog, wxID_OK);
        });
        return wxID_NONE;
    }
    int shown = 0;

private:
    wxString title_;
    bool* released_;
};
void TransformationDialogs(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const std::tuple<const char*, const char*, int, intptr_t> commands[] = {{"With...", "Enclose Selection", 'Q', 1},
                                                                            {"Modify Lines...", "Modify Lines", 'M', 0},
                                                                            {"Align Lines...", "Align Lines", 'J', 0},
                                                                            {"Sort Lines...", "Sort Lines", 'O', 0},
                                                                            {"Column Wrap...", "Column Wrap", 0, 0}};
    for (const auto& [label, title, key, anchor] : commands) {
        for (bool keyboard : {false, true}) {
            if (keyboard && !key) continue;
            TestEditor::ResetStream(editor, "beta beta\r\nalpha alpha\r\n", 0, 23);
            editor->Send(Message::SetReadOnly, 1);
            bool released = false;
            AcceptTransformation dialog(title, keyboard ? &released : nullptr);
            if (keyboard)
                NativeKeys::Shortcut(frame, key, false, true, false, &released);
            else
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", label));
            Require(dialog.shown == 1 && TestEditor::Text(editor) == "beta beta\r\nalpha alpha\r\n" &&
                        editor->Send(Message::GetReadOnly) && !editor->Send(Message::GetModify) &&
                        editor->Send(Message::GetUndoActions) == 0,
                    "actual native transformation dialogs retain protected bytes and history after accepting input");
            TestEditor::RequireSelection(editor, anchor, 24, "original protected native dialog selection repair");
        }
    }
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: protected native menu/shortcut enclosure, Modify Lines, alignment, sort and column-wrap dialogs");
}
class ReadOnlyApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                TogglePreservesDocument(frame);
                DocumentTransitions(frame);
                FileProtection(frame);
                CommandOutcomes(frame);
                SelectionShapes(frame);
                SavedFormats(frame);
                NavigationAndEditing(frame);
                TransformationDialogs(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Editor()->Send(Message::SetReadOnly, 0);
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(ReadOnlyApp);
