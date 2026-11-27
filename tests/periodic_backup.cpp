#include "native_key_helpers.h"
#include "diagnostics/print.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "file_command_driver.h"
#include "settings/settings_store.h"
#include "steady_stopwatch.h"
#include <wx/app.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dialog.h>
#include <wx/modalhook.h>
#include <wx/textctrl.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <exception>
#include <print>
#include <functional>
#include <algorithm>
#include <vector>
#include <stdexcept>
#ifdef __WXMSW__
#include <shlobj.h>
#endif

namespace {
namespace fs = std::filesystem;
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Click(wxWindow* control) {
    Require(control, "native dialog control exists");
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(control->GetHandle()), BM_CLICK, 0, 0);
#else
    gtk_button_clicked(GTK_BUTTON(control->GetHandle()));
#endif
}
class DialogDriver final : public wxModalDialogHook {
public:
    explicit DialogDriver(std::function<void(wxDialog*)> action) : action_(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "AutoSave Settings")
            wxTheApp->CallAfter([this, dialog] {
                NativeKeys::AfterPresentation(dialog, [this, dialog](bool presented) {
                    try {
                        Require(presented, "native AutoSave dialog is presented");
                        opened = true;
                        action_(dialog);
                    } catch (...) {
                        failure = std::current_exception();
                        dialog->EndModal(wxID_CANCEL);
                    }
                });
            });
        return wxID_NONE;
    }
    bool opened = false;
    std::exception_ptr failure;

private:
    std::function<void(wxDialog*)> action_;
};
void Settings(EditorFrame& frame, const std::function<void(wxDialog*)>& action) {
    const int command = frame.GetMenuBar()->FindMenuItem("Settings", "AutoSave Settings...");
    Require(command != wxNOT_FOUND, "Settings offers AutoSave Settings");
    DialogDriver driver(action);
    NativeSearch::Command(&frame, command);
    if (driver.failure) std::rethrow_exception(driver.failure);
    Require(driver.opened, "AutoSave settings opens natively");
}
wxCheckBox* Check(wxDialog* dialog, const char* name) {
    auto* control = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxCheckBox);
    Require(control, "native backup preference exists");
    return control;
}
wxTextCtrl* Period(wxDialog* dialog) {
    auto* control = wxDynamicCast(wxWindow::FindWindowByName("AutoSave period", dialog), wxTextCtrl);
    Require(control, "native seconds field exists");
    return control;
}
void InitialSettings(EditorFrame& frame, SettingsStore& store, const fs::path& ini) {
    Settings(frame, [](wxDialog* dialog) {
        Require(!Check(dialog, "Periodic backup")->GetValue(), "periodic backups default off");
        Require(Period(dialog)->GetValue() == "5.0", "original period defaults to five seconds");
        Click(Check(dialog, "Periodic backup"));
        Period(dialog)->SetValue("0.125");
        Click(dialog->FindWindow(wxID_CANCEL));
    });
    Settings(frame, [](wxDialog* dialog) {
        Require(!Check(dialog, "Periodic backup")->GetValue() && Period(dialog)->GetValue() == "5.0",
                "Cancel keeps live preferences");
        Click(Check(dialog, "Periodic backup"));
        Period(dialog)->SetValue("0.125");
        Click(dialog->FindWindow(wxID_OK));
    });
    Require(store.Save(true), "settings save through normal lifecycle");
    const auto bytes = TestFiles::Bytes(ini);
    const wxString saved(bytes.data(), wxMBConvUTF16LE(), bytes.size());
    Require(saved.Contains("AutoSaveOption=23") && saved.Contains("AutoSavePeriod=125"),
            "fractional seconds persist milliseconds and preserve unsupported imported bits");
}
void Pump(long milliseconds) {
    TestClock::StopWatch clock;
    while (clock.Time() < milliseconds) {
        wxTheApp->Yield(true);
        wxMilliSleep(5);
    }
}
template <typename Condition> void Wait(const Condition& condition, const char* message) {
    TestClock::StopWatch clock;
    while (!condition() && clock.Time() < 2000)
        Pump(5);
    Require(condition(), message);
}
std::vector<fs::path> Backups(const fs::path& root) {
    std::vector<fs::path> result;
    for (const auto& item : fs::directory_iterator(root))
        if (item.path().filename().string().starts_with("source.txt ")) result.push_back(item.path());
    std::ranges::sort(result);
    return result;
}
void RealPeriodicTimer(EditorFrame& frame, const fs::path& root) {
    const auto folder = root / "real-timer";
    fs::create_directories(folder);
    const auto source = folder / "source.txt";
    TestFiles::WriteBytes(source, "seed\n");
    Require(frame.Open(TestFiles::Filename(source)).status == OpenStatus::Opened, "opens named periodic fixture");
    Pump(300);
    Require(Backups(folder).empty(), "unchanged document produces no periodic backup");
    TestEditor::Append(&frame, "first");
    Wait([&folder] { return Backups(folder).size() == 1; }, "first edit starts actual periodic backup timer");
    const auto first = Backups(folder).front();
    Require(TestFiles::Bytes(first) == "seed\nfirst", "periodic backup writes complete live bytes");
    const auto name = TestFiles::Filename(first.filename());
    const auto pid = wxString::Format("%lu.txt", wxGetProcessId());
    Require(name.StartsWith("source.txt ") && name.EndsWith(pid) &&
                name.length() == 11 + 8 + 1 + 6 + 1 + 3 + pid.length(),
            "automatic name includes date, time, milliseconds and actual PID");
    Pump(300);
    Require(Backups(folder).size() == 1, "already backed revision creates no additional files");
    TestClock::StopWatch clock;
    while (clock.Time() < 500) {
        TestEditor::Append(&frame, "x");
        Pump(35);
    }
    Require(Backups(folder).size() >= 3, "continuously edited document is backed up during editing, without debounce");
    Require(TestFiles::Bytes(source) == "seed\n" && frame.DocumentPath() == TestFiles::Filename(source) &&
                frame.DocumentIsModified() && frame.Editor()->Send(Message::CanUndo),
            "imported overwrite bit cannot alter original file, live path or save point/history");
    Require(frame.Save().status == SaveStatus::Saved, "ordinary save remains available");
}
void Rotation(EditorFrame& frame, const fs::path& root) {
    const auto folder = root / "rotation";
    fs::create_directories(folder);
    const auto source = folder / "source.txt";
    TestFiles::WriteBytes(source, "seed\n");
    Require(frame.Open(TestFiles::Filename(source)).status == OpenStatus::Opened, "opens rotation fixture");
    std::string expected = "seed\n";
    for (char ch = '1'; ch <= '7'; ++ch) {
        expected += ch;
        TestEditor::Append(&frame, std::string(1, ch));
        Wait(
            [&folder, &expected] {
                for (const auto& path : Backups(folder))
                    if (TestFiles::Bytes(path) == expected) return true;
                return false;
            },
            "each distinct revision produces a complete automatic backup");
    }
    Require(Backups(folder).size() == 6, "seventh periodic backup rotates the oldest of six managed entries");
    const auto command = frame.GetMenuBar()->FindMenuItem("File", "Save Backup");
    NativeSearch::Command(&frame, command);
    const auto manual = Backups(folder);
    Require(manual.size() == 7, "manual backup is additional to six automatic entries");
    Require(frame.Save().status == SaveStatus::Saved, "ordinary save succeeds after rotation");
    Require(Backups(folder).size() == 1 && TestFiles::Bytes(Backups(folder).front()) == expected,
            "ordinary Save deletes managed entries while preserving manual Save Backup");
}
void SetOptions(EditorFrame& frame, bool enabled, bool retained, const char* seconds = "0.125") {
    Settings(frame, [enabled, retained, seconds](wxDialog* dialog) {
        auto* periodic = Check(dialog, "Periodic backup");
        auto* manual = Check(dialog, "Manually delete backups");
        if (periodic->GetValue() != enabled) Click(periodic);
        if (manual->GetValue() != retained) Click(manual);
        Period(dialog)->SetValue(seconds);
        Click(dialog->FindWindow(wxID_OK));
    });
}
void IntervalReset(EditorFrame& frame, const fs::path& root) {
    const auto folder = root / "interval-reset";
    fs::create_directories(folder);
    const auto source = folder / "source.txt";
    TestFiles::WriteBytes(source, "interval");
    Require(frame.Open(TestFiles::Filename(source)).status == OpenStatus::Opened, "opens interval-reset fixture");
    // Leave room for native dialog presentation and event-loop scheduling
    // while still observing after the old deadline and before the new one.
    constexpr long BeforeChangeWaitMilliseconds = 3500;
    constexpr long AfterChangeWaitMilliseconds = 2000;
    SetOptions(frame, true, false, "5.0");
    TestEditor::Append(&frame, "changed");
    Pump(BeforeChangeWaitMilliseconds);
    Require(Backups(folder).empty(), "original period has not expired");
    TestClock::StopWatch reconfigure;
    SetOptions(frame, true, false, "3.0");
    const auto settingsMilliseconds = reconfigure.Time();
    Pump(AfterChangeWaitMilliseconds);
    if (!Backups(folder).empty())
        std::fprintf(stderr, "interval reset: settings=%ldms, after-settings=%ldms, backups=%zu\n",
                     settingsMilliseconds, reconfigure.Time() - settingsMilliseconds, Backups(folder).size());
    Require(Backups(folder).empty(), "changing interval resets the running timer rather than keeping its old deadline");
    Wait([&folder] { return Backups(folder).size() == 1; }, "changed interval produces a backup at its new deadline");
    SetOptions(frame, true, false, "0");
    TestEditor::Append(&frame, "zero");
    Pump(300);
    Require(Backups(folder).size() == 1, "zero period stops scheduling while retaining tracked files");
    SetOptions(frame, true, false);
    Wait([&folder] { return Backups(folder).size() == 2; }, "nonzero interval restores pending revision eligibility");
    Require(frame.Save().status == SaveStatus::Saved && Backups(folder).empty(),
            "Save cleans entries tracked before and after a zero-period pause");
}
void RetentionAndBoundaries(EditorFrame& frame, SettingsStore& store, const fs::path& root) {
    const auto folder = root / "retention";
    fs::create_directories(folder);
    const auto source = folder / "source.txt";
    TestFiles::WriteBytes(source, "retain");
    Require(frame.Open(TestFiles::Filename(source)).status == OpenStatus::Opened, "opens retention fixture");
    SetOptions(frame, true, true);
    std::string expected = "retain";
    for (size_t index = 0; index < 8; ++index) {
        expected += '!';
        TestEditor::Append(&frame, "!");
        Wait([&folder, index] { return Backups(folder).size() == index + 1; }, "retention keeps rotated files");
    }
    Require(frame.Save().status == SaveStatus::Saved && Backups(folder).size() == 8,
            "manual retention keeps tracked and previously rotated files on Save");
    SetOptions(frame, true, false);
    TestEditor::Append(&frame, "x");
    Wait([&folder] { return Backups(folder).size() == 9; }, "new lifecycle can create a managed backup");
    SetOptions(frame, false, false);
    Require(Backups(folder).size() == 8, "disabling backups cleans only currently managed files");
    TestEditor::Append(&frame, "disabled");
    Pump(300);
    Require(Backups(folder).size() == 8, "disabled periodic backup stays inactive after editing");
    SetOptions(frame, true, false, "0");
    Pump(300);
    Require(Backups(folder).size() == 8, "zero period disables scheduling without an arbitrary minimum");
    for (const auto& [seconds, milliseconds] : std::array<std::pair<const char*, int>, 7>{{{"0.0009", 0},
                                                                                           {"0.001", 1},
                                                                                           {"0.9999", 999},
                                                                                           {"1.2349 seconds", 1234},
                                                                                           {"-0.001", -1},
                                                                                           {"2147483.5", 2147483520},
                                                                                           {"0.125", 125}}}) {
        SetOptions(frame, true, false, seconds);
        Require(store.Save(true), "boundary settings save normally");
        const auto data = TestFiles::Bytes(root / "Notepad4.ini");
        const wxString saved(data.data(), wxMBConvUTF16LE(), data.size());
        Require(saved.Contains(wxString::Format("AutoSavePeriod=%d", milliseconds)),
                "defined float-second boundary preserves original truncation and signed INI bits");
    }
    Wait([&folder] { return Backups(folder).size() == 9; }, "restoring nonzero period restarts live scheduling");
    Settings(frame, [](wxDialog* dialog) {
        Period(dialog)->SetValue("0");
        Click(Check(dialog, "Periodic backup"));
        Click(dialog->FindWindow(wxID_CANCEL));
    });
    TestEditor::Append(&frame, "after-cancel");
    Wait([&folder] { return Backups(folder).size() == 10; }, "Cancel leaves live repeating scheduler enabled");
    Require(frame.Save().status == SaveStatus::Saved && Backups(folder).size() == 8,
            "old manually retained files remain unrelated to later cleanup");
}
void Lifecycle(EditorFrame& frame, const fs::path& root) {
    const auto folder = root / "lifecycle";
    fs::create_directories(folder);
    const auto source = folder / "source.txt";
    TestFiles::WriteBytes(source, "lifecycle");
    Require(frame.Open(TestFiles::Filename(source)).status == OpenStatus::Opened, "opens lifecycle fixture");
    TestEditor::Append(&frame, "one");
    Wait([&folder] { return Backups(folder).size() == 1; }, "lifecycle starts with managed backup");
    const auto first = Backups(folder).front();
    Require(frame.SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                frame.SaveAs(TestFiles::Filename(folder)).status == SaveStatus::Failed &&
                frame.Open(std::nullopt).status == OpenStatus::Cancelled &&
                frame.Open(TestFiles::Filename(folder / "missing.txt")).status == OpenStatus::Failed &&
                fs::exists(first),
            "cancelled or failed file actions retain managed backups");
    TestEditor::Append(&frame, "two");
    Wait([&folder] { return Backups(folder).size() == 2; },
         "failed actions leave scheduler and revision eligibility intact");
    Require(frame.SaveCopy(TestFiles::Filename(folder / "copy.txt")).status == SaveStatus::Saved &&
                Backups(folder).empty() && frame.DocumentIsModified(),
            "successful Save Copy stops and cleans managed backups without changing the live save point");
    TestEditor::Append(&frame, "after-copy");
    Wait([&folder] { return Backups(folder).size() == 1; }, "editing restarts scheduler after Save Copy");
    Require(frame.ReloadCurrentFile().status == OpenStatus::Opened && Backups(folder).empty(),
            "successful same-file reload deletes managed entries");
    TestEditor::Append(&frame, "manual");
    NativeSearch::Command(&frame, frame.GetMenuBar()->FindMenuItem("File", "Save Backup"));
    Pump(300);
    Require(Backups(folder).size() == 1, "successful manual backup suppresses automatic backup of that revision");
    TestEditor::Append(&frame, "next");
    Wait([&folder] { return Backups(folder).size() == 2; }, "next edit becomes eligible after manual backup");
    Require(frame.Save().status == SaveStatus::Saved && Backups(folder).size() == 1,
            "manual backup remains untracked through later ordinary save");
    TestEditor::Append(&frame, "discard");
    Wait([&folder] { return Backups(folder).size() == 2; }, "new managed backup before discard");
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL);
        Require(!frame.NewDocument() && driver.prompts == 1 && Backups(folder).size() == 2,
                "native Cancel preserves document and backups");
    }
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, wxID_NO);
        Require(frame.NewDocument() && driver.prompts == 1 && Backups(folder).size() == 1,
                "explicit native Discard deletes managed entries, preserving manual backup");
    }
    Require(frame.Open(TestFiles::Filename(source)).status == OpenStatus::Opened, "reopens lifecycle fixture");
    TestEditor::Append(&frame, "undo");
    Wait([&folder] { return Backups(folder).size() == 2; }, "undo lifecycle has managed backup");
    frame.Editor()->Send(Message::Undo);
    Require(!frame.DocumentIsModified() && frame.NewDocument() && Backups(folder).size() == 2,
            "New from unmodified revision retains original lifecycle backups");
    Require(frame.Open(TestFiles::Filename(source)).status == OpenStatus::Opened, "reopens before different-file load");
    TestEditor::Append(&frame, "different");
    Wait([&folder] { return Backups(folder).size() == 3; }, "different-file lifecycle has managed backup");
    frame.Editor()->Send(Message::Undo);
    const auto other = folder / "other.txt";
    TestFiles::WriteBytes(other, "other");
    Require(frame.Open(TestFiles::Filename(other)).status == OpenStatus::Opened && Backups(folder).size() == 3,
            "successful different-file open retains prior lifecycle backups");
}
void PeriodicPayloads(EditorFrame& frame, SettingsStore& settings, const fs::path& root) {
    using namespace std::string_view_literals;
    struct Fixture {
        const char* name;
        EncodingChoice encoding;
        std::string_view source;
        std::string expected;
    };
    std::vector<Fixture> fixtures{
        {"utf16", EncodingChoice(FileEncoding::Utf16Le, true), "\xff\xfe\x2d\x4e\r\0\n\0A\0\0\0B\0"sv,
         std::string("中\r\nA\0B"sv)},
        {"explicit-legacy", EncodingChoice(FileEncoding::Windows1252), "caf\xe9\r\nlast"sv, "café\r\nlast"}};
#ifdef __WXMSW__
    constexpr unsigned CapturedAnsiCodePage = 936;
    for (const bool oem : {false, true}) {
        const auto native = oem ? NativeOemEncodingChoice() : NativeEncodingChoice();
        if (!native) continue;
        const auto codePage = oem ? GetOEMCP() : GetACP();
        const auto input = codePage == CapturedAnsiCodePage ? "a\xa8\xa6\xd6\xd0\nend"sv : "native identity\nend"sv;
        fixtures.push_back({oem ? "native-oem" : "native-ansi", *native, input,
                            std::string(oem && codePage == CapturedAnsiCodePage ? "aé中\nend"sv : input)});
    }
#endif
    for (const auto& fixture : fixtures) {
        const auto folder = root / fixture.name;
        fs::create_directories(folder);
        const auto source = folder / "source.txt";
        TestFiles::WriteBytes(source, fixture.source);
        Require(frame.OpenRestartDocument(TestFiles::Filename(source), fixture.encoding, std::nullopt, false, 0, 0)
                        .status == OpenStatus::Opened,
                "opens periodic native/Unicode/legacy payload fixture");
        auto* editor = frame.Editor();
        for (const char* text : {"!", "?"}) {
            editor->Send(Message::BeginUndoAction);
            TestEditor::Append(&frame, text);
            editor->Send(Message::EndUndoAction);
        }
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, 3, 1);
        const auto recent = settings.values.recentFiles;
        const auto folded = editor->Send(Message::GetFoldExpanded, 0);
        Wait([&folder] { return Backups(folder).size() == 1; }, "actual periodic timer saves encoded live revision");
        Require(TestFiles::Bytes(Backups(folder).front()) == fixture.expected + "!" &&
                    TestFiles::Bytes(source) == fixture.source,
                "periodic payload preserves native ANSI versus UTF-8, embedded NUL/EOL and untouched source bytes");
        Require(frame.DocumentEncoding() == fixture.encoding.encoding &&
                    frame.DocumentHasBom() == fixture.encoding.bom && frame.DocumentIsModified() &&
                    editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                    editor->Send(Message::GetFoldExpanded, 0) == folded && settings.values.recentFiles == recent,
                "periodic writer preserves current encoding/BOM, save point, folds, pending redo and Recent Files");
        TestEditor::RequireSelection(editor, 3, 1, "periodic encoded backup preserves backwards selection");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor).ends_with("!?"),
                "pending original redo remains callable after periodic backup");
        Require(frame.Save().status == SaveStatus::Saved && Backups(folder).empty(),
                "encoded ordinary save cleans periodic output");
    }
}
fs::path Fallback(const fs::path& root) {
#ifdef __WXMSW__
    PWSTR local = nullptr;
    Require(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local) == S_OK,
            "host resolves independent Windows backup folder");
    const fs::path directory(local);
    CoTaskMemFree(local);
#else
    const auto directory = root / "xdg";
#endif
    return directory / "Notepad4" / "AutoSave";
}
void UntitledAndFallback(EditorFrame& frame, const fs::path& root) {
    Require(frame.NewDocument(), "creates clean untitled fixture");
    const auto folder = Fallback(root);
    fs::create_directories(folder);
    const auto suffix = wxString::Format("%lu.bak", wxGetProcessId());
    const auto owned = [&folder, &suffix] {
        std::vector<fs::path> result;
        for (const auto& item : fs::directory_iterator(folder)) {
            const auto name = TestFiles::Filename(item.path().filename());
            if (name.StartsWith("Untitled ") && name.EndsWith(suffix)) result.push_back(item.path());
        }
        return result;
    };
    Require(owned().empty(), "untitled fixture owns a unique process suffix");
    Pump(300);
    Require(owned().empty(), "empty untitled document produces no backup");
    const auto payload = "untitled-" + root.filename().string();
    TestEditor::Append(&frame, payload);
    frame.Editor()->Send(Message::SetSel, 5, 1);
    Wait([&owned] { return owned().size() == 1; }, "changed untitled document uses application data backup folder");
    Require(TestFiles::Bytes(owned().front()) == payload && frame.DocumentPath().empty() && frame.DocumentIsModified(),
            "untitled automatic backup preserves literal payload and live identity/save point");
    TestEditor::RequireSelection(frame.Editor(), 5, 1, "periodic backup preserves backwards selection");
    SetOptions(frame, false, false);
    Require(owned().empty(), "disabling periodic removes owned untitled backup");
    frame.Editor()->Send(Message::Undo);
    Require(frame.NewDocument(), "cleans untitled fixture without discarding unrelated text");
    SetOptions(frame, true, false);
}
#ifndef __WXMSW__
class BackupDirectoryWriteBlock final {
public:
    explicit BackupDirectoryWriteBlock(fs::path directory)
        : directory_(std::move(directory)), original_(fs::status(directory_).permissions()) {
        constexpr auto WritePermissions = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
        fs::permissions(directory_, original_ & ~WritePermissions);
    }
    ~BackupDirectoryWriteBlock() {
        std::error_code error;
        fs::permissions(directory_, original_, error);
        if (error)
            DiagnosticPrint::Print(stderr, "FAIL: restore private backup directory permissions: {}\n",
                                   error.message().c_str());
    }
    BackupDirectoryWriteBlock(const BackupDirectoryWriteBlock&) = delete;
    BackupDirectoryWriteBlock& operator=(const BackupDirectoryWriteBlock&) = delete;
    void Restore() const { fs::permissions(directory_, original_); }

private:
    fs::path directory_;
    fs::perms original_;
};
void FailedBackupRetry(EditorFrame& frame, const fs::path& root) {
    const auto folder = root / "retry";
    fs::create_directories(folder);
    const auto source = folder / "source.txt";
    TestFiles::WriteBytes(source, "original");
    Require(frame.Open(TestFiles::Filename(source)).status == OpenStatus::Opened,
            "opens real filesystem-failure fixture");
    const auto fallback = Fallback(root);
    fs::create_directories(fallback);
    Require(Backups(fallback).empty(), "private fallback starts without this fixture's backups");
    TestEditor::Append(&frame, "new-revision");
    const auto originalSize = frame.GetSize();
    {
        // Restrict only the two private backup destinations. A process-wide
        // file-size limit would also reject Wayland's shared-memory ftruncate
        // while this real timer test pumps native GUI events.
        const BackupDirectoryWriteBlock siblingBlocked(folder);
        const BackupDirectoryWriteBlock fallbackBlocked(fallback);
        for (const auto& directory : {folder, fallback}) {
            const std::ofstream denied(directory / "write-probe", std::ios::binary);
            Require(!denied, "both private backup destinations reject actual filesystem writes");
        }
        // Resizing forces Wayland to allocate a new backing buffer while the
        // backup failure is active, reproducing the old process-limit hazard.
        frame.SetSize(originalSize + frame.FromDIP(wxSize(200, 100)));
        Pump(300);
        Require(Backups(folder).empty() && Backups(fallback).empty(),
                "failed periodic backups leave no incomplete files in either destination and stay silent");
        siblingBlocked.Restore();
        fallbackBlocked.Restore();
    }
    frame.SetSize(originalSize);
    Wait([&folder] { return Backups(folder).size() == 1; },
         "failed backup keeps same revision eligible for later period");
    Require(TestFiles::Bytes(Backups(folder).front()) == "originalnew-revision" &&
                TestFiles::Bytes(source) == "original" && frame.DocumentIsModified(),
            "retry produces complete bytes without touching source/save point");
    Require(frame.Save().status == SaveStatus::Saved && Backups(folder).empty(), "save cleans retried backup");
}
#endif
class PeriodicFixtureRoot {
public:
    PeriodicFixtureRoot() : path_(TestFiles::TemporaryDirectory()) {
        // Preserve the original first-dot naming rule; isolate the Windows
        // reservation's .tmp suffix from ordinary same-directory fixtures.
        path_.replace_extension();
        path_ += "-periodic";
        Require(fs::create_directory(path_), "creates exclusive dot-free periodic fixture directory");
    }
    ~PeriodicFixtureRoot() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    PeriodicFixtureRoot(const PeriodicFixtureRoot&) = delete;
    PeriodicFixtureRoot& operator=(const PeriodicFixtureRoot&) = delete;
    const fs::path& Path() const { return path_; }

private:
    fs::path path_;
};
class PeriodicBackupApp final : public wxApp {
public:
    bool OnInit() override {
        static const PeriodicFixtureRoot fixture;
        const auto& root = fixture.Path();
        const auto ini = root / "Notepad4.ini";
#ifndef __WXMSW__
        wxSetEnv("XDG_DATA_HOME", TestFiles::Filename(root / "xdg"));
#endif
        TestFiles::WriteBytes(ini, "[Settings]\nAutoSaveOption=22\nWarnLineEndings=0\nFileWatchingMode=0\n");
        store_ = std::make_unique<SettingsStore>(TestFiles::Filename(ini));
        auto* frame = new EditorFrame(store_.get());
        frame->Show();
        CallAfter([this, frame, ini] {
            try {
                InitialSettings(*frame, *store_, ini);
                RealPeriodicTimer(*frame, ini.parent_path());
                Rotation(*frame, ini.parent_path());
                IntervalReset(*frame, ini.parent_path());
                RetentionAndBoundaries(*frame, *store_, ini.parent_path());
                Lifecycle(*frame, ini.parent_path());
                PeriodicPayloads(*frame, *store_, ini.parent_path());
                UntitledAndFallback(*frame, ini.parent_path());
#ifndef __WXMSW__
                FailedBackupRetry(*frame, ini.parent_path());
#endif
                std::puts("PASS: native periodic backup settings");
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
                result_ = 1;
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
    std::unique_ptr<SettingsStore> store_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(PeriodicBackupApp);
