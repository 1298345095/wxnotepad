#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include <wx/textctrl.h>
#include <functional>
#include <wx/stdpaths.h>
#include <wx/filedlg.h>
#include <wx/display.h>
#include "file_helpers.h"

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class DialogDriver final : public wxModalDialogHook {
public:
    explicit DialogDriver(std::function<void(wxDialog*)> action) : action_(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Run Command") {
            opened = true;
            wxTheApp->CallAfter([this, dialog] {
                try {
                    action_(dialog);
                } catch (...) {
                    failure_ = std::current_exception();
                    if (dialog->IsModal()) dialog->EndModal(wxID_CANCEL);
                }
            });
        }
        return wxID_NONE;
    }
    bool opened = false;
    void Check() const {
        if (failure_) std::rethrow_exception(failure_);
    }

private:
    std::function<void(wxDialog*)> action_;
    std::exception_ptr failure_;
};
void InitialCommand(EditorFrame* frame) {
    const auto id = frame->GetMenuBar()->FindMenuItem("Tools", "Run Command...");
    Require(id != wxNOT_FOUND, "Tools menu exposes Run Command");
    DialogDriver driver([](wxDialog* dialog) {
        auto* input = wxDynamicCast(wxWindow::FindWindowByName("Command", dialog), wxTextCtrl);
        Require(input && input->GetValue().empty(), "untitled Run Command starts empty");
        auto* run = dialog->FindWindow(wxID_OK);
        Require(run && !run->IsEnabled(), "empty first argument disables Run");
        input->SetValue("   \"\"   ignored");
        Require(!run->IsEnabled(), "an empty quoted program disables Run even with remaining text");
        input->SetValue("program \"\"");
        Require(run->IsEnabled(), "a parsed executable enables Run");
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    NativeSearch::Command(frame, id);
    driver.Check();
    Require(driver.opened, "native menu opens Run Command");
    std::puts("PASS: Run Command menu, initial command and parsed enablement");
}
void LaunchArguments(EditorFrame* frame) {
    const auto directory = TestFiles::TemporaryDirectory("run-command");
    std::filesystem::create_directories(directory);
    const auto record = directory / (frame->DocumentPath().empty() ? "untitled-record.txt" : "named-record.txt");
    wxSetEnv("WXNOTEPAD_RUN_RECORD", TestFiles::Filename(record));
    wxSetEnv("WXNOTEPAD_RUN_DOCUMENT", frame->DocumentPath());
    Require(wxSetEnv("WXNOTEPAD_RUN_VALUE", wxString::FromUTF8("expanded 中文 value")), "set native command variable");
    wxFileName child(wxStandardPaths::Get().GetExecutablePath());
#ifdef __WXMSW__
    child.SetFullName("run_command_child.exe");
    const wxString arguments = wxString::FromUTF8(" \"two words\" \"\" \"%WXNOTEPAD_RUN_VALUE%\" \"中文\" tail");
#else
    child.SetFullName("run_command_child");
    const wxString arguments =
        wxString::FromUTF8(" 'two words' \"\" \"${WXNOTEPAD_RUN_VALUE}\" '中文' tail '$WXNOTEPAD_RUN_VALUE' "
                           "\\$WXNOTEPAD_RUN_VALUE $WXNOTEPAD_RUN_VALUE ; | > '*.txt' '$(not-run)'");
#endif
    if (!frame->DocumentPath().empty()) {
        const auto copiedFolder = directory / TestFiles::Utf8Path("program folder 中文");
        std::filesystem::create_directories(copiedFolder);
#ifdef __WXMSW__
        const auto copiedProgram = copiedFolder / TestFiles::Utf8Path("record program 中文.exe");
        for (const auto& dependency :
             std::filesystem::directory_iterator(TestFiles::Utf8Path(child.GetPath().utf8_string())))
            if (dependency.path().extension() == ".dll")
                std::filesystem::copy_file(dependency.path(), copiedFolder / dependency.path().filename(),
                                           std::filesystem::copy_options::overwrite_existing);
#else
        const auto copiedProgram = copiedFolder / TestFiles::Utf8Path("record program 中文");
#endif
        std::filesystem::copy_file(TestFiles::Utf8Path(child.GetFullPath().utf8_string()), copiedProgram,
                                   std::filesystem::copy_options::overwrite_existing);
        child.Assign(TestFiles::Filename(copiedProgram));
    }
    Require(wxSetEnv("WXNOTEPAD_RUN_PROGRAM", child.GetFullPath()), "set actual executable environment variable");
    const auto startup = wxGetCwd();
    const auto expectedDirectory =
        frame->DocumentPath().empty() ? startup : wxFileName(frame->DocumentPath()).GetPath();
    wxSetWorkingDirectory(TestFiles::Filename(directory));
    const auto id = frame->GetMenuBar()->FindMenuItem("Tools", "Run Command...");
    DialogDriver driver([&](wxDialog* dialog) {
        auto* input = wxDynamicCast(wxWindow::FindWindowByName("Command", dialog), wxTextCtrl);
        wxString program = child.GetFullPath();
        if (!frame->DocumentPath().empty()) {
#ifdef __WXMSW__
            program = "%WXNOTEPAD_RUN_PROGRAM%";
#else
            program = "$WXNOTEPAD_RUN_PROGRAM";
#endif
        }
        input->SetValue("\"" + program + "\"" + arguments);
        NativeSearch::Click(dialog, wxID_OK);
    });
    NativeSearch::Command(frame, id);
    driver.Check();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!std::filesystem::exists(record) && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    wxSetWorkingDirectory(startup);
    Require(std::filesystem::exists(record), "Run launches an independent recording child");
    const auto observed = TestFiles::Bytes(record);
    auto expected = expectedDirectory.utf8_string() + "\ntwo words\n\nexpanded 中文 value\n中文\ntail\n";
#ifndef __WXMSW__
    expected += "$WXNOTEPAD_RUN_VALUE\n$WXNOTEPAD_RUN_VALUE\nexpanded 中文 value\n;\n|\n>\n*.txt\n$(not-run)\n";
#endif
    if (observed != expected)
        std::fprintf(stderr, "Observed child bytes: <%s> expected <%s>\n", observed.c_str(), expected.c_str());
    Require(observed == expected, "independent child receives exact argv, empty/Unicode expansion and original cwd");
    if (!frame->DocumentPath().empty()) {
        const auto diskRecord = record.parent_path() / (record.filename().string() + ".disk");
        Require(std::filesystem::exists(diskRecord) && TestFiles::Bytes(diskRecord) == "disk document",
                "independent child observes unchanged disk bytes while the buffer contains edits");
        std::printf("CHILD disk bytes: <%s>\n", TestFiles::Bytes(diskRecord).c_str());
    }
    std::printf("CHILD executable=%s\nobserved cwd and argv (one entry per line, including empty entries):\n%s",
                child.GetFullPath().utf8_string().c_str(), observed.c_str());
    std::puts("PASS: independent child argv, environment expansion and untitled startup cwd");
}
class ProgramBrowser final : public wxTimer {
public:
    explicit ProgramBrowser(wxString destination) : destination_(std::move(destination)) { Start(50); }
    int seen = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<ProgramBrowser*>(data);
                wchar_t title[128]{};
                GetWindowTextW(window, title, 128);
                if (IsWindowVisible(window) &&
                    (wxString(title) == "Choose program" || wxString(title) == "Save As" ||
                     wxString(title) == "Save file") &&
                    !driver.stage_++) {
                    ++driver.seen;
                    NativeDialogs::ChooseFile(window, driver.destination_);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#else
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!GTK_IS_FILE_CHOOSER(widget) || !gtk_widget_get_visible(widget)) continue;
            if (!stage_) {
                if (!gtk_widget_get_mapped(widget) || NativeDialogs::FileChooserBusy(widget)) continue;
                ++stage_;
                ++seen;
                NativeDialogs::ChooseFile(widget, destination_);
            } else if (stage_ == 1 && !destination_.empty() && NativeDialogs::CanAcceptFile(widget, destination_)) {
                ++stage_;
                NativeDialogs::Respond(widget, GTK_RESPONSE_ACCEPT);
            }
        }
        g_list_free(windows);
#endif
    }

private:
    wxString destination_;
    int stage_ = 0;
};
void BrowseProgram(EditorFrame* frame) {
    const auto id = frame->GetMenuBar()->FindMenuItem("Tools", "Run Command...");
    wxFileName child(wxStandardPaths::Get().GetExecutablePath());
#ifdef __WXMSW__
    child.SetFullName("run_command_child.exe");
    const wxString arguments = " \"%WXNOTEPAD_RUN_VALUE%\" \"\" tail";
    const wxString expectedArguments = wxString::FromUTF8(" \"expanded 中文 value\" \"\" tail");
#else
    child.SetFullName("run_command_child");
    const wxString arguments = " \"${WXNOTEPAD_RUN_VALUE}\" \"\" tail";
    const wxString expectedArguments = arguments;
#endif
    DialogDriver driver([&](wxDialog* dialog) {
        auto* input = wxDynamicCast(wxWindow::FindWindowByName("Command", dialog), wxTextCtrl);
        auto* browse = wxDynamicCast(wxWindow::FindWindowByName("Browse programs", dialog), wxButton);
        Require(browse, "Run Command offers the native executable browser");
        const auto before = "\"" + child.GetFullPath() + "\"" + arguments;
        input->SetValue(before);
        {
            ProgramBrowser browser({});
            NativeSearch::Click(dialog, browse->GetId());
            Require(browser.seen == 1 && input->GetValue() == before, "cancelled native browser retains exact command");
        }
        {
            ProgramBrowser browser(child.GetFullPath());
            NativeSearch::Click(dialog, browse->GetId());
            Require(browser.seen == 1, "program browser accepts a real executable");
            Require(input->GetValue().EndsWith(expectedArguments),
                    "browser replacement preserves arguments and original environment policy");
        }
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    NativeSearch::Command(frame, id);
    driver.Check();
    std::puts("PASS: native executable browser accept/cancel and remaining arguments");
}
#ifndef __WXMSW__
void LiteralNamedPrograms(EditorFrame* frame) {
    const auto directory = TestFiles::TemporaryDirectory("literal-run-paths");
    std::filesystem::create_directories(directory);
    wxFileName child(wxStandardPaths::Get().GetExecutablePath());
    child.SetFullName("run_command_child");
    const auto id = frame->GetMenuBar()->FindMenuItem("Tools", "Run Command...");
    for (const auto* name : {"$HOME", "double\"quote", "back\\slash", "single'quote"}) {
        const auto program = directory / name;
        const auto script = "#!/bin/sh\nexec \"" + child.GetFullPath().utf8_string() + "\" \"$0\" \"$@\"\n";
        TestFiles::WriteBytes(program, script);
        std::filesystem::permissions(program, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
        Require(frame->Open(TestFiles::Filename(program)).status == OpenStatus::Opened,
                "open a real named executable with literal filename punctuation");
        const auto record = directory / (std::string(name) + ".record");
        wxSetEnv("WXNOTEPAD_RUN_RECORD", TestFiles::Filename(record));
        wxSetEnv("WXNOTEPAD_RUN_DOCUMENT", frame->DocumentPath());
        DialogDriver driver([](wxDialog* dialog) {
            auto* input = wxDynamicCast(wxWindow::FindWindowByName("Command", dialog), wxTextCtrl);
            input->SetValue(input->GetValue() + " 'literal argument'");
            NativeSearch::Click(dialog, wxID_OK);
            if (dialog->IsModal()) {
                NativeSearch::Click(dialog, wxID_CANCEL);
                throw std::runtime_error("named-document prefill launches its literal executable path");
            }
        });
        NativeSearch::Command(frame, id);
        driver.Check();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!std::filesystem::exists(record) && std::chrono::steady_clock::now() < deadline) {
            wxYield();
            wxMilliSleep(10);
        }
        Require(std::filesystem::exists(record) &&
                    TestFiles::Bytes(record) == directory.string() + "\n" + program.string() + "\nliteral argument\n",
                "actual child argv retains dollar, quote and backslash characters from the named document");
        Require(TestFiles::Bytes(record.string() + ".disk") == script,
                "recording child reads the literal document on disk");
    }
    std::puts("PASS: real named-document prefills preserve literal Linux executable filenames and child argv");
}
#endif
void NamedAndFailure(EditorFrame* frame) {
    const auto directory = TestFiles::TemporaryDirectory("named-tools") / TestFiles::Utf8Path("folder 中文");
    std::filesystem::create_directories(directory);
    const auto document = directory / "current file.txt";
    TestFiles::WriteBytes(document, "disk document");
    Require(frame->Open(TestFiles::Filename(document)).status == OpenStatus::Opened, "open named tool document");
    TestEditor::Append(frame, " dirty");
    bool released = false;
    DialogDriver driver([&](wxDialog* dialog) {
        NativeKeys::AfterPresentation(dialog, [&, dialog](bool presented) {
            try {
                Require(presented, "compositor presents Ctrl+R dialog");
                auto* input = wxDynamicCast(wxWindow::FindWindowByName("Command", dialog), wxTextCtrl);
                Require(input->GetValue() == "\"" + frame->DocumentPath() + "\"",
                        "named command starts with quoted full path");
                input->SetValue("wxnotepad-no-such-program-245");
                NativeSearch::Click(dialog, wxID_OK);
                Require(dialog->IsModal() && input->IsEditable(), "failed ordinary launch keeps editable modal dialog");
                auto* feedback = wxWindow::FindWindowByName("Run command feedback", dialog);
                Require(feedback && !feedback->GetLabel().empty(), "failed launch gives feedback");
                NativeSearch::Click(dialog, wxID_CANCEL);
            } catch (...) {
                dialog->EndModal(wxID_CANCEL);
                throw;
            }
        });
    });
    NativeKeys::Shortcut(frame, 'R', true, false, false, &released);
    driver.Check();
    Require(driver.opened, "Ctrl+R opens Run Command");
    Require(frame->DocumentIsModified() && TestFiles::Bytes(document) == "disk document",
            "preference default retains dirty document and disk after failed launch");
    frame->Editor()->Send(Scintilla::Message::SetSavePoint);
    std::puts("PASS: Ctrl+R, named initialization, failed launch feedback and unchanged document/disk");
}
void WidthPersistence(EditorFrame* frame, SettingsStore& settings) {
    const auto importedPath = TestFiles::TemporaryDirectory() / "original-width.ini";
    const auto screen = wxDisplay(static_cast<unsigned>(wxDisplay::GetFromWindow(frame))).GetGeometry().GetSize();
    const auto originalSection = wxString::Format("[Window Position %dx%d]\nRunDlgSizeX=500\n", screen.x, screen.y);
    TestFiles::WriteBytes(importedPath, originalSection.utf8_string());
    SettingsStore imported(TestFiles::Filename(importedPath));
    auto* importedFrame = new EditorFrame(&imported);
    DialogDriver import([&](wxDialog* dialog) {
        Require(dialog->GetClientSize().x == dialog->ConvertDialogToPixels(wxSize(500, 0)).x,
                "import original normalized client width through native dialog units");
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    NativeSearch::Command(importedFrame, importedFrame->GetMenuBar()->FindMenuItem("Tools", "Run Command..."));
    import.Check();
    Require(imported.Save(true), "save imported width through shared settings");
    const auto savedBytes = TestFiles::Bytes(importedPath);
    const wxString savedOriginal(savedBytes.data(), wxMBConvUTF16LE(), savedBytes.size());
    auto preserved = originalSection;
    preserved.Replace("\n", "\r\n");
    Require(savedOriginal.Contains(preserved), "original per-monitor width entry remains preserved");
    importedFrame->Destroy();
    const auto id = frame->GetMenuBar()->FindMenuItem("Tools", "Run Command...");
    int wanted = 0;
    DialogDriver resize([&](wxDialog* dialog) {
        wanted = dialog->ToDIP(dialog->GetSize()).x + 180;
        dialog->SetSize(dialog->FromDIP(wxSize(wanted, dialog->ToDIP(dialog->GetSize()).y)));
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    NativeSearch::Command(frame, id);
    resize.Check();
    Require(settings.Save(true), "save shared settings after resizing Run Command");
    SettingsStore reloaded(settings.Path());
    auto* other = new EditorFrame(&reloaded);
    DialogDriver recall([&](wxDialog* dialog) {
        Require(dialog->ToDIP(dialog->GetSize()).x == wanted,
                "Run Command width survives settings reload at native DPI");
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    NativeSearch::Command(other, other->GetMenuBar()->FindMenuItem("Tools", "Run Command..."));
    recall.Check();
    other->Destroy();
    std::puts("PASS: Run Command native DPI width survives cancel and shared settings reload");
}
class SaveDecision final : public wxTimer {
public:
    explicit SaveDecision(int response) : response_(response) { Start(50); }
    int confirmations = 0;
    int errors = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<SaveDecision*>(data);
                wchar_t title[128]{};
                GetWindowTextW(window, title, 128);
                if (!IsWindowVisible(window)) return TRUE;
                if (wxString(title) == "Save changes?") {
                    ++driver.confirmations;
                    SendMessageW(window, WM_USER + 102,
                                 driver.response_ == wxID_YES  ? IDYES
                                 : driver.response_ == wxID_NO ? IDNO
                                                               : IDCANCEL,
                                 0);
                } else if (wxString(title) == "Could not save file") {
                    ++driver.errors;
                    SendMessageW(window, WM_USER + 102, IDOK, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#else
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!GTK_IS_MESSAGE_DIALOG(widget) || !gtk_widget_get_visible(widget)) continue;
            const auto title = wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget)));
            if (title == "Save changes?") {
                ++confirmations;
                NativeDialogs::Respond(widget, response_ == wxID_YES  ? GTK_RESPONSE_YES
                                               : response_ == wxID_NO ? GTK_RESPONSE_NO
                                                                      : GTK_RESPONSE_CANCEL);
            } else if (title == "Could not save file") {
                ++errors;
                NativeDialogs::Respond(widget, GTK_RESPONSE_OK);
            }
        }
        g_list_free(windows);
#endif
    }

private:
    int response_;
};
void SaveBeforeTools(EditorFrame* frame, SettingsStore& settings) {
    settings.values.saveBeforeRunningTools = true;
    TestEditor::Append(frame, " pending tool save");
    const auto original = TestFiles::Bytes(TestFiles::Utf8Path(frame->DocumentPath().utf8_string()));
    const auto id = frame->GetMenuBar()->FindMenuItem("Tools", "Run Command...");
    {
        SaveDecision cancel(wxID_CANCEL);
        DialogDriver run([](wxDialog* dialog) { NativeSearch::Click(dialog, wxID_CANCEL); });
        NativeSearch::Command(frame, id);
        run.Check();
        Require(cancel.confirmations == 1 && !run.opened && frame->DocumentIsModified(),
                "save cancellation prevents Run dialog and retains edits");
    }
    {
        SaveDecision discard(wxID_NO);
        DialogDriver run([&](wxDialog* dialog) {
            Require(discard.confirmations == 1 && frame->DocumentIsModified(),
                    "discard decision precedes Run and preserves dirty buffer");
            NativeSearch::Click(dialog, wxID_CANCEL);
        });
        NativeSearch::Command(frame, id);
        run.Check();
        Require(run.opened, "discard save decision permits Run");
    }
    {
        SaveDecision save(wxID_YES);
        DialogDriver run([&](wxDialog* dialog) {
            Require(save.confirmations == 1 && !frame->DocumentIsModified(),
                    "successful save completes before Run appears");
            const auto bytes = TestFiles::Bytes(TestFiles::Utf8Path(frame->DocumentPath().utf8_string()));
            Require(bytes != original && bytes.ends_with(" pending tool save"),
                    "independent disk bytes contain edits before Run appears");
            NativeSearch::Click(dialog, wxID_CANCEL);
        });
        NativeSearch::Command(frame, id);
        run.Check();
        Require(run.opened, "successful save permits Run");
    }
    {
        TestEditor::Append(frame, " failed save");
        const auto path = TestFiles::Utf8Path(frame->DocumentPath().utf8_string());
        const auto saved = TestFiles::Bytes(path);
        std::filesystem::remove(path);
        std::filesystem::create_directory(path);
        SaveDecision save(wxID_YES);
        DialogDriver run([](wxDialog* dialog) { NativeSearch::Click(dialog, wxID_CANCEL); });
        NativeSearch::Command(frame, id);
        run.Check();
        Require(save.errors == 1 && !run.opened && frame->DocumentIsModified(),
                "failed save blocks Run and retains buffer");
        std::filesystem::remove(path);
        TestFiles::WriteBytes(path, saved);
        frame->Editor()->Send(Scintilla::Message::SetSavePoint);
    }
    Require(frame->NewDocument(), "create untitled save cancellation fixture");
    TestEditor::Append(frame, "unsaved tool document");
    {
        SaveDecision save(wxID_YES);
        ProgramBrowser chooser({});
        DialogDriver run([](wxDialog* dialog) { NativeSearch::Click(dialog, wxID_CANCEL); });
        NativeSearch::Command(frame, id);
        run.Check();
        Require(chooser.seen == 1 && !run.opened && frame->DocumentIsModified(),
                "cancelled untitled Save As blocks Run");
    }
    frame->Editor()->Send(Scintilla::Message::SetSavePoint);
    settings.values.saveBeforeRunningTools = false;
    std::puts("PASS: Save Before Tools cancel/discard/save ordering, dirty state and independent disk bytes");
}
void SelfLaunch(EditorFrame* frame) {
    const auto record = TestFiles::TemporaryDirectory() / "self-record.txt";
    wxSetEnv("WXNOTEPAD_RUN_RECORD", TestFiles::Filename(record));
    DialogDriver run([](wxDialog* dialog) {
        auto* input = wxDynamicCast(wxWindow::FindWindowByName("Command", dialog), wxTextCtrl);
        input->SetValue("Notepad4 --run-self-record");
        NativeSearch::Click(dialog, wxID_OK);
        Require(!dialog->IsShown(), "self alias hides Run Command before returning from launch");
    });
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Tools", "Run Command..."));
    run.Check();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!std::filesystem::exists(record) && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(std::filesystem::exists(record) && TestFiles::Bytes(record) == "own wxnotepad executable",
            "original Notepad4 alias launches this executable as an independent child");
    std::puts("PASS: original self alias launches current executable after Run closes");
}
class RunCommandApp final : public wxApp {
public:
    bool OnInit() override {
        if (argc == 2 && wxString(argv[1]) == "--run-self-record") {
            wxString record;
            if (!wxGetEnv("WXNOTEPAD_RUN_RECORD", &record)) return false;
            wxFFile output(record, "wb");
            output.Write("own wxnotepad executable");
            output.Close();
            CallAfter([this] { ExitMainLoop(); });
            return true;
        }
        settings_ =
            std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory() / "run-settings.ini"));
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "compositor presents owned frame");
                InitialCommand(frame);
                LaunchArguments(frame);
                WidthPersistence(frame, *settings_);
                BrowseProgram(frame);
#ifndef __WXMSW__
                LiteralNamedPrograms(frame);
#endif
                NamedAndFailure(frame);
                LaunchArguments(frame);
                SaveBeforeTools(frame, *settings_);
                SelfLaunch(frame);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(RunCommandApp);
