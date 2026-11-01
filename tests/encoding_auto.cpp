#include "document/document_session.h"
#include "editor/editor_control.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string Text(EditorControl& editor) {
    std::string text(static_cast<size_t>(editor.Send(Scintilla::Message::GetLength)) + 1, '\0');
    editor.Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
class EncodingApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "Automatic encoding");
        auto* editor = new EditorControl(frame);
        CallAfter([this, frame, editor] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-encoding-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                DocumentSession document(*editor);
                const auto path = directory / "declared.txt";
                const std::string bytes = "# coding: gb2312\n\xd6\xd0\xce\xc4\n";
                std::ofstream(path, std::ios::binary) << bytes;
                Require(document.Open(TestFiles::Filename(path), [] { return true; }).status == OpenStatus::Opened,
                        "automatic Open must recognize a CP936 declaration");
                Require(Text(*editor) == "# coding: gb2312\n中文\n" && document.Encoding() == FileEncoding::Gbk,
                        "declaration selects the supported CP936 codec");
                Require(document.Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes,
                        "Save must retain the chosen encoding and exact source bytes");
                std::puts("PASS: declared CP936 automatic Open and exact Save");
                std::ofstream(path, std::ios::binary) << "# coding: gbk\nplain\n";
                Require(document.Open(TestFiles::Filename(path), [] { return true; }).status == OpenStatus::Opened &&
                            document.Encoding() == FileEncoding::Gbk,
                        "ASCII declarations precede the ASCII-as-UTF8 default");
                const auto check = [&](const char* name, const std::string& input, const std::string& text,
                                       FileEncoding encoding, bool bom, const EncodingOptions& options = {},
                                       std::optional<FileEncoding> strong = {}, std::optional<FileEncoding> weak = {}) {
                    const auto fixture = directory / name;
                    std::ofstream(fixture, std::ios::binary) << input;
                    DocumentSession opened(*editor, options);
                    const auto result = opened.Open(TestFiles::Filename(fixture), [] { return true; }, strong, weak);
                    if (result.status != OpenStatus::Opened)
                        std::fprintf(stderr, "%s: %s\n", name, result.error.utf8_str().data());
                    Require(result.status == OpenStatus::Opened, name);
                    Require(Text(*editor) == text && opened.Encoding() == encoding && opened.HasBom() == bom, name);
                    Require(opened.Save().status == SaveStatus::Saved && TestFiles::Bytes(fixture) == input, name);
                };
                check("latin-utf16.txt", std::string("H\0e\0l\0l\0o\0\n\0", 12), "Hello\n", FileEncoding::Utf16Le,
                      false);
                check("bom-utf16.txt", std::string("\xff\xfe\x2d\x4e\x87\x65\x0a\0", 8), "中文\n",
                      FileEncoding::Utf16Le, true);
                check("declared-utf8.txt", "# coding: gbk\n中文\n", "# coding: gbk\n中文\n", FileEncoding::Utf8, false);
                check("bom-utf8.txt", "\xef\xbb\xbf# coding: gbk\n中文\n", "# coding: gbk\n中文\n", FileEncoding::Utf8,
                      true);
                check("weak-ascii.txt", "# coding: utf8\nplain\n", "# coding: utf8\nplain\n", FileEncoding::Gbk, false,
                      {}, {}, FileEncoding::Gbk);
                check("weak-utf8.txt", "中文\n", "中文\n", FileEncoding::Utf8, false, {}, {}, FileEncoding::Gbk);
                check("weak-utf16.txt", "plain", "plain", FileEncoding::Utf8, false, {}, {}, FileEncoding::Utf16Le);
                check("strong.txt", "# coding: gbk\nplain", "# coding: gbk\nplain", FileEncoding::Utf8, false, {},
                      FileEncoding::Utf8);
                check("strong-legacy.txt", "\xc3\xa9", "茅", FileEncoding::Gbk, false, {}, FileEncoding::Gbk);
                std::puts("PASS: original BOM, Latin UTF-16, UTF-8 validation, explicit and weak precedence");
                check("nonlatin-utf16.txt", std::string("\x2d\x4e\x87\x65 \0E\0n\0g\0l\0i\0s\0h\0\n\0", 22),
                      "中文 English\n", FileEncoding::Utf16Le, false);
                EncodingOptions unicodeStatistics;
                unicodeStatistics.skipUnicodeDetection = false;
                check("statistics-utf16.txt", std::string("\xd0\x10\xd1\x10\xd2\x10\xd3\x10", 8), "აბგდ",
                      FileEncoding::Utf16Le, false, unicodeStatistics);
                const std::string masked = "# tab-width: 8\n" + std::string(600, 'x') + "\n# coding: windows1252\n";
                check("first-window.txt", masked, masked, FileEncoding::Utf8, false);
                const std::string footer = std::string(600, 'x') + "\n# coding: gbk\n\xd6\xd0\xce\xc4";
                check("last-window.txt", footer, std::string(600, 'x') + "\n# coding: gbk\n中文", FileEncoding::Gbk,
                      false);
                EncodingOptions ignoreTags;
                ignoreTags.noEncodingTags = true;
                check("disabled-tag.txt", "# coding: windows1252\nplain", "# coding: windows1252\nplain",
                      FileEncoding::Utf8, false, ignoreTags);
                check("unicode-declaration.txt", "# coding: utf16le\nplain", "# coding: utf16le\nplain",
                      FileEncoding::Utf8, false);
                for (const char* declaration : {"<?xml encoding='GB_2312'?>\n", "<meta charset=cp936>\n",
                                                "# fileencoding: ms936\n", "/*!40101 SET NAMES gbk */\n"}) {
                    check("declarations.txt", std::string(declaration) + "\xd6\xd0\xce\xc4",
                          std::string(declaration) + "中文", FileEncoding::Gbk, false);
                }
                check("css.txt", "@charset \"GBK\";\nplain", "@charset \"GBK\";\nplain", FileEncoding::Utf8, false);
                check("case.txt", "# Coding: gbk\nplain", "# Coding: gbk\nplain", FileEncoding::Utf8, false);
                const std::string localDisabled = "# enable-local-variables: 0; coding: gbk\nplain";
                check("local-variables.txt", localDisabled, localDisabled, FileEncoding::Utf8, false);
                EncodingOptions ignoreVariables;
                ignoreVariables.noFileVariables = true;
                check("disabled-variables.txt", localDisabled, localDisabled, FileEncoding::Gbk, false,
                      ignoreVariables);
                EncodingOptions legacy;
                legacy.defaultEncoding = 936;
                legacy.loadASCIIasUTF8 = false;
                check("configured-ascii.txt", "plain", "plain", FileEncoding::Gbk, false, legacy);
                check("configured-legacy.txt", "\xd6\xd0\xce\xc4", "中文", FileEncoding::Gbk, false, legacy);
                check("preferred.sh", "# coding: gbk\nplain", "# coding: gbk\nplain", FileEncoding::Utf8, false,
                      legacy);
                EncodingOptions art;
                art.loadNFOasOEM = false;
                check("disabled.nfo", "plain", "plain", FileEncoding::Utf8, false, art);

                const auto retained = directory / "retained.txt";
                std::ofstream(retained, std::ios::binary) << "\xef\xbb\xbfkeep";
                DocumentSession protectedDocument(*editor);
                Require(protectedDocument.Open(TestFiles::Filename(retained), [] { return true; }).status ==
                            OpenStatus::Opened,
                        "retained document opens");
                editor->Send(Scintilla::Message::AddText, 5, reinterpret_cast<intptr_t>("dirty"));
                const auto unchanged = [&] {
                    Require(Text(*editor) == "dirtykeep" && protectedDocument.Path() == TestFiles::Filename(retained) &&
                                protectedDocument.Encoding() == FileEncoding::Utf8 && protectedDocument.HasBom() &&
                                editor->Send(Scintilla::Message::GetModify) &&
                                editor->Send(Scintilla::Message::CanUndo),
                            "failed or cancelled automatic Open retains editor, history, path and encoding");
                };
                const auto unsupported = directory / "unsupported.txt";
                std::ofstream(unsupported, std::ios::binary) << "# coding: windows1252\nplain";
                const auto rejected = protectedDocument.Open(TestFiles::Filename(unsupported), [] { return true; });
                Require(rejected.status == OpenStatus::Failed && rejected.error.Contains("1252") &&
                            rejected.error.Contains("not migrated"),
                        "recognized unsupported encoding is explicit");
                unchanged();
                std::ofstream(unsupported, std::ios::binary) << "\xfe\xff\0A";
                Require(protectedDocument.Open(TestFiles::Filename(unsupported), [] { return true; }).status ==
                            OpenStatus::Failed,
                        "UTF-16BE is not silently labeled UTF-8 or UTF-16LE");
                unchanged();
                std::ofstream(unsupported, std::ios::binary) << "\xc0\xaf";
                Require(
                    protectedDocument.Open(
                                         TestFiles::Filename(unsupported), [] { return true; }, FileEncoding::Utf8)
                            .status == OpenStatus::Failed,
                    "explicit UTF-8 invalid data cannot fall back to CP936");
                unchanged();
                std::ofstream(unsupported, std::ios::binary) << "\xef\xbb\xbf\x80";
                const auto badBom = protectedDocument.Open(TestFiles::Filename(unsupported), [] { return true; });
                Require(badBom.status == OpenStatus::Failed && badBom.error.Contains("UTF-8"),
                        "UTF-8 BOM wins over legacy fallback even when its payload is invalid");
                unchanged();
                std::ofstream(unsupported, std::ios::binary) << "# coding: gbk\n\xd6\xd0\xce\xc4";
                Require(protectedDocument.Open(TestFiles::Filename(unsupported), [] { return false; }).status ==
                            OpenStatus::Cancelled,
                        "discard cancellation after detection cancels automatic Open");
                unchanged();
                Require(protectedDocument.Open(std::nullopt, [] { return true; }).status == OpenStatus::Cancelled,
                        "file chooser cancellation cancels automatic Open");
                unchanged();
                std::puts("PASS: configured fallback, original preferences, unsupported codecs, invalid data and "
                          "cancellation");
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
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(EncodingApp);
