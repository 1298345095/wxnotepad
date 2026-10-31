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
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), bytes.size());
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    Require(stream.is_open(), "file must exist");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
std::string Text(EditorFrame* frame) {
    const auto length = frame->Editor()->Send(Scintilla::Message::GetLength);
    std::string text(length + 1, '\0');
    frame->Editor()->Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
void Add(EditorFrame* frame, const std::string& text) {
    frame->Editor()->Send(Scintilla::Message::GotoPos, frame->Editor()->Send(Scintilla::Message::GetLength));
    frame->Editor()->Send(Scintilla::Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
}
class GbkApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-gbk-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto source = directory / "source.txt";
                const std::string original = "ASCII\r\n\xd6\xd0\xce\xc4\n\x80\r";
                Write(source, original);
                Require(frame->Open(Filename(source), FileEncoding::Gbk).status == OpenStatus::Opened,
                        "explicit GBK must open");
                Require(Text(frame) == "ASCII\r\n中文\n€\r", "GBK must decode CJK/Euro and retain EOL");
                Require(frame->DocumentEncoding() == FileEncoding::Gbk && !frame->DocumentHasBom(),
                        "GBK must record encoding without BOM");
                Add(frame, "中文");
                Require(frame->Save().status == SaveStatus::Saved && Bytes(source) == original + "\xd6\xd0\xce\xc4",
                        "Save must preserve GBK bytes and mixed EOL");
                const auto copy = directory / "copy.txt";
                Require(frame->SaveAs(Filename(copy)).status == SaveStatus::Saved && Bytes(copy) == Bytes(source),
                        "Save As must retain GBK");
                std::puts("PASS: explicit GBK CJK/Euro Save/Save As exact bytes and mixed EOL");
                Add(frame, "¥☃😀𝄞");
                const auto rich = Text(frame);
                const auto saved = Bytes(copy);
                Require(frame->Save([] { return GbkLossChoice::Cancel; }).status == SaveStatus::Cancelled,
                        "loss cancellation must return Cancelled");
                Require(Bytes(copy) == saved && Text(frame) == rich &&
                            frame->Editor()->Send(Scintilla::Message::GetModify),
                        "loss cancellation must preserve bytes, text and dirty state");
                Require(frame->Save([] { return GbkLossChoice::Replace; }).status == SaveStatus::Saved,
                        "explicit replacement must save");
                Require(Bytes(copy) == saved + "??????" && Text(frame) == "ASCII\r\n中文\n€\r中文??????",
                        "loss bytes must match original no-best-fit BMP/surrogate widths");
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify), "saved replacement text must be clean");
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(Text(frame) == rich && frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Undo must restore rich text and dirty state");
                frame->Editor()->Send(Scintilla::Message::Redo);
                Require(Text(frame) == "ASCII\r\n中文\n€\r中文??????" &&
                            !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Redo must restore saved replacement text and clean state");
                Add(frame, "😀");
                const auto fallback = Text(frame);
                Require(frame->Save([] { return GbkLossChoice::Utf8; }).status == SaveStatus::Saved,
                        "UTF8 fallback must save losslessly");
                Require(Bytes(copy) == fallback && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                            !frame->DocumentHasBom(),
                        "fallback must write exact UTF8 without BOM and update format");
                std::puts("PASS: loss Cancel, original replacement bytes/Undo/Redo, UTF8 fallback");
                const std::string pua = "\xa1\x40\xa2\xe3\xaa\xa1\xfe\xa0";
                const auto puaPath = directory / "pua.txt";
                Write(puaPath, pua);
                auto* repertoire = new EditorFrame;
                Require(repertoire->Open(Filename(puaPath), FileEncoding::Gbk).status == OpenStatus::Opened,
                        "Windows CP936 PUA repertoire must open on each platform");
                Require(Text(repertoire) == "\xee\x93\x86\xee\x9d\xac\xee\x80\x80\xee\xa1\xa4",
                        "CP936 PUA values must match original Windows mapping");
                Require(repertoire->Save().status == SaveStatus::Saved && Bytes(puaPath) == pua,
                        "CP936 PUA must roundtrip without replacement");
                Add(repertoire, "\xef\xa3\xb5"); // U+F8F5 maps to invalid FF in the original Win32 encoder.
                Require(repertoire->Save([] { return GbkLossChoice::Cancel; }).status == SaveStatus::Cancelled &&
                            Bytes(puaPath) == pua,
                        "asymmetric F8F5 must require explicit loss choice");
                Require(repertoire->Save([] { return GbkLossChoice::Replace; }).status == SaveStatus::Saved &&
                            Bytes(puaPath) == pua + "?",
                        "asymmetric F8F5 replacement must be strict-readable question mark");
                repertoire->Destroy();
                std::puts("PASS: original CP936 PUA mapping and strict-readable F8F5 loss choice");
                Add(frame, "pending");
                const auto pending = Text(frame);
                const auto identity = frame->DocumentPath();
                const std::string malformed[] = {
                    "\x81", "\xff", "\x81\x30", "\x81\x7f", "\x81\xff", std::string("A\0\x81", 3), "\xef\xbb\xbf"};
                for (const auto& bytes : malformed) {
                    const auto invalid = directory / "invalid.txt";
                    Write(invalid, bytes);
                    const auto result = frame->Open(Filename(invalid), FileEncoding::Gbk);
                    Require(result.status == OpenStatus::Failed && !result.error.empty(),
                            "malformed GBK must fail clearly before prompting about dirty document");
                    Require(Text(frame) == pending && frame->DocumentPath() == identity &&
                                frame->DocumentEncoding() == FileEncoding::Utf8 &&
                                frame->Editor()->Send(Scintilla::Message::GetModify),
                            "malformed GBK must preserve current text/identity/encoding/dirty state");
                }
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify),
                        "malformed open must retain Undo savepoint");
                frame->Editor()->Send(Scintilla::Message::Redo);
                Require(Text(frame) == pending, "malformed open must retain Redo");
                const auto nulPath = directory / "nul.txt";
                const std::string nulBytes("A\0\xd6\xd0", 4);
                Write(nulPath, nulBytes);
                auto* binary = new EditorFrame;
                Require(binary->Open(Filename(nulPath)).status == OpenStatus::Failed,
                        "default open must not guess GBK");
                Require(binary->Open(Filename(nulPath), FileEncoding::Gbk).status == OpenStatus::Opened &&
                            Text(binary) == std::string("A\0中", 5),
                        "explicit GBK must decode after embedded NUL");
                Require(binary->Save().status == SaveStatus::Saved && Bytes(nulPath) == nulBytes,
                        "embedded NUL must roundtrip");
                binary->Destroy();
                std::puts("PASS: malformed complete stream rejection, no guessing/BOM stripping, embedded NUL");
                const auto blocked = directory / "blocked.txt";
                std::filesystem::create_directory(blocked);
                Write(blocked / "sentinel", "keep");
                for (const auto choice : {GbkLossChoice::Cancel, GbkLossChoice::Utf8, GbkLossChoice::Replace}) {
                    auto* failure = new EditorFrame;
                    Require(failure->Open(Filename(source), FileEncoding::Gbk).status == OpenStatus::Opened,
                            "failure source must open");
                    Add(failure, "¥☃😀");
                    const auto before = Text(failure);
                    Require(failure->SaveAs(std::nullopt, [choice] { return choice; }).status == SaveStatus::Cancelled,
                            "cancelled picker must not save");
                    const auto result = failure->SaveAs(Filename(blocked), [choice] { return choice; });
                    Require(result.status ==
                                (choice == GbkLossChoice::Cancel ? SaveStatus::Cancelled : SaveStatus::Failed),
                            "loss branch must cancel or report replacement failure");
                    Require(Text(failure) == before && failure->DocumentPath() == Filename(source) &&
                                failure->DocumentEncoding() == FileEncoding::Gbk && !failure->DocumentHasBom() &&
                                failure->Editor()->Send(Scintilla::Message::GetModify),
                            "loss cancellation/failure must preserve rich document, identity, format and dirty state");
                    Require(Bytes(source) == original + "\xd6\xd0\xce\xc4" && Bytes(blocked / "sentinel") == "keep",
                            "failed branch must preserve source and target bytes");
                    failure->Editor()->Send(Scintilla::Message::Undo);
                    Require(!failure->Editor()->Send(Scintilla::Message::GetModify),
                            "failure must retain original savepoint");
                    failure->Editor()->Send(Scintilla::Message::Redo);
                    Require(Text(failure) == before, "failure must retain rich Redo text");
                    failure->Destroy();
                }
                for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
                    Require(entry.path().filename().string().find(".wxnotepad-") == std::string::npos,
                            "failed saves must clean temporary siblings");
                std::puts("PASS: every loss choice cancellation/replacement failure preserves disk/session/history");
                auto* lossyAs = new EditorFrame;
                Require(lossyAs->Open(Filename(source), FileEncoding::Gbk).status == OpenStatus::Opened,
                        "lossy Save As source must open");
                Add(lossyAs, "¥☃😀𝄞");
                const auto richAs = Text(lossyAs);
                const auto lossyTarget = directory / "lossy-as.txt";
                Require(lossyAs->SaveAs(Filename(lossyTarget), [] { return GbkLossChoice::Replace; }).status ==
                            SaveStatus::Saved,
                        "confirmed lossy Save As must succeed");
                Require(Bytes(lossyTarget) == original + "\xd6\xd0\xce\xc4??????" &&
                            Bytes(source) == original + "\xd6\xd0\xce\xc4" &&
                            lossyAs->DocumentPath() == Filename(lossyTarget) &&
                            lossyAs->DocumentEncoding() == FileEncoding::Gbk &&
                            !lossyAs->Editor()->Send(Scintilla::Message::GetModify),
                        "lossy Save As must change identity only on success and retain source bytes");
                lossyAs->Editor()->Send(Scintilla::Message::Undo);
                Require(Text(lossyAs) == richAs && lossyAs->Editor()->Send(Scintilla::Message::GetModify),
                        "lossy Save As Undo must restore rich text dirty");
                lossyAs->Editor()->Send(Scintilla::Message::Redo);
                Require(!lossyAs->Editor()->Send(Scintilla::Message::GetModify),
                        "lossy Save As Redo must restore clean saved text");
                lossyAs->Destroy();
                std::puts("PASS: confirmed lossy Save As exact bytes, source isolation and Undo/Redo");
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
wxIMPLEMENT_APP(GbkApp);
