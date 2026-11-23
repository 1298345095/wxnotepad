#include "native_completion_helpers.h"
#include "file_helpers.h"
#include <wx/clipbrd.h>
#include <wx/stattext.h>
#include <cstdio>
#include <print>
#include <exception>

namespace {
using enum Scintilla::Message;

void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
}

void Phase(const char* phase) {
    std::println(stderr, "Character Info phase: {}", phase);
    std::fflush(stderr);
}

class CharacterFeedback final : public wxTimer {
public:
    CharacterFeedback() { Start(20); }
    wxString payload;
    int shown = 0;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal()) continue;
            auto* text = wxDynamicCast(dialog->FindWindowByName("Character information", dialog), wxStaticText);
            if (!text) continue;
            Phase("observed owned Character Info dialog");
            Stop();
            ++shown;
            payload = text->GetLabelText();
            NativeSearch::Click(dialog, wxID_OK);
            return;
        }
    }
};

void CheckInvalidByte(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("native-byte-information");
    std::filesystem::create_directory(directory);
    const auto path = directory / "utf8.txt";
    TestFiles::WriteBytes(path, "");
    Phase("opening explicit UTF8 fixture");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "open the explicit UTF-8 character information fixture");
    TestEditor::ResetStream(editor, "gQ==", 0, 4);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Tools", "Decode"));
    Phase("decoded byte81");
    Require(TestEditor::Text(editor) == std::string(1, '\x81'), "native Base64 Decode produces invalid UTF-8 byte 81");
    const auto revision = editor->DocumentRevision();
#ifdef __WXGTK__
    NativeKeys::Shortcut(frame, 'A', true); // Supplies Wayland's owned input serial for clipboard publication.
#endif
    {
        Phase("opening clipboard for owned seed");
        wxClipboardLocker clipboard;
        if (!clipboard) throw TestEditor::Failure("open the owned Character Info clipboard");
        Phase("clipboard opened; publishing owned seed");
        Require(wxTheClipboard->SetData(new wxTextDataObject("owned invalid-byte information offer")),
                "seed the owned Character Info clipboard offer");
    }
    Phase("owned seed published; invoking Character Info");
    CharacterFeedback feedback;
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Show Character Info"));
    Phase("Character Info returned");
    const auto output = feedback.payload.utf8_string();
    std::println("Character Info: {}", output.c_str());
    Require(feedback.shown == 1 && output.starts_with("Unicode: U+DD01\nUTF-16: DD01\nUTF-8: EF BF BD\nHTML: &#56577;"),
            "invalid UTF-8 Character Info retains the original sentinel and replacement UTF-8 field");
    Phase("opening clipboard for result");
    wxTextDataObject copied;
    {
        wxClipboardLocker clipboard;
        if (!clipboard) throw TestEditor::Failure("open the Character Info clipboard result");
        Phase("clipboard opened; reading result");
#ifdef __WXMSW__
        const auto owner = GetClipboardOwner();
        const auto openHolder = GetOpenClipboardWindow();
        DWORD ownerProcess = 0, openHolderProcess = 0;
        const auto ownerThread = GetWindowThreadProcessId(owner, &ownerProcess);
        const auto openHolderThread = GetWindowThreadProcessId(openHolder, &openHolderProcess);
        std::fprintf(stderr,
                     "Character Info clipboard read: expectedOwner=%p owner=%p ownerProcess=%lu ownerThread=%lu "
                     "openHolder=%p openHolderProcess=%lu openHolderThread=%lu sequence=%lu unicodeAvailable=%d\n",
                     static_cast<void*>(editor->GetHandle()), static_cast<void*>(owner), ownerProcess, ownerThread,
                     static_cast<void*>(openHolder), openHolderProcess, openHolderThread, GetClipboardSequenceNumber(),
                     IsClipboardFormatAvailable(CF_UNICODETEXT));
        std::fflush(stderr);
#endif
        const bool received = wxTheClipboard->GetData(copied);
        Phase("clipboard GetData returned");
        Require(received && copied.GetText() == feedback.payload,
                "native Character Info copies its exact visible payload");
    }
    Phase("clipboard result verified and closed");
    Require(TestEditor::Text(editor) == std::string(1, '\x81') && editor->DocumentRevision() == revision &&
                editor->Send(CanUndo) && !editor->Send(CanRedo),
            "character information preserves invalid bytes and edit history");
    std::puts("PASS: invalid UTF-8 Character Info retains original technical fields and copied payload");
    // TemporaryRoot removes the directory after wxEntryCleanup destroys the frame and its watcher.
}

class NativeByteInformationApp final : public wxApp {
public:
    bool OnInit() override {
        Phase("OnInit");
        auto* frame = new EditorFrame;
        frame->Show();
        Phase("waiting for native presentation");
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            Phase("presented callback");
            try {
                Require(presented, "native compositor presents the invalid-byte information fixture");
                CheckInvalidByte(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
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
wxIMPLEMENT_APP(NativeByteInformationApp);
