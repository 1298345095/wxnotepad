#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include <wx/wx.h>
#include <wx/timer.h>
#include <wx/statusbr.h>
#include "native_dialog_helpers.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
wxString Filename(const std::filesystem::path& path) {
#ifdef __WXMSW__
    return wxString(path.wstring());
#else
    return wxString::FromUTF8(path.string());
#endif
}
std::string Bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    Require(stream.is_open(), "file must exist");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
std::string Text(EditorFrame* frame) {
    const auto length = frame->Editor()->Send(Scintilla::Message::GetLength);
    std::string text(length + 1, '\0');
    frame->Editor()->Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
void Add(EditorFrame* frame, const std::string& text) {
    frame->Editor()->Send(Scintilla::Message::GotoPos, frame->Editor()->Send(Scintilla::Message::GetLength));
    frame->Editor()->Send(Scintilla::Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
}
#ifdef __WXGTK__
using NativeDialogs::Respond;
#endif
// Only activate native modal widgets owned by this test process. No global input.
class NativeDriver final : public wxTimer {
public:
    explicit NativeDriver(int choice, wxString path = {}) : choice_(choice), path_(path) { Start(150); }
    int losses = 0, choosers = 0, errors = 0;
    wxString selectedType;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<NativeDriver*>(data);
                if (!IsWindowVisible(window)) return TRUE;
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                if (wxString(title) == "GBK character loss") {
                    ++driver.losses;
                    // The displayed native buttons must make all three choices explicit.
                    struct Labels {
                        bool utf8 = false, replace = false, cancel = false;
                    } labels;
                    EnumChildWindows(
                        window,
                        [](HWND child, LPARAM data) -> BOOL {
                            wchar_t text[1024]{};
                            GetWindowTextW(child, text, 1024);
                            auto& labels = *reinterpret_cast<Labels*>(data);
                            labels.utf8 |= wxString(text).Contains("UTF-8");
                            labels.replace |= wxString(text).Contains("Replace in GBK");
                            labels.cancel |= wxString(text).Contains("Cancel");
                            return TRUE;
                        },
                        reinterpret_cast<LPARAM>(&labels));
                    Require(labels.utf8 && labels.replace && labels.cancel,
                            "native loss prompt must expose UTF8/GBK replacement/Cancel");
                    SendMessageW(window, WM_USER + 102,
                                 driver.choice_ == wxID_YES  ? IDYES
                                 : driver.choice_ == wxID_NO ? IDNO
                                                             : IDCANCEL,
                                 0);
                } else if (wxString(title) == "Open file" || wxString(title) == "Save As") {
                    if (!driver.stage_++) {
                        ++driver.choosers;
                        if (!driver.selectedType.empty()) NativeDialogs::ChooseFileType(window, driver.selectedType);
                        NativeDialogs::ChooseFile(window, driver.path_);
                    }
                } else if (wxString(title) == "Could not save file" || wxString(title) == "Could not open file") {
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
                    if (!selectedType.empty()) NativeDialogs::ChooseFileType(widget, selectedType);
                    NativeDialogs::ChooseFile(widget, path_);
                } else if (stage_ == 2 && !path_.empty())
                    Respond(widget, GTK_RESPONSE_ACCEPT);
            } else if (GTK_IS_MESSAGE_DIALOG(widget)) {
                const auto title = wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget)));
                if (title == "GBK character loss") {
                    ++losses;
                    const auto label = [widget](int response) {
                        auto* button = gtk_dialog_get_widget_for_response(GTK_DIALOG(widget), response);
                        Require(button && GTK_IS_BUTTON(button), "native loss button must exist");
                        return wxString::FromUTF8(gtk_button_get_label(GTK_BUTTON(button)));
                    };
                    Require(label(GTK_RESPONSE_YES).Contains("UTF-8") &&
                                label(GTK_RESPONSE_NO).Contains("Replace in GBK") &&
                                label(GTK_RESPONSE_CANCEL).Contains("Cancel"),
                            "native loss choices must be explicit");
                    Respond(widget, choice_ == wxID_YES  ? GTK_RESPONSE_YES
                                    : choice_ == wxID_NO ? GTK_RESPONSE_NO
                                                         : GTK_RESPONSE_CANCEL);
                } else if (title == "Could not save file" || title == "Could not open file") {
                    ++errors;
                    Respond(widget, GTK_RESPONSE_OK);
                }
            }
        }
        g_list_free(windows);
#endif
    }

private:
    int choice_, stage_ = 0;
    wxString path_;
};
void Command(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    Require(frame->GetEventHandler()->ProcessEvent(event), "menu command must be handled");
}
class GbkNativeApp final : public wxApp {
public:
    bool OnInit() override {
        SetExitOnFrameDelete(false);
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-gbk-native-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto source = directory / "source.txt";
                const std::string original = "Native\r\n\xd6\xd0\xce\xc4\n\x80\r";
                {
                    std::ofstream stream(source, std::ios::binary);
                    stream << original;
                }
                const auto openGbk = frame->GetMenuBar()->FindMenuItem("File", "Open As GBK...");
                Require(openGbk != wxNOT_FOUND, "File menu must expose explicit GBK open");
                NativeDriver opening(wxID_CANCEL, Filename(source));
                Command(frame, openGbk);
                opening.Stop();
                Require(opening.choosers == 1 && frame->DocumentEncoding() == FileEncoding::Gbk &&
                            frame->GetStatusBar()->GetStatusText().Contains("GBK"),
                        "native explicit open must record and display GBK");
                Add(frame, "¥☃😀𝄞");
                const auto python = frame->GetMenuBar()->FindMenuItem("Language", "Python");
                Command(frame, python);
                const auto rich = Text(frame);
                NativeDriver cancelAs(wxID_CANCEL);
                Command(frame, wxID_SAVEAS);
                cancelAs.Stop();
                Require(cancelAs.choosers == 1 && cancelAs.losses == 0 && Bytes(source) == original &&
                            Text(frame) == rich,
                        "cancelled native Save As must not prompt about loss or change bytes/text");
                const auto cancelled = directory / "cancelled.CPP";
                NativeDriver cancelLoss(wxID_CANCEL, Filename(cancelled));
                cancelLoss.selectedType = "JSON";
                Command(frame, wxID_SAVEAS);
                cancelLoss.Stop();
                Require(cancelLoss.choosers == 1 && cancelLoss.losses == 1 && !std::filesystem::exists(cancelled) &&
                            NativeDialogs::PathEquals(frame->DocumentPath(), Filename(source)) &&
                            frame->DocumentEncoding() == FileEncoding::Gbk && Bytes(source) == original &&
                            Text(frame) == rich && frame->GetMenuBar()->IsChecked(python) &&
                            frame->GetStatusBar()->GetStatusText().Contains("Python (override)"),
                        "GBK loss cancellation after Save As naming must retain path, override, text and disk bytes");
                NativeDriver cancel(wxID_CANCEL);
                Command(frame, wxID_SAVE);
                cancel.Stop();
                Require(cancel.losses == 1 && Bytes(source) == original && Text(frame) == rich &&
                            frame->Editor()->Send(Scintilla::Message::GetModify),
                        "native Cancel must retain all text/disk state");
                NativeDriver replace(wxID_NO);
                Command(frame, wxID_SAVE);
                replace.Stop();
                Require(replace.losses == 1 && Bytes(source) == original + "??????" &&
                            Text(frame) == "Native\r\n中文\n€\r??????" &&
                            !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "native confirmed loss must save original placeholder bytes and clean replacement text");
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(Text(frame) == rich && frame->Editor()->Send(Scintilla::Message::GetModify),
                        "native loss Undo must restore richer dirty text");
                frame->Editor()->Send(Scintilla::Message::Redo);
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify),
                        "native loss Redo must reach clean saved text");
                Add(frame, "😀");
                const auto fallback = Text(frame);
                const auto target = directory / "utf8.CPP";
                NativeDriver utf8(wxID_YES, Filename(target));
                Command(frame, wxID_SAVEAS);
                utf8.Stop();
                Require(
                    utf8.choosers == 1 && utf8.losses == 1 && Bytes(target) == fallback &&
                        frame->DocumentEncoding() == FileEncoding::Utf8 && !frame->DocumentHasBom() &&
                        frame->GetMenuBar()->IsChecked(python) &&
                        frame->GetStatusBar()->GetStatusText().Contains("Python (override)"),
                    "native Save As UTF8 must retain bytes without BOM and force selected current Python despite .CPP");
                const auto invalid = directory / "invalid.txt";
                {
                    std::ofstream stream(invalid, std::ios::binary);
                    stream << "\x81";
                }
                NativeDriver invalidOpen(wxID_CANCEL, Filename(invalid));
                Command(frame, openGbk);
                invalidOpen.Stop();
                Require(invalidOpen.choosers == 1 && invalidOpen.errors == 1 && Text(frame) == fallback &&
                            frame->DocumentPath() == Filename(target) &&
                            frame->DocumentEncoding() == FileEncoding::Utf8,
                        "native malformed GBK must show error and retain current document and format");
                std::puts("PASS: native GBK menu/picker/status, loss Cancel/Replace/UTF8 Save As and Undo/Redo");
                for (const auto choice : {wxID_YES, wxID_NO}) {
                    auto* failure = new EditorFrame;
                    failure->Show();
                    Require(failure->Open(Filename(source), FileEncoding::Gbk).status == OpenStatus::Opened,
                            "failure fixture must open");
                    Add(failure, "☃😀");
                    const auto before = Text(failure);
#ifdef __WXMSW__
                    HANDLE lock = CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                              FILE_ATTRIBUTE_NORMAL, nullptr);
                    Require(lock != INVALID_HANDLE_VALUE, "must lock file against replacement");
#else
                    std::filesystem::permissions(directory, std::filesystem::perms::owner_read |
                                                                std::filesystem::perms::owner_exec);
#endif
                    NativeDriver failed(choice);
                    Command(failure, wxID_SAVE);
                    failed.Stop();
#ifdef __WXMSW__
                    CloseHandle(lock);
#else
                    std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
#endif
                    Require(failed.losses == 1 && failed.errors == 1 && Text(failure) == before &&
                                failure->DocumentPath() == Filename(source) &&
                                failure->DocumentEncoding() == FileEncoding::Gbk &&
                                failure->Editor()->Send(Scintilla::Message::GetModify),
                            "native failed UTF8/replacement save must retain rich text, identity, GBK and dirty state");
                    Require(Bytes(source) == original + "??????", "native failed save must preserve disk bytes");
                    failure->Destroy();
                }
                std::puts("PASS: native UTF8/replacement Save write failures preserve disk and session");
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
wxIMPLEMENT_APP(GbkNativeApp);
