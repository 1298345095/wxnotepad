#include "native_completion_helpers.h"
#include "file_helpers.h"
#include <wx/clipbrd.h>
#include <wx/stattext.h>
#include <cstdio>
#include <exception>

namespace {
using enum Scintilla::Message;

void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
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
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "open the explicit UTF-8 character information fixture");
    TestEditor::ResetStream(editor, "gQ==", 0, 4);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Tools", "Decode"));
    Require(TestEditor::Text(editor) == std::string(1, '\x81'), "native Base64 Decode produces invalid UTF-8 byte 81");
    const auto revision = editor->DocumentRevision();
#ifdef __WXGTK__
    NativeKeys::Shortcut(frame, 'A', true); // Supplies Wayland's owned input serial for clipboard publication.
#endif
    {
        wxClipboardLocker clipboard;
        if (!clipboard) throw TestEditor::Failure("open the owned Character Info clipboard");
        Require(wxTheClipboard->SetData(new wxTextDataObject("owned invalid-byte information offer")),
                "seed the owned Character Info clipboard offer");
    }
    CharacterFeedback feedback;
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Show Character Info"));
    const auto output = feedback.payload.utf8_string();
    std::printf("Character Info: %s\n", output.c_str());
    Require(feedback.shown == 1 && output.starts_with("Unicode: U+DD01\nUTF-16: DD01\nUTF-8: EF BF BD\nHTML: &#56577;"),
            "invalid UTF-8 Character Info retains the original sentinel and replacement UTF-8 field");
    wxTextDataObject copied;
    {
        wxClipboardLocker clipboard;
        if (!clipboard) throw TestEditor::Failure("open the Character Info clipboard result");
        Require(wxTheClipboard->GetData(copied) && copied.GetText() == feedback.payload,
                "native Character Info copies its exact visible payload");
    }
    Require(TestEditor::Text(editor) == std::string(1, '\x81') && editor->DocumentRevision() == revision &&
                editor->Send(CanUndo) && !editor->Send(CanRedo),
            "character information preserves invalid bytes and edit history");
    std::puts("PASS: invalid UTF-8 Character Info retains original technical fields and copied payload");
    std::filesystem::remove_all(directory);
}

class NativeByteInformationApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native compositor presents the invalid-byte information fixture");
                CheckInvalidByte(frame);
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
wxIMPLEMENT_APP(NativeByteInformationApp);
