#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <cstdio>
#include <chrono>
#include <memory>
#include <stdexcept>
#ifdef __WXMSW__
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace {
using Scintilla::Message;
constexpr long long TimestampToleranceSeconds = 2;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool Enabled(EditorFrame* frame, int id) {
    wxUpdateUIEvent event(id);
    frame->GetEventHandler()->ProcessEvent(event);
    return event.GetEnabled();
}
void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(event);
}
#ifdef __WXMSW__
constexpr long long FileTimeTicksPerSecond = 10'000'000;
constexpr long long WindowsToUnixEpochSeconds = 11'644'473'600;
struct Times {
    long long creation;
    long long access;
    long long modified;
    long long change;
    bool operator==(const Times&) const = default;
};
Times ReadTimes(const std::filesystem::path& path) {
    const auto file =
        CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    FILE_BASIC_INFO info{};
    Require(file != INVALID_HANDLE_VALUE, "metadata fixture must open");
    const bool read = GetFileInformationByHandleEx(file, FileBasicInfo, &info, sizeof(info)) != 0;
    CloseHandle(file);
    Require(read, "metadata must be independently readable");
    return {info.CreationTime.QuadPart, info.LastAccessTime.QuadPart, info.LastWriteTime.QuadPart,
            info.ChangeTime.QuadPart};
}
void SetOldTimes(const std::filesystem::path& path) {
    const auto file =
        CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(file != INVALID_HANDLE_VALUE, "fixture times must be writable");
    FILE_BASIC_INFO info{};
    info.CreationTime.QuadPart = 132537600000000000LL;
    info.LastAccessTime.QuadPart = 132538464000000000LL;
    info.LastWriteTime.QuadPart = 132539328000000000LL;
    info.ChangeTime.QuadPart = 132540192000000000LL;
    const bool set = SetFileInformationByHandle(file, FileBasicInfo, &info, sizeof(info)) != 0;
    CloseHandle(file);
    Require(set, "fixture timestamps must be settable");
}
long long ModifiedSeconds(const Times& times) {
    return times.modified / FileTimeTicksPerSecond - WindowsToUnixEpochSeconds;
}
bool WritableTimesEqual(const Times& before, const Times& after) {
    return before == after;
}
bool InitialTimesEqual(const Times& times) {
    return times.creation == times.access && times.access == times.modified && times.modified == times.change;
}
void LogTimes(const char* label, const Times& times) {
    std::printf("%s creation=%lld access=%lld modified=%lld change=%lld (100ns since Windows epoch)\n", label,
                times.creation, times.access, times.modified, times.change);
}
bool OtherTimesEqual(const Times& before, const Times& after) {
    return before.creation == after.creation && before.access == after.access && before.change == after.change;
}
#else
constexpr long long NanosecondsPerSecond = std::nano::den;
struct Times {
    long long access;
    long long modified;
    long long change;
};
Times ReadTimes(const std::filesystem::path& path) {
    struct stat info{};
    Require(::stat(path.c_str(), &info) == 0, "metadata must be independently readable");
    return {info.st_atim.tv_sec * NanosecondsPerSecond + info.st_atim.tv_nsec,
            info.st_mtim.tv_sec * NanosecondsPerSecond + info.st_mtim.tv_nsec,
            info.st_ctim.tv_sec * NanosecondsPerSecond + info.st_ctim.tv_nsec};
}
void SetOldTimes(const std::filesystem::path& path) {
    const timespec times[]{{1609459200, 123400000}, {1609545600, 567800000}};
    Require(::utimensat(AT_FDCWD, path.c_str(), times, 0) == 0, "fixture timestamps must be settable");
}
long long ModifiedSeconds(const Times& times) {
    return times.modified / NanosecondsPerSecond;
}
bool WritableTimesEqual(const Times& before, const Times& after) {
    return before.access == after.access && before.modified == after.modified;
}
bool InitialTimesEqual(const Times& times) {
    return times.access == times.modified;
}
void LogTimes(const char* label, const Times& times) {
    std::printf("%s access=%lld modified=%lld ctime=%lld (ns since Unix epoch)\n", label, times.access, times.modified,
                times.change);
}
bool OtherTimesEqual(const Times& before, const Times& after) {
    return before.access == after.access;
}
#endif
long long NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
void PreserveTimes(EditorFrame* frame, const std::filesystem::path& path, int save) {
    SetOldTimes(path);
    const auto before = ReadTimes(path);
    TestEditor::Append(frame, " dirty");
    Command(frame, save);
    const auto after = ReadTimes(path);
    LogTimes("Original-time save before", before);
    LogTimes("Original-time save after", after);
    Require(WritableTimesEqual(before, after),
            "original-time save must preserve all filesystem-representable writable times");
    Require(TestFiles::Bytes(path) == "backing dirty" && !frame->Editor()->Send(Message::GetModify),
            "original-time command must write content and establish normal save point");
    frame->Editor()->Send(Message::Undo);
    Require(frame->Editor()->Send(Message::GetModify), "Undo across original-time save must be dirty");
    frame->Editor()->Send(Message::Redo);
    Require(!frame->Editor()->Send(Message::GetModify), "Redo to original-time save must be clean");
    std::printf("PASS: original-time content save preserves timestamps, mtime=%lld\n", ModifiedSeconds(after));
}
void UpdateDirty(EditorFrame* frame, const std::filesystem::path& path, int update) {
    auto* editor = frame->Editor();
    TestEditor::Append(frame, " unsaved  \n");
    editor->Send(Message::SetSel, 8, 2);
    const auto text = TestEditor::Text(frame);
    const auto identity = frame->DocumentPath();
    const auto encoding = frame->DocumentEncoding();
    const auto actions = editor->Send(Message::GetUndoActions);
    SetOldTimes(path);
    const auto before = ReadTimes(path);
    const auto earliest = NowSeconds();
    Command(frame, update);
    const auto after = ReadTimes(path);
    LogTimes("Timestamp update before", before);
    LogTimes("Timestamp update after", after);
    Require(ModifiedSeconds(after) >= earliest - TimestampToleranceSeconds &&
                ModifiedSeconds(after) <= NowSeconds() + TimestampToleranceSeconds && OtherTimesEqual(before, after),
            "timestamp update must set current mtime and retain other writable times");
    Require(TestFiles::Bytes(path) == "backing dirty" && TestEditor::Text(frame) == text &&
                frame->DocumentPath() == identity && frame->DocumentEncoding() == encoding &&
                editor->Send(Message::GetUndoActions) == actions && editor->Send(Message::GetModify),
            "timestamp-only dirty document must keep independent backing bytes, content, history and identity");
    TestEditor::RequireSelection(editor, 8, 2, "timestamp-only action must retain reversed selection");
    editor->Send(Message::Undo);
    Require(!editor->Send(Message::GetModify), "timestamp-only action must retain previous save point");
    editor->Send(Message::Redo);
    Require(editor->Send(Message::GetModify) && TestEditor::Text(frame) == text,
            "timestamp-only action must retain pending Redo text");
    editor->Send(Message::Undo);
    std::printf("PASS: timestamp-only dirty update preserves disk bytes/editor state, mtime=%lld\n",
                ModifiedSeconds(after));
}
void SaveDestinations(EditorFrame* frame) {
    Require(frame->NewDocument(), "clean document must allow New");
    TestEditor::Append(frame, "new  \n");
    const auto directory = TestFiles::TemporaryDirectory();
    const auto created = directory / "created.txt";
    const auto earliest = NowSeconds();
    Require(
        frame->SaveFirst(TestFiles::Filename(created), {}, SaveType::CurrentScheme, SaveTimeMode::Original).status ==
            SaveStatus::Saved,
        "original-time first save must succeed");
    const auto newTimes = ReadTimes(created);
    LogTimes("New original-time destination", newTimes);
    Require(InitialTimesEqual(newTimes) && ModifiedSeconds(newTimes) >= earliest - TimestampToleranceSeconds &&
                ModifiedSeconds(newTimes) <= NowSeconds() + TimestampToleranceSeconds &&
                TestFiles::Bytes(created) == "new\n" && !frame->Editor()->Send(Message::GetModify),
            "new destination must keep newly created time and normal cleanup/save point");
    const auto existing = directory / "existing-as.txt";
    TestFiles::WriteBytes(existing, "old destination");
    SetOldTimes(existing);
    const auto before = ReadTimes(existing);
    Require(frame->SaveAs(TestFiles::Filename(existing), {}, SaveType::CurrentScheme, SaveTimeMode::Original).status ==
                    SaveStatus::Saved &&
                WritableTimesEqual(before, ReadTimes(existing)) && TestFiles::Bytes(existing) == "new\n" &&
                frame->DocumentPath() == TestFiles::Filename(existing),
            "original-time Save As must preserve destination times and adopt identity");
    TestEditor::Append(frame, "pending");
    const auto identity = frame->DocumentPath();
    Require(frame->SaveAs(std::nullopt, {}, SaveType::CurrentScheme, SaveTimeMode::Original).status ==
                    SaveStatus::Cancelled &&
                frame->SaveAs(TestFiles::Filename(directory / "missing" / "fail.txt"), {}, SaveType::CurrentScheme,
                              SaveTimeMode::Original)
                        .status == SaveStatus::Failed &&
                frame->DocumentPath() == identity && frame->Editor()->Send(Message::GetModify) &&
                TestFiles::Bytes(existing) == "new\n",
            "original-time cancelled/failed Save As must retain session and backing bytes");
    frame->Editor()->Send(Message::Undo);
    Require(!frame->Editor()->Send(Message::GetModify), "failed original-time save must retain save point");
    std::puts("PASS: original-time first-save cleanup, existing Save As times, cancellation and recoverable failure");
}
void LegacyLoss(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory() / "legacy.txt";
    TestFiles::WriteBytes(path, "legacy");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
            "legacy fixture must open");
    TestEditor::Append(frame, "😀  \n");
    SetOldTimes(path);
    const auto before = ReadTimes(path);
    bool cleaned = false;
    const auto cancelled = frame->Save(
        [frame, &cleaned] {
            cleaned = TestEditor::Text(frame) == "legacy😀\n";
            return GbkLossChoice::Cancel;
        },
        SaveTimeMode::Original);
    Require(cancelled.status == SaveStatus::Cancelled && cleaned && WritableTimesEqual(before, ReadTimes(path)) &&
                TestFiles::Bytes(path) == "legacy" && frame->Editor()->Send(Message::GetModify),
            "original-time legacy cancellation must retain disk/times after normal undoable cleanup");
    SetOldTimes(path);
    Require(frame->Save([] { return GbkLossChoice::Replace; }, SaveTimeMode::Original).status == SaveStatus::Saved &&
                WritableTimesEqual(before, ReadTimes(path)) && TestFiles::Bytes(path) == "legacy??\n" &&
                TestEditor::Text(frame) == "legacy😀\n" && !frame->Editor()->Send(Message::GetModify),
            "original-time legacy replacement must preserve times and establish normal save point");
    std::puts("PASS: original-time save shares cleanup and legacy-loss decisions");
}
void UpdateFailures(EditorFrame* frame, int update) {
    const auto directory = TestFiles::TemporaryDirectory() / "removed-parent";
    std::filesystem::create_directory(directory);
    const auto path = directory / "source.txt";
    TestFiles::WriteBytes(path, "disk");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "failure fixture must open");
    TestEditor::Append(frame, " unsaved  \n");
    const auto text = TestEditor::Text(frame);
#ifndef __WXMSW__
    struct RestoreUmask {
        RestoreUmask() = default;
        RestoreUmask(const RestoreUmask&) = delete;
        RestoreUmask& operator=(const RestoreUmask&) = delete;
        RestoreUmask(RestoreUmask&&) = delete;
        RestoreUmask& operator=(RestoreUmask&&) = delete;
        mode_t previous = ::umask(0);
        ~RestoreUmask() { ::umask(previous); }
    } restoreUmask;
    Require(::chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP) == 0, "fixture must have a known existing mode");
    Command(frame, update);
    struct stat existing{};
    Require(::stat(path.c_str(), &existing) == 0 && (existing.st_mode & 0777) == 0640,
            "timestamp-only update must retain an existing file's permissions under a permissive umask");
#endif
#ifdef __WXMSW__
    Require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_READONLY) != 0, "fixture must become read only");
    const bool updatedReadOnly = frame->UpdateFileTime();
    Command(frame, update);
    Require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL) != 0, "fixture must restore writable attribute");
    Require(!updatedReadOnly && TestFiles::Bytes(path) == "disk" && TestEditor::Text(frame) == text &&
                frame->Editor()->Send(Message::GetModify),
            "read-only timestamp-only failure must be silent and retain live/disk state");
    std::puts("PASS: Windows read-only timestamp-only failure is silent and retains live/disk state");
#endif
    std::filesystem::remove(path);
    Require(frame->UpdateFileTime() && TestFiles::Bytes(path).empty() && TestEditor::Text(frame) == text &&
                frame->Editor()->Send(Message::GetModify),
            "timestamp-only command must match original OPEN_ALWAYS for missing backing file");
#ifndef __WXMSW__
    struct stat created{};
    Require(::stat(path.c_str(), &created) == 0, "recreated file metadata must be independently readable");
    std::printf("Timestamp-only recreated file permissions=%03o under umask 000\n",
                static_cast<unsigned>(created.st_mode & 0777));
    Require((created.st_mode & 0777) == 0600,
            "timestamp-only recreation must grant only owner read/write under a permissive umask");
#endif
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    Require(!frame->UpdateFileTime(), "timestamp-only missing-parent update must fail");
    Command(frame, update);
    Require(TestEditor::Text(frame) == text && frame->Editor()->Send(Message::GetModify) &&
                frame->DocumentPath() == TestFiles::Filename(path) && !std::filesystem::exists(directory),
            "timestamp-only failure command must silently retain live state without creating parents");
    std::puts("PASS: timestamp-only missing-file recreation and silent failure retain live state");
}
class FileTimesApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        settings_->values.fixTrailingBlanks = true;
        auto* frame = new EditorFrame(settings_.get());
        CallAfter([this, frame] {
            try {
                const auto save = frame->GetMenuBar()->FindMenuItem("File", "Save with Original Time");
                const auto update = frame->GetMenuBar()->FindMenuItem("File", "Update File Time");
                Require(save != wxNOT_FOUND && update != wxNOT_FOUND, "file-time commands must exist");
                Require(!Enabled(frame, save) && !Enabled(frame, update), "clean untitled time commands disabled");
                TestEditor::Append(frame, "pending");
                Require(Enabled(frame, save) && !Enabled(frame, update), "dirty untitled enables original-time save");
                frame->Editor()->Send(Message::Undo);
                const auto path = TestFiles::TemporaryDirectory() / "times.txt";
                TestFiles::WriteBytes(path, "backing");
                Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "fixture must open");
                Require(!Enabled(frame, save) && Enabled(frame, update), "named clean enables timestamp update");
                PreserveTimes(frame, path, save);
                UpdateDirty(frame, path, update);
                SaveDestinations(frame);
                LegacyLoss(frame);
                UpdateFailures(frame, update);
                std::puts("PASS: file-time menu enablement and shared save action");
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
    int OnExit() override { return result_; }

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(FileTimesApp);
