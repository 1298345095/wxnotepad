#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include <wx/wx.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>

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
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), bytes.size());
}
std::string Text(EditorFrame* frame) {
    const auto length = static_cast<size_t>(frame->Editor()->Send(Scintilla::Message::GetLength));
    std::string text(length + 1, '\0');
    frame->Editor()->Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
class Utf16App final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-utf16-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto source = directory / "source.txt";
                const std::string sample("\xff\xfe\x41\0\x2d\x4e\x3d\xd8\0\xde\r\0\n\0\x42\0\n\0\x43\0\r\0", 22);
                Write(source, sample);
                const auto opened = frame->Open(Filename(source));
                Require(opened.status == OpenStatus::Opened, "UTF-16LE BOM must be detected");
                Require(Text(frame) == "A中😀\r\nB\nC\r",
                        "UTF-16LE must decode CJK, supplementary text and actual EOLs");
                Require(frame->Save().status == SaveStatus::Saved && Bytes(source) == sample,
                        "unchanged UTF-16LE Save must retain exact bytes");
                std::puts("PASS: UTF-16LE BOM detection and exact Unicode/EOL Save");
                const auto bad = directory / "bad.txt";
                const std::string invalid[] = {std::string("\xff\xfe\0\xdc", 4), std::string("\xff\xfe\0\xd8", 4),
                                               std::string("\xff\xfe\0\xd8\x41\0", 6), std::string("\xff\xfe\x41", 3),
                                               std::string("\xff\xfe\x41\0\0\0\0\xdc", 8)};
                for (const auto& bytes : invalid) {
                    Write(bad, bytes);
                    const auto failed = frame->Open(Filename(bad), FileEncoding::Utf16Le);
                    Require(failed.status == OpenStatus::Failed && failed.error.Contains("UTF-16LE"),
                            "malformed UTF-16LE must report a strict decoding error");
                    Require(Text(frame) == "A中😀\r\nB\nC\r" && frame->DocumentPath() == Filename(source),
                            "malformed UTF-16LE must retain document and identity");
                }
                std::puts("PASS: incomplete bytes and unpaired surrogates rejected before replacement");
                const std::string payloads[] = {
                    "", std::string("\x41\0\x2d\x4e\0\0\x3d\xd8\0\xde\r\0\n\0\x42\0\n\0\x43\0\r\0", 22),
                    std::string("\n\0", 2)};
                for (bool bom : {false, true})
                    for (const auto& payload : payloads) {
                        const auto bytes = (bom ? std::string("\xff\xfe", 2) : std::string{}) + payload;
                        Write(source, bytes);
                        Require(frame->Open(Filename(source), FileEncoding::Utf16Le).status == OpenStatus::Opened,
                                "explicit UTF-16LE must accept BOM/no-BOM including empty");
                        Require(frame->DocumentEncoding() == FileEncoding::Utf16Le && frame->DocumentHasBom() == bom &&
                                    !frame->DocumentHasUtf8Bom(),
                                "UTF-16LE format metadata must distinguish BOM");
                        Require(frame->Save().status == SaveStatus::Saved && Bytes(source) == bytes,
                                "Save must preserve UTF-16LE with NUL and actual EOLs");
                        frame->Editor()->Send(Scintilla::Message::GotoPos,
                                              frame->Editor()->Send(Scintilla::Message::GetLength));
                        frame->Editor()->Send(Scintilla::Message::AddText, 4, reinterpret_cast<intptr_t>("😀"));
                        const auto copy = directory / u8"另存.txt";
                        Require(frame->SaveAs(Filename(copy)).status == SaveStatus::Saved &&
                                    Bytes(copy) == bytes + std::string("\x3d\xd8\0\xde", 4),
                                "edited Save As must preserve UTF-16LE BOM and exact bytes");
                        Require(Bytes(source) == bytes, "Save As must preserve original source bytes");
                        frame->Editor()->Send(Scintilla::Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                        const auto oldText = Text(frame);
                        Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                                    frame->SaveAs(Filename(directory / "missing" / "bad.txt")).status ==
                                        SaveStatus::Failed,
                                "cancelled/failed Save As must report outcome");
                        Require(Text(frame) == oldText && frame->DocumentPath() == Filename(copy) &&
                                    frame->DocumentHasBom() == bom &&
                                    frame->DocumentEncoding() == FileEncoding::Utf16Le &&
                                    frame->Editor()->Send(Scintilla::Message::GetModify),
                                "save failure must retain text/identity/format/dirty state");
                        Write(bad, invalid[0]);
                        Require(frame->Open(Filename(bad), FileEncoding::Utf16Le).status == OpenStatus::Failed &&
                                    Text(frame) == oldText && frame->Editor()->Send(Scintilla::Message::GetModify),
                                "strict failure must preserve dirty state without prompting");
                        frame->Editor()->Send(Scintilla::Message::Undo);
                        Require(!frame->Editor()->Send(Scintilla::Message::GetModify),
                                "failure must preserve Undo to save point");
                        Require(frame->New() && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                                    !frame->DocumentHasBom(),
                                "New must reset UTF8/no-BOM metadata");
                    }
                std::puts(
                    "PASS: explicit no-BOM/empty/NUL roundtrips, edited Save As, failure/history and New metadata");
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
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(Utf16App);
