#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <filesystem>
#include <fstream>
#include "settings/settings_store.h"
#include <memory>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
using TestFiles::Filename;
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), bytes.size());
}
using TestEditor::Append;
using TestEditor::Text;
using TestFiles::Bytes;
void PrivateUseRepertoire(SettingsStore* settings, const std::filesystem::path& directory) {
    const std::string pua = "\xa1\x40\xa2\xe3\xaa\xa1\xfe\xa0";
    const auto puaPath = directory / "pua.txt";
    Write(puaPath, pua);
    auto* repertoire = new EditorFrame(settings);
    Require(repertoire->Open(Filename(puaPath), FileEncoding::Gbk).status == OpenStatus::Opened,
            "Windows CP936 PUA repertoire must open on each platform");
    Require(Text(repertoire) == "\xee\x93\x86\xee\x9d\xac\xee\x80\x80\xee\xa1\xa4",
            "CP936 PUA values must match original Windows mapping");
    Require(repertoire->Save().status == SaveStatus::Saved && Bytes(puaPath) == pua,
            "CP936 PUA must roundtrip without replacement");
    Append(repertoire, "\x{ef}\x{a3}\x{b5}"); // U+F8F5 maps to invalid FF in the original Win32 encoder.
    Require(repertoire->Save([] { return GbkLossChoice::Cancel; }).status == SaveStatus::Cancelled &&
                Bytes(puaPath) == pua,
            "asymmetric F8F5 must require explicit loss choice");
    Require(repertoire->Save([] { return GbkLossChoice::Replace; }).status == SaveStatus::Saved &&
                Bytes(puaPath) == pua + "?",
            "asymmetric F8F5 replacement must be strict-readable question mark");
    repertoire->Destroy();
    std::puts("PASS: original CP936 PUA mapping and strict-readable F8F5 loss choice");
}
void MalformedAndNulStreams(EditorFrame* frame, SettingsStore* settings, const std::filesystem::path& directory) {
    Append(frame, "pending");
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
                    frame->DocumentEncoding() == FileEncoding::Gbk &&
                    frame->Editor()->Send(Scintilla::Message::GetModify),
                "malformed GBK must preserve current text/identity/encoding/dirty state");
    }
    frame->Editor()->Send(Scintilla::Message::Undo);
    Require(!frame->Editor()->Send(Scintilla::Message::GetModify), "malformed open must retain Undo savepoint");
    frame->Editor()->Send(Scintilla::Message::Redo);
    Require(Text(frame) == pending, "malformed open must retain Redo");
    const auto nulPath = directory / "nul.txt";
    const std::string nulBytes("A\0\xd6\xd0", 4);
    Write(nulPath, nulBytes);
    auto* binary = new EditorFrame(settings);
    Require(binary->Open(Filename(nulPath), FileEncoding::Utf8).status == OpenStatus::Failed,
            "explicit UTF-8 must not fall back to GBK");
    Require(binary->Open(Filename(nulPath), FileEncoding::Gbk).status == OpenStatus::Opened &&
                Text(binary) == std::string("A\0中", 5),
            "explicit GBK must decode after embedded NUL");
    Require(binary->Save().status == SaveStatus::Saved && Bytes(nulPath) == nulBytes, "embedded NUL must roundtrip");
    binary->Destroy();
    std::puts("PASS: malformed stream rejection, explicit encoding/BOM handling, embedded NUL");
}
void LossChoiceFailures(SettingsStore* settings, const std::filesystem::path& directory,
                        const std::filesystem::path& source, const std::string& original) {
    const auto blocked = directory / "blocked.txt";
    std::filesystem::create_directory(blocked);
    Write(blocked / "sentinel", "keep");
    for (const auto choice : {GbkLossChoice::Cancel, GbkLossChoice::Replace}) {
        auto* failure = new EditorFrame(settings);
        Require(failure->Open(Filename(source), FileEncoding::Gbk).status == OpenStatus::Opened,
                "failure source must open");
        Append(failure, "¥☃😀");
        const auto before = Text(failure);
        Require(failure->SaveAs(std::nullopt, [choice] { return choice; }).status == SaveStatus::Cancelled,
                "cancelled picker must not save");
        const auto result = failure->SaveAs(Filename(blocked), [choice] { return choice; }, SyntaxLanguage::Python);
        Require(result.status == (choice == GbkLossChoice::Cancel ? SaveStatus::Cancelled : SaveStatus::Failed),
                "loss branch must cancel or report replacement failure");
        Require(Text(failure) == before && failure->DocumentPath() == Filename(source) &&
                    failure->DocumentEncoding() == FileEncoding::Gbk && !failure->DocumentHasBom() &&
                    !failure->GetStatusBar()->GetStatusText().Contains("override") &&
                    failure->GetStatusBar()->GetStatusText().Contains("Plain Text") &&
                    failure->Editor()->Send(Scintilla::Message::GetModify),
                "loss cancellation/failure must preserve rich document, identity, format and dirty state");
        Require(Bytes(source) == original + "\xd6\xd0\xce\xc4" && Bytes(blocked / "sentinel") == "keep",
                "failed branch must preserve source and target bytes");
        failure->Editor()->Send(Scintilla::Message::Undo);
        Require(!failure->Editor()->Send(Scintilla::Message::GetModify), "failure must retain original savepoint");
        failure->Editor()->Send(Scintilla::Message::Redo);
        Require(Text(failure) == before, "failure must retain rich Redo text");
        failure->Destroy();
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
        Require(!entry.path().filename().u8string().contains(u8".wxnotepad-"),
                "failed saves must clean temporary siblings");
    std::puts("PASS: every loss choice cancellation/replacement failure preserves disk/session/history");
}
void LossySaveAs(SettingsStore* settings, const std::filesystem::path& directory, const std::filesystem::path& source,
                 const std::string& original) {
    auto* lossyAs = new EditorFrame(settings);
    Require(lossyAs->Open(Filename(source), FileEncoding::Gbk).status == OpenStatus::Opened,
            "lossy Save As source must open");
    Append(lossyAs, "¥☃😀𝄞");
    const auto richAs = Text(lossyAs);
    const auto lossyTarget = directory / "lossy-as.txt";
    Require(lossyAs->SaveAs(Filename(lossyTarget), [] { return GbkLossChoice::Replace; }).status == SaveStatus::Saved,
            "confirmed lossy Save As must succeed");
    Require(Bytes(lossyTarget) == original + "\xd6\xd0\xce\xc4??????" &&
                Bytes(source) == original + "\xd6\xd0\xce\xc4" && Text(lossyAs) == richAs &&
                lossyAs->DocumentPath() == Filename(lossyTarget) && lossyAs->DocumentEncoding() == FileEncoding::Gbk &&
                !lossyAs->Editor()->Send(Scintilla::Message::GetModify),
            "lossy Save As must change identity only on success and retain source bytes");
    lossyAs->Editor()->Send(Scintilla::Message::Undo);
    Require(Text(lossyAs) == "ASCII\r\n中文\n€\r中文" && lossyAs->Editor()->Send(Scintilla::Message::GetModify),
            "lossy Save As Undo must undo only the preceding user edit");
    lossyAs->Editor()->Send(Scintilla::Message::Redo);
    Require(Text(lossyAs) == richAs && !lossyAs->Editor()->Send(Scintilla::Message::GetModify),
            "lossy Save As Redo must restore rich text at the save point");
    lossyAs->Destroy();
    std::puts("PASS: confirmed lossy Save As exact bytes, source isolation and Undo/Redo");
}
class GbkApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        settings_->values.wordWrap = false;
        auto* frame = new EditorFrame(settings_.get());
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-gbk");
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
                Append(frame, "中文");
                Require(frame->Save().status == SaveStatus::Saved && Bytes(source) == original + "\xd6\xd0\xce\xc4",
                        "Save must preserve GBK bytes and mixed EOL");
                const auto copy = directory / "copy.txt";
                Require(frame->SaveAs(Filename(copy)).status == SaveStatus::Saved && Bytes(copy) == Bytes(source),
                        "Save As must retain GBK");
                std::puts("PASS: explicit GBK CJK/Euro Save/Save As exact bytes and mixed EOL");
                Append(frame, "¥☃😀𝄞");
                const auto rich = Text(frame);
                const auto saved = Bytes(copy);
                Require(frame->Save([] { return GbkLossChoice::Cancel; }).status == SaveStatus::Cancelled,
                        "loss cancellation must return Cancelled");
                Require(Bytes(copy) == saved && Text(frame) == rich &&
                            frame->Editor()->Send(Scintilla::Message::GetModify),
                        "loss cancellation must preserve bytes, text and dirty state");
                Require(frame->Save([] { return GbkLossChoice::Replace; }).status == SaveStatus::Saved,
                        "explicit replacement must save");
                Require(Bytes(copy) == saved + "??????" && Text(frame) == rich,
                        "accepted loss writes original placeholders while retaining the rich editor text");
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify),
                        "accepted loss establishes a save point");
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(Text(frame) == "ASCII\r\n中文\n€\r中文" && frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Undo must undo the pre-save edit without a conversion undo action");
                frame->Editor()->Send(Scintilla::Message::Redo);
                Require(Text(frame) == rich && !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Redo must restore the rich text at the save point");
                Require(frame->DocumentEncoding() == FileEncoding::Gbk && !frame->DocumentHasBom(),
                        "accepted loss retains GBK encoding without a BOM");
                std::puts("PASS: loss Cancel, original replacement bytes, rich text and savepoint Undo/Redo");
                PrivateUseRepertoire(settings_.get(), directory);
                MalformedAndNulStreams(frame, settings_.get(), directory);
                LossChoiceFailures(settings_.get(), directory, source, original);
                LossySaveAs(settings_.get(), directory, source, original);
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(GbkApp);
