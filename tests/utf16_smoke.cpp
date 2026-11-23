#include "diagnostics/print.h"
#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <array>
#include <filesystem>
#include <fstream>
#include "settings/settings_store.h"
#include <memory>
#include <cstdio>
#include <print>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
using TestFiles::Bytes;
using TestFiles::Filename;
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), bytes.size());
}
using TestEditor::Text;
class Utf16App final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        settings_->values.wordWrap = false;
        auto* frame = new EditorFrame(settings_.get());
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-utf16");
            try {
                std::filesystem::create_directory(directory);
                const auto source = directory / "source.txt";
                const std::string sample(
                    "\x{ff}\x{fe}\x{41}\0\x{2d}\x{4e}\x{3d}\x{d8}\0\x{de}\r\0\n\0\x{42}\0\n\0\x{43}"
                    "\0\r\0",
                    22);
                Write(source, sample);
                const auto opened = frame->Open(Filename(source));
                Require(opened.status == OpenStatus::Opened, "UTF-16LE BOM must be detected");
                Require(Text(frame) == "A中😀\r\nB\nC\r",
                        "UTF-16LE must decode CJK, supplementary text and actual EOLs");
                Require(frame->Save().status == SaveStatus::Saved && Bytes(source) == sample,
                        "unchanged UTF-16LE Save must retain exact bytes");
                std::puts("PASS: UTF-16LE BOM detection and exact Unicode/EOL Save");
                const auto bad = directory / "bad.txt";
                const std::array<std::string, 5> invalid = {
                    std::string("\x{ff}\x{fe}\0\x{dc}", 4), std::string("\x{ff}\x{fe}\0\x{d8}", 4),
                    std::string("\x{ff}\x{fe}\0\x{d8}\x{41}\0", 6), std::string("\x{ff}\x{fe}\x{41}", 3),
                    std::string("\x{ff}\x{fe}\x{41}\0\0\0\0\x{dc}", 8)};
                for (const auto& bytes : invalid) {
                    Write(bad, bytes);
                    const auto failed = frame->Open(Filename(bad), FileEncoding::Utf16Le);
                    Require(failed.status == OpenStatus::Failed && failed.error.Contains("UTF-16LE"),
                            "malformed UTF-16LE must report a strict decoding error");
                    Require(Text(frame) == "A中😀\r\nB\nC\r" && frame->DocumentPath() == Filename(source),
                            "malformed UTF-16LE must retain document and identity");
                }
                std::puts("PASS: incomplete bytes and unpaired surrogates rejected before replacement");
                const std::array<std::string, 3> payloads = {
                    "", std::string("\x{41}\0\x{2d}\x{4e}\0\0\x{3d}\x{d8}\0\x{de}\r\0\n\0\x{42}\0\n\0\x{43}\0\r\0", 22),
                    std::string("\n\0", 2)};
                for (bool bom : {false, true})
                    for (const auto& payload : payloads) {
                        using enum Scintilla::Message;
                        const auto bytes = (bom ? std::string("\x{ff}\x{fe}", 2) : std::string{}) + payload;
                        Write(source, bytes);
                        Require(frame->Open(Filename(source), FileEncoding::Utf16Le).status == OpenStatus::Opened,
                                "explicit UTF-16LE must accept BOM/no-BOM including empty");
                        Require(frame->DocumentEncoding() == FileEncoding::Utf16Le && frame->DocumentHasBom() == bom &&
                                    !frame->DocumentHasUtf8Bom(),
                                "UTF-16LE format metadata must distinguish BOM");
                        Require(frame->Save().status == SaveStatus::Saved && Bytes(source) == bytes,
                                "Save must preserve UTF-16LE with NUL and actual EOLs");
                        frame->Editor()->Send(GotoPos, frame->Editor()->Send(GetLength));
                        frame->Editor()->Send(AddText, 4, reinterpret_cast<intptr_t>("😀"));
                        const auto copy = directory / u8"另存.txt";
                        Require(frame->SaveAs(Filename(copy)).status == SaveStatus::Saved &&
                                    Bytes(copy) == bytes + std::string("\x{3d}\x{d8}\0\x{de}", 4),
                                "edited Save As must preserve UTF-16LE BOM and exact bytes");
                        Require(Bytes(source) == bytes, "Save As must preserve original source bytes");
                        frame->Editor()->Send(AddText, 1, reinterpret_cast<intptr_t>("!"));
                        const auto oldText = Text(frame);
                        Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                                    frame->SaveAs(Filename(directory / "missing" / "bad.txt")).status ==
                                        SaveStatus::Failed,
                                "cancelled/failed Save As must report outcome");
                        Require(Text(frame) == oldText && frame->DocumentPath() == Filename(copy) &&
                                    frame->DocumentHasBom() == bom &&
                                    frame->DocumentEncoding() == FileEncoding::Utf16Le &&
                                    frame->Editor()->Send(GetModify),
                                "save failure must retain text/identity/format/dirty state");
                        Write(bad, invalid[0]);
                        Require(frame->Open(Filename(bad), FileEncoding::Utf16Le).status == OpenStatus::Failed &&
                                    Text(frame) == oldText && frame->Editor()->Send(GetModify),
                                "strict failure must preserve dirty state without prompting");
                        frame->Editor()->Send(Undo);
                        Require(!frame->Editor()->Send(GetModify), "failure must preserve Undo to save point");
                        Require(frame->NewDocument() && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                                    !frame->DocumentHasBom(),
                                "New must reset UTF8/no-BOM metadata");
                    }
                std::puts(
                    "PASS: explicit no-BOM/empty/NUL roundtrips, edited Save As, failure/history and New metadata");
                result_ = 0;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(Utf16App);
