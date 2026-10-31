#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "saved file must exist");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void Key(EditorControl* editor, int key, bool control = false) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = key;
    event.m_controlDown = control;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
class EolApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        CallAfter([this, frame] {
            const auto directory = std::filesystem::temp_directory_path() /
                ("wxnotepad-eol-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                using namespace std::string_literals;
                struct Sample {
                    const char* name;
                    std::string source;
                    std::string expected;
                    std::optional<FileEncoding> encoding = std::nullopt;
                };
                const std::vector<Sample> samples = {
                    {"LF", "first\nlast", "first\nlast\n"},
                    {"CRLF", "first\r\nlast", "first\r\nlast\r\n"},
                    {"CR", "first\rlast", "first\rlast\r"},
                    {"predominant LF", "a\r\nb\nc\rd\nlast", "a\r\nb\nc\rd\nlast\n"},
                    {"predominant CR", "a\r\nb\rc\nd\rlast", "a\r\nb\rc\nd\rlast\r"},
                    {"predominant CRLF", "a\r\nb\rc\nd\r\nlast", "a\r\nb\rc\nd\r\nlast\r\n"},
                    {"CRLF/LF tie", "a\r\nb\nlast", "a\r\nb\nlast\r\n"},
                    {"three-way tie", "a\r\nb\nc\rlast", "a\r\nb\nc\rlast\r\n"},
                    {"LF/CR tie above CRLF", "a\r\nb\nc\rd\ne\rlast", "a\r\nb\nc\rd\ne\rlast\n"},
                    {"UTF-8 BOM LF", "\xef\xbb\xbf中文\nlast", "\xef\xbb\xbf中文\nlast\n"},
                    {"no EOL resets default", "last", "last\r\n"},
                    {"UTF-16LE BOM LF", "\xff\xfe\x2d\x4e\n\0z\0"s, "\xff\xfe\x2d\x4e\n\0z\0\n\0"s},
                    {"GBK LF", "\xd6\xd0\nlast", "\xd6\xd0\nlast\n", FileEncoding::Gbk},
                };
                for (const auto& sample : samples) {
                    const auto path = directory / "sample.txt";
                    { std::ofstream output(path, std::ios::binary); output << sample.source; }
                    Require(frame->Open(Filename(path), sample.encoding).status == OpenStatus::Opened, "EOL sample must open");
                    auto* editor = frame->Editor();
                    Require(!editor->Send(Scintilla::Message::GetModify) && !editor->Send(Scintilla::Message::CanUndo),
                        "loading EOL mode must retain clean save point and isolated history");
                    Key(editor, WXK_END, true);
                    Key(editor, WXK_RETURN);
                    Require(editor->Send(Scintilla::Message::GetModify), "Enter must mark loaded document modified");
                    Key(editor, 'Z', true);
                    Require(!editor->Send(Scintilla::Message::GetModify), "Undo Enter must return to loaded save point");
                    Key(editor, 'Y', true);
                    Require(frame->Save().status == SaveStatus::Saved, "edited EOL sample must save");
                    if (Bytes(path) != sample.expected) {
                        std::fprintf(stderr, "Sample: %s\n", sample.name);
                        std::fprintf(stderr, "Saved bytes:");
                        for (unsigned char byte : Bytes(path)) std::fprintf(stderr, " %02x", byte);
                        std::fprintf(stderr, "\n");
                        Require(false, "Enter must follow loaded EOL policy and preserve original encoding, BOM and bytes");
                    }
                    std::printf("PASS: %s, actual Enter/Undo/Redo and saved bytes\n", sample.name);
                }
                Require(frame->New(), "New must replace a saved LF document");
                Key(frame->Editor(), WXK_RETURN);
                const auto newPath = directory / "new.txt";
                Require(frame->SaveFirst(Filename(newPath)).status == SaveStatus::Saved, "new document must save");
                Require(Bytes(newPath) == "\r\n", "New after an LF document must restore default CRLF insertion");
                std::puts("PASS: New restores default CRLF after loaded LF");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            std::filesystem::remove_all(directory);
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnExit() override { return result_; }
    int OnRun() override { wxApp::OnRun(); return result_; }
private:
    int result_ = 1;
};
}
wxIMPLEMENT_APP(EolApp);
