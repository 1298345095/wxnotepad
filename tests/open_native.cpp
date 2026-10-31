#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <wx/timer.h>
#include "native_dialog_helpers.h"
#include <wx/statusbr.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

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
    frame->Editor()->Send(Scintilla::Message::GotoPos, frame->Editor()->Send(Scintilla::Message::GetLength));
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
                } else if (IsWindowVisible(window) &&
                           (wxString(title) == "Save file" || wxString(title) == "Open file")) {
                    if (!driver.chooserStage_++) {
                        ++driver.choosers;
                        NativeDialogs::ChooseFile(window, driver.destination_);
                    }
                } else if (IsWindowVisible(window) &&
                           (wxString(title) == "Could not save file" || wxString(title) == "Could not open file")) {
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
            } else if (wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "Could not save file" ||
                       wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "Could not open file") {
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

class OpenNativeApp final : public wxApp {
public:
    bool OnInit() override {
        SetExitOnFrameDelete(false);
        auto* frame = new EditorFrame;
        CallAfter([this, frame] {
            const auto directory =
                std::filesystem::temp_directory_path() / ("wxnotepad-switch-" + std::to_string(wxGetProcessId()));
            try {
                std::filesystem::create_directory(directory);
                const auto path = directory / "source.txt";
                {
                    std::ofstream(path, std::ios::binary) << "opened 中文\n";
                }
                Add(frame, "pending");
                NativeDriver cancel(wxID_CANCEL);
                Require(frame->Open(Filename(path)).status == OpenStatus::Cancelled,
                        "Cancel must prevent Open replacing dirty document");
                cancel.Stop();
                Require(cancel.prompts == 1 && Text(frame) == "pending" && frame->DocumentPath().empty(),
                        "Open Cancel must retain identity and content");
                NativeDriver cancelNew(wxID_CANCEL);
                Require(!frame->New(), "Cancel must prevent New replacing dirty document");
                cancelNew.Stop();
                Require(cancelNew.prompts == 1 && Text(frame) == "pending" &&
                            frame->Editor()->Send(Scintilla::Message::CanUndo),
                        "New Cancel must retain history");
                std::puts("PASS: Open/New Cancel preserve the editable document");
                NativeDriver cancelledSave(wxID_YES);
                Require(frame->Open(Filename(path)).status == OpenStatus::Cancelled,
                        "cancelled first Save must prevent Open");
                cancelledSave.Stop();
                Require(cancelledSave.prompts == 1 && cancelledSave.choosers == 1 && Text(frame) == "pending",
                        "cancelled Save chooser must retain the current document");
                NativeDriver cancelledNewSave(wxID_YES);
                Require(!frame->New(), "cancelled first Save must prevent New");
                cancelledNewSave.Stop();
                Require(cancelledNewSave.prompts == 1 && cancelledNewSave.choosers == 1 &&
                            frame->Editor()->Send(Scintilla::Message::GetModify),
                        "cancelled New Save must retain dirty state");
                const auto savedPath = directory / "saved.txt";
                NativeDriver saveNew(wxID_YES, Filename(savedPath));
                Require(frame->New(), "successful first Save must allow New");
                saveNew.Stop();
                Require(saveNew.prompts == 1 && saveNew.choosers == 1 && Bytes(savedPath) == "pending",
                        "New Save must write the old document first");
                Require(Text(frame).empty() && frame->DocumentPath().empty() && !frame->DocumentHasUtf8Bom(),
                        "New must reset identity, BOM and content");
                Require(!frame->Editor()->Send(Scintilla::Message::GetModify) &&
                            !frame->Editor()->Send(Scintilla::Message::CanUndo) &&
                            frame->Editor()->Send(Scintilla::Message::GetEOLMode) == 0,
                        "New must be clean, isolate Undo and default to CRLF");
                std::puts(
                    "PASS: cancelled Save prevents switching; successful first Save then New resets the document");
                Require(frame->Open(Filename(savedPath)).status == OpenStatus::Opened, "saved source must open");
                Add(frame, " changed");
                NativeDriver saveOpen(wxID_YES);
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened,
                        "successful named Save must allow Open");
                saveOpen.Stop();
                Require(saveOpen.prompts == 1 && saveOpen.choosers == 0 && Bytes(savedPath) == "pending changed",
                        "Open Save must reuse the old path");
                Require(Text(frame) == "opened 中文\n" && !frame->Editor()->Send(Scintilla::Message::CanUndo),
                        "successful Open must clear previous history");
                Add(frame, " changed");
                const auto oldPath = frame->DocumentPath();
                std::filesystem::remove(path);
                std::filesystem::create_directory(path);
                {
                    std::ofstream(path / "sentinel", std::ios::binary) << "keep";
                }
                NativeDriver failedOpenSave(wxID_YES);
                Require(frame->Open(Filename(savedPath)).status == OpenStatus::Cancelled,
                        "failed old-document Save must prevent Open");
                failedOpenSave.Stop();
                Require(failedOpenSave.prompts == 1 && failedOpenSave.errors == 1 && frame->DocumentPath() == oldPath &&
                            Text(frame) == "opened 中文\n changed",
                        "failed Open Save must retain identity and text");
                NativeDriver failedNewSave(wxID_YES);
                Require(!frame->New(), "failed old-document Save must prevent New");
                failedNewSave.Stop();
                Require(failedNewSave.errors == 1 && Bytes(path / "sentinel") == "keep",
                        "failed New Save must preserve target");
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(Text(frame) == "opened 中文\n" && !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "failed switches must preserve the original save point and Undo");
                Add(frame, " discard");
                NativeDriver discardNew(wxID_NO);
                Require(frame->New(), "Discard must allow New");
                discardNew.Stop();
                Require(discardNew.prompts == 1 && Bytes(path / "sentinel") == "keep", "Discard New must not save");
                Add(frame, "discard me");
                NativeDriver discardOpen(wxID_NO);
                Require(frame->Open(Filename(savedPath)).status == OpenStatus::Opened, "Discard must allow Open");
                discardOpen.Stop();
                Require(discardOpen.prompts == 1 && Text(frame) == "pending changed" &&
                            !frame->Editor()->Send(Scintilla::Message::CanUndo),
                        "Discard Open must replace and isolate history");
                std::puts("PASS: named Save/Discard/failure protection shared by Open and New");
                NativeDriver pickerCancel(wxID_CANCEL);
                wxCommandEvent openCancel(wxEVT_MENU, wxID_OPEN);
                frame->GetEventHandler()->ProcessEvent(openCancel);
                pickerCancel.Stop();
                Require(pickerCancel.choosers == 1 && pickerCancel.prompts == 0 && Text(frame) == "pending changed",
                        "native Open cancellation must retain the clean document");
                const auto pickedPath = directory / "picked.txt";
                {
                    std::ofstream(pickedPath, std::ios::binary) << "\xef\xbb\xbfPicked\r\n";
                }
                NativeDriver pickerAccept(wxID_NO, Filename(pickedPath));
                wxCommandEvent openAccept(wxEVT_MENU, wxID_OPEN);
                frame->GetEventHandler()->ProcessEvent(openAccept);
                pickerAccept.Stop();
                Require(pickerAccept.choosers == 1 && frame->DocumentPath() == Filename(pickedPath) &&
                            frame->DocumentHasUtf8Bom() && Text(frame) == "Picked\r\n",
                        "native Open command must use the chosen path");
                Add(frame, "discard");
                NativeDriver menuNew(wxID_NO);
                wxCommandEvent newCommand(wxEVT_MENU, wxID_NEW);
                frame->GetEventHandler()->ProcessEvent(newCommand);
                menuNew.Stop();
                Require(menuNew.prompts == 1 && Text(frame).empty() && frame->DocumentPath().empty() &&
                            !frame->DocumentHasUtf8Bom(),
                        "native New command must reset the document");
                Require(Bytes(pickedPath) == "\xef\xbb\xbfPicked\r\n", "Discard New must preserve source bytes");
                std::puts("PASS: native Open chooser accept/cancel and New menu commands");
                Require(frame->Open(Filename(pickedPath)).status == OpenStatus::Opened,
                        "same-path reopen setup must load");
                Add(frame, "saved");
                NativeDriver samePathSave(wxID_YES);
                Require(frame->Open(Filename(pickedPath)).status == OpenStatus::Opened,
                        "Save then reopen same path must succeed");
                samePathSave.Stop();
                Require(samePathSave.prompts == 1 && Text(frame) == "Picked\r\nsaved" &&
                            Bytes(pickedPath) == "\xef\xbb\xbfPicked\r\nsaved",
                        "same-path Open Save must show the newly saved content");
                std::puts("PASS: reopening the current path after Save uses the saved content");
                const std::vector<std::string> samples = {"",
                                                          "\xef\xbb\xbf",
                                                          "ASCII",
                                                          "\xef\xbb\xbf"
                                                          "ASCII",
                                                          "中文😀\r\nsecond\r\n",
                                                          "\xef\xbb\xbf中文😀\nsecond\n",
                                                          "one\r\ntwo\nthree\rlast",
                                                          "\xef\xbb\xbf"
                                                          "one\r\ntwo\nthree\rlast"};
                for (size_t index = 0; index < samples.size(); ++index) {
                    const auto roundtripPath = directory / ("roundtrip-" + std::to_string(index) + ".txt");
                    {
                        std::ofstream output(roundtripPath, std::ios::binary);
                        output.write(samples[index].data(), samples[index].size());
                    }
                    auto* roundtrip = new EditorFrame;
                    Require(roundtrip->Open(Filename(roundtripPath)).status == OpenStatus::Opened,
                            "roundtrip source must open");
                    const auto originalText = Text(roundtrip);
                    // The content is unchanged; an edit/delete moves away from the
                    // save point so the real close-Save path rewrites the bytes.
                    Add(roundtrip, "!");
                    roundtrip->Editor()->Send(Scintilla::Message::DeleteBack);
                    Require(Text(roundtrip) == originalText && roundtrip->Editor()->Send(Scintilla::Message::GetModify),
                            "roundtrip must exercise close Save with unchanged content");
                    NativeDriver closeSave(wxID_YES);
                    Require(roundtrip->Close(), "roundtrip close Save must succeed");
                    closeSave.Stop();
                    Require(closeSave.prompts == 1 && closeSave.choosers == 0 && Bytes(roundtripPath) == samples[index],
                            "close Save must roundtrip exact encoding/BOM/EOL bytes");
                }
                std::puts("PASS: eight unchanged-content UTF-8/BOM/EOL byte roundtrips through close Save");
                frame->Show();
                frame->Update();
                const int utf16Menu = frame->GetMenuBar()->GetMenu(0)->FindItem("Open As UTF-16LE...");
                Require(utf16Menu != wxNOT_FOUND, "File menu must offer explicit UTF-16LE Open As");
                const auto utf16Path = directory / "utf16-no-bom.txt";
                {
                    std::ofstream output(utf16Path, std::ios::binary);
                    const std::string bytes("\x41\0\x2d\x4e\x3d\xd8\0\xde\r\0\n\0", 12);
                    output.write(bytes.data(), bytes.size());
                }
                NativeDriver utf16Pick(wxID_NO, Filename(utf16Path));
                wxCommandEvent utf16Open(wxEVT_MENU, utf16Menu);
                frame->GetEventHandler()->ProcessEvent(utf16Open);
                utf16Pick.Stop();
                Require(utf16Pick.choosers == 1 && Text(frame) == "A中😀\r\n" &&
                            frame->DocumentEncoding() == FileEncoding::Utf16Le && !frame->DocumentHasBom(),
                        "native explicit Open As must decode no-BOM UTF-16LE");
                Require(frame->GetStatusBar() && frame->GetStatusBar()->GetStatusText().Contains("UTF-16LE"),
                        "status must identify current UTF-16LE encoding");
                NativeDriver utf16Cancel(wxID_CANCEL);
                frame->GetEventHandler()->ProcessEvent(utf16Open);
                utf16Cancel.Stop();
                Require(utf16Cancel.choosers == 1 && Text(frame) == "A中😀\r\n",
                        "cancelled native Open As must preserve current document");
                const auto invalidUtf16Path = directory / "invalid-utf16.txt";
                {
                    std::ofstream output(invalidUtf16Path, std::ios::binary);
                    const std::string bytes("\0\xd8", 2);
                    output.write(bytes.data(), bytes.size());
                }
                NativeDriver utf16Invalid(wxID_CANCEL, Filename(invalidUtf16Path));
                frame->GetEventHandler()->ProcessEvent(utf16Open);
                utf16Invalid.Stop();
                Require(utf16Invalid.errors == 1 && utf16Invalid.prompts == 0 && Text(frame) == "A中😀\r\n",
                        "invalid native UTF-16LE Open As must show error and retain document");
                std::puts("PASS: native UTF-16LE Open As accept/cancel/error and visible encoding status");
                frame->Destroy();
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                if (!frame->IsBeingDeleted()) frame->Destroy();
            }
            std::filesystem::remove_all(directory);
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
wxIMPLEMENT_APP(OpenNativeApp);
