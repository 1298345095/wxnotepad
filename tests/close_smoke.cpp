#include "editor/editor_control.h"
#include "app/editor_frame.h"
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
std::string Text(EditorFrame* frame) {
    const auto length = static_cast<size_t>(frame->Editor()->Send(Scintilla::Message::GetLength));
    std::string text(length + 1, '\0');
    frame->Editor()->Send(Scintilla::Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(length);
    return text;
}
void Add(EditorFrame* frame, const std::string& text) {
    frame->Editor()->Send(Scintilla::Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
}
#ifdef __WXGTK__
using NativeDialogs::Respond;
#endif

// Drives actual native modal widgets belonging to this test process. It does
// not inject keyboard/mouse input or require ownership of the foreground app.
class NativeDriver final : public wxTimer {
public:
    explicit NativeDriver(int choice, wxString destination = {}) : choice_(choice), destination_(destination) {
        Start(100);
    }
    int prompts = 0;
    int choosers = 0;
    int errors = 0;
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<NativeDriver*>(data);
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                if (IsWindowVisible(window) && wxString(title) == "Save changes?") {
                    ++driver.prompts;
                    const int button = driver.choice_ == wxID_YES ? IDYES : driver.choice_ == wxID_NO ? IDNO : IDCANCEL;
                    // wxMessageDialog uses the Windows native task dialog.
                    SendMessageW(window, WM_USER + 102, button, 0); // TDM_CLICK_BUTTON
                } else if (IsWindowVisible(window) && wxString(title) == "Save file") {
                    if (!driver.chooserStage_++) {
                        ++driver.choosers;
                        NativeDialogs::ChooseFile(window, driver.destination_);
                    }
                } else if (IsWindowVisible(window) && wxString(title) == "Could not save file") {
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
                if (!chooserStage_++) {
                    ++choosers;
                    NativeDialogs::ChooseFile(widget, destination_);
                } else if (chooserStage_ == 2 && !destination_.empty()) {
                    Respond(widget, GTK_RESPONSE_ACCEPT);
                }
                continue;
            }
            if (!GTK_IS_MESSAGE_DIALOG(widget)) continue;
            if (wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "Save changes?") {
                ++prompts;
                Respond(widget, choice_ == wxID_YES  ? GTK_RESPONSE_YES
                                : choice_ == wxID_NO ? GTK_RESPONSE_NO
                                                     : GTK_RESPONSE_CANCEL);
            } else if (wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "Could not save file") {
                ++errors;
                Respond(widget, GTK_RESPONSE_OK);
            }
        }
        g_list_free(windows);
#endif
    }

private:
    int choice_;
    wxString destination_;
    int chooserStage_ = 0;
};

class CloseApp final : public wxApp {
public:
    bool OnInit() override {
        SetExitOnFrameDelete(false);
        auto* frame = new EditorFrame;
        CallAfter([this, frame] {
            try {
                Add(frame, "Unsaved 中文\r\n");
                NativeDriver driver(wxID_CANCEL);
                Require(!frame->Close(), "Cancel must veto the actual close event");
                driver.Stop();
                Require(driver.prompts == 1, "dirty close must show the native confirmation");
                Require(!frame->IsBeingDeleted(), "Cancel must retain the window");
                Require(Text(frame) == "Unsaved 中文\r\n" && frame->Editor()->Send(Scintilla::Message::GetModify),
                        "Cancel must retain text and dirty state");
                Require(frame->DocumentPath().empty(), "Cancel must retain unnamed identity");
                Add(frame, "continued");
                Require(Text(frame) == "Unsaved 中文\r\ncontinued", "Cancel must allow continued editing");
                std::puts("PASS: close Cancel retains document and window");
                const auto directory =
                    std::filesystem::temp_directory_path() / ("wxnotepad-close-" + std::to_string(wxGetProcessId()));
                std::filesystem::create_directory(directory);
                auto* saved = new EditorFrame;
                const auto path = directory / "known.txt";
                Add(saved, "first");
                Require(saved->SaveFirst(Filename(path)).status == SaveStatus::Saved, "named-close setup must save");
                Add(saved, " 中文\r\n");
                NativeDriver saveDriver(wxID_YES);
                Require(saved->Close(), "successful Save must allow closing");
                saveDriver.Stop();
                Require(saveDriver.prompts == 1, "named dirty document must show native confirmation");
                Require(Bytes(path) == "first \xe4\xb8\xad\xe6\x96\x87\r\n",
                        "close Save must write changes to the known path");
                std::puts("PASS: close Save writes changes to known path");
                auto* clean = new EditorFrame;
                NativeDriver cleanDriver(wxID_CANCEL);
                Require(clean->Close(), "unmodified document must close directly");
                cleanDriver.Stop();
                Require(cleanDriver.prompts == 0, "unmodified close must not show a confirmation");
                std::puts("PASS: unmodified document closes directly");
                auto* discard = new EditorFrame;
                const auto discardPath = directory / "discard.txt";
                Add(discard, "original bytes");
                Require(discard->SaveFirst(Filename(discardPath)).status == SaveStatus::Saved,
                        "discard setup must save");
                Add(discard, " changed");
                NativeDriver discardDriver(wxID_NO);
                Require(discard->Close(), "Discard must allow closing");
                discardDriver.Stop();
                Require(discardDriver.prompts == 1 && Bytes(discardPath) == "original bytes",
                        "Discard must prompt and leave disk unchanged");
                std::puts("PASS: close Discard does not write changes");
                auto* cancelledSave = new EditorFrame;
                Add(cancelledSave, "pending");
                NativeDriver cancelledDriver(wxID_YES);
                Require(!cancelledSave->Close(), "cancelling the Save chooser must veto closing");
                cancelledDriver.Stop();
                Require(cancelledDriver.prompts == 1 && cancelledDriver.choosers == 1,
                        "close Save must show native confirmation and chooser");
                Require(Text(cancelledSave) == "pending" && cancelledSave->DocumentPath().empty() &&
                            cancelledSave->Editor()->Send(Scintilla::Message::GetModify),
                        "cancelled Save must retain text, identity and dirty state");
                Add(cancelledSave, " continued");
                Require(Text(cancelledSave) == "pending continued", "cancelled Save must allow continued editing");
                cancelledSave->Destroy();
                std::puts("PASS: cancelled close Save retains the editable document");
                auto* firstSave = new EditorFrame;
                const auto firstPath = directory / "first-close.txt";
                Add(firstSave, "First 中文😀\r\n");
                NativeDriver firstDriver(wxID_YES, Filename(firstPath));
                Require(firstSave->Close(), "successful first Save must allow closing");
                firstDriver.Stop();
                Require(firstDriver.prompts == 1 && firstDriver.choosers == 1,
                        "first close Save must use the native confirmation and path chooser");
                Require(Bytes(firstPath) == "First \xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80\r\n",
                        "first close Save must preserve exact UTF-8 bytes without BOM");
                std::puts("PASS: first close Save uses the native path chooser");
                auto* failedSave = new EditorFrame;
                const auto failedPath = directory / "failure.txt";
                Add(failedSave, "original");
                Require(failedSave->SaveFirst(Filename(failedPath)).status == SaveStatus::Saved,
                        "failed-close setup must save");
                Add(failedSave, " changed");
                std::filesystem::remove(failedPath);
                std::filesystem::create_directory(failedPath);
                {
                    std::ofstream(failedPath / "sentinel", std::ios::binary) << "keep";
                }
                NativeDriver failedDriver(wxID_YES);
                Require(!failedSave->Close(), "failed Save must veto closing");
                failedDriver.Stop();
                Require(failedDriver.prompts == 1 && failedDriver.errors == 1 && failedDriver.choosers == 0,
                        "failed known-path Save must show its error without another path chooser");
                Require(Text(failedSave) == "original changed" && failedSave->DocumentPath() == Filename(failedPath) &&
                            failedSave->Editor()->Send(Scintilla::Message::GetModify),
                        "failed Save must retain text, identity and dirty state");
                Require(Bytes(failedPath / "sentinel") == "keep", "failed close Save must preserve the target");
                failedSave->Editor()->Send(Scintilla::Message::Undo);
                Require(Text(failedSave) == "original" && !failedSave->Editor()->Send(Scintilla::Message::GetModify),
                        "failed close Save must retain history and the previous save point");
                failedSave->Destroy();
                std::filesystem::remove_all(directory);
                std::puts("PASS: failed close Save retains document, identity and history");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            if (!frame->IsBeingDeleted()) frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnExit() override { return result_; }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(CloseApp);
