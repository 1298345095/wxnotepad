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
#include <wx/checkbox.h>
#include <wx/listbox.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <stdexcept>

namespace {
constexpr int EventPollIntervalMs = 10;
constexpr int DialogResponseIntervalMs = 20;
constexpr int NoChangeObservationMs = 200;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template <typename Predicate> void Wait(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (!predicate() && std::chrono::steady_clock::now() < end) {
        wxYield();
        wxMilliSleep(EventPollIntervalMs);
    }
    Require(predicate(), "external change must reach the live document");
}
void Peer(const wxString& path, const wxString& text, const wxString& action = "write", bool asynchronous = false) {
    const wxString arguments[] = {wxStandardPaths::Get().GetExecutablePath(), "--peer", path, text, action};
    const wchar_t* argv[] = {arguments[0].wc_str(), arguments[1].wc_str(), arguments[2].wc_str(),
                             arguments[3].wc_str(), arguments[4].wc_str(), nullptr};
    // Running the independent writer must preserve the observed application's
    // native window enablement and focus.
    const auto result = wxExecute(argv, asynchronous ? wxEXEC_ASYNC : wxEXEC_SYNC | wxEXEC_NODISABLE);
    Require(asynchronous ? result > 0 : result == 0, "independent file writer must start or finish");
}
void Pause(int milliseconds) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end) {
        wxYield();
        wxMilliSleep(EventPollIntervalMs);
    }
}
class FileWatchingDialogDriver final : public wxModalDialogHook {
public:
    FileWatchingDialogDriver(FileWatchingOptions expected, FileWatchingMode chosen, bool accept, int flags, bool reset)
        : expected_(expected), chosen_(chosen), accept_(accept), flags_(flags), reset_(reset) {
        Register();
    }
    std::exception_ptr failure;
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() != "File Change Notification") return wxID_NONE;
        wxTheApp->CallAfter([this, dialog] {
            try {
                ApplyChoice(dialog);
            } catch (...) {
                failure = std::current_exception();
                if (dialog->IsModal()) dialog->EndModal(wxID_CANCEL);
            }
        });
        return wxID_NONE;
    }

private:
    void ApplyChoice(wxDialog* dialog) {
        wxRadioBox* choices = nullptr;
        for (auto* child : dialog->GetChildren())
            if (auto* radio = wxDynamicCast(child, wxRadioBox)) choices = radio;
        Require(choices && choices->GetCount() == 3 && choices->GetSelection() == static_cast<int>(expected_.mode),
                "native settings dialog reflects all three original modes and the applied selection");
        ApplyCheckboxes(dialog);
        choices->SetSelection(static_cast<int>(chosen_));
        NativeSearch::Click(dialog, accept_ ? wxID_OK : wxID_CANCEL);
    }
    void ApplyCheckboxes(wxDialog* dialog) {
        int checkboxes = 0;
        for (auto* child : dialog->GetChildren()) {
            if (auto* checkbox = wxDynamicCast(child, wxCheckBox)) {
                Require(checkbox->IsEnabled(), "original monitoring options remain enabled in every mode");
                const wxString labels[] = {"&Monitor continuously updated file (e.g. log file).",
                                           "&Keep at document end after reloading.", "&Reset if a new file is opened."};
                Require(checkboxes < 3 && checkbox->GetLabel() == labels[checkboxes],
                        "native dialog offers the original three option labels");
                const bool expected =
                    checkboxes == 2 ? expected_.resetOnNewFile : (expected_.flags & (1 << checkboxes)) != 0;
                Require(checkbox->GetValue() == expected, "native dialog reflects applied option values");
                checkbox->SetValue(checkboxes == 2 ? reset_ : (flags_ & (1 << checkboxes)) != 0);
                ++checkboxes;
            }
        }
        Require(checkboxes == 3, "native dialog offers log, keep-at-end and reset options");
    }
    FileWatchingOptions expected_;
    FileWatchingMode chosen_;
    bool accept_;
    int flags_;
    bool reset_;
};
void ChooseMode(EditorFrame* frame, FileWatchingMode chosen, bool accept = true, int flags = 0, bool reset = false) {
    FileWatchingDialogDriver driver(frame->FileWatching(), chosen, accept, flags, reset);
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
    ChangeDecision() { Start(DialogResponseIntervalMs); }
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<ChangeDecision*>(data);
                if (!IsWindowVisible(window)) return TRUE;
                wchar_t title[256]{};
                GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                if (wxString(title) == "File Change Notification") {
                    driver.Stop();
                    ++driver.prompts;
                    if (auto action = std::exchange(driver.beforeChoice, {})) action();
                    SendMessageW(window, TDM_CLICK_BUTTON,
                                 driver.choice == wxID_YES  ? IDYES
                                 : driver.choice == wxID_NO ? IDNO
                                                            : IDCANCEL,
                                 0);
                    driver.Start(DialogResponseIntervalMs);
                } else if (wxString(title) == "Could not open file" || wxString(title) == "Could not save file") {
                    ++driver.errors;
                    SendMessageW(window, TDM_CLICK_BUTTON, IDOK, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget)) continue;
            if (!GTK_IS_MESSAGE_DIALOG(widget)) continue;
            const auto title = wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget)));
            if (title == "File Change Notification") {
                Stop();
                ++prompts;
                if (auto action = std::exchange(beforeChoice, {})) action();
                NativeDialogs::Respond(widget, choice == wxID_YES  ? GTK_RESPONSE_YES
                                               : choice == wxID_NO ? GTK_RESPONSE_NO
                                                                   : GTK_RESPONSE_CANCEL);
                Start(DialogResponseIntervalMs);
            } else if (title == "Could not open file" || title == "Could not save file") {
                ++errors;
                NativeDialogs::Respond(widget, GTK_RESPONSE_OK);
            }
        }
        g_list_free(windows);
#endif
    }
};
std::filesystem::path CheckMessageDecisionsAndFileLifecycle(EditorFrame* frame, const std::filesystem::path& directory,
                                                            const std::filesystem::path& path, ChangeDecision& decision,
                                                            FileWatchingOptions& options) {
    options.mode = FileWatchingMode::ShowMessage;
    frame->SetFileWatchingOptions(options);
    Peer(TestFiles::Filename(path), "declined external text\n");
    Wait([&decision] { return decision.prompts == 1; });
    Pause(NoChangeObservationMs);
    Require(TestEditor::Text(frame->Editor()) == "external replacement\n" && decision.prompts == 1,
            "No retains live text and consumes one decision");
    decision.choice = wxID_YES;
    Peer(TestFiles::Filename(path), "accepted external text\n");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "accepted external text\n"; });
    Require(decision.prompts == 2, "Show message asks once for each actual change");
    options.mode = FileWatchingMode::AutoReload;
    frame->SetFileWatchingOptions(options);
    TestEditor::Append(frame, "dirty live text");
    decision.choice = wxID_NO;
    Peer(TestFiles::Filename(path), "dirty external text\n");
    Wait([&decision] { return decision.prompts == 3; });
    Require(TestEditor::Text(frame->Editor()) == "accepted external text\ndirty live text" &&
                frame->Editor()->Send(Scintilla::Message::GetModify),
            "dirty automatic mode asks and No retains live edits");
    decision.choice = wxID_YES;
    Peer(TestFiles::Filename(path), "approved dirty reload\n");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "approved dirty reload\n"; });
    Require(decision.prompts == 4 && !frame->Editor()->Send(Scintilla::Message::GetModify),
            "Yes reloads dirty content without an additional save question");
    std::puts("PASS: actual native Yes/No questions cover clean and dirty reload decisions");
    options.mode = FileWatchingMode::ShowMessage;
    frame->SetFileWatchingOptions(options);
    TestEditor::Append(frame, "saved locally");
    Require(frame->Save().status == SaveStatus::Saved, "application save succeeds");
    Pause(NoChangeObservationMs);
    Require(decision.prompts == 4 && TestFiles::Bytes(path) == "approved dirty reload\nsaved locally",
            "own save refreshes baseline and produces no prompt");
    const auto modified = std::filesystem::last_write_time(path);
    Require(frame->UpdateFileTime(), "application timestamp-only update succeeds");
    Pause(NoChangeObservationMs);
    Require(decision.prompts == 4 && std::filesystem::last_write_time(path) > modified &&
                TestFiles::Bytes(path) == "approved dirty reload\nsaved locally",
            "timestamp-only update refreshes baseline and produces no self-change question");
    options.mode = FileWatchingMode::None;
    frame->SetFileWatchingOptions(options);
    Peer(TestFiles::Filename(path), "off mode external content\n");
    Pause(NoChangeObservationMs);
    Require(TestEditor::Text(frame->Editor()) == "approved dirty reload\nsaved locally" && decision.prompts == 4,
            "None stops current notifications");
    options.mode = FileWatchingMode::ShowMessage;
    frame->SetFileWatchingOptions(options);
    const auto other = directory / "other.txt";
    Peer(TestFiles::Filename(other), "unrelated directory change\n");
    Pause(NoChangeObservationMs);
    Require(decision.prompts == 4, "a sibling file does not trigger a current-file question");
    Require(frame->Open(TestFiles::Filename(other)).status == OpenStatus::Opened, "switch file succeeds");
    Peer(TestFiles::Filename(path), "old file changed again\n");
    Pause(NoChangeObservationMs);
    Require(decision.prompts == 4 && TestEditor::Text(frame->Editor()) == "unrelated directory change\n",
            "switching files removes old watch identity");
    Peer(TestFiles::Filename(other), "replaced current content\n", "replace");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "replaced current content\n"; });
    Require(decision.prompts == 5, "replacement is detected as one current-file change");
    const auto savedAs = directory / "save-as.txt";
    Require(frame->SaveAs(TestFiles::Filename(savedAs)).status == SaveStatus::Saved, "Save As succeeds");
    Peer(TestFiles::Filename(other), "former identity\n");
    Pause(NoChangeObservationMs);
    Require(decision.prompts == 5, "Save As stops watching the old path");
    Peer(TestFiles::Filename(savedAs), "new identity changed\n");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "new identity changed\n"; });
    Require(decision.prompts == 6, "Save As monitors the new path");
    std::puts("PASS: own save, None, sibling changes, file switching, replacement and Save As lifecycle");
    Peer(TestFiles::Filename(savedAs), "", "remove");
    Wait([&decision] { return decision.prompts == 7; });
    Require(TestFiles::Bytes(savedAs) == "new identity changed\n" &&
                !frame->Editor()->Send(Scintilla::Message::GetModify),
            "removed-file Yes recreates even clean live content");
    decision.choice = wxID_NO;
    Peer(TestFiles::Filename(savedAs), "", "remove");
    Wait([&decision] { return decision.prompts == 8; });
    Require(!std::filesystem::exists(savedAs) && TestEditor::Text(frame->Editor()) == "new identity changed\n",
            "removed-file No retains live text and leaves disk absent");
    Peer(TestFiles::Filename(savedAs), "recreated externally\n");
    Wait([&decision] { return decision.prompts == 9; });
    Require(TestEditor::Text(frame->Editor()) == "new identity changed\n",
            "No resumes monitoring and asks about an external recreation");
    decision.choice = wxID_CANCEL;
    Peer(TestFiles::Filename(savedAs), "", "remove");
    Wait([&decision] { return decision.prompts == 10; });
    Peer(TestFiles::Filename(savedAs), "after stop\n");
    Pause(NoChangeObservationMs);
    Require(decision.prompts == 10 && frame->FileWatching().mode == FileWatchingMode::ShowMessage &&
                TestEditor::Text(frame->Editor()) == "new identity changed\n",
            "removed-file Cancel stops current monitoring while retaining configured mode and live text");
    Require(frame->Save().status == SaveStatus::Saved, "save restarts stopped current monitoring");
    decision.choice = wxID_YES;
    Peer(TestFiles::Filename(savedAs), "watch restarted\n");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "watch restarted\n"; });
    Require(decision.prompts == 11, "application save reinstalls notifications after stop");
    Require(frame->NewDocument(), "New succeeds on a clean document");
    Peer(TestFiles::Filename(savedAs), "former document\n");
    Pause(NoChangeObservationMs);
    Require(decision.prompts == 11 && frame->DocumentPath().empty() && TestEditor::Text(frame->Editor()).empty(),
            "New stops current-file monitoring");
    std::puts("PASS: actual native removed-file Yes/No/Cancel, recreation and stop/restart decisions");
    return savedAs;
}

void CheckNativeOptionsAndReloadFailures(EditorFrame* frame, const std::filesystem::path& savedAs,
                                         ChangeDecision& decision, FileWatchingOptions& options) {
    decision.Stop();
    ChooseMode(frame, FileWatchingMode::None, false);
    Require(frame->FileWatching().mode == FileWatchingMode::ShowMessage, "Cancel retains applied mode");
    ChooseMode(frame, FileWatchingMode::ShowMessage, true, 3, true);
    Require(frame->FileWatching().flags == 3 && frame->FileWatching().resetOnNewFile,
            "native options applies both original flags and Reset");
    ChooseMode(frame, FileWatchingMode::None, false);
    Require(frame->FileWatching().mode == FileWatchingMode::ShowMessage && frame->FileWatching().flags == 3 &&
                frame->FileWatching().resetOnNewFile,
            "Cancel retains applied mode and all three options");
    for (auto mode : {FileWatchingMode::None, FileWatchingMode::ShowMessage, FileWatchingMode::AutoReload}) {
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
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "first changed\nlast plus\nend"; });
    Require(frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == 1 &&
                frame->Editor()->Send(Scintilla::Message::GetAnchor) == 3 && wxWindow::FindFocus() == focus,
            "quiet auto-reload preserves non-tail reversed selection and modeless Find focus");
    frame->Editor()->Send(Scintilla::Message::GotoLine, 2);
    Peer(TestFiles::Filename(savedAs), "first changed\nlast plus\nend\nnew last");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "first changed\nlast plus\nend\nnew last"; });
    Require(frame->Editor()->Send(Scintilla::Message::GetCurrentPos) ==
                    frame->Editor()->Send(Scintilla::Message::GetLength) &&
                wxWindow::FindFocus() == focus,
            "old caret on final logical line follows the new document end without taking focus");
    std::puts("PASS: native mode choices/Cancel, original tail condition, restored selection and quiet focus");
    options.mode = FileWatchingMode::ShowMessage;
    frame->SetFileWatchingOptions(options);
    decision.Start(DialogResponseIntervalMs);
    const auto live = TestEditor::Text(frame->Editor());
    decision.beforeChoice = [&savedAs] { Peer(TestFiles::Filename(savedAs), "", "block"); };
    Peer(TestFiles::Filename(savedAs), "reload fails after confirmation\n");
    Wait([&decision] { return decision.errors == 1; });
    Require(TestEditor::Text(frame->Editor()) == live && frame->DocumentPath() == TestFiles::Filename(savedAs),
            "failed approved reload retains the live document and identity");
    Peer(TestFiles::Filename(savedAs), "", "remove");
    Peer(TestFiles::Filename(savedAs), "restored backing file\n");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "restored backing file\n"; });
    decision.beforeChoice = [&savedAs] { Peer(TestFiles::Filename(savedAs), "", "block"); };
    Peer(TestFiles::Filename(savedAs), "", "remove");
    Wait([&decision] { return decision.errors == 2; });
    Require(TestEditor::Text(frame->Editor()) == "restored backing file\n" &&
                frame->DocumentPath() == TestFiles::Filename(savedAs),
            "failed deleted-file recreation retains live text and identity");
    std::puts("PASS: actual reload and recreation failures retain live text and document identity");
}

std::filesystem::path CheckFirstSaveAndLogTail(EditorFrame* frame, const std::filesystem::path& directory,
                                               ChangeDecision& decision, FileWatchingOptions& options) {
    Require(frame->NewDocument(), "new live document stops failed backing-file watch");
    options.mode = FileWatchingMode::AutoReload;
    frame->SetFileWatchingOptions(options);
    const auto firstSave = directory / "first-save.txt";
    Require(frame->SaveFirst(TestFiles::Filename(firstSave)).status == SaveStatus::Saved, "first named save succeeds");
    Peer(TestFiles::Filename(firstSave), "first named external content\n");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "first named external content\n"; });
    options.flags = FileWatchingOption_LogFile | FileWatchingOption_KeepAtEnd;
    frame->SetFileWatchingOptions(options);
    frame->Editor()->Send(Scintilla::Message::GotoPos, 1);
    Peer(TestFiles::Filename(firstSave), "replacement\nnew tail", "replace");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "replacement\nnew tail"; });
    Require(frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == 20 &&
                !frame->Editor()->Send(Scintilla::Message::CanUndo),
            "Keep at end follows replacement from a non-tail caret with original reload undo behavior");
    frame->Editor()->Send(Scintilla::Message::GotoPos, 0);
    Peer(TestFiles::Filename(firstSave), "x");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "x"; });
    Require(frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == 1, "Keep at end follows truncation");
    options.flags = FileWatchingOption_LogFile;
    frame->SetFileWatchingOptions(options);
    Peer(TestFiles::Filename(firstSave), "head\nlast");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "head\nlast"; });
    Require(frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == 9,
            "log mode without Keep at end follows an old final-line caret during growth");
    frame->Editor()->Send(Scintilla::Message::GotoPos, 1);
    Peer(TestFiles::Filename(firstSave), "changed head\nchanged last", "replace");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "changed head\nchanged last"; });
    Require(frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == 1,
            "log mode without Keep at end preserves a non-tail caret during replacement");
    Peer(TestFiles::Filename(firstSave), "x");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "x"; });
    options.flags = FileWatchingOption_LogFile | FileWatchingOption_KeepAtEnd;
    std::puts("PASS: log Keep at end follows replacement and truncation with full-reload undo behavior");
    return firstSave;
}

void CheckResetTransitions(EditorFrame* frame, const std::filesystem::path& directory,
                           const std::filesystem::path& firstSave, ChangeDecision& decision,
                           FileWatchingOptions& options) {
    options.resetOnNewFile = true;
    frame->SetFileWatchingOptions(options);
    TestEditor::Append(frame, "unsaved log edits");
    {
        class CancelDiscard final : public wxModalDialogHook {
        public:
            CancelDiscard() { Register(); }
            int Enter(wxDialog* dialog) override {
                return dialog->GetTitle() == "Save changes?" ? wxID_CANCEL : wxID_NONE;
            }
        } cancel;
        Require(!frame->NewDocument() && frame->Open(TestFiles::Filename(firstSave)).status == OpenStatus::Cancelled &&
                    frame->FileWatching().mode == FileWatchingMode::AutoReload &&
                    TestEditor::Text(frame->Editor()) == "xunsaved log edits",
                "canceling a dirty New/Open preserves live text and active log monitoring");
    }
    Require(frame->ReloadCurrentFile().status == OpenStatus::Opened &&
                frame->FileWatching().mode == FileWatchingMode::AutoReload,
            "reloading the same document preserves monitoring with Reset enabled");
    const auto beforeManualReload = decision.prompts;
    std::ofstream(firstSave, std::ios::binary) << "manual encoding reload\n";
    Require(frame->ReloadAsUtf8().status == OpenStatus::Opened &&
                frame->FileWatching().mode == FileWatchingMode::AutoReload,
            "quick encoding reload preserves active monitoring with Reset enabled");
    TestEditor::Append(frame, "local after manual reload");
    Pause(NoChangeObservationMs);
    Require(decision.prompts == beforeManualReload,
            "quick encoding reload refreshes the watch baseline before subsequent live edits");
    frame->ReloadCurrentFile();
    {
        class AcceptEncoding final : public wxModalDialogHook {
        public:
            AcceptEncoding() { Register(); }
            int Enter(wxDialog* dialog) override {
                if (dialog->GetTitle() != "Reload With Encoding") return wxID_NONE;
                for (auto* child : dialog->GetChildren())
                    if (auto* choices = wxDynamicCast(child, wxListBox)) choices->SetSelection(0);
                return wxID_OK;
            }
        } accept;
        std::ofstream(firstSave, std::ios::binary) << "x";
        Require(frame->ReloadWithEncoding().status == OpenStatus::Opened &&
                    frame->FileWatching().mode == FileWatchingMode::AutoReload,
                "explicit encoding reload preserves active monitoring with Reset enabled");
    }
    TestEditor::Append(frame, "local after explicit reload");
    Pause(NoChangeObservationMs);
    Require(decision.prompts == beforeManualReload, "explicit encoding reload refreshes the active watch baseline");
    frame->ReloadCurrentFile();
    Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled &&
                frame->Open(TestFiles::Filename(directory / "missing.txt")).status == OpenStatus::Failed &&
                frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                frame->SaveAs(TestFiles::Filename(directory / "missing" / "new.txt")).status == SaveStatus::Failed &&
                frame->FileWatching().mode == FileWatchingMode::AutoReload,
            "canceled and failed Open/Save As retain monitoring");
    Peer(TestFiles::Filename(firstSave), "retained after failed transition\n");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "retained after failed transition\n"; });
    Require(frame->Open(TestFiles::Filename(firstSave)).status == OpenStatus::Opened &&
                frame->FileWatching().mode == FileWatchingMode::None && frame->FileWatching().flags == 3 &&
                frame->FileWatching().resetOnNewFile,
            "successful Open resets mode and retains original option bits");
    frame->SetFileWatchingOptions(options);
    Require(frame->Save().status == SaveStatus::Saved && frame->FileWatching().mode == FileWatchingMode::AutoReload,
            "ordinary Save preserves monitoring");
    const auto resetSave = directory / "reset-save.txt";
    Require(frame->SaveAs(TestFiles::Filename(resetSave)).status == SaveStatus::Saved &&
                frame->FileWatching().mode == FileWatchingMode::None,
            "successful Save As resets monitoring");
    frame->SetFileWatchingOptions(options);
    Require(frame->NewDocument() && frame->FileWatching().mode == FileWatchingMode::None,
            "successful New resets monitoring");
    frame->SetFileWatchingOptions(options);
    Require(frame->SaveFirst(TestFiles::Filename(firstSave)).status == SaveStatus::Saved &&
                frame->FileWatching().mode == FileWatchingMode::AutoReload,
            "first named Save preserves monitoring like original Untitled save");
    options.resetOnNewFile = false;
    frame->SetFileWatchingOptions(options);
    Peer(TestFiles::Filename(firstSave), "x");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "x"; });
    std::puts("PASS: Reset applies at successful Open/New/Save As; failures, cancellation, first save, "
              "Save and reload preserve monitoring");
}

void CheckContinuousLogAndShutdown(EditorFrame*& frame, const std::filesystem::path& directory,
                                   ChangeDecision& decision, FileWatchingOptions& options) {
    const auto stream = directory / "stream.txt";
    const auto streamPrompts = decision.prompts;
    Peer(TestFiles::Filename(stream), "", "stream", true);
    Wait([&stream] { return std::filesystem::exists(std::filesystem::path(stream).concat(".ready")); });
    options.autoReloadDelay = 120;
    frame->SetFileWatchingOptions(options);
    Require(frame->Open(TestFiles::Filename(stream)).status == OpenStatus::Opened,
            "actively written log opens while the writer holds its file handle");
    Pause(800);
    const auto during = TestEditor::Text(frame->Editor());
    Require(during.starts_with("entry 1\nentry 2\n") && during.find("entry 12\n") == std::string::npos,
            "log polling reloads during continuous writes without extending the original delay");
    Wait([&frame] { return TestEditor::Text(frame->Editor()).ends_with("entry 12\n"); });
    Wait([&stream] { return std::filesystem::exists(std::filesystem::path(stream).concat(".done")); });
    Peer(TestFiles::Filename(stream), "after continuous writer\n");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "after continuous writer\n"; });
    Require(decision.prompts == streamPrompts && frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == 24,
            "log monitoring resumes after completed reloads with no duplicate native decisions");
    std::puts("PASS: live writer held open, repeated delayed full reloads and subsequent writer remain "
              "detectable");
    TestEditor::Append(frame, "dirty live log");
    decision.choice = wxID_NO;
    Peer(TestFiles::Filename(stream), "declined dirty log\n");
    Wait([&decision, &streamPrompts] { return decision.prompts == streamPrompts + 1; });
    Require(TestEditor::Text(frame->Editor()) == "after continuous writer\ndirty live log" &&
                frame->Editor()->Send(Scintilla::Message::GetModify),
            "modified log still asks and No retains live edits");
    decision.choice = wxID_YES;
    Peer(TestFiles::Filename(stream), "approved dirty log\n");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "approved dirty log\n"; });
    Require(decision.prompts == streamPrompts + 2 && !frame->Editor()->Send(Scintilla::Message::GetModify),
            "modified log Yes performs one clean full reload");
    decision.choice = wxID_NO;
    Peer(TestFiles::Filename(stream), "", "remove");
    Wait([&decision, &streamPrompts] { return decision.prompts == streamPrompts + 3; });
    Require(TestEditor::Text(frame->Editor()) == "approved dirty log\n", "deleted log No retains live text");
    Peer(TestFiles::Filename(stream), "after continuous writer\n");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "after continuous writer\n"; });
    std::puts("PASS: log mode retains native dirty Yes/No and deleted-file decisions");
    const auto count = decision.prompts;
    options.autoReloadDelay = 300;
    frame->SetFileWatchingOptions(options);
    Peer(TestFiles::Filename(stream), "pending disabled reload\n");
    Pause(50);
    options.mode = FileWatchingMode::None;
    frame->SetFileWatchingOptions(options);
    Pause(350);
    Require(TestEditor::Text(frame->Editor()) == "after continuous writer\n" && decision.prompts == count,
            "disabling monitoring cancels an already delayed change");
    options.mode = FileWatchingMode::AutoReload;
    frame->SetFileWatchingOptions(options);
    Peer(TestFiles::Filename(stream), "pending closing reload\n");
    Pause(50);
    frame->Close();
    frame = nullptr;
    Pause(350);
    Require(decision.prompts == count, "closing cancels the pending reload and produces no late decision");
    std::puts("PASS: first save installs monitoring; disable and close cancel delayed changes");
}

void CheckFileChangeScenarios(EditorFrame*& frame, const std::filesystem::path& directory) {
    Require(frame->FileWatching().mode == FileWatchingMode::AutoReload && frame->FileWatching().checkInterval == 1000 &&
                frame->FileWatching().autoReloadDelay == 1000,
            "original default is automatic unmodified reload with 1000 ms interval and delay");
    frame->SetFileWatchingOptions({FileWatchingMode::AutoReload, 20, 50});
    std::filesystem::create_directory(directory);
    const auto path = directory / "watched.txt";
    std::ofstream(path, std::ios::binary) << "original\n";
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "fixture opens");
    frame->Show();
    ChangeDecision decision;
    Peer(TestFiles::Filename(path), "external replacement\n");
    Require(TestFiles::Bytes(path) == "external replacement\n",
            "independent writer must produce the requested fixture bytes");
    Wait([&frame] { return TestEditor::Text(frame->Editor()) == "external replacement\n"; });
    Require(!frame->Editor()->Send(Scintilla::Message::GetModify), "automatic reload is clean");
    std::puts("PASS: independent process change automatically reloads clean content");
    auto options = frame->FileWatching();
    const auto savedAs = CheckMessageDecisionsAndFileLifecycle(frame, directory, path, decision, options);
    CheckNativeOptionsAndReloadFailures(frame, savedAs, decision, options);
    const auto firstSave = CheckFirstSaveAndLogTail(frame, directory, decision, options);
    CheckResetTransitions(frame, directory, firstSave, decision, options);
    CheckContinuousLogAndShutdown(frame, directory, decision, options);
}

class FileChangeApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        if (argc == 5 && wxString(argv[1]) == "--peer") {
            RunPeer();
            peer_ = true;
            return true;
        }
        CallAfter([this] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-watch");
            auto* frame = new EditorFrame;
            try {
                CheckFileChangeScenarios(frame, directory);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            if (frame) {
                frame->SetFileWatchingOptions({FileWatchingMode::None});
                frame->Destroy();
            }
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
    void RunPeer() {
        const auto path = TestFiles::Utf8Path(wxString(argv[2]).ToStdString(wxConvUTF8));
        const auto action = wxString(argv[4]);
        if (action == "stream") {
            std::ofstream output(path, std::ios::binary);
            output << "entry 1\n" << std::flush;
            std::ofstream(std::filesystem::path(path).concat(".ready")) << "ready";
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            for (int i = 2; i <= 12; ++i) {
                output << "entry " << i << '\n' << std::flush;
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            output.close();
            std::ofstream(std::filesystem::path(path).concat(".done")) << "done";
        } else if (action == "block") {
            std::filesystem::remove(path);
            std::filesystem::create_directory(path);
        } else if (action == "remove") {
            std::filesystem::remove(path);
        } else {
            const auto target = action == "replace" ? std::filesystem::path(path).concat(".peer") : path;
            {
                std::ofstream output(target, std::ios::binary);
                const auto text = wxString(argv[3]).ToStdString(wxConvUTF8);
                output.write(text.data(), static_cast<std::streamsize>(text.size()));
            }
            if (action == "replace" && !wxRenameFile(TestFiles::Filename(target), TestFiles::Filename(path), true))
                throw std::runtime_error("independent replacement must succeed");
        }
    }
    bool peer_ = false;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(FileChangeApp);
