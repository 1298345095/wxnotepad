#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "file_command_driver.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#ifdef WXNOTEPAD_TEST_DIRECTORY_LOAD_ORDER
#include "gtk_directory_load_order.h"
#endif
#include <wx/wx.h>
#include <wx/listbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/stdpaths.h>
#include <wx/ffile.h>
#include <wx/dialog.h>
#include <chrono>
#include <fstream>
#ifdef __WXMSW__
#include <shlobj.h>
#endif
#ifdef __WXGTK__
#include <gio/gdesktopappinfo.h>
#endif

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Phase(const char* phase) {
    std::fprintf(stderr, "phase: %s\n", phase);
    std::fflush(stderr);
}
std::filesystem::path Root() {
    if (wxTheApp && wxTheApp->argc >= 3 && wxString(wxTheApp->argv[1]) == "--record")
        return TestFiles::Utf8Path(wxString(wxTheApp->argv[2]).utf8_string());
    wxString root;
    Require(wxGetEnv("WXNOTEPAD_DOCUMENT_TOOLS_ROOT", &root), "owned root must be configured");
    return TestFiles::Utf8Path(root.utf8_string());
}
wxString Name(const std::filesystem::path& path) {
    return TestFiles::Filename(path);
}
void WaitForChild(const std::string& expected, const wxString& path, const wxString& cwd) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    const auto marker = Root() / "record.txt";
    while (!std::filesystem::exists(marker) && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(std::filesystem::exists(marker), "real child records launch");
    wxFFile file(Name(marker), "rb");
    wxString recorded;
    Require(file.ReadAll(&recorded, wxConvUTF8), "child record is readable");
    std::fprintf(stderr, "recorded: %s\nexpected: %s | %s | %s\n", recorded.utf8_str().data(), path.utf8_str().data(),
                 cwd.utf8_str().data(), expected.c_str());
    std::fflush(stderr);
    Require(recorded ==
                wxString::Format("%d\n%s\n%s\n%s", path.empty() ? 0 : 1, path, cwd, wxString::FromUTF8(expected)),
            "child argv, working directory and disk bytes match independent literals");
    file.Close();
    std::filesystem::remove(marker);
}
class LaunchErrors final : public wxTimer {
public:
    LaunchErrors() { Start(50); }
    int errors = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                wchar_t title[256]{};
                GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                if (IsWindowVisible(window) && (wxString(title) == "Could not execute document" ||
                                                wxString(title) == "Could not open document with program")) {
                    ++reinterpret_cast<LaunchErrors*>(data)->errors;
                    EnumChildWindows(
                        window,
                        [](HWND child, LPARAM) -> BOOL {
                            wchar_t text[1024]{};
                            GetWindowTextW(child, text, static_cast<int>(std::size(text)));
                            std::fprintf(stderr, "Native launch error control: %s\n", wxString(text).utf8_str().data());
                            std::fflush(stderr);
                            return TRUE;
                        },
                        0);
                    SendMessageW(window, TDM_CLICK_BUTTON, IDOK, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#else
        GList* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget) || !GTK_IS_MESSAGE_DIALOG(widget)) continue;
            const wxString title = wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget)));
            if (title == "Could not execute document" || title == "Could not open document with program") {
                gchar* text = nullptr;
                g_object_get(widget, "text", &text, nullptr);
                std::fprintf(stderr, "Native launch error: %s\n", text ? text : "");
                g_free(text);
                ++errors;
                NativeDialogs::Respond(widget, GTK_RESPONSE_OK);
            }
        }
        g_list_free(windows);
#endif
    }
};
class ProgramDriver final : public wxTimer {
public:
    explicit ProgramDriver(wxString program, bool cancel = false) : program_(std::move(program)), cancel_(cancel) {
        Start(50);
    }
    int choosers = 0;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsShown() || dialog->GetTitle() != "Open Document With") continue;
            auto* list = wxDynamicCast(dialog->FindWindowByName("open-with-programs", dialog), wxListBox);
            if (!list) continue;
            ++choosers;
            Stop();
            if (!cancel_) {
                const auto index = list->FindString(program_);
                Require(index != wxNOT_FOUND, "native list includes owned program");
                list->SetSelection(index);
                wxCommandEvent selection(wxEVT_LISTBOX, list->GetId());
                list->GetEventHandler()->ProcessEvent(selection);
            }
            wxCommandEvent click(wxEVT_BUTTON, cancel_ ? wxID_CANCEL : wxID_OK);
            if (cancel_) Phase("program chooser Cancel response enter");
            dialog->GetEventHandler()->ProcessEvent(click);
            if (cancel_) Phase("program chooser Cancel response returned");
            return;
        }
    }

private:
    wxString program_;
    bool cancel_;
};

class DirectoryDriver final : public wxTimer {
public:
    explicit DirectoryDriver(wxString directory) : directory_(std::move(directory)) { Start(50); }
    bool changed = false;
    void Notify() override {
        if (!dialog_) {
            for (auto* window : wxTopLevelWindows) {
                auto* candidate = wxDynamicCast(window, wxDialog);
                if (!candidate || !candidate->IsShown() ||
                    !candidate->FindWindowByName("open-with-programs", candidate))
                    continue;
                dialog_ = candidate;
                auto* browse = candidate->FindWindowByName("open-with-browse", candidate);
                Require(browse != nullptr, "chooser exposes its directory browse button");
                candidate->CallAfter([candidate, browse] {
                    Phase("directory Browse button enter");
                    wxCommandEvent click(wxEVT_BUTTON, browse->GetId());
                    browse->GetEventHandler()->ProcessEvent(click);
                    Phase("directory Browse button returned");
                });
                break;
            }
            return;
        }
        const auto* path = wxDynamicCast(dialog_->FindWindowByName("open-with-directory", dialog_), wxTextCtrl);
        if (path && path->GetValue() == directory_) {
#ifndef __WXMSW__
            const auto* programs = wxDynamicCast(dialog_->FindWindowByName("open-with-programs", dialog_), wxListBox);
            Require(programs && programs->FindString("Windows.EXE") == wxNOT_FOUND,
                    "Linux excludes imported Windows programs even with executable permission");
#endif
            changed = true;
            Stop();
            Phase("directory path updated; outer chooser Cancel response enter");
            wxCommandEvent cancel(wxEVT_BUTTON, wxID_CANCEL);
            dialog_->GetEventHandler()->ProcessEvent(cancel);
            Phase("directory outer chooser Cancel response returned");
            return;
        }
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<DirectoryDriver*>(data);
                if (!IsWindowVisible(window) || window == driver.dialog_->GetHandle()) return TRUE;
                wchar_t title[256]{};
                GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                if (wxString(title) != "Open Document With") return TRUE;
                if (!driver.selected_) {
                    driver.selected_ = true;
                    NativeDialogs::ChooseFile(window, driver.directory_);
                } else {
                    PostMessageW(window, WM_COMMAND, IDOK, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#else
        GList* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget) || !GTK_IS_FILE_CHOOSER(widget)) continue;
            ObserveChooser(widget);
            if (!selected_) {
                // The initial query can start before realization, so a mapped
                // chooser may still have no folder and no busy cursor.
                char* initialFolder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(widget));
                const bool folderReady = initialFolder != nullptr;
                g_free(initialFolder);
                if (!folderReady) continue;
                // An older folder model can consume the pending selection
                // while the requested folder is still being queried by GTK.
                if (NativeDialogs::FileChooserBusy(widget)) continue;
                selected_ = true;
                Phase("directory native selection request enter");
                gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(widget), directory_.utf8_str());
                Phase("directory native selection request returned");
            } else if (NativeDialogs::CanAcceptFile(widget, directory_)) {
                Phase("directory native Accept response enter");
                NativeDialogs::Respond(widget, GTK_RESPONSE_ACCEPT);
                Phase("directory native Accept response returned");
            }
        }
        g_list_free(windows);
#endif
    }

private:
#ifdef __WXGTK__
    void ObserveChooser(GtkWidget* widget) {
        auto* folder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(widget));
        auto* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widget));
        auto* accept = gtk_dialog_get_widget_for_response(GTK_DIALOG(widget), GTK_RESPONSE_ACCEPT);
        const auto state = wxString::Format(
            "directory native state folder='%s' selected='%s' expected='%s' mapped=%d busy=%d accept_sensitive=%d",
            folder ? wxString::FromUTF8(folder) : wxString("<none>"),
            selected ? wxString::FromUTF8(selected) : wxString("<none>"), directory_, gtk_widget_get_mapped(widget),
            NativeDialogs::FileChooserBusy(widget), accept && gtk_widget_is_sensitive(accept));
        g_free(folder);
        g_free(selected);
        if (state == chooserState_) return;
        chooserState_ = state;
        Phase(state.utf8_str().data());
    }
    wxString chooserState_;
#endif
    wxString directory_;
    wxDialog* dialog_ = nullptr;
    bool selected_ = false;
};
#ifdef __WXMSW__
void RegistryString(const wxString& key, const wxString& value) {
    HKEY created = nullptr;
    Require(RegCreateKeyExW(HKEY_CURRENT_USER, key.wc_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &created, nullptr) ==
                ERROR_SUCCESS,
            "create owned handler registration");
    const auto result = RegSetValueExW(created, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.wc_str()),
                                       static_cast<DWORD>((value.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(created);
    Require(result == ERROR_SUCCESS, "register owned handler value");
}
void Shortcut(const std::filesystem::path& shortcut, const wxString& target) {
    IShellLinkW* link = nullptr;
    Require(SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))),
            "create owned native shell shortcut");
    auto result = link->SetPath(target.wc_str());
    IPersistFile* file = nullptr;
    if (SUCCEEDED(result)) result = link->QueryInterface(IID_PPV_ARGS(&file));
    if (SUCCEEDED(result)) result = file->Save(shortcut.c_str(), TRUE);
    if (file) file->Release();
    link->Release();
    Require(SUCCEEDED(result), "save owned native shell shortcut");
}
#endif
class Association final {
public:
    Association() {
        std::fprintf(stderr, "phase: registering association\n");
        std::fflush(stderr);
        const auto executable = wxStandardPaths::Get().GetExecutablePath();
#ifdef __WXMSW__
        key_ = "Software/Classes/.wxlaunch244";
        key_.Replace("/", wxString(wxUniChar(92)));
        RegistryString(key_, "wxnotepad.launch244");
        classKey_ = "Software/Classes/wxnotepad.launch244";
        classKey_.Replace("/", wxString(wxUniChar(92)));
        auto command = classKey_ + wxString(wxUniChar(92)) + "shell" + wxString(wxUniChar(92)) + "open" +
                       wxString(wxUniChar(92)) + "command";
        RegistryString(command, "\"" + executable + "\" --record \"" + Name(Root()) + "\" \"%1\"");
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#else
        const auto desktop = Root() / "data" / "applications" / "handler.desktop";
        std::filesystem::create_directories(desktop.parent_path());
        TestFiles::WriteBytes(desktop, "[Desktop Entry]\nType=Application\nName=Owned document handler\nExec=\"" +
                                           executable.utf8_string() + "\" %f\nNoDisplay=true\n");
        auto* application = g_desktop_app_info_new_from_filename(Name(desktop).utf8_str());
        Require(application != nullptr, "native desktop entry loads");
        GError* error = nullptr;
        Require(g_app_info_set_as_default_for_type(G_APP_INFO(application), "text/plain", &error),
                "private native association must register");
        g_object_unref(application);
#endif
    }
    ~Association() {
#ifdef __WXMSW__
        RegDeleteTreeW(HKEY_CURRENT_USER, key_.wc_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, classKey_.wc_str());
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif
    }

private:
    wxString key_;
    wxString classKey_;
};
void Actions(EditorFrame* frame, SettingsStore& settings) {
    const auto path = Root() / TestFiles::Utf8Path("document 中文 & space.wxlaunch244");
    TestFiles::WriteBytes(path, "disk bytes");
    Association association;
    LaunchErrors launchErrors;
#ifndef __WXMSW__
    {
        ProgramDriver chooser({}, true);
        Require(!frame->OpenDocumentWith(), "the default directory chooser can be cancelled for an untitled document");
        Require(chooser.choosers == 1 && settings.values.openWithDirectory == Name(Root() / "home"),
                "without an XDG Desktop configuration Open With defaults to the native home directory");
    }
#endif
    std::fprintf(stderr, "phase: direct Execute menu\n");
    std::fflush(stderr);
    Require(frame->Open(Name(path)).status == OpenStatus::Opened, "open named document");
    auto* editor = frame->Editor();
    TestEditor::Append(frame, " unsaved");
    editor->Send(Message::SetSel, 2, 6);
    const auto encoding = frame->DocumentEncoding();
    const auto revision = editor->DocumentRevision();
    const auto execute = frame->GetMenuBar()->FindMenuItem("Tools", "Execute Document");
    const auto openWith = frame->GetMenuBar()->FindMenuItem("Tools", "Open Document With...");
    const auto preference = frame->GetMenuBar()->FindMenuItem("Tools", "Save Before Running Tools");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(execute), "named document enables Execute");
    int executeEvents = 0;
    frame->Bind(
        wxEVT_MENU,
        [&executeEvents](wxCommandEvent& event) {
            ++executeEvents;
            event.Skip();
        },
        execute);
    NativeSearch::Command(frame, execute);
    Require(executeEvents == 1, "direct native Execute menu dispatches its owned command");
    Require(launchErrors.errors == 0, "direct Execute menu starts the registered native handler without an error");
    WaitForChild("disk bytes", Name(path), Name(Root()));
    NativeKeys::Shortcut(frame, 'L', true);
    Require(executeEvents == 2, "native Ctrl+L dispatches the Execute Document menu event");
    WaitForChild("disk bytes", Name(path), Name(Root()));
    Require(editor->DocumentRevision() == revision && editor->Send(Message::GetModify) &&
                editor->Send(Message::CanUndo) && editor->Send(Message::GetAnchor) == 2 &&
                editor->Send(Message::GetCurrentPos) == 6 && frame->DocumentEncoding() == encoding &&
                TestEditor::Text(frame) == "disk bytes unsaved",
            "Ctrl+L with preference off preserves the complete live document");
    std::fprintf(stderr, "phase: Open With\n");
    std::fflush(stderr);
    const auto program = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetFullName();
    settings.values.openWithDirectory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    {
        ProgramDriver chooser(program);
        NativeSearch::Command(frame, openWith);
        Require(chooser.choosers == 1, "Open With uses the program directory chooser");
    }
    WaitForChild("disk bytes", Name(path), Name(Root()));
#ifndef __WXMSW__
    settings.values.openWithDirectory = Name(Root() / "data" / "applications");
    {
        ProgramDriver chooser("handler.desktop");
        Require(frame->OpenDocumentWith(), "a selected native desktop entry receives the document");
    }
    WaitForChild("disk bytes", Name(path), Name(Root()));
    settings.values.openWithDirectory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
#endif
    {
        Phase("program chooser Cancel command enter");
        ProgramDriver chooser(program, true);
        Require(!frame->OpenDocumentWith(), "chooser Cancel stops launch");
        Phase("program chooser Cancel command returned");
    }
    Require(!std::filesystem::exists(Root() / "record.txt"), "Cancel launches no program");
    const auto changedDirectory = Root() / "changed-programs";
    std::filesystem::create_directory(changedDirectory);
#ifndef __WXMSW__
    const auto windowsProgram = changedDirectory / "Windows.EXE";
    TestFiles::WriteBytes(windowsProgram, "Windows program placeholder");
    std::filesystem::permissions(windowsProgram, std::filesystem::perms::owner_all);
#endif
    {
        Phase("directory Browse and Cancel command enter");
        DirectoryDriver directory(Name(changedDirectory));
        Require(!frame->OpenDocumentWith(), "Browse followed by Cancel does not launch");
        Phase("directory Browse and Cancel command returned");
        Require(directory.changed && settings.values.openWithDirectory == Name(changedDirectory),
                "native directory selection immediately updates OpenWithDir, including after Cancel");
    }
    Require(!std::filesystem::exists(Root() / "record.txt"), "directory change and Cancel launch no child");
    std::fprintf(stderr, "phase: save policy\n");
    std::fflush(stderr);
    NativeSearch::Command(frame, preference);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
    Require(settings.values.saveBeforeRunningTools, "visible preference persists");
    SettingsStore reloaded(settings.Path());
    Require(reloaded.values.saveBeforeRunningTools &&
                reloaded.values.openWithDirectory == settings.values.openWithDirectory,
            "Settings SaveBeforeRunningTools and OpenWithDir round trip");
    const auto browsedDirectory = settings.values.openWithDirectory;
    const auto documentsDirectory = wxStandardPaths::Get().GetDocumentsDir();
    settings.values.openWithDirectory = documentsDirectory;
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
    {
        wxFFile config(settings.Path(), "rb");
        wxString text;
        Require(config.ReadAll(&text, wxMBConvUTF16LE()) && config.Close(), "independently read saved settings");
        Require(text.Contains("OpenWithDir=%CSIDL:MYDOCUMENTS%"),
                "portable Documents directory uses the original token for the directory root itself");
    }
    SettingsStore documentsReloaded(settings.Path());
    Require(
        wxFileName::DirName(documentsReloaded.values.openWithDirectory).SameAs(wxFileName::DirName(documentsDirectory)),
        "portable Documents directory resolves to the native directory after reload");
    settings.values.openWithDirectory = browsedDirectory;
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
    {
        NativeDialogs::FileCommandDriver decision(NativeDialogs::FileDialogProfile::Save, wxID_CANCEL);
        ProgramDriver chooser(program, true);
        Require(!frame->OpenDocumentWith(), "save decision Cancel stops before chooser");
        Require(decision.prompts == 1 && chooser.choosers == 0, "save decision precedes chooser");
    }
    {
        NativeDialogs::FileCommandDriver decision(NativeDialogs::FileDialogProfile::Save, wxID_NO);
        Require(frame->ExecuteDocument(), "Discard permits Execute");
        Require(decision.prompts == 1 && editor->Send(Message::GetModify), "Discard keeps in-memory edits");
    }
    WaitForChild("disk bytes", Name(path), Name(Root()));
    // Replacing the backing file with a directory makes the ordinary required
    // Save fail on both platforms, without changing editor identity.
    std::fprintf(stderr, "phase: failed save\n");
    std::fflush(stderr);
    std::filesystem::remove(path);
    std::filesystem::create_directory(path);
    {
        NativeDialogs::FileCommandDriver decision(NativeDialogs::FileDialogProfile::Save, wxID_YES);
        ProgramDriver chooser(program, true);
        Require(!frame->OpenDocumentWith(), "failed required save stops before chooser");
        Require(decision.prompts == 1 && decision.errors == 1 && chooser.choosers == 0 &&
                    editor->Send(Message::GetModify) && !std::filesystem::exists(Root() / "record.txt"),
                "failed Save retains edits and prevents chooser and child");
    }
    std::filesystem::remove(path);
    TestFiles::WriteBytes(path, "disk bytes");
    {
        NativeDialogs::FileCommandDriver decision(NativeDialogs::FileDialogProfile::Save, wxID_YES);
        Require(frame->ExecuteDocument(), "Save permits Execute");
        Require(decision.prompts == 1 && !editor->Send(Message::GetModify), "Save uses ordinary save point");
    }
    WaitForChild("disk bytes unsaved", Name(path), Name(Root()));
#ifdef __WXMSW__
    const auto shortcuts = Root() / "shortcuts";
    std::filesystem::create_directory(shortcuts);
    const auto documentShortcut = Root() / "document.lnk";
    Shortcut(documentShortcut, Name(path));
    Shortcut(shortcuts / "program.lnk", wxStandardPaths::Get().GetExecutablePath());
    Require(frame->Open(Name(documentShortcut)).status == OpenStatus::Opened, "open the named shortcut document");
    const auto shortcutIdentity = frame->DocumentPath();
    const auto shortcutEncoding = frame->DocumentEncoding();
    const auto shortcutDirty = editor->Send(Message::GetModify);
    const auto shortcutText = TestEditor::Text(frame);
    const auto shortcutRevision = editor->DocumentRevision();
    const auto shortcutUndo = editor->Send(Message::CanUndo);
    settings.values.openWithDirectory = Name(shortcuts);
    {
        ProgramDriver chooser("program.lnk");
        Require(frame->OpenDocumentWith(), "Windows program shortcut remains usable in the program directory");
    }
    WaitForChild("disk bytes unsaved", Name(path), Name(Root()));
    Require(
        frame->DocumentPath() == shortcutIdentity && frame->DocumentEncoding() == shortcutEncoding &&
            editor->Send(Message::GetModify) == shortcutDirty && TestEditor::Text(frame) == shortcutText &&
            editor->DocumentRevision() == shortcutRevision && editor->Send(Message::CanUndo) == shortcutUndo,
        "resolving the document shortcut retains the actual loaded identity, encoding, dirty state, text and history");
    Require(frame->Open(Name(path)).status == OpenStatus::Opened, "restore the ordinary named document");
#endif
    const auto failedPrograms = Root() / "failed-programs";
    std::filesystem::create_directory(failedPrograms);
#ifdef __WXMSW__
    const auto brokenProgram = failedPrograms / "broken.exe";
    TestFiles::WriteBytes(brokenProgram, "not an executable");
#else
    const auto brokenProgram = failedPrograms / "broken.desktop";
    TestFiles::WriteBytes(brokenProgram, R"([Desktop Entry]
Type=Application
Name=Missing owned program
Exec=/missing-wxnotepad-owned-program-244 %f
)");
#endif
    const auto errorsBefore = launchErrors.errors;
    settings.values.openWithDirectory = Name(failedPrograms);
    {
        ProgramDriver chooser(Name(brokenProgram.filename()));
        Require(!frame->OpenDocumentWith(), "native program start failure is reported");
    }
    Require(launchErrors.errors == errorsBefore + 1 && frame->DocumentPath() == Name(path) &&
                TestEditor::Text(frame) == "disk bytes unsaved" && !editor->Send(Message::GetModify) &&
                !std::filesystem::exists(Root() / "record.txt"),
            "failed launch retains the current document and reports one recoverable error");
    settings.values.openWithDirectory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    std::fprintf(stderr, "phase: untitled\n");
    std::fflush(stderr);
    Require(frame->NewDocument(), "clean named document becomes untitled");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(execute) && frame->GetMenuBar()->IsEnabled(openWith),
            "untitled disables only Execute");
    settings.values.saveBeforeRunningTools = false;
    const auto startupDirectory = wxGetCwd();
    {
        ProgramDriver chooser(program);
        Require(frame->OpenDocumentWith(), "untitled Open With launches chosen program with no document argument");
    }
    WaitForChild("", "", startupDirectory);
    std::puts("PASS: menu, Ctrl+L, owned native handler argv/cwd/disk, save policy and untitled Open With");
}
class ToolsApp final : public wxApp {
public:
#ifdef WXNOTEPAD_TEST_DIRECTORY_LOAD_ORDER
    NativeDialogs::DirectoryLoadOrder* DirectoryLoadOrderFixture() const { return directoryLoadOrder_.get(); }
#endif
    bool OnInit() override {
        const bool fixture = argc == 2 && wxString(argv[1]) == "--fixture";
        const auto log = fixture ? wxString("fixture.log") : wxString::Format("child-%lu.log", wxGetProcessId());
#ifdef __WXMSW__
        FILE* diagnostic = nullptr;
        _wfreopen_s(&diagnostic, Name(Root() / TestFiles::Utf8Path(log.utf8_string())).wc_str(), L"ab", stderr);
#else
        std::freopen(Name(Root() / TestFiles::Utf8Path(log.utf8_string())).utf8_str(), "ab", stderr);
#endif
        std::fprintf(stderr, "phase: init argc=%d\n", argc);
        std::fflush(stderr);
        if (!fixture) {
            const int firstArgument = argc >= 3 && wxString(argv[1]) == "--record" ? 3 : 1;
            const auto argument = argc > firstArgument ? wxString(argv[firstArgument]) : wxString{};
            std::string disk;
            if (!argument.empty()) disk = TestFiles::Bytes(TestFiles::Utf8Path(argument.utf8_string()));
            wxFFile output(Name(Root() / "record.tmp"), "wb");
            output.Write(wxString::Format("%d\n%s\n%s\n%s", argc - firstArgument, argument, wxGetCwd(),
                                          wxString::FromUTF8(disk)),
                         wxConvUTF8);
            output.Close();
            wxRenameFile(Name(Root() / "record.tmp"), Name(Root() / "record.txt"));
            result_ = 0;
            CallAfter([this] { ExitMainLoop(); });
            return true;
        }
#ifdef WXNOTEPAD_TEST_DIRECTORY_LOAD_ORDER
        directoryLoadOrder_ = std::make_unique<NativeDialogs::DirectoryLoadOrder>();
#endif
        settings_ = std::make_unique<SettingsStore>(Name(Root() / "Notepad4.ini"));
        std::fprintf(stderr, "phase: constructing frame\n");
        std::fflush(stderr);
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        std::fprintf(stderr, "phase: awaiting presentation\n");
        std::fflush(stderr);
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            std::fprintf(stderr, "phase: presented=%d\n", ready);
            std::fflush(stderr);
            try {
                Require(ready, "owned editor must be presented");
                Actions(frame, *settings_);
#ifdef WXNOTEPAD_TEST_DIRECTORY_LOAD_ORDER
                Require(directoryLoadOrder_->Completed(), "both controlled native directory loads ran");
#endif
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

private:
#ifdef WXNOTEPAD_TEST_DIRECTORY_LOAD_ORDER
    std::unique_ptr<NativeDialogs::DirectoryLoadOrder> directoryLoadOrder_;
#endif
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
#ifdef WXNOTEPAD_TEST_DIRECTORY_LOAD_ORDER
namespace NativeDialogs {
DirectoryLoadOrder* CurrentDirectoryLoadOrder() {
    const auto* application = dynamic_cast<ToolsApp*>(wxTheApp);
    return application ? application->DirectoryLoadOrderFixture() : nullptr;
}
} // namespace NativeDialogs
#endif
wxIMPLEMENT_APP(ToolsApp);
