#include "document/document_session.h"
#include "editor/editor_control.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>
#ifdef __WXMSW__
#include <windows.h>
#endif

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
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-encoding");
            try {
                std::filesystem::create_directory(directory);
                DocumentSession document(*editor);
                const auto western = directory / "western.vhdl";
                TestFiles::WriteBytes(western, "-- \x80\xa0\r\n");
                Require(document.Open(TestFiles::Filename(western), [] { return true; }).status == OpenStatus::Opened,
                        "VHDL preference opens original Windows-1252 bytes");
                Require(Text(*editor) == "-- €\u00a0\r\n" && document.Save().status == SaveStatus::Saved &&
                            TestFiles::Bytes(western) == "-- \x80\xa0\r\n",
                        "Windows-1252 preference retains Euro, NBSP and exact saved bytes");
                const auto artPath = directory / "original.nfo";
                TestFiles::WriteBytes(artPath, "\xda\xc4\xbf\r\n");
                Require(document.Open(TestFiles::Filename(artPath), [] { return true; }).status == OpenStatus::Opened,
                        "NFO preference opens original DOS-437 box drawing");
                Require(Text(*editor) == "┌─┐\r\n" && document.Save().status == SaveStatus::Saved &&
                            TestFiles::Bytes(artPath) == "\xda\xc4\xbf\r\n",
                        "DOS-437 preference retains exact saved bytes");
                const auto bigEndian = directory / "big-endian.txt";
                TestFiles::WriteBytes(bigEndian, std::string("\xfe\xff\0A\x4e\x2d\xd8\x3d\xde\0\0\r\0\n", 14));
                Require(document.Open(TestFiles::Filename(bigEndian), [] { return true; }).status == OpenStatus::Opened,
                        "automatic Open recognizes the original UTF-16BE BOM signature");
                Require(Text(*editor) == "A中😀\r\n" && document.HasBom(), "UTF-16BE decodes to live UTF-8");
                Require(document.Save().status == SaveStatus::Saved &&
                            TestFiles::Bytes(bigEndian) == std::string("\xfe\xff\0A\x4e\x2d\xd8\x3d\xde\0\0\r\0\n", 14),
                        "Save retains original UTF-16BE byte order and BOM");
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
                                       std::optional<FileEncoding> strong = {},
                                       std::optional<EncodingChoice> weak = {}) {
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
                check("latin-utf16be.txt", std::string("\0H\0e\0l\0l\0o\0\n", 12), "Hello\n", FileEncoding::Utf16Be,
                      false);
                check("weak-utf16be-ascii.txt", "plain", "plain", FileEncoding::Utf8, false, {}, {},
                      EncodingChoice(FileEncoding::Utf16Be, true));
                check("strong-utf16be.txt", std::string("\xfe\xff\x4e\x2d", 4), "中", FileEncoding::Utf16Be, true, {},
                      FileEncoding::Utf16Be, EncodingChoice(FileEncoding::Utf16Le));
                check("strong-utf16le-over-be.txt", std::string("\xfe\xff\0A", 4), "\xef\xbf\xbe\xe4\x84\x80",
                      FileEncoding::Utf16Le, false, {}, FileEncoding::Utf16Le);
                EncodingOptions bigEndianDefault;
                bigEndianDefault.loadASCIIasUTF8 = false;
                bigEndianDefault.defaultEncoding = 7; // Original BOM-less UTF-16BE persisted identity.
                check("default-utf16be-empty.txt", "", "", FileEncoding::Utf16Be, false, bigEndianDefault);
                bigEndianDefault.defaultEncoding = 2; // Original UTF-16BE BOM persisted identity.
                const auto defaultEmpty = directory / "default-empty.txt";
                TestFiles::WriteBytes(defaultEmpty, "");
                DocumentSession defaultDocument(*editor, bigEndianDefault);
                Require(defaultDocument.Open(TestFiles::Filename(defaultEmpty), [] { return true; }).status ==
                                OpenStatus::Opened &&
                            defaultDocument.Encoding() == FileEncoding::Utf16Be && defaultDocument.HasBom() &&
                            defaultDocument.Save().status == SaveStatus::Saved &&
                            TestFiles::Bytes(defaultEmpty) == "\xfe\xff",
                        "existing DefaultEncoding UTF-16BE BOM identity is consumed for an empty Open");
                TestFiles::WriteBytes(defaultEmpty, "");
                Require(defaultDocument
                                    .Open(
                                        TestFiles::Filename(defaultEmpty), [] { return true; }, {},
                                        EncodingChoice(FileEncoding::Utf16Be, true))
                                    .status == OpenStatus::Opened &&
                            defaultDocument.HasBom() && defaultDocument.Encoding() == FileEncoding::Utf16Be,
                        "empty weak reload retains UTF-16BE and its BOM identity");
                // Literal observations from the independent original executable capture
                // (encoding-languages-2026-11-08/reference/original-observations.json).
                struct Boundary {
                    std::string bytes;
                    std::string text;
                    bool bom;
                };
                const Boundary boundaries[] = {{"", "", false},
                                               {"\xfe\xff", "", true},
                                               {std::string("\xfe\xff\0A\0", 5), "A", true},
                                               {std::string("\0A\0", 3), "A", false},
                                               {"\xfe\xff\xd8\x3d", "\xef\xbf\xbd", true},
                                               {std::string("\xfe\xff\xde\0", 4), "\xef\xbf\xbd", true},
                                               {std::string("\xfe\xff\xd8\x3d\0A", 6),
                                                "\xef\xbf\xbd"
                                                "A",
                                                true},
                                               {std::string("\xfe\xff\xd8\x3d\xde\0", 6), "😀", true},
                                               {std::string("\xfe\xff\0A\0\0\0B\0\r\0\n\0C\0\n\0D\0\r", 20),
                                                std::string("A\0B\r\nC\nD\r", 9), true}};
                const auto boundaryPath = directory / "boundary.txt";
                for (const auto& boundary : boundaries) {
                    TestFiles::WriteBytes(boundaryPath, boundary.bytes);
                    DocumentSession boundaryDocument(*editor);
                    Require(
                        boundaryDocument
                                    .Open(
                                        TestFiles::Filename(boundaryPath), [] { return true; }, FileEncoding::Utf16Be)
                                    .status == OpenStatus::Opened &&
                            Text(*editor) == boundary.text && boundaryDocument.Encoding() == FileEncoding::Utf16Be &&
                            boundaryDocument.HasBom() == boundary.bom && !boundaryDocument.IsModified() &&
                            !editor->Send(Scintilla::Message::CanUndo),
                        "UTF-16BE boundary bytes and identity match independently captured original results");
                    editor->Send(Scintilla::Message::AppendText, 1, reinterpret_cast<intptr_t>("x"));
                    editor->Send(Scintilla::Message::Undo);
                    Require(Text(*editor) == boundary.text && editor->Send(Scintilla::Message::CanRedo),
                            "Undo after UTF-16BE load preserves replacement/NUL bytes and supports Redo");
                    editor->Send(Scintilla::Message::Redo);
                    Require(Text(*editor) == boundary.text + "x", "Redo after UTF-16BE load restores the edit");
                }
                std::puts("PASS: UTF-16BE original boundary captures, defaults, strong/weak precedence and history");
                check("declared-utf8.txt", "# coding: gbk\n中文\n", "# coding: gbk\n中文\n", FileEncoding::Utf8, false);
                check("bom-utf8.txt", "\xef\xbb\xbf# coding: gbk\n中文\n", "# coding: gbk\n中文\n", FileEncoding::Utf8,
                      true);
                check("weak-ascii.txt", "# coding: utf8\nplain\n", "# coding: utf8\nplain\n", FileEncoding::Gbk, false,
                      {}, {}, EncodingChoice(FileEncoding::Gbk));
                check("weak-utf8.txt", "中文\n", "中文\n", FileEncoding::Utf8, false, {}, {},
                      EncodingChoice(FileEncoding::Gbk));
                check("weak-utf16.txt", "plain", "plain", FileEncoding::Utf8, false, {}, {},
                      EncodingChoice(FileEncoding::Utf16Le));
                const auto empty = directory / "empty-reload.txt";
                std::ofstream(empty, std::ios::binary).close();
                DocumentSession emptyReload(*editor);
                Require(emptyReload.Open(
                                       TestFiles::Filename(empty), [] { return true; }, {},
                                       EncodingChoice(FileEncoding::Utf16Le, true))
                                    .status == OpenStatus::Opened &&
                            emptyReload.Encoding() == FileEncoding::Utf16Le && emptyReload.HasBom(),
                        "an empty reload keeps the weak UTF-16 BOM choice");
                Require(emptyReload.Save().status == SaveStatus::Saved && TestFiles::Bytes(empty) == "\xff\xfe",
                        "Save writes the BOM selected for the empty reload");
                check("weak-utf16-bom-ascii.txt", "plain", "plain", FileEncoding::Utf8, false, {}, {},
                      EncodingChoice(FileEncoding::Utf16Le, true));
                check("weak-utf8-bom-ascii.txt", "plain", "plain", FileEncoding::Utf8, false, {}, {},
                      EncodingChoice(FileEncoding::Utf8, true));
                std::ofstream(empty, std::ios::binary).close();
                Require(emptyReload.Open(
                                       TestFiles::Filename(empty), [] { return true; }, {},
                                       EncodingChoice(FileEncoding::Utf8, true))
                                    .status == OpenStatus::Opened &&
                            emptyReload.Encoding() == FileEncoding::Utf8 && emptyReload.HasBom(),
                        "an empty reload keeps the weak UTF-8 BOM choice");
                Require(emptyReload.Save().status == SaveStatus::Saved && TestFiles::Bytes(empty) == "\xef\xbb\xbf",
                        "Save writes the UTF-8 BOM selected for the empty reload");
                const auto nativePath = directory / "native-ansi.txt";
                const std::string nativeBytes = "\xd6\xd0\xce\xc4\r\n";
                std::ofstream(nativePath, std::ios::binary) << nativeBytes;
                DocumentSession nativeDocument(*editor);
                const auto nativeResult = nativeDocument.Open(
                    TestFiles::Filename(nativePath), [] { return true; }, {},
                    EncodingChoice(FileEncoding::Gbk, false, true));
#ifdef __WXMSW__
                if (GetACP() == 936) {
                    Require(
                        nativeResult.status == OpenStatus::Opened && nativeDocument.Encoding() == FileEncoding::Gbk &&
                            nativeDocument.IsDefaultEncoding(),
                        "weak ANSI retains its original native-default identity instead of becoming explicit CP936");
                    Require(nativeDocument.Save().status == SaveStatus::Saved &&
                                TestFiles::Bytes(nativePath) == nativeBytes,
                            "weak ANSI native CP936 Save retains exact bytes");
                    DocumentSession convertedNative(*editor);
                    Require(convertedNative.Open(TestFiles::Filename(nativePath), [] { return true; }).status ==
                                    OpenStatus::Opened &&
                                !convertedNative.IsDefaultEncoding(),
                            "LoadANSIasUTF8 maps valid native CP936 to its explicit encoding without weak ANSI");
                }
#else
                if (wxLocale::GetSystemEncodingName().CmpNoCase("UTF-8") == 0)
                    Require(nativeResult.status == OpenStatus::Failed && nativeResult.error.Contains("UTF-8"),
                            "a native UTF-8 locale does not invent CP936 as weak ANSI fallback");
#endif
                check("strong.txt", "# coding: gbk\nplain", "# coding: gbk\nplain", FileEncoding::Utf8, false, {},
                      FileEncoding::Utf8);
                check("strong-legacy.txt", "\xc3\xa9", "茅", FileEncoding::Gbk, false, {}, FileEncoding::Gbk);
                std::puts("PASS: original BOM, Latin UTF-16, UTF-8 validation, explicit and weak precedence");
                check("nonlatin-utf16.txt", std::string("\x2d\x4e\x87\x65 \0E\0n\0g\0l\0i\0s\0h\0\n\0", 22),
                      "中文 English\n", FileEncoding::Utf16Le, false);
                EncodingOptions unicodeStatistics;
                unicodeStatistics.skipUnicodeDetection = false;
                // Eight C0 bytes reach the original MaybeBinary gate even when
                // the native code page accepts every byte (for example CP1252).
                check("statistics-utf16.txt",
                      std::string("\xd0\x10\xd1\x10\xd2\x10\xd3\x10\xd4\x10\xd5\x10\xd6\x10\xd7\x10", 16), "აბგდევზთ",
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
                for (const char* name : {"preferred.vhd", "preferred.VHDL"})
                    check(name, "plain", "plain", FileEncoding::Windows1252, false);
                for (const char* name : {"preferred.nfo", "preferred.DIZ"})
                    check(name, "plain", "plain", FileEncoding::Dos437, false);
                for (const char* alias : {"windows-1252", "xansi", "cp367", "ibm367", "us", "cp1252", "iso-8859-1"}) {
                    const auto declaration = std::string("# coding: ") + alias + "\n";
                    check("western-alias.txt", declaration + "\x80\xa0", declaration + "€\u00a0",
                          FileEncoding::Windows1252, false);
                }
                for (const char* alias : {"ibm437", "437", "cp437", "cspc8", "codepage437"}) {
                    const auto declaration = std::string("# coding: ") + alias + "\n";
                    check("oem-alias.txt", declaration + "\xda\xc4\xbf", declaration + "┌─┐", FileEncoding::Dos437,
                          false);
                }
                check("explicit.nfo", "\x80", "€", FileEncoding::Windows1252, false, {}, FileEncoding::Windows1252);
                check("explicit.vhdl", "\xec", "∞", FileEncoding::Dos437, false, {}, FileEncoding::Dos437);
                EncodingOptions westernDefault;
                westernDefault.defaultEncoding = 1252;
                westernDefault.loadASCIIasUTF8 = false;
                check("default-western.txt", "\x80", "€", FileEncoding::Windows1252, false, westernDefault);
                westernDefault.defaultEncoding = 437;
                check("default-oem.txt", "\xec", "∞", FileEncoding::Dos437, false, westernDefault);
                check("default-oem-ascii.txt", "plain", "plain", FileEncoding::Dos437, false, westernDefault);

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
                std::ofstream(unsupported, std::ios::binary) << "# coding: windows1251\nplain";
                const auto rejected = protectedDocument.Open(TestFiles::Filename(unsupported), [] { return true; });
                Require(rejected.status == OpenStatus::Failed && rejected.error.Contains("1251") &&
                            rejected.error.Contains("not migrated"),
                        "recognized unsupported encoding is explicit");
                unchanged();
                std::ofstream(unsupported, std::ios::binary).write("\xfe\xff\0A", 4);
                Require(protectedDocument.Open(TestFiles::Filename(unsupported), [] { return false; }).status ==
                            OpenStatus::Cancelled,
                        "cancelled UTF-16BE replacement retains the live document");
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
