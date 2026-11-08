#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <memory>
#include <wx/wx.h>
#include <wx/statusbr.h>
#include <wx/timer.h>
#include "native_dialog_helpers.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
using TestEditor::Append;
using TestFiles::Bytes;
using TestFiles::Filename;
#ifdef __WXGTK__
using NativeDialogs::Respond;
bool Click(GtkWidget* widget, const char* label) {
    if (GTK_IS_BUTTON(widget)) {
        const char* text = gtk_button_get_label(GTK_BUTTON(widget));
        if (text && std::string(text).find(label) != std::string::npos) {
            gtk_button_clicked(GTK_BUTTON(widget));
            return true;
        }
    }
    if (!GTK_IS_CONTAINER(widget)) return false;
    GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
    bool clicked = false;
    for (GList* item = children; item && !clicked; item = item->next)
        clicked = Click(GTK_WIDGET(item->data), label);
    g_list_free(children);
    return clicked;
}
#endif

// Activate only actual native widgets owned by this test process. No global input.
class NativeDriver final : public wxTimer {
public:
    explicit NativeDriver(wxString destination = {}, bool replace = true, bool blockWrites = false)
        : destination_(destination), replace_(replace), blockWrites_(blockWrites) {
        Start(150);
    }
    int choosers = 0;
    int copyChoosers = 0;
    int confirmations = 0;
    int errors = 0;
    wxString selectedType;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                return reinterpret_cast<NativeDriver*>(data)->HandleWindow(window);
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget)) continue;
            if (GTK_IS_FILE_CHOOSER(widget))
                HandleChooser(widget);
            else if (GTK_IS_MESSAGE_DIALOG(widget))
                HandleMessage(widget);
        }
        g_list_free(windows);
#endif
    }

private:
#ifdef __WXGTK__
    void HandleChooser(GtkWidget* widget) {
        if (stage_ == 0) {
            stage_ = 1;
            if (wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "Save Copy")
                ++copyChoosers;
            else
                ++choosers;
            if (!selectedType.empty()) NativeDialogs::ChooseFileType(widget, selectedType);
            NativeDialogs::ChooseFile(widget, destination_);
        } else if (cancelChooser_)
            Respond(widget, GTK_RESPONSE_CANCEL);
        else if (stage_ == 1 && !destination_.empty() && NativeDialogs::CanAcceptFile(widget, destination_)) {
            stage_ = 2;
            Respond(widget, GTK_RESPONSE_ACCEPT);
        }
    }
    void HandleMessage(GtkWidget* widget) {
        if (wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "Could not save file") {
            ++errors;
            Respond(widget, GTK_RESPONSE_OK);
        } else {
            ++confirmations;
            if (!replace_) cancelChooser_ = true;
            if (blockWrites_)
                std::filesystem::permissions(std::filesystem::path(destination_.ToStdString()).parent_path(),
                                             std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
            Require(Click(widget, replace_ ? "Replace" : "Cancel"),
                    "overwrite confirmation must expose Replace/Cancel");
        }
    }
#endif
#ifdef __WXMSW__
    BOOL HandleWindow(HWND window) {
        if (!IsWindowVisible(window)) return TRUE;
        wchar_t title[256]{};
        GetWindowTextW(window, title, 256);
        wchar_t ownerTitle[256]{};
        GetWindowTextW(GetWindow(window, GW_OWNER), ownerTitle, 256);
        if (wxString(title) == "Save As" || wxString(title) == "Save file" || wxString(title) == "Save Copy") {
            if (!stage_++) {
                if (wxString(title) == "Save Copy")
                    ++copyChoosers;
                else
                    ++choosers;
                if (!selectedType.empty()) NativeDialogs::ChooseFileType(window, selectedType);
                NativeDialogs::ChooseFile(window, destination_);
            } else if (cancelChooser_)
                SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
        } else if (wxString(ownerTitle) == "Save As" || wxString(ownerTitle) == "Save file") {
            ++confirmations;
            if (!replace_) cancelChooser_ = true;
            SendMessageW(window, WM_USER + 102, replace_ ? IDYES : IDNO, 0); // TDM_CLICK_BUTTON
        } else if (wxString(title) == "Could not save file") {
            ++errors;
            SendMessageW(window, WM_USER + 102, IDOK, 0);
        }
        return TRUE;
    }
#endif
    wxString destination_;
    bool replace_;
    bool blockWrites_;
    int stage_ = 0;
    bool cancelChooser_ = false;
};
void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "menu command must be handled");
}
void Shortcut(EditorFrame* frame, int key, bool control = false, bool shift = false, bool alt = false) {
    auto* editor = frame->Editor();
    editor->SetFocus();
    Require(wxWindow::FindFocus() == editor, "native Save As shortcuts must start with the editor focused");
#ifdef __WXMSW__
    BYTE previous[256];
    Require(GetKeyboardState(previous), "must read this test thread's keyboard state");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    modified[VK_CONTROL] = control ? 0x80 : 0;
    modified[VK_SHIFT] = shift ? 0x80 : 0;
    modified[VK_MENU] = alt ? 0x80 : 0;
    modified[VK_LWIN] = modified[VK_RWIN] = 0;
    Require(SetKeyboardState(modified), "must set this test thread's shortcut modifiers");
    MSG message{};
    message.hwnd = static_cast<HWND>(editor->GetHandle());
    message.message = WM_KEYDOWN;
    message.wParam = key == WXK_F6 ? VK_F6 : key;
    frame->MSWTranslateMessage(&message);
    SetKeyboardState(previous);
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = key == WXK_F6 ? GDK_KEY_F6 : GDK_KEY_s;
    event->key.state = (control ? GDK_CONTROL_MASK : 0U) | (shift ? GDK_SHIFT_MASK : 0U) | (alt ? GDK_MOD1_MASK : 0U);
    GdkKeymapKey* keys = nullptr;
    gint count = 0;
    auto* keymap = gdk_keymap_get_for_display(gtk_widget_get_display(widget));
    if (gdk_keymap_get_entries_for_keyval(keymap, event->key.keyval, &keys, &count) && count > 0) {
        event->key.hardware_keycode = keys[0].keycode;
        event->key.group = keys[0].group;
    }
    g_free(keys);
    gtk_widget_event(GTK_WIDGET(frame->GetHandle()), event);
    gdk_event_free(event);
#endif
    wxYield();
}
class SaveNativeApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        // Mixed-EOL warning choices are covered by mixed_line_ending_open.
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-save-native");
            try {
                std::filesystem::create_directory(directory);
                const auto source = directory / "source.txt";
                const auto copy = directory / "copy.txt";
                {
                    std::ofstream(source, std::ios::binary) << "\xef\xbb\xbf"
                                                               "Native 中文\r\nsecond\nthird\r";
                }
                Require(frame->Open(Filename(source)).status == OpenStatus::Opened, "source must open");
                const auto choose = [frame](const char* language) {
                    Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", language));
                };
                const auto hasOverride = [frame](const char* language) {
                    return frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Scheme", language)) &&
                           frame->GetStatusBar()->GetStatusText().Contains("override");
                };
                choose("C/C++");
                Append(frame, "pending");
                Require(frame->GetMenuBar()->FindItem(wxID_SAVEAS), "File menu must expose Save As");
                const std::unique_ptr<wxAcceleratorEntry> saveAccelerator(
                    frame->GetMenuBar()->FindItem(wxID_SAVE)->GetAccel());
                Require(saveAccelerator && saveAccelerator->GetKeyCode() == 'S' &&
                            saveAccelerator->GetFlags() == wxACCEL_CTRL,
                        "Save must bind Ctrl+S");
                const std::unique_ptr<wxAcceleratorEntry> accelerator(
                    frame->GetMenuBar()->FindItem(wxID_SAVEAS)->GetAccel());
                Require(accelerator && accelerator->GetKeyCode() == WXK_F6 && accelerator->GetFlags() == wxACCEL_NORMAL,
                        "Save As must bind unmodified F6");
                NativeDriver cancel;
                Shortcut(frame, WXK_F6);
                cancel.Stop();
                Require(cancel.choosers == 1 && frame->DocumentPath() == Filename(source) &&
                            frame->Editor()->Send(Scintilla::Message::GetModify) && hasOverride("C/C++"),
                        "native cancellation must retain identity/dirty state");
                const auto selection = frame->Editor()->Send(Scintilla::Message::GetCurrentPos);
                for (const auto modifiers : {1, 2, 4, 3, 5, 6, 7}) {
                    NativeDriver untouched;
                    Shortcut(frame, WXK_F6, (modifiers & 1) != 0, (modifiers & 2) != 0, (modifiers & 4) != 0);
                    untouched.Stop();
                    if (untouched.choosers != 0) std::fprintf(stderr, "Unexpected Save As modifiers: %d\n", modifiers);
                    Require(untouched.choosers == 0, "modified F6 must not invoke Save As");
                    Require(untouched.copyChoosers == (modifiers == 1 ? 1 : 0),
                            "exact Ctrl+F6 must invoke Save Copy and other modified F6 must not");
                }
                NativeDriver formerBinding;
                Shortcut(frame, 'S', true, true);
                formerBinding.Stop();
                Require(formerBinding.choosers == 0, "Ctrl+Shift+S must not invoke Save As");
                Require(frame->DocumentPath() == Filename(source) && frame->DocumentHasUtf8Bom() &&
                            frame->Editor()->Send(Scintilla::Message::GetModify) && hasOverride("C/C++") &&
                            frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == selection &&
                            TestEditor::Text(frame->Editor()) == "Native 中文\r\nsecond\nthird\rpending" &&
                            Bytes(source) == "\xef\xbb\xbf"
                                             "Native 中文\r\nsecond\nthird\r",
                        "cancelled F6 and rejected modifiers must preserve document, selection and disk bytes");
                std::puts("PASS: native F6 cancellation and exact modifiers retain document state");
                NativeDriver accept(Filename(copy));
                Command(frame, wxID_SAVEAS);
                accept.Stop();
                Require(accept.choosers == 1 && accept.confirmations == 0 &&
                            NativeDialogs::PathEquals(frame->DocumentPath(), Filename(copy)),
                        "native new target must update identity");
                Require(hasOverride("C/C++"),
                        "native Save As default current type must retain C++ despite the returned .txt name");
                Require(Bytes(copy) == "\xef\xbb\xbf"
                                       "Native 中文\r\nsecond\nthird\rpending" &&
                            Bytes(source) == "\xef\xbb\xbf"
                                             "Native 中文\r\nsecond\nthird\r",
                        "native Save As must retain exact bytes and original source");
                std::puts("PASS: native Save As menu/accelerator, picker Cancel and new target");
                Append(frame, " overwrite");
                choose("Python");
                const auto target = directory / "overwrite.txt";
                {
                    std::ofstream(target, std::ios::binary) << "sentinel";
                }
                NativeDriver reject(Filename(target), false);
                reject.selectedType = "JSON";
                Command(frame, wxID_SAVEAS);
                reject.Stop();
                Require(reject.choosers == 1 && reject.confirmations == 1 && Bytes(target) == "sentinel",
                        "declining overwrite must retain target");
                Require(frame->DocumentPath() == Filename(copy) &&
                            frame->Editor()->Send(Scintilla::Message::GetModify) && hasOverride("Python"),
                        "declining overwrite must retain session");
                NativeDriver overwrite(Filename(target));
                Command(frame, wxID_SAVEAS);
                overwrite.Stop();
                Require(overwrite.confirmations == 1 && frame->DocumentPath() == Filename(target) &&
                            Bytes(target) == "\xef\xbb\xbf"
                                             "Native 中文\r\nsecond\nthird\rpending overwrite",
                        "confirmed overwrite must save current bytes");
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify),
                        "confirmed overwrite must establish save point");
                std::puts("PASS: native overwrite refusal and acceptance");
                choose("Plain Text");
                NativeDriver samePath(Filename(target));
                Command(frame, wxID_SAVEAS);
                samePath.Stop();
                Require(samePath.choosers == 1 && samePath.confirmations == 1 && hasOverride("Plain Text"),
                        "explicit native Save As to the same pathname must retain the selected current Plain type");
                choose("JSON");
                Append(frame, " failure");
#ifdef __WXMSW__
                HANDLE sourceLock = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                                FILE_ATTRIBUTE_NORMAL, nullptr);
                Require(sourceLock != INVALID_HANDLE_VALUE, "must lock Save As target against replacement");
#endif
                NativeDriver failedAs(Filename(source), true, true);
                failedAs.selectedType = "C/C++";
                Command(frame, wxID_SAVEAS);
                failedAs.Stop();
#ifdef __WXMSW__
                CloseHandle(sourceLock);
#else
                std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
#endif
                Require(failedAs.choosers == 1 && failedAs.confirmations == 1 && failedAs.errors == 1,
                        "failed native Save As must show the shared save error after approved overwrite");
                Require(frame->DocumentPath() == Filename(target) && frame->DocumentHasUtf8Bom() &&
                            frame->Editor()->Send(Scintilla::Message::GetModify) && hasOverride("JSON"),
                        "failed native Save As must retain identity, BOM and dirty state");
                Require(Bytes(source) == "\xef\xbb\xbf"
                                         "Native 中文\r\nsecond\nthird\r",
                        "failed native Save As must retain destination bytes");
                std::puts("PASS: native Save As write failure preserves original identity and target");
#ifdef __WXMSW__
                HANDLE lock = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
                Require(lock != INVALID_HANDLE_VALUE, "must lock existing target against replacement");
#else
                std::filesystem::permissions(directory,
                                             std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
#endif
                NativeDriver failure;
                Command(frame, wxID_SAVE);
                failure.Stop();
#ifdef __WXMSW__
                CloseHandle(lock);
#else
                std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
#endif
                Require(failure.errors == 1 && failure.choosers == 0 && frame->DocumentPath() == Filename(target) &&
                            frame->Editor()->Send(Scintilla::Message::GetModify) && hasOverride("JSON"),
                        "failed native Save must show error and retain identity/dirty state");
                Require(Bytes(target) == "\xef\xbb\xbf"
                                         "Native 中文\r\nsecond\nthird\rpending overwrite",
                        "failed Save must preserve target bytes");
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify),
                        "native failure must preserve Undo save point");
                frame->Editor()->Send(Scintilla::Message::Redo);
                Require(frame->Editor()->Send(Scintilla::Message::GetModify), "native failure must preserve Redo text");
                std::puts("PASS: native Save failure error and retained disk/session/history");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
            std::filesystem::remove_all(directory);
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
wxIMPLEMENT_APP(SaveNativeApp);
