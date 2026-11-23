#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include <print>
#include "file_helpers.h"
#include "file_command_driver.h"
#include "editor_test_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <tuple>
#include <vector>
#ifdef __WXMSW__
#include <sddl.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
int Command(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("File", label);
    Require(id != wxNOT_FOUND, "File Mode exposes the original disk and editing commands");
    return id;
}
#ifdef __WXMSW__
// Elevated runners may enable SeRestorePrivilege, which lets SetFileAttributesW
// bypass the fixture DACL. Disable only that privilege on an owned thread token
// so the command still exercises a real native access-denied failure.
class WithoutRestorePrivilege final {
public:
    WithoutRestorePrivilege() {
        HANDLE source = nullptr;
        Token process(nullptr, CloseHandle);
        if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_IMPERSONATE, TRUE, &source)) {
            previous_.reset(source);
        } else {
            Require(GetLastError() == ERROR_NO_TOKEN, "read original thread token");
            Require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &source),
                    "read fixture process token");
            process.reset(source);
        }
        HANDLE duplicate = nullptr;
        Require(DuplicateTokenEx(source, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES | TOKEN_IMPERSONATE, nullptr,
                                 SecurityImpersonation, TokenImpersonation, &duplicate),
                "duplicate fixture token without changing process privileges");
        token_.reset(duplicate);
        TOKEN_PRIVILEGES privilege{};
        privilege.PrivilegeCount = 1;
        Require(LookupPrivilegeValueW(nullptr, SE_RESTORE_NAME, &privilege.Privileges[0].Luid),
                "locate native restore privilege");
        // A token without this privilege already has the required access policy.
        Require(AdjustTokenPrivileges(token_.get(), FALSE, &privilege, 0, nullptr, nullptr),
                "disable attribute-write bypass on the duplicate token");
        Require(SetThreadToken(nullptr, token_.get()), "scope attribute-denial token to the test thread");
    }
    ~WithoutRestorePrivilege() { SetThreadToken(nullptr, previous_.get()); }

private:
    using Token = std::unique_ptr<void, decltype(&CloseHandle)>;
    Token previous_{nullptr, CloseHandle};
    Token token_{nullptr, CloseHandle};
};
class AttributeWriteDenied final {
public:
    explicit AttributeWriteDenied(const std::filesystem::path& path) : path_(path) {
        DWORD size = 0;
        GetFileSecurityW(path_.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &size);
        original_.resize(size);
        Require(GetFileSecurityW(path_.c_str(), DACL_SECURITY_INFORMATION, original_.data(), size, &size),
                "capture owned fixture's original security");
        PSECURITY_DESCRIPTOR denied = nullptr;
        Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(D;;0x100;;;WD)(A;;FA;;;WD)", SDDL_REVISION_1,
                                                                     &denied, nullptr),
                "create native attribute-write denial fixture");
        const bool changed = SetFileSecurityW(path_.c_str(), DACL_SECURITY_INFORMATION, denied);
        LocalFree(denied);
        Require(changed, "apply attribute-write denial only to owned temporary file");
    }
    ~AttributeWriteDenied() { SetFileSecurityW(path_.c_str(), DACL_SECURITY_INFORMATION, original_.data()); }

private:
    WithoutRestorePrivilege token_;
    std::filesystem::path path_;
    std::vector<unsigned char> original_;
};
#endif
void OwnedFile(EditorFrame* frame, SettingsStore& settings) {
    auto* editor = frame->Editor();
    const int disk = Command(frame, "Read Only File");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(disk), "untitled document disables disk protection");
    const auto path = TestFiles::TemporaryDirectory() / "owner.txt";
    TestFiles::WriteBytes(path, "alpha\r\nbeta\r\n");
#ifdef __WXMSW__
    Require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN),
            "seed unrelated attributes");
#else
    Require(chmod(path.c_str(), 0664) == 0, "seed independent owner/group/other permission fixture");
#endif
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "open named file");
    const int language = frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++");
    NativeSearch::Command(frame, language);
    const auto encoding = frame->DocumentEncoding();
    const auto bom = frame->DocumentHasBom();
    const auto recent = settings.values.recentFiles;
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 4, 1);
    const auto actions = editor->Send(Message::GetUndoActions);
    const auto current = editor->Send(Message::GetUndoCurrent);
    const auto savePoint = editor->Send(Message::GetUndoSavePoint);
    for (bool protect : {true, false}) {
        NativeSearch::Command(frame, disk);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
#ifdef __WXMSW__
        const auto attributes = GetFileAttributesW(path.c_str());
        Require(attributes ==
                    (FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_HIDDEN | (protect ? FILE_ATTRIBUTE_READONLY : 0)),
                "disk command changes only the original Windows read-only attribute");
#else
        struct stat state{};
        Require(stat(path.c_str(), &state) == 0 && (state.st_mode & 07777) == (protect ? 0464 : 0664),
                "owner-write toggle preserves group/other permissions exactly: 0664 <-> 0464");
#endif
        Require(frame->GetMenuBar()->IsChecked(disk) == protect &&
                    frame->GetTitle().Contains("Read Only File") == protect,
                "fresh native menu/title state reflects actual disk metadata");
        Require(!editor->Send(Message::GetReadOnly), "disk protection alone does not impose F10 editing policy");
        Require(TestFiles::Bytes(path) == "alpha\r\nbeta\r\n" && TestEditor::Text(editor) == "alpha\r\nbeta\r\n" &&
                    editor->Send(Message::GetAnchor) == 4 && editor->Send(Message::GetCurrentPos) == 1 &&
                    editor->Send(Message::GetUndoActions) == actions &&
                    editor->Send(Message::GetUndoCurrent) == current &&
                    editor->Send(Message::GetUndoSavePoint) == savePoint && !editor->Send(Message::GetModify) &&
                    frame->DocumentEncoding() == encoding && frame->DocumentHasBom() == bom &&
                    frame->GetMenuBar()->IsChecked(language) && settings.values.recentFiles == recent,
                "disk toggles preserve bytes, format, language, selection, history and Recent Files");
    }
    const int mode = Command(frame, "Read Only Mode");
    for (const auto& [command, expectedMode, expectedFile] : {std::tuple{disk, false, true},
                                                              {mode, true, true},
                                                              {disk, false, false},
                                                              {mode, true, false},
                                                              {disk, true, true},
                                                              {mode, false, true},
                                                              {disk, false, false}}) {
        NativeSearch::Command(frame, command);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(static_cast<bool>(editor->Send(Message::GetReadOnly)) == expectedMode &&
                    frame->GetMenuBar()->IsChecked(disk) == expectedFile &&
                    frame->GetMenuBar()->IsChecked(mode) == expectedMode,
                "independent pinned original disk/F10 sequence and native checks match");
    }
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "alpha\r\nbeta\r\n!", "pending Redo survives disk and F10 toggles");
    NativeSearch::Command(frame, disk);
    NativeSearch::Command(frame, mode);
    const auto saved = TestFiles::TemporaryDirectory() / "saved.txt";
    Require(frame->SaveAs(TestFiles::Filename(saved)).status == SaveStatus::Saved &&
                TestFiles::Bytes(saved) == "alpha\r\nbeta\r\n!" && TestFiles::Bytes(path) == "alpha\r\nbeta\r\n" &&
                editor->Send(Message::GetReadOnly),
            "Save As preserves F10 while selecting writable independent disk bytes");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened && editor->Send(Message::GetReadOnly) &&
                frame->GetTitle().Contains("Read Only File"),
            "reopening protected file preserves ordinary initial editing protection");
    NativeSearch::Command(frame, disk);
    Require(!editor->Send(Message::GetReadOnly), "making reopened file writable clears editing protection");
#ifdef __WXMSW__
    NativeSearch::Command(frame, disk);
    NativeSearch::Command(frame, mode);
    {
        AttributeWriteDenied denied(path);
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Save, wxID_OK);
        NativeSearch::Command(frame, disk);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        std::fprintf(stderr,
                     "native attribute denial: errors=%d choosers=%d attributes=0x%lx diskCheck=%d editing=%lld "
                     "title=%s\n",
                     driver.errors, driver.choosers, GetFileAttributesW(path.c_str()),
                     frame->GetMenuBar()->IsChecked(disk), static_cast<long long>(editor->Send(Message::GetReadOnly)),
                     frame->GetTitle().ToUTF8().data());
        Require(driver.errors == 1 && driver.choosers == 0 &&
                    (GetFileAttributesW(path.c_str()) & FILE_ATTRIBUTE_READONLY) &&
                    frame->GetMenuBar()->IsChecked(disk) && editor->Send(Message::GetReadOnly) &&
                    frame->GetTitle().Contains("Read Only File"),
                "failed native attribute toggle reports actual disk state and retains protected editing");
    }
    NativeSearch::Command(frame, disk);
#endif
#ifdef __WXMSW__
    Require(SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY), "external protection");
#else
    Require(chmod(path.c_str(), 07464) == 0, "external protection with special mode bits");
#endif
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsChecked(disk) && frame->GetTitle().Contains("Read Only File"),
            "menu opening refresh observes external disk protection changes");
    NativeSearch::Command(frame, disk);
#ifndef __WXMSW__
    struct stat special{};
    Require(stat(path.c_str(), &special) == 0 && (special.st_mode & 07777) == 07664,
            "toggle preserves setuid, setgid, sticky and group/other bits");
    Require(chmod(path.c_str(), 0664) == 0, "restore ordinary owned test-file mode");
#endif
    std::filesystem::remove(path);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(disk) && !frame->GetMenuBar()->IsChecked(disk) &&
                !frame->GetTitle().Contains("Read Only File"),
            "removed file clears actual disk check/title");
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Save, wxID_OK);
        wxCommandEvent command(wxEVT_MENU, disk);
        Require(frame->GetEventHandler()->ProcessEvent(command), "stale disk command remains handled");
        Require(driver.errors == 1 && driver.choosers == 0 && !std::filesystem::exists(path),
                "removed-file toggle reports a native error without saving or creating a replacement");
    }
#ifndef __WXMSW__
    const auto foreign = std::filesystem::path("/etc/passwd");
    const auto foreignBytes = TestFiles::Bytes(foreign);
    Require(frame->Open(TestFiles::Filename(foreign)).status == OpenStatus::Opened, "open real non-owned file");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(disk) &&
                frame->GetMenuBar()->GetHelpString(disk).Contains("Only the file owner") &&
                TestFiles::Bytes(foreign) == foreignBytes,
            "non-owner file toggle is disabled and native help explains owner-only permissions");
    Require(
        !frame->GetMenuBar()->IsChecked(disk) && frame->GetTitle().Contains("Read Only File"),
        "non-owner disk checkbox observes owner bit while existing title/save protection observes effective access");
    const auto unmodifiable = TestFiles::TemporaryDirectory() / "unmodifiable.txt";
    TestFiles::WriteBytes(unmodifiable, "preserved fixture bytes");
    Require(frame->Open(TestFiles::Filename(unmodifiable)).status == OpenStatus::Opened,
            "open owned temporary fixture through ordinary document flow");
    // Proc files are not seekable document inputs. Replace only this owned
    // temporary path after Open so the real permission command reaches procfs.
    std::filesystem::remove(unmodifiable);
    std::filesystem::create_symlink("/proc/self/status", unmodifiable);
    struct stat before{}, after{};
    Require(stat(unmodifiable.c_str(), &before) == 0 && before.st_uid == geteuid() && (before.st_mode & S_IWUSR) == 0,
            "proc fixture belongs to this effective user with owner write disabled");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(disk) && frame->GetMenuBar()->IsChecked(disk),
            "owned proc fixture enables owner-permission toggle with actual readonly check");
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Save, wxID_OK);
        const auto editorBytes = TestEditor::Text(editor);
        NativeSearch::Command(frame, disk);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(
            driver.errors == 1 && driver.choosers == 0 && stat(unmodifiable.c_str(), &after) == 0 &&
                after.st_mode == before.st_mode && after.st_uid == before.st_uid &&
                frame->GetMenuBar()->IsChecked(disk) && frame->GetTitle().Contains("Read Only File") &&
                TestEditor::Text(editor) == editorBytes,
            "real procfs chmod failure reports native feedback and re-reads unchanged permission/title/check state");
    }
#endif
    std::puts("PASS: actual File Mode commands, original F10 sequence, failure and independent disk state");
}
class ReadOnlyFileApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory() / "test.ini"));
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "native editor is presented");
                OwnedFile(frame, *settings_);
                result_ = 0;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
            }
            frame->Editor()->Send(Message::SetReadOnly, 0);
            frame->Editor()->Send(Message::SetSavePoint);
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
wxIMPLEMENT_APP(ReadOnlyFileApp);
