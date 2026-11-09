#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "file_command_driver.h"
#include "editor_test_helpers.h"
#include <wx/wx.h>
#include <wx/menu.h>
#include <wx/checkbox.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/listctrl.h>
#include <wx/weakref.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class CancelHistory final : public wxTimer {
public:
    CancelHistory() { Start(20); }
    bool visited = false;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || dialog->GetTitle() != "Recent Files" || !dialog->IsShown()) continue;
            visited = true;
            auto* remember = wxDynamicCast(dialog->FindWindowByName("Remember recent files", dialog), wxCheckBox);
            auto* capacity = wxDynamicCast(dialog->FindWindowByName("Recent file capacity", dialog), wxTextCtrl);
            if (remember) remember->SetValue(true);
            if (capacity) capacity->SetValue("8");
            dialog->EndModal(wxID_CANCEL);
            Stop();
            return;
        }
    }
};
class ChangeRememberHistory final : public wxTimer {
public:
    ChangeRememberHistory() { Start(20); }
    bool initiallyRemembered = false;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || dialog->GetTitle() != "Recent Files" || !dialog->IsShown()) continue;
            auto* remember = wxDynamicCast(dialog->FindWindowByName("Remember recent files", dialog), wxCheckBox);
            initiallyRemembered = remember && remember->GetValue();
            if (remember) remember->SetValue(false);
            dialog->EndModal(wxID_CANCEL);
            Stop();
            return;
        }
    }
};
class ClearHistory final : public wxTimer {
public:
    ClearHistory() { Start(20); }
    bool cleared = false;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || dialog->GetTitle() != "Recent Files" || !dialog->IsShown()) continue;
            auto* clear = dialog->FindWindow(wxID_CLEAR);
            auto* list = wxDynamicCast(dialog->FindWindowByName("Recent files", dialog), wxListCtrl);
            if (clear && list) {
                wxCommandEvent click(wxEVT_BUTTON, wxID_CLEAR);
                clear->GetEventHandler()->ProcessEvent(click);
                cleared = list->GetItemCount() == 1 && list->GetSelectedItemCount() == 0 &&
                          !dialog->FindWindow(wxID_OK)->IsEnabled();
            }
            dialog->EndModal(wxID_CANCEL);
            Stop();
            return;
        }
    }
};
class SelectHistory final : public wxTimer {
public:
    explicit SelectHistory(long index, bool cancelAfterRefresh = false)
        : index_(index), cancelAfterRefresh_(cancelAfterRefresh) {
        Start(20);
    }
    bool emptied = false;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || dialog->GetTitle() != "Recent Files" || !dialog->IsShown()) continue;
            auto* list = wxDynamicCast(dialog->FindWindowByName("Recent files", dialog), wxListCtrl);
            if (!list) return;
            if (!selected_) {
                selected_ = true;
                list->SetItemState(index_, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                   wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
                wxListEvent activate(wxEVT_LIST_ITEM_ACTIVATED, list->GetId());
                list->GetEventHandler()->ProcessEvent(activate);
            } else if (cancelAfterRefresh_ && list->GetItemCount() == 0) {
                emptied = list->GetItemCount() == 0 && !dialog->FindWindow(wxID_OK)->IsEnabled();
                dialog->EndModal(wxID_CANCEL);
                Stop();
            }
            return;
        }
    }

private:
    long index_;
    bool cancelAfterRefresh_;
    bool selected_ = false;
};
class RemoveMissing final : public wxTimer {
public:
    RemoveMissing() { Start(10); }
    int prompts = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                wchar_t title[256]{};
                GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                if (IsWindowVisible(window) && wxString(title) == "Remove recent file?") {
                    ++reinterpret_cast<RemoveMissing*>(data)->prompts;
                    SendMessageW(window, TDM_CLICK_BUTTON, IDYES, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget)) continue;
            if (!GTK_IS_MESSAGE_DIALOG(widget)) continue;
            if (wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "Remove recent file?") {
                ++prompts;
                NativeDialogs::Respond(widget, GTK_RESPONSE_YES);
            }
        }
        g_list_free(windows);
#endif
    }
};
class HistorySnapshot final : public wxTimer {
public:
    HistorySnapshot() { Start(20); }
    bool visited = false;
    std::vector<wxString> paths;
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || dialog->GetTitle() != "Recent Files" || !dialog->IsShown()) continue;
            visited = true;
            auto* list = wxDynamicCast(dialog->FindWindowByName("Recent files", dialog), wxListCtrl);
            for (long index = 0; list && index < list->GetItemCount(); ++index)
                paths.push_back(list->GetItemText(index));
            dialog->EndModal(wxID_CANCEL);
            Stop();
            return;
        }
    }
};
class RecentFilesApp final : public wxApp {
public:
    bool OnInit() override {
        // Keep the blocking native command visible in CTest's timeout output.
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        SetExitOnFrameDelete(false);
        if (argc > 1 && argv[1] == "--history") {
            CallAfter([this] { CheckRestoredHistory(); });
            return true;
        }
        CallAfter([this] { CheckFileHistoryLifecycle(); });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    void CheckRestoredHistory() {
        SettingsStore settings;
        auto* frame = new EditorFrame(&settings);
        try {
            std::vector<wxString> expected;
            for (int index = 2; index < argc; ++index)
                expected.emplace_back(argv[index]);
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            const int recent = frame->GetMenuBar()->FindMenuItem("File", "Recent (History)...");
            Require(frame->GetMenuBar()->IsEnabled(recent) == !expected.empty(), "restart menu availability");
            HistorySnapshot snapshot;
            std::puts("recent-files: restart chooser");
            frame->ShowRecentFiles();
            snapshot.Stop();
            if (snapshot.paths != expected) {
                for (const auto& path : snapshot.paths)
                    std::fprintf(stderr, "observed history: %s\n", path.utf8_str().data());
                for (const auto& path : expected)
                    std::fprintf(stderr, "expected history: %s\n", path.utf8_str().data());
            }
            Require(snapshot.paths == expected && snapshot.visited == !expected.empty(),
                    "restart chooser history order");
            result_ = 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: %s\n", error.what());
        }
        frame->Destroy();
        ExitMainLoop();
    }

    void CheckFileHistoryLifecycle() {
        const auto directory = TestFiles::TemporaryDirectory("wxnotepad-recent");
        SettingsStore settings;
        settings.values.recentFiles.clear();
        settings.values.warnLineEndings = false;
        settings.values.saveRecentFiles = false;
        std::puts("recent-files: construct frame");
        auto* frame = new EditorFrame(&settings);
        try {
            std::puts("recent-files: file actions");
            std::filesystem::create_directory(directory);
            const auto first = directory / "first.txt";
            std::ofstream(first) << "first";
            const int recent = frame->GetMenuBar()->FindMenuItem("File", "Recent (History)...");
            Require(recent != wxNOT_FOUND, "File menu must expose Recent (History)");
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            Require(!frame->GetMenuBar()->IsEnabled(recent), "empty history must disable Recent");
            Require(frame->Open(TestFiles::Filename(first)).status == OpenStatus::Opened, "open first file");
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            Require(frame->GetMenuBar()->IsEnabled(recent), "successful open must enable Recent");
            const auto second = directory / "second.txt";
            Require(frame->SaveAs(TestFiles::Filename(second)).status == SaveStatus::Saved, "save as second file");
            Require(settings.values.recentFiles ==
                        std::vector<wxString>{TestFiles::Filename(second), TestFiles::Filename(first)},
                    "ordinary save must become the newest history entry");
            Require(frame->SaveCopy(TestFiles::Filename(directory / "copy.txt")).status == SaveStatus::Saved,
                    "save copy succeeds");
            Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled, "cancel save as");
            Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled, "cancel open");
            Require(frame->Open(TestFiles::Filename(directory / "missing.txt")).status == OpenStatus::Failed,
                    "missing open fails");
            Require(settings.values.recentFiles ==
                        std::vector<wxString>{TestFiles::Filename(second), TestFiles::Filename(first)},
                    "copy, cancelled and failed operations preserve history");
            Require(frame->Open(TestFiles::Filename(first)).status == OpenStatus::Opened, "reopen first file");
            Require(settings.values.recentFiles ==
                        std::vector<wxString>{TestFiles::Filename(first), TestFiles::Filename(second)},
                    "reopening deduplicates and reorders history");
            const int remember = frame->GetMenuBar()->FindMenuItem("Settings", "Remember Recent Files");
            Require(remember != wxNOT_FOUND, "Other Settings must expose Remember Recent Files");
            auto* settingsMenu = frame->GetMenuBar()->GetMenu(frame->GetMenuBar()->FindMenu("Settings"));
            const int otherId = settingsMenu->FindItem("Other Settings");
            Require(otherId != wxNOT_FOUND &&
                        settingsMenu->FindItem(otherId)->GetSubMenu()->FindItem("Remember Recent Files") == remember,
                    "Remember Recent Files must be placed in Settings > Other Settings");
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            Require(!frame->GetMenuBar()->IsChecked(remember), "remember menu reflects shared Off preference");
            const int capacity = settings.values.maxRecentFiles;
            wxCommandEvent toggleRemember(wxEVT_MENU, remember);
            frame->GetEventHandler()->ProcessEvent(toggleRemember);
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            Require(frame->GetMenuBar()->IsChecked(remember) && settings.values.saveRecentFiles &&
                        settings.values.maxRecentFiles == capacity,
                    "remember menu toggles shared preference without changing capacity");
            ChangeRememberHistory changeRemember;
            std::puts("recent-files: menu and chooser preference coherence");
            frame->ShowRecentFiles();
            changeRemember.Stop();
            Require(changeRemember.initiallyRemembered && !frame->GetMenuBar()->IsChecked(remember) &&
                        !settings.values.saveRecentFiles && settings.values.maxRecentFiles == capacity,
                    "dialog reads menu preference and Cancel updates the same menu check state without changing "
                    "capacity");
            CancelHistory cancel;
            std::puts("recent-files: cancel chooser and retain options");
            frame->ShowRecentFiles();
            cancel.Stop();
            Require(cancel.visited && settings.values.saveRecentFiles && settings.values.maxRecentFiles == 32,
                    "Cancel must retain remember preference and minimum history capacity");
            ClearHistory clear;
            std::puts("recent-files: clear history");
            frame->ShowRecentFiles();
            clear.Stop();
            Require(clear.cleared && settings.values.recentFiles == std::vector<wxString>{TestFiles::Filename(first)},
                    "Clear must re-add current named file with no selection and disabled OK");
            Require(settings.Save(false), "persist named current entry before removing missing file");
            settings.values.saveRecentFiles = false;
            std::filesystem::remove(first);
            RemoveMissing remove;
            NativeDialogs::FileCommandDriver errors(NativeDialogs::FileDialogProfile::Open, wxID_NO);
            SelectHistory missing(0, true);
            std::puts("recent-files: remove missing entry");
            frame->ShowRecentFiles();
            missing.Stop();
            errors.Stop();
            remove.Stop();
            Require(remove.prompts == 1 && missing.emptied && settings.values.recentFiles.empty(),
                    "missing selection prompts removal, refreshes list and disables OK");
            Require(TestFiles::Bytes(std::filesystem::path(settings.Path().ToStdWstring()))
                            .find(std::string("f\0i\0r\0s\0t\0.\0t\0x\0t\0", 18)) == std::string::npos,
                    "missing removal updates persisted history even with remember Off");
            std::ofstream(first) << "first";
            Require(frame->Open(TestFiles::Filename(first)).status == OpenStatus::Opened, "reopen recreated first");
            Require(frame->Open(TestFiles::Filename(second)).status == OpenStatus::Opened,
                    "open second before chooser");
            TestEditor::Append(frame, " changed");
            NativeDialogs::FileCommandDriver saveBeforeCancel(NativeDialogs::FileDialogProfile::Open, wxID_YES);
            CancelHistory savedCancel;
            std::puts("recent-files: save before chooser cancel");
            frame->ShowRecentFiles();
            savedCancel.Stop();
            saveBeforeCancel.Stop();
            Require(savedCancel.visited && saveBeforeCancel.prompts == 1 &&
                        TestFiles::Bytes(second) == "first changed" &&
                        !frame->Editor()->Send(Scintilla::Message::GetModify),
                    "save decision precedes chooser and remains effective after chooser Cancel");
            TestEditor::Append(frame, " discarded");
            NativeDialogs::FileCommandDriver discardBeforeOpen(NativeDialogs::FileDialogProfile::Open, wxID_NO);
            SelectHistory chooseFirst(1);
            std::puts("recent-files: discard before selected open");
            frame->ShowRecentFiles();
            chooseFirst.Stop();
            discardBeforeOpen.Stop();
            Require(discardBeforeOpen.prompts == 1 && frame->DocumentPath() == TestFiles::Filename(first) &&
                        TestEditor::Text(frame->Editor()) == "first",
                    "double-click opens chosen entry after one discard decision");
            HistorySnapshot shortcut;
            wxKeyEvent historyKey(wxEVT_CHAR_HOOK);
            historyKey.m_keyCode = 'H';
            historyKey.SetAltDown(true);
            std::puts("recent-files: Alt+H chooser");
            frame->GetEventHandler()->ProcessEvent(historyKey);
            shortcut.Stop();
            Require(shortcut.visited && shortcut.paths == settings.values.recentFiles,
                    "Alt+H presents the same ordered native history chooser");
            TestEditor::Append(frame, " pending");
            NativeDialogs::FileCommandDriver cancelDecision(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL);
            HistorySnapshot notOpened;
            std::puts("recent-files: cancel save decision");
            frame->ShowRecentFiles();
            cancelDecision.Stop();
            notOpened.Stop();
            Require(cancelDecision.prompts == 1 && !notOpened.visited &&
                        TestEditor::Text(frame->Editor()) == "first pending" &&
                        frame->DocumentPath() == TestFiles::Filename(first),
                    "cancelled save decision retains document and prevents history chooser");
            std::puts("PASS: Recent command tracks available file history");
            result_ = 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: %s\n", error.what());
        }
        // Destroy is deferred: release the native watcher before removing its directory.
        wxWeakRef<EditorFrame> closing = frame;
        frame->Destroy();
        while (closing)
            ProcessIdle();
        std::puts("recent-files: frame destroyed, removing fixture");
        std::filesystem::remove_all(directory);
        ExitMainLoop();
    }
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(RecentFilesApp);
