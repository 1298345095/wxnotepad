#include "diagnostics/print.h"
#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "native_search_helpers.h"
#include "settings/settings_store.h"
#include <wx/accel.h>
#include <wx/wx.h>
#include <wx/datetime.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <print>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>
#ifdef __WXMSW__
#include <shlobj.h>
#else
#include <csignal>
#include <sys/resource.h>
#endif

namespace {
using namespace std::string_view_literals;
namespace fs = std::filesystem;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

wxString RequireSourcePath(const EditorFrame& frame, const fs::path& source) {
    const auto path = frame.DocumentPath();
    std::error_code error;
    Require(fs::equivalent(TestFiles::Utf8Path(path.utf8_string()), source, error),
            "current document resolves to the original fixture");
    return path;
}

class BackupFixtureRoot {
public:
    BackupFixtureRoot() : path_(TestFiles::TemporaryDirectory()) {
        // Windows generates a .tmp reservation. Keep dots confined to the
        // fixture that explicitly exercises Notepad4's first-dot path rule.
        path_.replace_extension();
        path_ += "-backup";
        Require(fs::create_directory(path_), "creates a private backup fixture directory");
    }

    ~BackupFixtureRoot() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& Path() const { return path_; }

private:
    fs::path path_;
};

fs::path BackupFixtureDirectory(std::string_view fixture = {}) {
    static const BackupFixtureRoot root;
    return fixture.empty() ? root.Path() : root.Path() / fixture;
}

fs::path FallbackDirectory() {
#ifdef __WXMSW__
    PWSTR root = nullptr;
    Require(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &root) == S_OK,
            "test host resolves LocalAppData");
    const fs::path directory(root);
    CoTaskMemFree(root);
#else
    const auto directory = BackupFixtureDirectory("xdg");
#endif
    return directory / "Notepad4" / "AutoSave";
}

std::vector<fs::path> MatchingNames(const fs::path& directory, const fs::path& prefix) {
    std::vector<fs::path> result;
    if (fs::is_directory(directory))
        for (const auto& entry : fs::directory_iterator(directory))
            if (entry.path().filename().native().starts_with(prefix.native())) result.push_back(entry.path());
    return result;
}

class OwnedBackups {
public:
    ~OwnedBackups() {
        std::error_code error;
        for (const auto& path : paths)
            fs::remove(path, error);
    }

    std::vector<fs::path> paths;
};

wxDateTime BackupTimestamp() {
    auto time = wxDateTime::UNow();
    time.SetMillisecond(0);
    return time;
}

std::pair<wxDateTime, wxDateTime> TimestampedBackup(EditorFrame& frame, int command) {
    frame.UpdateWindowUI(wxUPDATE_UI_RECURSE);
    const auto start = BackupTimestamp();
    NativeSearch::ActivateCommand(&frame, command);
    const auto end = BackupTimestamp();
    wxYield();
    return {start, end};
}

void RequireTimestamp(const fs::path& file, const wxString& basename, const wxString& extension,
                      const wxDateTime& start, const wxDateTime& end) {
    for (auto time = start; time <= end; time += wxTimeSpan::Second()) {
        if (TestFiles::Filename(file.filename()) == basename + time.Format(" %Y%m%d %H%M%S") + extension) return;
    }
    throw std::runtime_error("backup name uses original basename, local date/second and extension");
}

void NamedBackup(EditorFrame& frame) {
    auto* menu = frame.GetMenuBar();
    const int command = menu->FindMenuItem("File", "Save Backup");
    Require(command != wxNOT_FOUND, "File menu offers Save Backup");
    auto* item = menu->FindItem(command);
    Require(!std::unique_ptr<wxAcceleratorEntry>(item->GetAccel()), "Save Backup has no shortcut");
    auto* file = menu->GetMenu(menu->FindMenu("File"));
    size_t position = 0;
    while (position < file->GetMenuItemCount() && file->FindItemByPosition(position) != item)
        ++position;
    Require(position > 0 && position + 1 < file->GetMenuItemCount(), "Save Backup is a top-level File command");
    Require(file->FindItemByPosition(position - 1)->GetItemLabelText() == "Save As..." &&
                file->FindItemByPosition(position + 1)->GetItemLabelText() == "Save Copy...",
            "Save Backup appears between Save As and Save Copy");
    const auto directory = BackupFixtureDirectory("save-backup");
    std::filesystem::create_directory(directory);
    const auto source = directory / "text.tar.gz";
    constexpr auto input = "\xef\xbb\xbf中é😀\r\nA\0B\rC\nD"sv;
    TestFiles::WriteBytes(source, input);
    Require(frame.Open(TestFiles::Filename(source)).status == OpenStatus::Opened && frame.DocumentHasUtf8Bom() &&
                !frame.DocumentIsModified(),
            "opened original UTF-8 BOM fixture is unmodified");
    const auto path = RequireSourcePath(frame, source);
    frame.UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(item->IsEnabled(), "unmodified named document can create a backup");
    const auto [start, end] = TimestampedBackup(frame, command);
    size_t backups = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path() == source) continue;
        ++backups;
        RequireTimestamp(entry.path(), "text.tar.gz", ".tar.gz", start, end);
        Require(TestFiles::Bytes(entry.path()) == "中é😀\r\nA\0B\rC\nD"sv,
                "backup matches original bytes without BOM and preserves mixed EOLs and NUL");
    }
    Require(backups == 1, "named backup creates exactly one sibling");
    Require(TestFiles::Bytes(source) == input, "named backup leaves the source bytes intact");
    Require(frame.DocumentPath() == path, "named backup keeps the original document path");
    Require(frame.DocumentHasUtf8Bom() && !frame.DocumentIsModified(), "named backup keeps format and save point");

    const auto empty = directory / "empty.txt";
    TestFiles::WriteBytes(empty, "");
    Require(frame.Open(TestFiles::Filename(empty)).status == OpenStatus::Opened, "opens original named-empty case");
    NativeSearch::Command(&frame, command);
    Require(item->IsEnabled() && MatchingNames(directory, "empty.txt ").empty(),
            "empty named file keeps Save Backup enabled without creating a file");
}

struct FallbackFixture {
    const char* directory;
    FileEncoding encoding;
    std::string_view input;
    std::string_view eolAndBackup;
};

void NamedFallback(EditorFrame& frame, const FallbackFixture& fixture) {
    auto* menu = frame.GetMenuBar();
    const auto command = menu->FindMenuItem("File", "Save Backup");
    const auto directory = BackupFixtureDirectory(fixture.directory) / "missing";
    const auto moved = directory.parent_path() / "moved";
    fs::create_directories(directory);
    const auto prefix = BackupFixtureDirectory().filename();
    auto name = prefix;
    name += TestFiles::Utf8Path("-é中🙂.txt");
    const auto source = directory / name;
    TestFiles::WriteBytes(source, fixture.input);
    Require(frame.Open(TestFiles::Filename(source), fixture.encoding).status == OpenStatus::Opened,
            "opens the original missing-directory fixture");
    const auto path = RequireSourcePath(frame, source);
    fs::rename(directory, moved);

    const auto fallback = FallbackDirectory();
    Require(MatchingNames(fallback, prefix).empty(), "fallback fixture namespace is unused");
    const auto [start, end] = TimestampedBackup(frame, command);
    const OwnedBackups owned{MatchingNames(fallback, prefix)};
    Require(owned.paths.size() == 1, "a missing source directory falls back to native user data");
    RequireTimestamp(owned.paths.front(), TestFiles::Filename(name), ".txt", start, end);
    auto expected = "AutoSave for " + path.utf8_string();
    expected += fixture.eolAndBackup;
    Require(TestFiles::Bytes(owned.paths.front()) == expected,
            "fallback writes the original UTF-8 metadata, active line ending and editor bytes");
    Require(frame.DocumentPath() == path && !frame.DocumentIsModified() &&
                frame.DocumentEncoding() == fixture.encoding && TestFiles::Bytes(moved / name) == fixture.input,
            "fallback keeps the live document and source bytes");
}

void UntitledBackup(EditorFrame& frame) {
    Require(frame.NewDocument(), "opens empty Untitled for testing");
    auto* menu = frame.GetMenuBar();
    const auto command = menu->FindMenuItem("File", "Save Backup");
    frame.UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(menu->FindItem(command)->IsEnabled(), "empty Untitled keeps the original command enabled");
    const auto fallback = FallbackDirectory();
    const auto before = MatchingNames(fallback, "Untitled ");
    NativeSearch::Command(&frame, command);
    Require(MatchingNames(fallback, "Untitled ").size() == before.size(), "empty document creates no backup");

    const auto payload = "Untitled sample " + BackupFixtureDirectory().filename().string();
    TestEditor::Append(&frame, payload);
    using enum Scintilla::Message;
    frame.Editor()->Send(SetSel, 3, 1);
    const auto [start, end] = TimestampedBackup(frame, command);
    OwnedBackups owned;
    for (const auto& item : MatchingNames(fallback, "Untitled ")) {
        if (std::ranges::find(before, item) == before.end() && TestFiles::Bytes(item) == payload)
            owned.paths.push_back(item);
    }
    Require(owned.paths.size() == 1, "nonempty Untitled uses fallback with no metadata and untouched editor bytes");
    RequireTimestamp(owned.paths.front(), "Untitled", ".bak", start, end);
    Require(frame.DocumentPath().empty() && frame.DocumentIsModified() && frame.Editor()->Send(CanUndo) &&
                TestEditor::Text(&frame) == payload,
            "backup preserves Untitled identity, edit history and text");
    TestEditor::RequireSelection(frame.Editor(), 3, 1, "backup preserves backwards selection");
    frame.Editor()->Send(Undo);
    Require(!frame.DocumentIsModified() && TestEditor::Text(&frame).empty(),
            "undo still reaches the save point that predates backup");
    Require(frame.NewDocument() && TestFiles::Bytes(owned.paths.front()) == payload,
            "undo and closing the document leave the manual backup available");
}

void ExtensionQuirks(EditorFrame& frame) {
    const auto command = frame.GetMenuBar()->FindMenuItem("File", "Save Backup");
    const auto directory = BackupFixtureDirectory("extension");
    fs::create_directory(directory);
    const auto path = directory / "file.my suffix";
    TestFiles::WriteBytes(path, "space");
    Require(frame.Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "opens original extension-space fixture");
    const auto [start, end] = TimestampedBackup(frame, command);
    const auto backups = MatchingNames(directory, "file.my suffix ");
    Require(backups.size() == 1 && TestFiles::Bytes(backups.front()) == "space",
            "space in extension creates one same-directory backup");
    const auto filename = TestFiles::Filename(backups.front().filename());
    const auto suffix = wxString::Format("%lu.my.my suffix", wxGetProcessId());
    Require(filename.EndsWith(suffix), "original extension space retains PID and repeats the extension stem");
    bool matches = false;
    for (auto second = start; second <= end; second += wxTimeSpan::Second()) {
        const auto prefix = "file.my suffix" + second.Format(" %Y%m%d %H%M%S ");
        matches = matches || (filename.StartsWith(prefix) && filename.size() == prefix.size() + 3 + suffix.size() &&
                              filename.Mid(prefix.size(), 3).IsNumber());
    }
    Require(matches, "extension space retains exactly three millisecond digits");

    const auto dotted = directory / "parent.dot";
    fs::create_directory(dotted);
    const auto unique = BackupFixtureDirectory().filename();
    auto name = unique;
    name += "-file.txt";
    const auto nested = dotted / name;
    TestFiles::WriteBytes(nested, "dot in parent");
    Require(frame.Open(TestFiles::Filename(nested), FileEncoding::Utf8).status == OpenStatus::Opened,
            "opens original dotted-parent fixture");
    const auto nestedPath = RequireSourcePath(frame, nested);
    const auto fallback = FallbackDirectory();
    Require(MatchingNames(fallback, unique).empty(), "dotted-parent fixture namespace is unused");
    NativeSearch::Command(&frame, command);
    const OwnedBackups owned{MatchingNames(fallback, unique)};
    Require(owned.paths == std::vector{fallback / name}, "first dot in full path uses bare leaf in fallback");
    Require(TestFiles::Bytes(owned.paths.front()) == "AutoSave for " + nestedPath.utf8_string() + "\r\ndot in parent",
            "dotted-parent fallback includes the original path and CRLF metadata");
    Require(frame.DocumentPath() == nestedPath, "dotted-parent fallback keeps the document path");
}

struct EncodedFixture {
    const char* label;
    FileEncoding encoding;
    std::string_view input;
    std::string_view backup;
    bool bom = false;
};

void RequireEncodedBackup(EditorFrame& frame, const fs::path& source, std::string_view input,
                          std::string_view expected) {
    const auto path = RequireSourcePath(frame, source);
    const auto command = frame.GetMenuBar()->FindMenuItem("File", "Save Backup");
    const auto [start, end] = TimestampedBackup(frame, command);
    const auto backups = MatchingNames(source.parent_path(), "source ");
    Require(backups.size() == 1, "encoded document creates exactly one sibling backup");
    RequireTimestamp(backups.front(), "source", ".bak", start, end);
    if (TestFiles::Bytes(backups.front()) != expected)
        throw std::runtime_error("backup bytes differ from captured Notepad4 output: " +
                                 TestFiles::Filename(source.parent_path().filename()).utf8_string());
    Require(TestFiles::Bytes(source) == input && frame.DocumentPath() == path,
            "encoded backup keeps the current document and input file");
}

void BackupWithPendingRedo(EditorFrame& frame, const SettingsStore& settings) {
    using enum Scintilla::Message;
    const auto directory = BackupFixtureDirectory("pending-redo");
    fs::create_directory(directory);
    const auto source = directory / "source";
    constexpr auto input = "中é😀\r\nA\0B\rC\nD"sv;
    TestFiles::WriteBytes(source, input);
    Require(frame.Open(TestFiles::Filename(source), FileEncoding::Utf8).status == OpenStatus::Opened,
            "opens original dirty UTF-8 fixture");
    auto* editor = frame.Editor();
    for (const char* text : {"!", "?"}) {
        editor->Send(BeginUndoAction);
        editor->Send(AddText, 1, reinterpret_cast<intptr_t>(text));
        editor->Send(EndUndoAction);
    }
    editor->Send(Undo);
    editor->Send(SetSel, 4, 1);
    const auto recent = settings.values.recentFiles;
    RequireEncodedBackup(frame, source, input, "!中é😀\r\nA\0B\rC\nD"sv);
    Require(settings.values.recentFiles == recent && frame.DocumentIsModified() && editor->Send(CanUndo) &&
                editor->Send(CanRedo),
            "dirty backup preserves recent files, modified state, undo and pending redo");
    TestEditor::RequireSelection(editor, 4, 1, "dirty backup preserves original backwards selection");
    editor->Send(Redo);
    Require(TestEditor::Text(editor) == "!?中é😀\r\nA\0B\rC\nD"sv, "original Redo remains callable after backup");
    editor->Send(Undo);
    editor->Send(Undo);
    Require(!frame.DocumentIsModified() && TestEditor::Text(editor) == input, "Undo reaches the original save point");
}

void BackupCollisions(EditorFrame& frame, bool collisionAtFallback) {
    const auto directory = BackupFixtureDirectory(collisionAtFallback ? "full-collision" : "sibling-collision");
    fs::create_directory(directory);
    auto prefix = BackupFixtureDirectory().filename();
    prefix += collisionAtFallback ? "-full" : "-sibling";
    auto name = prefix;
    name += ".txt";
    const auto source = directory / name;
    TestFiles::WriteBytes(source, "one🙂\ntwo");
    Require(frame.Open(TestFiles::Filename(source), FileEncoding::Utf8).status == OpenStatus::Opened,
            "opens original collision fixture");
    const auto path = RequireSourcePath(frame, source);
    const auto fallback = FallbackDirectory();
    Require(MatchingNames(fallback, prefix).empty(), "collision fixture namespace is unused");
    fs::create_directories(fallback);
    const auto start = BackupTimestamp();
    std::vector<fs::path> existing;
    OwnedBackups owned;
    for (int offset = -1; offset <= 4; ++offset) {
        const auto time = start + wxTimeSpan::Seconds(offset);
        const auto stamp = time.Format(" %Y%m%d %H%M%S.txt").utf8_string();
        const auto backupName = fs::path(name).concat(stamp);
        auto sibling = directory / backupName;
        TestFiles::WriteBytes(sibling, "existing same-directory backup");
        existing.push_back(std::move(sibling));
        if (collisionAtFallback) {
            const auto backup = fallback / backupName;
            TestFiles::WriteBytes(backup, "existing fallback backup");
            owned.paths.push_back(backup);
        }
    }
    const auto command = frame.GetMenuBar()->FindMenuItem("File", "Save Backup");
    NativeSearch::Command(&frame, command);
    const auto end = BackupTimestamp();
    Require(end <= start + wxTimeSpan::Seconds(4), "collision fixture spans the command's local timestamp");
    const auto observed = MatchingNames(fallback, prefix);
    Require(std::ranges::all_of(
                existing, [](const auto& item) { return TestFiles::Bytes(item) == "existing same-directory backup"; }),
            "backup keeps existing siblings unchanged");
    if (collisionAtFallback) {
        const auto existingFallbackCount = owned.paths.size();
        owned.paths = observed;
        Require(observed.size() == existingFallbackCount, "fallback collision creates no additional files");
        Require(std::ranges::all_of(
                    observed, [](const auto& item) { return TestFiles::Bytes(item) == "existing fallback backup"; }),
                "fallback collision preserves every existing backup");
    } else {
        owned.paths = observed;
        Require(owned.paths.size() == 1, "sibling collision falls back without overwriting");
        RequireTimestamp(owned.paths.front(), TestFiles::Filename(name), ".txt", start, end);
        Require(TestFiles::Bytes(owned.paths.front()) == "AutoSave for " + path.utf8_string() + "\none🙂\ntwo",
                "sibling collision fallback matches original payload and metadata");
    }
    Require(frame.DocumentPath() == path && !frame.DocumentIsModified() && TestFiles::Bytes(source) == "one🙂\ntwo",
            "collision leaves original bytes and live state untouched");
}

void BackupKeepsWatcher(EditorFrame& frame) {
    const auto directory = BackupFixtureDirectory("watcher");
    fs::create_directory(directory);
    const auto source = directory / "source";
    TestFiles::WriteBytes(source, "watched\n");
    frame.SetFileWatchingOptions({.mode = FileWatchingMode::AutoReload});
    Require(frame.Open(TestFiles::Filename(source), FileEncoding::Utf8).status == OpenStatus::Opened,
            "opens watched fixture");
    RequireEncodedBackup(frame, source, "watched\n", "watched\n");

    const auto backups = MatchingNames(directory, "source ");
    TestFiles::WriteBytes(backups.front(), "backup file changed externally\n");
    wxYield();
    Require(TestEditor::Text(&frame) == "watched\n", "writing a backup does not reload it as the live document");
    TestFiles::WriteBytes(source, "registered source changed externally\n");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::chrono::steady_clock::now() < deadline &&
           TestEditor::Text(&frame) != "registered source changed externally\n") {
        wxYield();
        wxMilliSleep(10);
    }
    Require(TestEditor::Text(&frame) == "registered source changed externally\n",
            "current source remains watched after manual backup");
    frame.SetFileWatchingOptions({.mode = FileWatchingMode::None});
}

#ifndef __WXMSW__
class ShortWriteLimit {
public:
    ShortWriteLimit() {
        Require(getrlimit(RLIMIT_FSIZE, &original_) == 0, "query original test process file size limit");
        previousSignal_ = std::signal(SIGXFSZ, SIG_IGN);
        Require(previousSignal_ != SIG_ERR, "configure isolated write failure signal");
        const rlimit limit{1, original_.rlim_max};
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
            std::signal(SIGXFSZ, previousSignal_);
            throw std::runtime_error("configure one-byte write limit for owned test process");
        }
    }

    ~ShortWriteLimit() {
        setrlimit(RLIMIT_FSIZE, &original_);
        std::signal(SIGXFSZ, previousSignal_);
    }
    ShortWriteLimit(const ShortWriteLimit&) = delete;
    ShortWriteLimit& operator=(const ShortWriteLimit&) = delete;

private:
    rlimit original_{};
    void (*previousSignal_)(int) = SIG_DFL;
};

void BackupWriteFailure(EditorFrame& frame) {
    const auto directory = BackupFixtureDirectory("short-write");
    fs::create_directory(directory);
    const auto source = directory / "source";
    constexpr auto input = "original source file\n"sv;
    TestFiles::WriteBytes(source, input);
    Require(frame.Open(TestFiles::Filename(source), FileEncoding::Utf8).status == OpenStatus::Opened,
            "opens write failure fixture");
    const auto path = RequireSourcePath(frame, source);
    const auto fallback = FallbackDirectory();
    Require(MatchingNames(fallback, "source ").empty(), "write failure fixture starts without a fallback");
    const auto command = frame.GetMenuBar()->FindMenuItem("File", "Save Backup");
    frame.UpdateWindowUI(wxUPDATE_UI_RECURSE);
    {
        const ShortWriteLimit limit;
        NativeSearch::ActivateCommand(&frame, command);
    }
    wxYield();
    Require(MatchingNames(directory, "source ").empty(), "partial backup bytes are deleted after a real short write");
    Require(MatchingNames(fallback, "source ").empty(), "successful creation followed by write failure does not retry");
    Require(TestFiles::Bytes(source) == input && TestEditor::Text(&frame) == input && !frame.DocumentIsModified() &&
                frame.DocumentPath() == path,
            "failed backup keeps the original file and live document intact");
}
#endif

void EncodingBackups(EditorFrame& frame) {
    using enum FileEncoding;
    constexpr auto unicode = "中é😀\r\nA\0B\rC\nD"sv;
    constexpr std::array fixtures{
        EncodedFixture{"GBK", Gbk, "\xd6\xd0\n\xb9\xfa"sv, "中\n国"sv},
        EncodedFixture{"GB18030", Gb18030, "\xd6\xd0\x94\x39\xfc\x36\n\xd7\xd6"sv, "中😀\n字"sv},
        EncodedFixture{"Big5", Big5, "\xba\x7e\n\xa6\x72"sv, "漢\n字"sv},
        EncodedFixture{"CP1252", Windows1252, "caf\xe9\r\nlast"sv, "café\r\nlast"sv},
        EncodedFixture{"DOS437", Dos437, "caf\x82\nline"sv, "café\nline"sv},
        EncodedFixture{"UTF16LE", Utf16Le, "\xff\xfe\x2d\x4e\xe9\0\x3d\xd8\0\xde\r\0\n\0A\0\0\0B\0\r\0C\0\n\0D\0"sv,
                       unicode, true},
        EncodedFixture{"UTF16BE", Utf16Be, "\xfe\xff\x4e\x2d\0\xe9\xd8\x3d\xde\0\0\r\0\n\0A\0\0\0B\0\r\0C\0\n\0D"sv,
                       unicode, true},
        EncodedFixture{"UTF16LE-no-BOM", Utf16Le, "\x2d\x4e\xe9\0\x3d\xd8\0\xde\r\0\n\0A\0\0\0B\0\r\0C\0\n\0D\0"sv,
                       unicode},
        EncodedFixture{"UTF16BE-no-BOM", Utf16Be, "\x4e\x2d\0\xe9\xd8\x3d\xde\0\0\r\0\n\0A\0\0\0B\0\r\0C\0\n\0D"sv,
                       unicode},
    };
    for (const auto& fixture : fixtures) {
        const auto directory = BackupFixtureDirectory(fixture.label);
        fs::create_directory(directory);
        const auto source = directory / "source";
        TestFiles::WriteBytes(source, fixture.input);
        Require(frame.Open(TestFiles::Filename(source), fixture.encoding).status == OpenStatus::Opened &&
                    frame.DocumentHasBom() == fixture.bom,
                "opens the captured encoding and BOM fixture");
        RequireEncodedBackup(frame, source, fixture.input, fixture.backup);
        Require(frame.DocumentEncoding() == fixture.encoding && frame.DocumentHasBom() == fixture.bom &&
                    !frame.DocumentIsModified(),
                "backup preserves current encoding, BOM and save point");
    }
}

#ifdef __WXMSW__
constexpr unsigned CapturedAnsiCodePage = 936;

void NativeAnsiFallback(EditorFrame& frame, EncodingChoice native) {
    const auto directory = BackupFixtureDirectory("ansi-fallback");
    fs::create_directory(directory);
    const auto prefix = BackupFixtureDirectory().filename();
    auto name = prefix;
    name += TestFiles::Utf8Path("-é中🙂.txt");
    const auto source = directory / name;
    constexpr auto input = "one\xa8\xa6\r\ntwo"sv;
    TestFiles::WriteBytes(source, input);
    Require(frame.OpenRestartDocument(TestFiles::Filename(source), native, std::nullopt, false, 0, 0).status ==
                OpenStatus::Opened,
            "opens original native ANSI fallback fixture");
    const auto path = RequireSourcePath(frame, source);
    const auto modified = frame.DocumentIsModified();
    fs::rename(directory, BackupFixtureDirectory("ansi-fallback-moved"));
    const auto fallback = FallbackDirectory();
    Require(MatchingNames(fallback, prefix).empty(), "ANSI fallback fixture namespace is unused");
    const auto command = frame.GetMenuBar()->FindMenuItem("File", "Save Backup");
    const auto [start, end] = TimestampedBackup(frame, command);
    const OwnedBackups owned{MatchingNames(fallback, prefix)};
    Require(owned.paths.size() == 1, "native ANSI document uses fallback when its directory disappears");
    RequireTimestamp(owned.paths.front(), TestFiles::Filename(name), ".txt", start, end);
    const auto bytes = TestFiles::Bytes(owned.paths.front());
    Require(bytes.starts_with("AutoSave for ") && bytes.ends_with("-\xa8\xa6\xd6\xd0??.txt\r\none\xa8\xa6\r\ntwo"),
            "CP936 fallback metadata and body match the original, including path substitutions and CRLF");
    Require(frame.DocumentIsModified() == modified && frame.DocumentPath() == path,
            "ANSI fallback preserves modified state and source identity");
}

void NativeEncodingBackups(EditorFrame& frame) {
    constexpr auto cp936 = "a\xa8\xa6\xd6\xd0\nend"sv;
    constexpr auto unicode = "aé中\nend"sv;
    for (const bool oem : {false, true}) {
        const auto native = oem ? NativeOemEncodingChoice() : NativeEncodingChoice();
        if (!native) continue;
        const auto codePage = oem ? GetOEMCP() : GetACP();
        const auto input = codePage == CapturedAnsiCodePage ? cp936 : "native identity\nend"sv;
        const auto directory = BackupFixtureDirectory(oem ? "native-oem" : "native-ansi");
        fs::create_directory(directory);
        const auto source = directory / "source";
        TestFiles::WriteBytes(source, input);
        Require(frame.OpenRestartDocument(TestFiles::Filename(source), native, std::nullopt, false, 0, 0).status ==
                    OpenStatus::Opened,
                "opens the native ANSI or OEM encoding fixture");
        const auto expected = oem && codePage == CapturedAnsiCodePage ? unicode : input;
        RequireEncodedBackup(frame, source, input, expected);
        if (!oem) {
            auto* menu = frame.GetMenuBar();
            const auto ansi = menu->FindMenuItem("File", "ANSI");
            Require(ansi != wxNOT_FOUND && menu->IsChecked(ansi), "backup retains native ANSI identity");
        }
    }
    if (const auto native = NativeEncodingChoice(); native && GetACP() == CapturedAnsiCodePage)
        NativeAnsiFallback(frame, *native);
}
#endif

class SaveBackupApp final : public wxApp {
public:
    bool OnInit() override {
#ifndef __WXMSW__
        wxSetEnv("XDG_DATA_HOME", TestFiles::Filename(BackupFixtureDirectory("xdg")));
#endif
        const auto ini = BackupFixtureDirectory("Notepad4.ini");
        constexpr auto autosaveSettings = "[Settings]\nAutoSaveOption=15\nAutoSavePeriod=120000\n";
        TestFiles::WriteBytes(ini, autosaveSettings);
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(ini));
        settings_->values.warnLineEndings = false;
        settings_->values.fixLineEndings = settings_->values.fixTrailingBlanks = true;
        settings_->values.fileWatching.mode = FileWatchingMode::None;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame, ini] {
            try {
                NamedBackup(*frame);
                NamedFallback(*frame, {"fallback-lf", FileEncoding::Windows1252, "caf\xe9\nlast", "\ncafé\nlast"});
                NamedFallback(*frame, {"fallback-cr", FileEncoding::Utf8, "one🙂\rtwo", "\rone🙂\rtwo"});
                UntitledBackup(*frame);
                ExtensionQuirks(*frame);
                EncodingBackups(*frame);
                BackupWithPendingRedo(*frame, *settings_);
                BackupCollisions(*frame, false);
                BackupCollisions(*frame, true);
                BackupKeepsWatcher(*frame);
#ifdef __WXMSW__
                NativeEncodingBackups(*frame);
#else
                BackupWriteFailure(*frame);
#endif
                Require(TestFiles::Bytes(ini) == autosaveSettings, "manual Save Backup preserves autosave settings");
                std::puts("PASS: native Save Backup bytes, naming, collision policy and document state");
                result_ = 0;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace

wxIMPLEMENT_APP(SaveBackupApp);
