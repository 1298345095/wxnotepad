#include "document/document_session.h"
#include "editor/editor_control.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <array>
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
class AutomaticFixture {
public:
    AutomaticFixture(EditorControl* editor, const std::filesystem::path& directory)
        : editor_(editor), directory_(directory) {}
    void operator()(const char* name, const std::string& input, const std::string& text, FileEncoding encoding,
                    bool bom, const EncodingOptions& options = {}, std::optional<FileEncoding> strong = {},
                    std::optional<EncodingChoice> weak = {}) const {
        const auto fixture = directory_ / name;
        std::ofstream(fixture, std::ios::binary) << input;
        DocumentSession opened(*editor_, options);
        const auto result = opened.Open(TestFiles::Filename(fixture), [] { return true; }, strong, weak);
        if (result.status != OpenStatus::Opened) std::fprintf(stderr, "%s: %s\n", name, result.error.utf8_str().data());
        Require(result.status == OpenStatus::Opened, name);
        Require(Text(*editor_) == text && opened.Encoding() == encoding && opened.HasBom() == bom, name);
        Require(opened.Save().status == SaveStatus::Saved && TestFiles::Bytes(fixture) == input, name);
    }

private:
    EditorControl* editor_;
    const std::filesystem::path& directory_;
};
void CheckWeakNativeAnsi(EditorControl* editor, const std::filesystem::path& directory);
void CheckConfiguredDetection(EditorControl* editor, const std::filesystem::path& directory,
                              const AutomaticFixture& check);
void CheckAutomaticDetection(EditorControl* editor, const std::filesystem::path& directory) {
    DocumentSession document(*editor);
    const auto western = directory / "western.vhdl";
    TestFiles::WriteBytes(western, "-- \x{80}\x{a0}\r\n");
    Require(document.Open(TestFiles::Filename(western), [] { return true; }).status == OpenStatus::Opened,
            "VHDL preference opens original Windows-1252 bytes");
    Require(Text(*editor) == "-- €\u{00a0}\r\n" && document.Save().status == SaveStatus::Saved &&
                TestFiles::Bytes(western) == "-- \x{80}\x{a0}\r\n",
            "Windows-1252 preference retains Euro, NBSP and exact saved bytes");
    const auto artPath = directory / "original.nfo";
    TestFiles::WriteBytes(artPath, "\x{da}\x{c4}\x{bf}\r\n");
    Require(document.Open(TestFiles::Filename(artPath), [] { return true; }).status == OpenStatus::Opened,
            "NFO preference opens original DOS-437 box drawing");
    Require(Text(*editor) == "┌─┐\r\n" && document.Save().status == SaveStatus::Saved &&
                TestFiles::Bytes(artPath) == "\x{da}\x{c4}\x{bf}\r\n",
            "DOS-437 preference retains exact saved bytes");
    const auto bigEndian = directory / "big-endian.txt";
    TestFiles::WriteBytes(bigEndian, std::string("\x{fe}\x{ff}\0A\x{4e}\x{2d}\x{d8}\x{3d}\x{de}\0\0\r\0\n", 14));
    Require(document.Open(TestFiles::Filename(bigEndian), [] { return true; }).status == OpenStatus::Opened,
            "automatic Open recognizes the original UTF-16BE BOM signature");
    Require(Text(*editor) == "A中😀\r\n" && document.HasBom(), "UTF-16BE decodes to live UTF-8");
    Require(document.Save().status == SaveStatus::Saved &&
                TestFiles::Bytes(bigEndian) ==
                    std::string("\x{fe}\x{ff}\0A\x{4e}\x{2d}\x{d8}\x{3d}\x{de}\0\0\r\0\n", 14),
            "Save retains original UTF-16BE byte order and BOM");
    const auto path = directory / "declared.txt";
    const std::string bytes = "# coding: gb2312\n\x{d6}\x{d0}\x{ce}\x{c4}\n";
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
    const AutomaticFixture check(editor, directory);
    check("latin-utf16.txt", std::string("H\0e\0l\0l\0o\0\n\0", 12), "Hello\n", FileEncoding::Utf16Le, false);
    check("bom-utf16.txt", std::string("\x{ff}\x{fe}\x{2d}\x{4e}\x{87}\x{65}\x{0a}\0", 8), "中文\n",
          FileEncoding::Utf16Le, true);
    check("latin-utf16be.txt", std::string("\0H\0e\0l\0l\0o\0\n", 12), "Hello\n", FileEncoding::Utf16Be, false);
    check("weak-utf16be-ascii.txt", "plain", "plain", FileEncoding::Utf8, false, {}, {},
          EncodingChoice(FileEncoding::Utf16Be, true));
    check("strong-utf16be.txt", std::string("\x{fe}\x{ff}\x{4e}\x{2d}", 4), "中", FileEncoding::Utf16Be, true, {},
          FileEncoding::Utf16Be, EncodingChoice(FileEncoding::Utf16Le));
    check("strong-utf16le-over-be.txt", std::string("\x{fe}\x{ff}\0A", 4), "\x{ef}\x{bf}\x{be}\x{e4}\x{84}\x{80}",
          FileEncoding::Utf16Le, false, {}, FileEncoding::Utf16Le);
    EncodingOptions bigEndianDefault;
    bigEndianDefault.loadASCIIasUTF8 = false;
    bigEndianDefault.defaultEncoding = 7; // Original BOM-less UTF-16BE persisted identity.
    check("default-utf16be-empty.txt", "", "", FileEncoding::Utf16Be, false, bigEndianDefault);
    bigEndianDefault.defaultEncoding = 2; // Original UTF-16BE BOM persisted identity.
    const auto defaultEmpty = directory / "default-empty.txt";
    TestFiles::WriteBytes(defaultEmpty, "");
    DocumentSession defaultDocument(*editor, bigEndianDefault);
    Require(defaultDocument.Open(TestFiles::Filename(defaultEmpty), [] { return true; }).status == OpenStatus::Opened &&
                defaultDocument.Encoding() == FileEncoding::Utf16Be && defaultDocument.HasBom() &&
                defaultDocument.Save().status == SaveStatus::Saved && TestFiles::Bytes(defaultEmpty) == "\x{fe}\x{ff}",
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
    const Boundary boundaries[] = {
        {"", "", false},
        {"\x{fe}\x{ff}", "", true},
        {std::string("\x{fe}\x{ff}\0A\0", 5), "A", true},
        {std::string("\0A\0", 3), "A", false},
        {"\x{fe}\x{ff}\x{d8}\x{3d}", "\x{ef}\x{bf}\x{bd}", true},
        {std::string("\x{fe}\x{ff}\x{de}\0", 4), "\x{ef}\x{bf}\x{bd}", true},
        {std::string("\x{fe}\x{ff}\x{d8}\x{3d}\0A", 6),
         "\x{ef}\x{bf}\x{bd}"
         "A",
         true},
        {std::string("\x{fe}\x{ff}\x{d8}\x{3d}\x{de}\0", 6), "😀", true},
        {std::string("\x{fe}\x{ff}\0A\0\0\0B\0\r\0\n\0C\0\n\0D\0\r", 20), std::string("A\0B\r\nC\nD\r", 9), true}};
    const auto boundaryPath = directory / "boundary.txt";
    for (const auto& boundary : boundaries) {
        TestFiles::WriteBytes(boundaryPath, boundary.bytes);
        DocumentSession boundaryDocument(*editor);
        Require(boundaryDocument.Open(
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
    check("bom-utf8.txt", "\x{ef}\x{bb}\x{bf}# coding: gbk\n中文\n", "# coding: gbk\n中文\n", FileEncoding::Utf8, true);
    check("weak-ascii.txt", "# coding: utf8\nplain\n", "# coding: utf8\nplain\n", FileEncoding::Gbk, false, {}, {},
          EncodingChoice(FileEncoding::Gbk));
    check("weak-utf8.txt", "中文\n", "中文\n", FileEncoding::Utf8, false, {}, {}, EncodingChoice(FileEncoding::Gbk));
    check("weak-utf16.txt", "plain", "plain", FileEncoding::Utf8, false, {}, {}, EncodingChoice(FileEncoding::Utf16Le));
    const auto empty = directory / "empty-reload.txt";
    std::ofstream(empty, std::ios::binary).close();
    DocumentSession emptyReload(*editor);
    Require(
        emptyReload.Open(
                       TestFiles::Filename(empty), [] { return true; }, {}, EncodingChoice(FileEncoding::Utf16Le, true))
                    .status == OpenStatus::Opened &&
            emptyReload.Encoding() == FileEncoding::Utf16Le && emptyReload.HasBom(),
        "an empty reload keeps the weak UTF-16 BOM choice");
    Require(emptyReload.Save().status == SaveStatus::Saved && TestFiles::Bytes(empty) == "\x{ff}\x{fe}",
            "Save writes the BOM selected for the empty reload");
    check("weak-utf16-bom-ascii.txt", "plain", "plain", FileEncoding::Utf8, false, {}, {},
          EncodingChoice(FileEncoding::Utf16Le, true));
    check("weak-utf8-bom-ascii.txt", "plain", "plain", FileEncoding::Utf8, false, {}, {},
          EncodingChoice(FileEncoding::Utf8, true));
    std::ofstream(empty, std::ios::binary).close();
    Require(
        emptyReload.Open(
                       TestFiles::Filename(empty), [] { return true; }, {}, EncodingChoice(FileEncoding::Utf8, true))
                    .status == OpenStatus::Opened &&
            emptyReload.Encoding() == FileEncoding::Utf8 && emptyReload.HasBom(),
        "an empty reload keeps the weak UTF-8 BOM choice");
    Require(emptyReload.Save().status == SaveStatus::Saved && TestFiles::Bytes(empty) == "\x{ef}\x{bb}\x{bf}",
            "Save writes the UTF-8 BOM selected for the empty reload");
    CheckWeakNativeAnsi(editor, directory);
    check("strong.txt", "# coding: gbk\nplain", "# coding: gbk\nplain", FileEncoding::Utf8, false, {},
          FileEncoding::Utf8);
    check("strong-legacy.txt", "\x{c3}\x{a9}", "茅", FileEncoding::Gbk, false, {}, FileEncoding::Gbk);
    std::puts("PASS: original BOM, Latin UTF-16, UTF-8 validation, explicit and weak precedence");
    CheckConfiguredDetection(editor, directory, check);
}
void CheckWeakNativeAnsi(EditorControl* editor, const std::filesystem::path& directory) {
    const auto nativePath = directory / "native-ansi.txt";
    const std::string nativeBytes = "\x{d6}\x{d0}\x{ce}\x{c4}\r\n";
    std::ofstream(nativePath, std::ios::binary) << nativeBytes;
    DocumentSession nativeDocument(*editor);
    const auto nativeResult = nativeDocument.Open(
        TestFiles::Filename(nativePath), [] { return true; }, {}, EncodingChoice(FileEncoding::Gbk, false, true));
#ifdef __WXMSW__
    if (GetACP() == 936) {
        Require(nativeResult.status == OpenStatus::Opened && nativeDocument.Encoding() == FileEncoding::Gbk &&
                    nativeDocument.IsDefaultEncoding(),
                "weak ANSI retains its original native-default identity instead of becoming explicit CP936");
        Require(nativeDocument.Save().status == SaveStatus::Saved && TestFiles::Bytes(nativePath) == nativeBytes,
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
}
void CheckUnicodeLineEndings(const AutomaticFixture& check) {
    using enum FileEncoding;
    EncodingOptions statistics;
    statistics.skipUnicodeDetection = false;
    statistics.loadANSIasUTF8 = false;
    // The Georgian prefix reaches Unicode statistics even on native single-byte
    // code pages. The suffixes distinguish Unicode code units from byte CR/LF.
    const std::string prefix = "\x{d0}\x{10}\x{d1}\x{10}\x{d2}\x{10}\x{d3}\x{10}"
                               "\x{d4}\x{10}\x{d5}\x{10}\x{d6}\x{10}\x{d7}\x{10}";
    check("statistics-unicode-line-bytes.txt", prefix + "\x{15}\x{0a}\x{15}\x{0d}\x{15}\x{0d}",
          "აბგდევზთ\u{a15}\u{d15}\u{d15}", Utf16Le, false, statistics);

    struct ByteLineEnding {
        const char* name;
        const char* suffix;
    };
    constexpr std::array samples = {
        ByteLineEnding{"statistics-cross-lfcr.txt", "\x{15}\x{0a}\x{0d}\x{10}\x{15}\x{0a}"},
        ByteLineEnding{"statistics-cross-crlf.txt", "\x{15}\x{0d}\x{0a}\x{10}"},
        ByteLineEnding{"statistics-final-lfcr.txt", "\x{0a}\x{0d}"},
    };
    auto skipStatistics = statistics;
    skipStatistics.skipUnicodeDetection = true;
    for (const auto& sample : samples) {
        const auto bytes = prefix + sample.suffix;
        // These streams contain no NUL. The existing skip option provides the
        // platform's native/legacy fallback, including Windows UTF-8 ACP mode.
        const auto fallback = DetectEncoding(sample.name, bytes, std::nullopt, skipStatistics);
        Require(fallback.encoding != Utf16Le && fallback.encoding != Utf16Be && !fallback.bom,
                "skipping Unicode statistics retains the byte stream's native fallback");
        const auto detected = DetectEncoding(sample.name, bytes, std::nullopt, statistics);
        Require(detected.encoding == fallback.encoding && detected.bom == fallback.bom &&
                    detected.unsupportedCodePage == fallback.unsupportedCodePage &&
                    detected.systemDefault == fallback.systemDefault && detected.systemOem == fallback.systemOem,
                sample.name);
    }
    std::puts("PASS: Unicode code units and byte CR/LF retain distinct automatic encoding decisions");
}
void CheckConfiguredDetection(EditorControl* editor, const std::filesystem::path& directory,
                              const AutomaticFixture& check) {
    check("nonlatin-utf16.txt", std::string("\x{2d}\x{4e}\x{87}\x{65} \0E\0n\0g\0l\0i\0s\0h\0\n\0", 22),
          "中文 English\n", FileEncoding::Utf16Le, false);
    EncodingOptions unicodeStatistics;
    unicodeStatistics.skipUnicodeDetection = false;
    // Eight C0 bytes reach the original MaybeBinary gate even when
    // the native code page accepts every byte (for example CP1252).
    check("statistics-utf16.txt",
          std::string("\x{d0}\x{10}\x{d1}\x{10}\x{d2}\x{10}\x{d3}\x{10}\x{d4}\x{10}\x{d5}\x{10}\x{d6}"
                      "\x{10}\x{d7}\x{10}",
                      16),
          "აბგდევზთ", FileEncoding::Utf16Le, false, unicodeStatistics);
    CheckUnicodeLineEndings(check);
    const std::string masked = "# tab-width: 8\n" + std::string(600, 'x') + "\n# coding: windows1252\n";
    check("first-window.txt", masked, masked, FileEncoding::Utf8, false);
    const std::string footer = std::string(600, 'x') + "\n# coding: gbk\n\x{d6}\x{d0}\x{ce}\x{c4}";
    check("last-window.txt", footer, std::string(600, 'x') + "\n# coding: gbk\n中文", FileEncoding::Gbk, false);
    EncodingOptions ignoreTags;
    ignoreTags.noEncodingTags = true;
    check("disabled-tag.txt", "# coding: windows1252\nplain", "# coding: windows1252\nplain", FileEncoding::Utf8, false,
          ignoreTags);
    check("unicode-declaration.txt", "# coding: utf16le\nplain", "# coding: utf16le\nplain", FileEncoding::Utf8, false);
    for (const char* declaration : {"<?xml encoding='GB_2312'?>\n", "<meta charset=cp936>\n", "# fileencoding: ms936\n",
                                    "/*!40101 SET NAMES gbk */\n"}) {
        check("declarations.txt", std::string(declaration) + "\x{d6}\x{d0}\x{ce}\x{c4}",
              std::string(declaration) + "中文", FileEncoding::Gbk, false);
    }
    check("css.txt", "@charset \"GBK\";\nplain", "@charset \"GBK\";\nplain", FileEncoding::Utf8, false);
    check("case.txt", "# Coding: gbk\nplain", "# Coding: gbk\nplain", FileEncoding::Utf8, false);
    const std::string localDisabled = "# enable-local-variables: 0; coding: gbk\nplain";
    check("local-variables.txt", localDisabled, localDisabled, FileEncoding::Utf8, false);
    EncodingOptions ignoreVariables;
    ignoreVariables.noFileVariables = true;
    check("disabled-variables.txt", localDisabled, localDisabled, FileEncoding::Gbk, false, ignoreVariables);
    EncodingOptions legacy;
    legacy.defaultEncoding = 936;
    legacy.loadASCIIasUTF8 = false;
    check("configured-ascii.txt", "plain", "plain", FileEncoding::Gbk, false, legacy);
    check("configured-legacy.txt", "\x{d6}\x{d0}\x{ce}\x{c4}", "中文", FileEncoding::Gbk, false, legacy);
    check("preferred.sh", "# coding: gbk\nplain", "# coding: gbk\nplain", FileEncoding::Utf8, false, legacy);
    EncodingOptions art;
    art.loadNFOasOEM = false;
    check("disabled.nfo", "plain", "plain", FileEncoding::Utf8, false, art);
    for (const char* name : {"preferred.vhd", "preferred.VHDL"})
        check(name, "plain", "plain", FileEncoding::Windows1252, false);
    for (const char* name : {"preferred.nfo", "preferred.DIZ"})
        check(name, "plain", "plain", FileEncoding::Dos437, false);
    for (const char* alias : {"windows-1252", "xansi", "cp367", "ibm367", "us", "cp1252", "iso-8859-1"}) {
        const auto declaration = std::string("# coding: ") + alias + "\n";
        check("western-alias.txt", declaration + "\x{80}\x{a0}", declaration + "€\u{00a0}", FileEncoding::Windows1252,
              false);
    }
    for (const char* alias : {"ibm437", "437", "cp437", "cspc8", "codepage437"}) {
        const auto declaration = std::string("# coding: ") + alias + "\n";
        check("oem-alias.txt", declaration + "\x{da}\x{c4}\x{bf}", declaration + "┌─┐", FileEncoding::Dos437, false);
    }
    check("explicit.nfo", "\x{80}", "€", FileEncoding::Windows1252, false, {}, FileEncoding::Windows1252);
    check("explicit.vhdl", "\x{ec}", "∞", FileEncoding::Dos437, false, {}, FileEncoding::Dos437);
    EncodingOptions westernDefault;
    westernDefault.defaultEncoding = 1252;
    westernDefault.loadASCIIasUTF8 = false;
    check("default-western.txt", "\x{80}", "€", FileEncoding::Windows1252, false, westernDefault);
    westernDefault.defaultEncoding = 437;
    check("default-oem.txt", "\x{ec}", "∞", FileEncoding::Dos437, false, westernDefault);
    check("default-oem-ascii.txt", "plain", "plain", FileEncoding::Dos437, false, westernDefault);

    const auto retained = directory / "retained.txt";
    std::ofstream(retained, std::ios::binary) << "\x{ef}\x{bb}\x{bf}keep";
    DocumentSession protectedDocument(*editor);
    Require(protectedDocument.Open(TestFiles::Filename(retained), [] { return true; }).status == OpenStatus::Opened,
            "retained document opens");
    editor->Send(Scintilla::Message::AddText, 5, reinterpret_cast<intptr_t>("dirty"));
    const auto unchanged = [&] {
        Require(Text(*editor) == "dirtykeep" && protectedDocument.Path() == TestFiles::Filename(retained) &&
                    protectedDocument.Encoding() == FileEncoding::Utf8 && protectedDocument.HasBom() &&
                    editor->Send(Scintilla::Message::GetModify) && editor->Send(Scintilla::Message::CanUndo),
                "failed or cancelled automatic Open retains editor, history, path and encoding");
    };
    const auto unsupported = directory / "unsupported.txt";
    std::ofstream(unsupported, std::ios::binary) << "# coding: windows1251\nplain";
    const auto rejected = protectedDocument.Open(TestFiles::Filename(unsupported), [] { return true; });
    Require(rejected.status == OpenStatus::Failed && rejected.error.Contains("1251") &&
                rejected.error.Contains("not migrated"),
            "recognized unsupported encoding is explicit");
    unchanged();
    std::ofstream(unsupported, std::ios::binary).write("\x{fe}\x{ff}\0A", 4);
    Require(protectedDocument.Open(TestFiles::Filename(unsupported), [] { return false; }).status ==
                OpenStatus::Cancelled,
            "cancelled UTF-16BE replacement retains the live document");
    unchanged();
    std::ofstream(unsupported, std::ios::binary) << "\x{c0}\x{af}";
    Require(protectedDocument.Open(
                                 TestFiles::Filename(unsupported), [] { return true; }, FileEncoding::Utf8)
                    .status == OpenStatus::Failed,
            "explicit UTF-8 invalid data cannot fall back to CP936");
    unchanged();
    std::ofstream(unsupported, std::ios::binary) << "\x{ef}\x{bb}\x{bf}\x{80}";
    const auto badBom = protectedDocument.Open(TestFiles::Filename(unsupported), [] { return true; });
    Require(badBom.status == OpenStatus::Failed && badBom.error.Contains("UTF-8"),
            "UTF-8 BOM wins over legacy fallback even when its payload is invalid");
    unchanged();
    std::ofstream(unsupported, std::ios::binary) << "# coding: gbk\n\x{d6}\x{d0}\x{ce}\x{c4}";
    Require(protectedDocument.Open(TestFiles::Filename(unsupported), [] { return false; }).status ==
                OpenStatus::Cancelled,
            "discard cancellation after detection cancels automatic Open");
    unchanged();
    Require(protectedDocument.Open(std::nullopt, [] { return true; }).status == OpenStatus::Cancelled,
            "file chooser cancellation cancels automatic Open");
    unchanged();
    std::puts("PASS: configured fallback, original preferences, unsupported codecs, invalid data and "
              "cancellation");
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
                CheckAutomaticDetection(editor, directory);
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
