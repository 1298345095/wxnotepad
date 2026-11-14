#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "file_command_driver.h"
#include <wx/wx.h>
#include <wx/stdpaths.h>
#include <filesystem>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Drop(EditorFrame* frame, const wxArrayString& files) {
    wxDropFilesEvent event(wxEVT_DROP_FILES, static_cast<int>(files.size()), new wxString[files.size()]);
    for (size_t i = 0; i < files.size(); ++i)
        event.GetFiles()[i] = files[i];
    event.SetEventObject(frame->Editor());
    frame->Editor()->GetEventHandler()->ProcessEvent(event);
}
struct DropObservation {
    std::vector<std::string> decisions;
    wxString chooserDirectory;
    void Observe(const char* transition) {
        if (std::string_view(transition) == "chooser selection begin") {
            decisions.emplace_back("chooser");
#ifdef __WXGTK__
            auto* windows = gtk_window_list_toplevels();
            for (auto* item = windows; item; item = item->next) {
                if (!GTK_IS_FILE_CHOOSER(item->data) || !gtk_widget_get_visible(GTK_WIDGET(item->data))) continue;
                auto* directory = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(item->data));
                if (directory) chooserDirectory = wxString::FromUTF8(directory);
                g_free(directory);
            }
            g_list_free(windows);
#endif
        } else if (std::string_view(transition) == "confirmation response begin") {
            decisions.emplace_back("save");
        }
    }
};
void PeerWrite(const std::filesystem::path& path, const wxString& text) {
    const wxString arguments[] = {wxStandardPaths::Get().GetExecutablePath(), "--write-file", TestFiles::Filename(path),
                                  text};
    const wchar_t* argv[] = {arguments[0].wc_str(), arguments[1].wc_str(), arguments[2].wc_str(), arguments[3].wc_str(),
                             nullptr};
    Require(wxExecute(argv, wxEXEC_SYNC) == 0, "independent writer must finish changing the dropped file");
}
class FileDropApp final : public wxApp {
public:
    bool OnInit() override {
        if (argc == 4 && argv[1] == "--write-file") {
            TestFiles::WriteBytes(TestFiles::Utf8Path(wxString(argv[2]).ToStdString(wxConvUTF8)),
                                  wxString(argv[3]).ToStdString(wxConvUTF8));
            peer_ = true;
            return true;
        }
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        auto* frame = new EditorFrame;
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-file-drop");
            std::filesystem::create_directory(directory);
            try {
                const auto first = directory / std::filesystem::path(u8"first 中 %20.txt");
                const auto second = directory / "second.txt";
                TestFiles::WriteBytes(first, "first file\n");
                TestFiles::WriteBytes(second, "second file\n");
                Drop(frame, {TestFiles::Filename(first), TestFiles::Filename(second)});
                Require(frame->DocumentPath() == TestFiles::Filename(first) &&
                            TestEditor::Text(frame) == "first file\n" && TestFiles::Bytes(second) == "second file\n",
                        "file drop must open the first host filename and retain additional file bytes");
                std::puts("PASS: first native drop item preserves Unicode, spaces and literal percent filenames");
                TestEditor::Append(frame, "pending");
                NativeDialogs::FileCommandDriver cancelDirectory(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL);
                Drop(frame, {TestFiles::Filename(directory)});
                cancelDirectory.Stop();
                Require(cancelDirectory.choosers == 1 && cancelDirectory.prompts == 0 &&
                            frame->DocumentPath() == TestFiles::Filename(first) &&
                            TestEditor::Text(frame) == "first file\npending" &&
                            TestFiles::Bytes(first) == "first file\n",
                        "directory chooser Cancel must precede saving and retain the dirty document");
                std::puts("PASS: cancelled directory chooser retains the modified document before save decision");
                DropObservation observation;
                auto chosen = TestFiles::Filename(second);
#ifdef __WXMSW__
                // Relative selection independently checks the native chooser's initial directory.
                chosen = "second.txt";
#endif
                NativeDialogs::FileCommandDriver selectDirectory(
                    NativeDialogs::FileDialogProfile::Open, wxID_CANCEL, chosen,
                    [&observation](const char* transition) { observation.Observe(transition); });
                selectDirectory.WaitForInitialDirectory();
                Drop(frame, {TestFiles::Filename(directory)});
                selectDirectory.Stop();
                Require(selectDirectory.choosers == 1 && selectDirectory.prompts == 1 &&
                            observation.decisions == std::vector<std::string>{"chooser", "save"} &&
                            TestEditor::Text(frame) == "first file\npending" &&
                            frame->DocumentPath() == TestFiles::Filename(first),
                        "directory selection must lead to the ordinary save decision after the chooser");
#ifdef __WXGTK__
                Require(observation.chooserDirectory == TestFiles::Filename(directory),
                        "directory drop must root the native chooser at that directory");
#endif
                std::puts("PASS: rooted directory chooser selection precedes the cancelled save decision");
                NativeDialogs::FileCommandDriver save(NativeDialogs::FileDialogProfile::Open, wxID_YES);
                Drop(frame, {TestFiles::Filename(second)});
                save.Stop();
                Require(save.prompts == 1 && TestFiles::Bytes(first) == "first file\npending" &&
                            TestEditor::Text(frame) == "second file\n" &&
                            frame->DocumentPath() == TestFiles::Filename(second),
                        "file drop Save must write the current file before replacing it");
                TestEditor::Append(frame, "discarded");
                NativeDialogs::FileCommandDriver discard(NativeDialogs::FileDialogProfile::Open, wxID_NO);
                Drop(frame, {TestFiles::Filename(first)});
                discard.Stop();
                Require(discard.prompts == 1 && TestFiles::Bytes(second) == "second file\n" &&
                            TestEditor::Text(frame) == "first file\npending",
                        "file drop Discard must retain old disk bytes and open the requested document");
                frame->Editor()->Send(Scintilla::Message::SetSel, 2, 5);
                Drop(frame, {TestFiles::Filename(first)});
                Require(frame->Editor()->Send(Scintilla::Message::GetAnchor) == 2 &&
                            frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == 5,
                        "same-file drop must retain the existing open operation's selection restoration");
                NativeDialogs::FileCommandDriver missing(NativeDialogs::FileDialogProfile::Open, wxID_NO);
                Drop(frame, {TestFiles::Filename(directory / "missing.txt")});
                missing.Stop();
                Require(missing.errors == 1 && TestEditor::Text(frame) == "first file\npending" &&
                            frame->DocumentPath() == TestFiles::Filename(first),
                        "failed dropped input must retain the live document");
#ifdef __WXMSW__
                frame->Show();
                frame->Iconize();
                Drop(frame, {"{UUID}|solution|" + TestFiles::Filename(second)});
                Require(!frame->IsIconized() && frame->DocumentPath() == TestFiles::Filename(second),
                        "Visual Studio drop paths must use their final component and restore a minimized target");
#else
                const auto pipe = directory / "host|filename.txt";
                TestFiles::WriteBytes(pipe, "host pipe");
                Drop(frame, {TestFiles::Filename(pipe)});
                Require(frame->DocumentPath() == TestFiles::Filename(pipe) && TestEditor::Text(frame) == "host pipe",
                        "valid Unix pipe filenames must retain their full host path");
#endif
                std::puts("PASS: drop Save/Discard, same-file selection, failure retention and host path rules");
                frame->Editor()->Send(Scintilla::Message::SetSavePoint);
                frame->SetFileWatchingOptions({FileWatchingMode::AutoReload, 20, 20});
                Drop(frame, {TestFiles::Filename(first)});
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
                PeerWrite(first, "external change after drop\n");
                while (TestEditor::Text(frame) != "external change after drop\n" &&
                       std::chrono::steady_clock::now() < until) {
                    wxYield();
                    wxMilliSleep(10);
                }
                Require(frame->DocumentPath() == TestFiles::Filename(first) &&
                            TestEditor::Text(frame) == "external change after drop\n" &&
                            TestFiles::Bytes(first) == "external change after drop\n" &&
                            !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "dropping another file must transfer watching to the newly opened document");
                frame->SetFileWatchingOptions({FileWatchingMode::None});
                std::puts("PASS: dropped document receives independent external changes through file watching");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Destroy();
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
wxIMPLEMENT_APP(FileDropApp);
