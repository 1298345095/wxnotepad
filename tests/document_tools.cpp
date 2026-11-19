#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "file_command_driver.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <wx/listbox.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/ffile.h>
#include <wx/dialog.h>
#include <chrono>
#include <fstream>
#ifdef __WXGTK__
#include <gio/gdesktopappinfo.h>
#endif

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::filesystem::path Root() {
    wxString root;
    Require(wxGetEnv("WXNOTEPAD_DOCUMENT_TOOLS_ROOT", &root), "owned root must be configured");
    return TestFiles::Utf8Path(root.utf8_string());
}
wxString Name(const std::filesystem::path& path) { return TestFiles::Filename(path); }
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
    Require(recorded == wxString::Format("%d\n%s\n%s\n%s", path.empty() ? 0 : 1, path, cwd,
                                         wxString::FromUTF8(expected)),
            "child argv, working directory and disk bytes match independent literals");
    file.Close();
    std::filesystem::remove(marker);
}
class ProgramDriver final : public wxTimer {
public:
    ProgramDriver(wxString program, bool cancel = false) : program_(std::move(program)), cancel_(cancel) { Start(50); }
    int choosers = 0;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = dynamic_cast<wxDialog*>(window);
            if (!dialog || !dialog->IsShown() || dialog->GetTitle() != "Open Document With") continue;
            auto* list = dynamic_cast<wxListBox*>(dialog->FindWindowByName("open-with-programs", dialog));
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
            dialog->GetEventHandler()->ProcessEvent(click);
            return;
        }
    }
private:
    wxString program_;
    bool cancel_;
};
#ifdef __WXMSW__
void RegistryString(const wxString& key, const wxString& value) {
    HKEY created = nullptr;
    Require(RegCreateKeyExW(HKEY_CURRENT_USER, key.wc_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &created, nullptr) == ERROR_SUCCESS,
            "create owned handler registration");
    const auto result = RegSetValueExW(created, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.wc_str()),
                                      static_cast<DWORD>((value.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(created);
    Require(result == ERROR_SUCCESS, "register owned handler value");
}
#endif
class Association final {
public:
    Association() {
        const auto executable = wxStandardPaths::Get().GetExecutablePath();
#ifdef __WXMSW__
        key_ = "Software/Classes/.wxlaunch244";
        key_.Replace("/", wxString(wxUniChar(92)));
        RegistryString(key_, "wxnotepad.launch244");
        classKey_ = "Software/Classes/wxnotepad.launch244";
        classKey_.Replace("/", wxString(wxUniChar(92)));
        auto command = classKey_ + wxString(wxUniChar(92)) + "shell" + wxString(wxUniChar(92)) + "open" + wxString(wxUniChar(92)) + "command";
        RegistryString(command, "\"" + executable + "\" \"%1\"");
#else
        const auto desktop = Root() / "handler.desktop";
        TestFiles::WriteBytes(desktop, "[Desktop Entry]\nType=Application\nName=Owned document handler\nExec=\"" + executable.utf8_string() + "\" %f\nNoDisplay=true\n");
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
    wxSetEnv("WXNOTEPAD_DOCUMENT_TOOLS_CHILD", "1");
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
    NativeKeys::Shortcut(frame, 'L', true);
    WaitForChild("disk bytes", Name(path), Name(Root()));
    Require(editor->DocumentRevision() == revision && editor->Send(Message::GetModify) && editor->Send(Message::CanUndo) &&
                editor->Send(Message::GetAnchor) == 2 && editor->Send(Message::GetCurrentPos) == 6 &&
                frame->DocumentEncoding() == encoding && TestEditor::Text(frame) == "disk bytes unsaved",
            "Ctrl+L with preference off preserves the complete live document");
    const auto program = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetFullName();
    settings.values.openWithDirectory = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    {
        ProgramDriver chooser(program);
        NativeSearch::Command(frame, openWith);
        Require(chooser.choosers == 1, "Open With uses the program directory chooser");
    }
    WaitForChild("disk bytes", Name(path), Name(Root()));
    {
        ProgramDriver chooser(program, true);
        Require(!frame->OpenDocumentWith(), "chooser Cancel stops launch");
    }
    Require(!std::filesystem::exists(Root() / "record.txt"), "Cancel launches no program");
    NativeSearch::Command(frame, preference);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
    Require(settings.values.saveBeforeRunningTools, "visible preference persists");
    SettingsStore reloaded(settings.Path());
    Require(reloaded.values.saveBeforeRunningTools && reloaded.values.openWithDirectory == settings.values.openWithDirectory,
            "Settings SaveBeforeRunningTools and OpenWithDir round trip");
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
    wxUnsetEnv("WXNOTEPAD_DOCUMENT_TOOLS_CHILD");
    std::puts("PASS: menu, Ctrl+L, owned native handler argv/cwd/disk, save policy and untitled Open With");
}
class ToolsApp final : public wxApp {
public:
    bool OnInit() override {
        if (wxGetEnv("WXNOTEPAD_DOCUMENT_TOOLS_CHILD", nullptr)) {
            const auto argument = argc > 1 ? wxString(argv[1]) : wxString{};
            std::string disk;
            if (!argument.empty()) disk = TestFiles::Bytes(TestFiles::Utf8Path(argument.utf8_string()));
            wxFFile output(Name(Root() / "record.tmp"), "wb");
            output.Write(wxString::Format("%d\n%s\n%s\n%s", argc - 1, argument, wxGetCwd(), wxString::FromUTF8(disk)), wxConvUTF8);
            output.Close();
            wxRenameFile(Name(Root() / "record.tmp"), Name(Root() / "record.txt"));
            result_ = 0;
            CallAfter([this] { ExitMainLoop(); });
            return true;
        }
        settings_ = std::make_unique<SettingsStore>(Name(Root() / "Notepad4.ini"));
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "owned editor must be presented");
                Actions(frame, *settings_);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            wxUnsetEnv("WXNOTEPAD_DOCUMENT_TOOLS_CHILD");
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override { wxApp::OnRun(); return result_; }
private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
}
wxIMPLEMENT_APP(ToolsApp);
