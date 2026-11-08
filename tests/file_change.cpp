#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "native_dialog_helpers.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/stdpaths.h>
#include <wx/radiobox.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename Predicate> void Wait(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (!predicate() && std::chrono::steady_clock::now() < end) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(predicate(), "external change must reach the live document");
}
void Peer(const wxString& path, const wxString& text, const wxString& action = "write") {
    const wxString arguments[] = {wxStandardPaths::Get().GetExecutablePath(), "--peer", path, text, action};
    const wchar_t* argv[] = {arguments[0].wc_str(), arguments[1].wc_str(), arguments[2].wc_str(),
                             arguments[3].wc_str(), arguments[4].wc_str(), nullptr};
    Require(wxExecute(argv, wxEXEC_SYNC) == 0, "independent file writer must finish");
}
void Pause(int milliseconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end) {
        wxYield();
        wxMilliSleep(10);
    }
}
void ChooseMode(EditorFrame* frame, FileWatchingMode chosen, bool accept = true) {
    class Driver final : public wxModalDialogHook {
    public:
        Driver(FileWatchingMode expected, FileWatchingMode chosen, bool accept)
            : expected_(expected), chosen_(chosen), accept_(accept) {
            Register();
        }
        std::exception_ptr failure;
        int Enter(wxDialog* dialog) override {
            if (dialog->GetTitle() != "File Change Notification") return wxID_NONE;
            wxTheApp->CallAfter([this, dialog] {
                try {
                    wxRadioBox* choices = nullptr;
                    for (auto* child : dialog->GetChildren())
                        if (auto* radio = wxDynamicCast(child, wxRadioBox)) choices = radio;
                    Require(choices && choices->GetCount() == 3 &&
                                choices->GetSelection() == static_cast<int>(expected_),
                            "native settings dialog reflects all three original modes and the applied selection");
                    choices->SetSelection(static_cast<int>(chosen_));
                    NativeSearch::Click(dialog, accept_ ? wxID_OK : wxID_CANCEL);
                } catch (...) {
                    failure = std::current_exception();
                    if (dialog->IsModal()) dialog->EndModal(wxID_CANCEL);
                }
            });
            return wxID_NONE;
        }

    private:
        FileWatchingMode expected_, chosen_;
        bool accept_;
    } driver(frame->FileWatching().mode, chosen, accept);
    int id = frame->GetMenuBar()->FindMenuItem("Settings", "File Change Notification...");
    if (id == wxNOT_FOUND) id = frame->GetMenuBar()->FindMenuItem("View", "File Change Notification...");
    Require(id != wxNOT_FOUND, "native file change settings menu exists");
    NativeSearch::Command(frame, id);
    if (driver.failure) std::rethrow_exception(driver.failure);
}
// Respond only to this test thread/process's actual native questions.
class ChangeDecision final : public wxTimer {
public:
    int choice = wxID_NO;
    int prompts = 0;
    int errors = 0;
    std::function<void()> beforeChoice;
    ChangeDecision() { Start(20); }
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<ChangeDecision*>(data);
                if (!IsWindowVisible(window)) return TRUE;
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                if (wxString(title) == "File Change Notification") {
                    driver.Stop();
                    ++driver.prompts;
                    if (auto action = std::exchange(driver.beforeChoice, {})) action();
                    SendMessageW(window, WM_USER + 102,
                                 driver.choice == wxID_YES  ? IDYES
                                 : driver.choice == wxID_NO ? IDNO
                                                            : IDCANCEL,
                                 0);
                    driver.Start(20);
                } else if (wxString(title) == "Could not open file" || wxString(title) == "Could not save file") {
                    ++driver.errors;
                    SendMessageW(window, WM_USER + 102, IDOK, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget) || !GTK_IS_MESSAGE_DIALOG(widget)) continue;
            const auto title = wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget)));
            if (title == "File Change Notification") {
                Stop();
                ++prompts;
                if (auto action = std::exchange(beforeChoice, {})) action();
                NativeDialogs::Respond(widget, choice == wxID_YES  ? GTK_RESPONSE_YES
                                               : choice == wxID_NO ? GTK_RESPONSE_NO
                                                                   : GTK_RESPONSE_CANCEL);
                Start(20);
            } else if (title == "Could not open file" || title == "Could not save file") {
                ++errors;
                NativeDialogs::Respond(widget, GTK_RESPONSE_OK);
            }
        }
        g_list_free(windows);
#endif
    }
};
class FileChangeApp final : public wxApp {
public:
    bool OnInit() override {
        if (argc == 5 && wxString(argv[1]) == "--peer") {
            const auto path = TestFiles::Utf8Path(wxString(argv[2]).ToStdString(wxConvUTF8));
            const auto action = wxString(argv[4]);
            if (action == "block") {
                std::filesystem::remove(path);
                std::filesystem::create_directory(path);
            } else if (action == "remove") {
                std::filesystem::remove(path);
            } else {
                const auto target = action == "replace" ? std::filesystem::path(path).concat(".peer") : path;
                {
                    std::ofstream output(target, std::ios::binary);
                    const auto text = wxString(argv[3]).ToUTF8();
                    output.write(text.data(), static_cast<std::streamsize>(text.length()));
                }
                if (action == "replace" && !wxRenameFile(TestFiles::Filename(target), TestFiles::Filename(path), true))
                    throw std::runtime_error("independent replacement must succeed");
            }
            peer_ = true;
            return true;
        }
        CallAfter([this] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-watch");
            auto* frame = new EditorFrame;
            try {
                Require(frame->FileWatching().mode == FileWatchingMode::AutoReload &&
                            frame->FileWatching().checkInterval == 1000 &&
                            frame->FileWatching().autoReloadDelay == 1000,
                        "original default is automatic unmodified reload with 1000 ms interval and delay");
                frame->SetFileWatchingOptions({FileWatchingMode::AutoReload, 20, 50});
                std::filesystem::create_directory(directory);
                const auto path = directory / "watched.txt";
                std::ofstream(path, std::ios::binary) << "original\n";
                Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "fixture opens");
                frame->Show();
                Peer(TestFiles::Filename(path), "external replacement\n");
                Wait([&] { return TestEditor::Text(frame->Editor()) == "external replacement\n"; });
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify), "automatic reload is clean");
                std::puts("PASS: independent process change automatically reloads clean content");
                ChangeDecision decision;
                auto options = frame->FileWatching();
                options.mode = FileWatchingMode::ShowMessage;
                frame->SetFileWatchingOptions(options);
                Peer(TestFiles::Filename(path), "declined external text\n");
                Wait([&] { return decision.prompts == 1; });
                Pause(200);
                Require(TestEditor::Text(frame->Editor()) == "external replacement\n" && decision.prompts == 1,
                        "No retains live text and consumes one decision");
                decision.choice = wxID_YES;
                Peer(TestFiles::Filename(path), "accepted external text\n");
                Wait([&] { return TestEditor::Text(frame->Editor()) == "accepted external text\n"; });
                Require(decision.prompts == 2, "Show message asks once for each actual change");
                options.mode = FileWatchingMode::AutoReload;
                frame->SetFileWatchingOptions(options);
                TestEditor::Append(frame, "dirty live text");
                decision.choice = wxID_NO;
                Peer(TestFiles::Filename(path), "dirty external text\n");
                Wait([&] { return decision.prompts == 3; });
                Require(TestEditor::Text(frame->Editor()) == "accepted external text\ndirty live text" &&
                            frame->Editor()->Send(Scintilla::Message::GetModify),
                        "dirty automatic mode asks and No retains live edits");
                decision.choice = wxID_YES;
                Peer(TestFiles::Filename(path), "approved dirty reload\n");
                Wait([&] { return TestEditor::Text(frame->Editor()) == "approved dirty reload\n"; });
                Require(decision.prompts == 4 && !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Yes reloads dirty content without an additional save question");
                std::puts("PASS: actual native Yes/No questions cover clean and dirty reload decisions");
                TestEditor::Append(frame, "saved locally");
                Require(frame->Save().status == SaveStatus::Saved, "application save succeeds");
                Pause(200);
                Require(decision.prompts == 4 && TestFiles::Bytes(path) == "approved dirty reload\nsaved locally",
                        "own save refreshes baseline and produces no prompt");
                options.mode = FileWatchingMode::None;
                frame->SetFileWatchingOptions(options);
                Peer(TestFiles::Filename(path), "off mode external content\n");
                Pause(200);
                Require(TestEditor::Text(frame->Editor()) == "approved dirty reload\nsaved locally" &&
                            decision.prompts == 4,
                        "None stops current notifications");
                options.mode = FileWatchingMode::ShowMessage;
                frame->SetFileWatchingOptions(options);
                const auto other = directory / "other.txt";
                Peer(TestFiles::Filename(other), "unrelated directory change\n");
                Pause(200);
                Require(decision.prompts == 4, "a sibling file does not trigger a current-file question");
                Require(frame->Open(TestFiles::Filename(other)).status == OpenStatus::Opened, "switch file succeeds");
                Peer(TestFiles::Filename(path), "old file changed again\n");
                Pause(200);
                Require(decision.prompts == 4 && TestEditor::Text(frame->Editor()) == "unrelated directory change\n",
                        "switching files removes old watch identity");
                Peer(TestFiles::Filename(other), "replaced current content\n", "replace");
                Wait([&] { return TestEditor::Text(frame->Editor()) == "replaced current content\n"; });
                Require(decision.prompts == 5, "replacement is detected as one current-file change");
                const auto savedAs = directory / "save-as.txt";
                Require(frame->SaveAs(TestFiles::Filename(savedAs)).status == SaveStatus::Saved, "Save As succeeds");
                Peer(TestFiles::Filename(other), "former identity\n");
                Pause(200);
                Require(decision.prompts == 5, "Save As stops watching the old path");
                Peer(TestFiles::Filename(savedAs), "new identity changed\n");
                Wait([&] { return TestEditor::Text(frame->Editor()) == "new identity changed\n"; });
                Require(decision.prompts == 6, "Save As monitors the new path");
                std::puts("PASS: own save, None, sibling changes, file switching, replacement and Save As lifecycle");
                Peer(TestFiles::Filename(savedAs), "", "remove");
                Wait([&] { return decision.prompts == 7; });
                Require(TestFiles::Bytes(savedAs) == "new identity changed\n" &&
                            !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "removed-file Yes recreates even clean live content");
                decision.choice = wxID_NO;
                Peer(TestFiles::Filename(savedAs), "", "remove");
                Wait([&] { return decision.prompts == 8; });
                Require(!std::filesystem::exists(savedAs) &&
                            TestEditor::Text(frame->Editor()) == "new identity changed\n",
                        "removed-file No retains live text and leaves disk absent");
                Peer(TestFiles::Filename(savedAs), "recreated externally\n");
                Wait([&] { return decision.prompts == 9; });
                Require(TestEditor::Text(frame->Editor()) == "new identity changed\n",
                        "No resumes monitoring and asks about an external recreation");
                decision.choice = wxID_CANCEL;
                Peer(TestFiles::Filename(savedAs), "", "remove");
                Wait([&] { return decision.prompts == 10; });
                Peer(TestFiles::Filename(savedAs), "after stop\n");
                Pause(200);
                Require(decision.prompts == 10 && frame->FileWatching().mode == FileWatchingMode::ShowMessage &&
                            TestEditor::Text(frame->Editor()) == "new identity changed\n",
                        "removed-file Cancel stops current monitoring while retaining configured mode and live text");
                Require(frame->Save().status == SaveStatus::Saved, "save restarts stopped current monitoring");
                decision.choice = wxID_YES;
                Peer(TestFiles::Filename(savedAs), "watch restarted\n");
                Wait([&] { return TestEditor::Text(frame->Editor()) == "watch restarted\n"; });
                Require(decision.prompts == 11, "application save reinstalls notifications after stop");
                Require(frame->NewDocument(), "New succeeds on a clean document");
                Peer(TestFiles::Filename(savedAs), "former document\n");
                Pause(200);
                Require(decision.prompts == 11 && frame->DocumentPath().empty() &&
                            TestEditor::Text(frame->Editor()).empty(),
                        "New stops current-file monitoring");
                std::puts("PASS: actual native removed-file Yes/No/Cancel, recreation and stop/restart decisions");
                decision.Stop();
                ChooseMode(frame, FileWatchingMode::None, false);
                Require(frame->FileWatching().mode == FileWatchingMode::ShowMessage, "Cancel retains applied mode");
                for (auto mode :
                     {FileWatchingMode::None, FileWatchingMode::ShowMessage, FileWatchingMode::AutoReload}) {
                    ChooseMode(frame, mode);
                    Require(frame->FileWatching().mode == mode, "native settings applies chosen monitoring mode");
                }
                Peer(TestFiles::Filename(savedAs), "first\nlast");
                Require(frame->Open(TestFiles::Filename(savedAs)).status == OpenStatus::Opened, "view fixture opens");
                frame->Editor()->Send(Scintilla::Message::SetSel, 3, 1);
                NativeSearch::Command(frame, wxID_FIND);
                auto* focus = wxWindow::FindFocus();
                Require(focus && focus != frame->Editor(), "modeless Find owns application focus before quiet reload");
                Peer(TestFiles::Filename(savedAs), "first changed\nlast plus\nend");
                Wait([&] { return TestEditor::Text(frame->Editor()) == "first changed\nlast plus\nend"; });
                Require(frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == 1 &&
                            frame->Editor()->Send(Scintilla::Message::GetAnchor) == 3 && wxWindow::FindFocus() == focus,
                        "quiet auto-reload preserves non-tail reversed selection and modeless Find focus");
                frame->Editor()->Send(Scintilla::Message::GotoLine, 2);
                Peer(TestFiles::Filename(savedAs), "first changed\nlast plus\nend\nnew last");
                Wait([&] { return TestEditor::Text(frame->Editor()) == "first changed\nlast plus\nend\nnew last"; });
                Require(frame->Editor()->Send(Scintilla::Message::GetCurrentPos) ==
                                frame->Editor()->Send(Scintilla::Message::GetLength) &&
                            wxWindow::FindFocus() == focus,
                        "old caret on final logical line follows the new document end without taking focus");
                std::puts(
                    "PASS: native mode choices/Cancel, original tail condition, restored selection and quiet focus");
                options.mode = FileWatchingMode::ShowMessage;
                frame->SetFileWatchingOptions(options);
                decision.Start(20);
                const auto live = TestEditor::Text(frame->Editor());
                decision.beforeChoice = [&] { Peer(TestFiles::Filename(savedAs), "", "block"); };
                Peer(TestFiles::Filename(savedAs), "reload fails after confirmation\n");
                Wait([&] { return decision.errors == 1; });
                Require(TestEditor::Text(frame->Editor()) == live &&
                            frame->DocumentPath() == TestFiles::Filename(savedAs),
                        "failed approved reload retains the live document and identity");
                Peer(TestFiles::Filename(savedAs), "", "remove");
                Peer(TestFiles::Filename(savedAs), "restored backing file\n");
                Wait([&] { return TestEditor::Text(frame->Editor()) == "restored backing file\n"; });
                decision.beforeChoice = [&] { Peer(TestFiles::Filename(savedAs), "", "block"); };
                Peer(TestFiles::Filename(savedAs), "", "remove");
                Wait([&] { return decision.errors == 2; });
                Require(TestEditor::Text(frame->Editor()) == "restored backing file\n" &&
                            frame->DocumentPath() == TestFiles::Filename(savedAs),
                        "failed deleted-file recreation retains live text and identity");
                std::puts("PASS: actual reload and recreation failures retain live text and document identity");
                Require(frame->NewDocument(), "new live document stops failed backing-file watch");
                options.mode = FileWatchingMode::AutoReload;
                frame->SetFileWatchingOptions(options);
                const auto firstSave = directory / "first-save.txt";
                Require(frame->SaveFirst(TestFiles::Filename(firstSave)).status == SaveStatus::Saved,
                        "first named save succeeds");
                Peer(TestFiles::Filename(firstSave), "first named external content\n");
                Wait([&] { return TestEditor::Text(frame->Editor()) == "first named external content\n"; });
                const auto count = decision.prompts;
                options.autoReloadDelay = 300;
                frame->SetFileWatchingOptions(options);
                Peer(TestFiles::Filename(firstSave), "pending disabled reload\n");
                Pause(50);
                options.mode = FileWatchingMode::None;
                frame->SetFileWatchingOptions(options);
                Pause(350);
                Require(TestEditor::Text(frame->Editor()) == "first named external content\n" &&
                            decision.prompts == count,
                        "disabling monitoring cancels an already delayed change");
                options.mode = FileWatchingMode::AutoReload;
                frame->SetFileWatchingOptions(options);
                Peer(TestFiles::Filename(firstSave), "pending closing reload\n");
                Pause(50);
                frame->Close();
                frame = nullptr;
                Pause(350);
                Require(decision.prompts == count, "closing cancels the pending reload and produces no late decision");
                std::puts("PASS: first save installs monitoring; disable and close cancel delayed changes");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            if (frame) frame->Destroy();
            std::filesystem::remove_all(directory);
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        if (peer_) return 0;
        wxApp::OnRun();
        return result_;
    }

private:
    bool peer_ = false;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(FileChangeApp);
