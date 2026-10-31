#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <wx/timer.h>
#include "native_dialog_helpers.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    Require(input.is_open(), "saved file must exist");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void Add(EditorFrame* frame, const std::string& text) {
    auto* editor = frame->Editor();
    editor->Send(Scintilla::Message::GotoPos, editor->Send(Scintilla::Message::GetLength));
    editor->Send(Scintilla::Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
}
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
    int confirmations = 0;
    int errors = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<NativeDriver*>(data);
                if (!IsWindowVisible(window)) return TRUE;
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                wchar_t ownerTitle[256]{};
                GetWindowTextW(GetWindow(window, GW_OWNER), ownerTitle, 256);
                if (wxString(title) == "Save As" || wxString(title) == "Save file") {
                    if (!driver.stage_++) {
                        ++driver.choosers;
                        NativeDialogs::ChooseFile(window, driver.destination_);
                    } else if (driver.cancelChooser_)
                        SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
                } else if (wxString(ownerTitle) == "Save As" || wxString(ownerTitle) == "Save file") {
                    ++driver.confirmations;
                    if (!driver.replace_) driver.cancelChooser_ = true;
                    SendMessageW(window, WM_USER + 102, driver.replace_ ? IDYES : IDNO, 0); // TDM_CLICK_BUTTON
                } else if (wxString(title) == "Could not save file") {
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
            if (!gtk_widget_get_visible(widget)) continue;
            if (GTK_IS_FILE_CHOOSER(widget)) {
                if (!stage_++) {
                    ++choosers;
                    NativeDialogs::ChooseFile(widget, destination_);
                } else if (cancelChooser_)
                    Respond(widget, GTK_RESPONSE_CANCEL);
                else if (stage_ == 2 && !destination_.empty())
                    Respond(widget, GTK_RESPONSE_ACCEPT);
            } else if (GTK_IS_MESSAGE_DIALOG(widget)) {
                if (wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "Could not save file") {
                    ++errors;
                    Respond(widget, GTK_RESPONSE_OK);
                } else {
                    ++confirmations;
                    if (!replace_) cancelChooser_ = true;
                    if (blockWrites_)
                        std::filesystem::permissions(std::filesystem::path(destination_.ToStdString()).parent_path(),
                                                     std::filesystem::perms::owner_read |
                                                         std::filesystem::perms::owner_exec);
                    Require(Click(widget, replace_ ? "Replace" : "Cancel"),
                            "overwrite confirmation must expose Replace/Cancel");
                }
            }
        }
        g_list_free(windows);
#endif
    }

private:
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
class SaveNativeApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-save-native-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto source = directory / "source.txt";
                const auto copy = directory / "copy.txt";
                {
                    std::ofstream(source, std::ios::binary) << "\xef\xbb\xbf"
                                                               "Native 中文\r\nsecond\nthird\r";
                }
                Require(frame->Open(Filename(source)).status == OpenStatus::Opened, "source must open");
                Add(frame, "pending");
                Require(frame->GetMenuBar()->FindItem(wxID_SAVEAS), "File menu must expose Save As");
                const auto* saveAccelerator = frame->GetMenuBar()->FindItem(wxID_SAVE)->GetAccel();
                Require(saveAccelerator && saveAccelerator->GetKeyCode() == 'S' &&
                            saveAccelerator->GetFlags() == wxACCEL_CTRL,
                        "Save must bind Ctrl+S");
                delete saveAccelerator;
                const auto* accelerator = frame->GetMenuBar()->FindItem(wxID_SAVEAS)->GetAccel();
                Require(accelerator && accelerator->GetKeyCode() == 'S' &&
                            accelerator->GetFlags() == (wxACCEL_CTRL | wxACCEL_SHIFT),
                        "Save As must bind Ctrl+Shift+S");
                delete accelerator;
                NativeDriver cancel;
                Command(frame, wxID_SAVEAS);
                cancel.Stop();
                Require(cancel.choosers == 1 && frame->DocumentPath() == Filename(source) &&
                            frame->Editor()->Send(Scintilla::Message::GetModify),
                        "native cancellation must retain identity/dirty state");
                NativeDriver accept(Filename(copy));
                Command(frame, wxID_SAVEAS);
                accept.Stop();
                Require(accept.choosers == 1 && accept.confirmations == 0 && frame->DocumentPath() == Filename(copy),
                        "native new target must update identity");
                Require(Bytes(copy) == "\xef\xbb\xbf"
                                       "Native 中文\r\nsecond\nthird\rpending" &&
                            Bytes(source) == "\xef\xbb\xbf"
                                             "Native 中文\r\nsecond\nthird\r",
                        "native Save As must retain exact bytes and original source");
                std::puts("PASS: native Save As menu/accelerator, picker Cancel and new target");
                Add(frame, " overwrite");
                const auto target = directory / "overwrite.txt";
                {
                    std::ofstream(target, std::ios::binary) << "sentinel";
                }
                NativeDriver reject(Filename(target), false);
                Command(frame, wxID_SAVEAS);
                reject.Stop();
                Require(reject.choosers == 1 && reject.confirmations == 1 && Bytes(target) == "sentinel",
                        "declining overwrite must retain target");
                Require(frame->DocumentPath() == Filename(copy) && frame->Editor()->Send(Scintilla::Message::GetModify),
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
                Add(frame, " failure");
#ifdef __WXMSW__
                HANDLE sourceLock = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                                FILE_ATTRIBUTE_NORMAL, nullptr);
                Require(sourceLock != INVALID_HANDLE_VALUE, "must lock Save As target against replacement");
#endif
                NativeDriver failedAs(Filename(source), true, true);
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
                            frame->Editor()->Send(Scintilla::Message::GetModify),
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
                            frame->Editor()->Send(Scintilla::Message::GetModify),
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SaveNativeApp);
