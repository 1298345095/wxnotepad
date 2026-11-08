#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "file_command_driver.h"
#include "native_key_helpers.h"
#include "native_scroll_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Command(EditorFrame* frame, int id) {
    wxCommandEvent command(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(command);
}
void ModifiedRevert(EditorFrame* frame, int revert, const std::filesystem::path& path) {
    auto* editor = frame->Editor();
    std::string before;
    for (int line = 0; line < 100; ++line)
        before += std::string(200, 'x') + "\n";
    TestFiles::WriteBytes(path, before);
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "view fixture must open");
    const int python = frame->GetMenuBar()->FindMenuItem("Scheme", "Python");
    Command(frame, python);
    editor->Send(Message::BeginUndoAction);
    TestEditor::Append(frame, "dirty");
    editor->Send(Message::EndUndoAction);
    editor->Send(Message::BeginUndoAction);
    TestEditor::Append(frame, "redo");
    editor->Send(Message::EndUndoAction);
    editor->Send(Message::Undo);
    Require(editor->Send(Message::GetModify) && editor->Send(Message::CanRedo),
            "cancel fixture has a modified document and a separate redo action");
    Require(NativeScroll::WaitForHorizontalRange(editor, true, 120), "long lines provide a horizontal viewport");
    editor->Send(Message::SetSel, 6040, 6020);
    editor->Send(Message::SetFirstVisibleLine, 25);
    editor->Send(Message::SetXOffset, 120);
    const auto revision = editor->DocumentRevision();
    const auto title = frame->GetTitle();
    const std::string external = "disk!" + before.substr(5);
    TestFiles::WriteBytes(path, external);
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL);
        Require(frame->Revert().status == OpenStatus::Cancelled, "Cancel rejects modified Revert");
        Require(driver.prompts == 1 && driver.choosers == 0 && driver.errors == 0,
                "modified Revert uses one native OK/Cancel confirmation");
    }
    Require(TestEditor::Text(frame) == before + "dirty" && editor->DocumentRevision() == revision &&
                editor->Send(Message::GetModify) && editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                editor->Send(Message::GetAnchor) == 6040 && editor->Send(Message::GetCurrentPos) == 6020 &&
                editor->Send(Message::GetFirstVisibleLine) == 25 && editor->Send(Message::GetXOffset) == 120 &&
                frame->GetMenuBar()->IsChecked(python) && frame->GetTitle() == title &&
                frame->DocumentPath() == TestFiles::Filename(path) && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                !frame->DocumentHasBom() && TestFiles::Bytes(path) == external,
            "Cancel retains live bytes, identity, encoding, save point, history, manual scheme and directed view");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(frame) == before + "dirtyredo", "cancelled Revert preserves the actual redo operation");
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 6040, 6020);
    editor->Send(Message::SetFirstVisibleLine, 25);
    editor->Send(Message::SetXOffset, 120);
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, wxID_OK);
        Command(frame, revert);
        Require(driver.prompts == 1 && driver.choosers == 0 && driver.errors == 0,
                "OK reloads after exactly one confirmation without an ordinary save decision");
    }
    Require(TestEditor::Text(frame) == external && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo) &&
                editor->Send(Message::GetAnchor) == 6040 && editor->Send(Message::GetCurrentPos) == 6020 &&
                editor->Send(Message::GetFirstVisibleLine) == 25 && editor->Send(Message::GetXOffset) == 120 &&
                editor->GetScrollRange(wxHORIZONTAL) - editor->GetScrollThumb(wxHORIZONTAL) >= 120 &&
                frame->GetMenuBar()->IsChecked(python) && TestFiles::Bytes(path) == external,
            "confirmed Revert restores original same-file view and scheme with clean undo state and unchanged disk");
    TestFiles::WriteBytes(path, "short");
    NativeKeys::Shortcut(frame, WXK_F5, false);
    Require(TestEditor::Text(frame) == "short" && editor->Send(Message::GetCurrentPos) == 5 &&
                editor->Send(Message::GetAnchor) == 5 && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo) && TestFiles::Bytes(path) == "short",
            "native F5 invokes Revert and a deleted saved line uses original EOF fallback");
    std::puts("PASS: modified OK/Cancel, history, manual scheme, viewport and native F5 shortened-file fallback");
}

void EncodedRevert(EditorFrame* frame, const std::filesystem::path& path) {
    struct Fixture {
        FileEncoding encoding;
        std::string before;
        std::string after;
        std::string expected;
        bool bom;
    };
    const Fixture fixtures[] = {
        {FileEncoding::Utf8, "before \xe4\xb8\xad\xe6\x96\x87", "after \xe4\xb8\xad\xe6\x96\x87",
         "after \xe4\xb8\xad\xe6\x96\x87", false},
        {FileEncoding::Gbk, "\xd6\xd0\xce\xc4", "plain\r\nsecond\nthird\r", "plain\r\nsecond\nthird\r", false},
        {FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x87\x65", 6), std::string("\xff\xfe\x2d\x4e", 4),
         "\xe4\xb8\xad", true},
    };
    for (const auto& fixture : fixtures) {
        TestFiles::WriteBytes(path, fixture.before);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "encoded fixture must open");
        TestFiles::WriteBytes(path, fixture.after);
        const auto result = frame->Revert();
        Require(result.status == OpenStatus::Opened && TestEditor::Text(frame) == fixture.expected &&
                    frame->DocumentEncoding() == fixture.encoding && frame->DocumentHasBom() == fixture.bom &&
                    !frame->Editor()->Send(Message::GetModify) && TestFiles::Bytes(path) == fixture.after,
                "Revert supports delivered codecs and retains a GBK weak hint for ASCII mixed EOL content");
        if (fixture.encoding == FileEncoding::Gbk)
            Require(result.lineEndings.crlf == 1 && result.lineEndings.cr == 1 && result.lineEndings.lf == 1 &&
                        frame->Editor()->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::CrLf),
                    "weak-encoding Revert retains original mixed-EOL counts and tie-breaking insertion mode");
    }
    const std::string utf8Bom = "\xef\xbb\xbfUTF-8 wins";
    TestFiles::WriteBytes(path, utf8Bom);
    Require(frame->Revert().status == OpenStatus::Opened && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                frame->DocumentHasBom() && TestEditor::Text(frame) == "UTF-8 wins" && TestFiles::Bytes(path) == utf8Bom,
            "UTF-8 BOM detection overrides the current UTF-16 weak encoding hint");
    TestFiles::WriteBytes(path, std::string("\xff\xfe\x2d\x4e", 4));
    Require(frame->Revert().status == OpenStatus::Opened && frame->DocumentEncoding() == FileEncoding::Utf16Le &&
                frame->DocumentHasBom() && TestEditor::Text(frame) == "\xe4\xb8\xad",
            "UTF-16 BOM detection overrides the current UTF-8 weak encoding hint");
    std::puts("PASS: UTF-8, UTF-16LE and GBK weak hints, BOM precedence and mixed EOL bytes");
}

void FailedRevert(EditorFrame* frame, int revert, const std::filesystem::path& path) {
    auto* editor = frame->Editor();
    const auto revision = editor->DocumentRevision();
    const auto text = TestEditor::Text(frame);
#ifdef __WXMSW__
    const auto locked =
        CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(locked != INVALID_HANDLE_VALUE, "unreadable fixture exclusively locks its backing file");
    const auto unreadable = frame->Revert();
    CloseHandle(locked);
    Require(unreadable.status == OpenStatus::Failed && TestEditor::Text(frame) == text &&
                editor->DocumentRevision() == revision,
            "an unreadable backing file returns recoverable failure and retains the live document");
#endif
    TestFiles::WriteBytes(path, std::string("\xff\xfe\x00\xd8", 4));
    Require(frame->Revert().status == OpenStatus::Failed && TestEditor::Text(frame) == text &&
                editor->DocumentRevision() == revision && frame->DocumentEncoding() == FileEncoding::Utf16Le &&
                TestFiles::Bytes(path) == std::string("\xff\xfe\x00\xd8", 4),
            "invalid encoded input retains the live document and source bytes");
    std::filesystem::remove(path);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(revert), "a missing named backing file retains original Revert enablement");
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, wxID_OK);
        Command(frame, revert);
        Require(driver.errors == 1 && driver.prompts == 0 && driver.choosers == 0,
                "missing backing file reports the existing recoverable open error");
    }
    Require(TestEditor::Text(frame) == text && editor->DocumentRevision() == revision &&
                frame->DocumentPath() == TestFiles::Filename(path) && !std::filesystem::exists(path),
            "missing backing file retains document identity and text without creating a file");
    std::puts("PASS: invalid encoding and removed backing file preserve the live document");
}
class RevertApp final : public wxApp {
public:
    bool OnInit() override {
        settings_.values.warnLineEndings = false;
        settings_.values.wordWrap = false;
        auto* frame = new EditorFrame(&settings_);
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native Revert frame is presented");
                const int revert = frame->GetMenuBar()->FindMenuItem("File", "Revert");
                Require(revert != wxNOT_FOUND, "File menu must expose Revert");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetMenuBar()->IsEnabled(revert), "untitled documents disable Revert");
                const auto path = TestFiles::TemporaryDirectory("revert.cpp");
                TestFiles::WriteBytes(path, "first\nsecond\nthird\n");
                Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                        "the named fixture must open");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsEnabled(revert), "named documents enable Revert");
                frame->Editor()->Send(Message::SetSel, 9, 2);
                TestFiles::WriteBytes(path, "disk!\nsecond\nthird\n");
                Command(frame, revert);
                Require(TestEditor::Text(frame) == "disk!\nsecond\nthird\n" &&
                            frame->Editor()->Send(Message::GetAnchor) == 9 &&
                            frame->Editor()->Send(Message::GetCurrentPos) == 2 &&
                            !frame->Editor()->Send(Message::GetModify) && !frame->Editor()->Send(Message::CanUndo) &&
                            TestFiles::Bytes(path) == "disk!\nsecond\nthird\n",
                        "clean Revert reloads without prompting, preserves selection direction and never writes");
                std::puts("PASS: native Revert entry, enablement and clean same-file reload");
                ModifiedRevert(frame, revert, path);
                EncodedRevert(frame, path);
                FailedRevert(frame, revert, path);
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
    SettingsStore settings_;
};
} // namespace
wxIMPLEMENT_APP(RevertApp);
