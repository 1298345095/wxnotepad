#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <wx/timer.h>
#include "file_command_driver.h"
#include <wx/statusbr.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "settings/settings_store.h"
#include <memory>
#include <stdexcept>
#include <vector>
#include <utility>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
using NativeDialogs::FileCommandDriver;
using NativeDialogs::FileDialogProfile;
using TestEditor::Append;
using TestEditor::Text;
using TestFiles::Bytes;
using TestFiles::Filename;

#ifdef __WXGTK__
// Hold the previous native selection while a requested filename completes.
// The user-visible Open As result must come from the requested UTF-16 file.
class DelayedFileSelection final : public wxTimer {
public:
    DelayedFileSelection(wxString expected, wxString previous)
        : expected_(std::move(expected)), previous_(std::move(previous)) {
        Start(10);
    }
    ~DelayedFileSelection() override {
        if (holdSource_) g_source_remove(holdSource_);
        if (restoreSource_) g_source_remove(restoreSource_);
        if (chooser_) {
            if (g_signal_handler_is_connected(chooser_, selection_)) g_signal_handler_disconnect(chooser_, selection_);
            g_object_unref(chooser_);
        }
    }
    bool Held() const { return held_; }
    bool Restored() const { return restored_; }
    void Notify() override {
        GList* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item && !chooser_; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget) || !GTK_IS_FILE_CHOOSER(widget)) continue;
            chooser_ = GTK_FILE_CHOOSER(g_object_ref(widget));
            selection_ = g_signal_connect(chooser_, "selection-changed", G_CALLBACK(SelectionChanged), this);
            Stop();
        }
        g_list_free(windows);
    }

private:
    static void SelectionChanged(GtkFileChooser* chooser, gpointer data) {
        auto& self = *static_cast<DelayedFileSelection*>(data);
        char* selected = gtk_file_chooser_get_filename(chooser);
        const bool requested = selected && wxString::FromUTF8(selected) == self.expected_;
        g_free(selected);
        if (self.held_ || !requested) return;
        self.held_ = true;
        self.holdSource_ = g_idle_add(
            [](gpointer data) -> gboolean {
                auto& self = *static_cast<DelayedFileSelection*>(data);
                self.holdSource_ = 0;
                gtk_file_chooser_set_filename(self.chooser_, self.previous_.utf8_str());
                self.restoreSource_ = g_timeout_add(
                    350,
                    [](gpointer data) -> gboolean {
                        auto& self = *static_cast<DelayedFileSelection*>(data);
                        self.restoreSource_ = 0;
                        self.restored_ = true;
                        gtk_file_chooser_set_filename(self.chooser_, self.expected_.utf8_str());
                        return G_SOURCE_REMOVE;
                    },
                    data);
                return G_SOURCE_REMOVE;
            },
            data);
    }
    wxString expected_, previous_;
    GtkFileChooser* chooser_ = nullptr;
    gulong selection_ = 0;
    guint holdSource_ = 0, restoreSource_ = 0;
    bool held_ = false, restored_ = false;
};

void DelayedOpenAs(EditorFrame* frame, const std::filesystem::path& directory) {
    const auto expected = directory / "delayed-utf16.txt";
    const auto previous = directory / "previous-utf8.txt";
    TestFiles::WriteBytes(expected, "\x2d\x4e");
    TestFiles::WriteBytes(previous, "wrong");
    DelayedFileSelection selection(Filename(expected), Filename(previous));
    FileCommandDriver driver(FileDialogProfile::Open, wxID_NO, Filename(expected));
    const auto result = frame->OpenAs(FileEncoding::Utf16Le);
    driver.Stop();
    if (result.status != OpenStatus::Opened || frame->DocumentPath() != Filename(expected))
        std::fprintf(stderr, "Delayed native Open As status=%d held=%d restored=%d path='%s' error='%s'\n",
                     static_cast<int>(result.status), selection.Held(), selection.Restored(),
                     frame->DocumentPath().utf8_str().data(), result.error.utf8_str().data());
    Require(selection.Held() && selection.Restored() && result.status == OpenStatus::Opened &&
                frame->DocumentPath() == Filename(expected) && Text(frame) == "中" &&
                frame->DocumentEncoding() == FileEncoding::Utf16Le && !frame->DocumentHasBom() &&
                Bytes(expected) == "\x2d\x4e" && Bytes(previous) == "wrong",
            "Open As must await the requested native filename instead of accepting a previous selection");
    Require(frame->New(), "delayed native Open As fixture must restore a fresh document");
    std::puts("PASS: delayed native selection opens the requested UTF-16LE file and preserves independent disk bytes");
}
#endif

class OpenNativeApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        settings_->values.wordWrap = false;
        SetExitOnFrameDelete(false);
        auto* frame = new EditorFrame(settings_.get());
        CallAfter([this, frame] {
            const auto directory = TestFiles::TemporaryDirectory("wxnotepad-switch");
            try {
                std::filesystem::create_directory(directory);
#ifdef __WXGTK__
                DelayedOpenAs(frame, directory);
#endif
                const auto path = directory / "source.txt";
                {
                    std::ofstream(path, std::ios::binary) << "opened 中文\n";
                }
                Append(frame, "pending");
                FileCommandDriver cancel(FileDialogProfile::Open, wxID_CANCEL);
                Require(frame->Open(Filename(path)).status == OpenStatus::Cancelled,
                        "Cancel must prevent Open replacing dirty document");
                cancel.Stop();
                Require(cancel.prompts == 1 && Text(frame) == "pending" && frame->DocumentPath().empty(),
                        "Open Cancel must retain identity and content");
                FileCommandDriver cancelNew(FileDialogProfile::Open, wxID_CANCEL);
                Require(!frame->New(), "Cancel must prevent New replacing dirty document");
                cancelNew.Stop();
                Require(cancelNew.prompts == 1 && Text(frame) == "pending" &&
                            frame->Editor()->Send(Scintilla::Message::CanUndo),
                        "New Cancel must retain history");
                std::puts("PASS: Open/New Cancel preserve the editable document");
                FileCommandDriver cancelledSave(FileDialogProfile::Open, wxID_YES);
                Require(frame->Open(Filename(path)).status == OpenStatus::Cancelled,
                        "cancelled first Save must prevent Open");
                cancelledSave.Stop();
                Require(cancelledSave.prompts == 1 && cancelledSave.choosers == 1 && Text(frame) == "pending",
                        "cancelled Save chooser must retain the current document");
                FileCommandDriver cancelledNewSave(FileDialogProfile::Open, wxID_YES);
                Require(!frame->New(), "cancelled first Save must prevent New");
                cancelledNewSave.Stop();
                Require(cancelledNewSave.prompts == 1 && cancelledNewSave.choosers == 1 &&
                            frame->Editor()->Send(Scintilla::Message::GetModify),
                        "cancelled New Save must retain dirty state");
                const auto savedPath = directory / "saved.txt";
                FileCommandDriver saveNew(FileDialogProfile::Open, wxID_YES, Filename(savedPath));
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
                Append(frame, " changed");
                FileCommandDriver saveOpen(FileDialogProfile::Open, wxID_YES);
                Require(frame->Open(Filename(path)).status == OpenStatus::Opened,
                        "successful named Save must allow Open");
                saveOpen.Stop();
                Require(saveOpen.prompts == 1 && saveOpen.choosers == 0 && Bytes(savedPath) == "pending changed",
                        "Open Save must reuse the old path");
                Require(Text(frame) == "opened 中文\n" && !frame->Editor()->Send(Scintilla::Message::CanUndo),
                        "successful Open must clear previous history");
                Append(frame, " changed");
                const auto oldPath = frame->DocumentPath();
                std::filesystem::remove(path);
                std::filesystem::create_directory(path);
                {
                    std::ofstream(path / "sentinel", std::ios::binary) << "keep";
                }
                FileCommandDriver failedOpenSave(FileDialogProfile::Open, wxID_YES);
                Require(frame->Open(Filename(savedPath)).status == OpenStatus::Cancelled,
                        "failed old-document Save must prevent Open");
                failedOpenSave.Stop();
                Require(failedOpenSave.prompts == 1 && failedOpenSave.errors == 1 && frame->DocumentPath() == oldPath &&
                            Text(frame) == "opened 中文\n changed",
                        "failed Open Save must retain identity and text");
                FileCommandDriver failedNewSave(FileDialogProfile::Open, wxID_YES);
                Require(!frame->New(), "failed old-document Save must prevent New");
                failedNewSave.Stop();
                Require(failedNewSave.errors == 1 && Bytes(path / "sentinel") == "keep",
                        "failed New Save must preserve target");
                frame->Editor()->Send(Scintilla::Message::Undo);
                Require(Text(frame) == "opened 中文\n" && !frame->Editor()->Send(Scintilla::Message::GetModify),
                        "failed switches must preserve the original save point and Undo");
                Append(frame, " discard");
                FileCommandDriver discardNew(FileDialogProfile::Open, wxID_NO);
                Require(frame->New(), "Discard must allow New");
                discardNew.Stop();
                Require(discardNew.prompts == 1 && Bytes(path / "sentinel") == "keep", "Discard New must not save");
                Append(frame, "discard me");
                FileCommandDriver discardOpen(FileDialogProfile::Open, wxID_NO);
                Require(frame->Open(Filename(savedPath)).status == OpenStatus::Opened, "Discard must allow Open");
                discardOpen.Stop();
                Require(discardOpen.prompts == 1 && Text(frame) == "pending changed" &&
                            !frame->Editor()->Send(Scintilla::Message::CanUndo),
                        "Discard Open must replace and isolate history");
                std::puts("PASS: named Save/Discard/failure protection shared by Open and New");
                FileCommandDriver pickerCancel(FileDialogProfile::Open, wxID_CANCEL);
                wxCommandEvent openCancel(wxEVT_MENU, wxID_OPEN);
                frame->GetEventHandler()->ProcessEvent(openCancel);
                pickerCancel.Stop();
                Require(pickerCancel.choosers == 1 && pickerCancel.prompts == 0 && Text(frame) == "pending changed",
                        "native Open cancellation must retain the clean document");
                const auto pickedPath = directory / "picked.txt";
                {
                    std::ofstream(pickedPath, std::ios::binary) << "\xef\xbb\xbfPicked\r\n";
                }
                FileCommandDriver pickerAccept(FileDialogProfile::Open, wxID_NO, Filename(pickedPath));
                wxCommandEvent openAccept(wxEVT_MENU, wxID_OPEN);
                frame->GetEventHandler()->ProcessEvent(openAccept);
                pickerAccept.Stop();
                Require(pickerAccept.choosers == 1 &&
                            NativeDialogs::PathEquals(frame->DocumentPath(), Filename(pickedPath)) &&
                            frame->DocumentHasUtf8Bom() && Text(frame) == "Picked\r\n",
                        "native Open command must use the chosen path");
                Append(frame, "discard");
                FileCommandDriver menuNew(FileDialogProfile::Open, wxID_NO);
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
                Append(frame, "saved");
                FileCommandDriver samePathSave(FileDialogProfile::Open, wxID_YES);
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
                    auto* roundtrip = new EditorFrame(settings_.get());
                    Require(roundtrip->Open(Filename(roundtripPath)).status == OpenStatus::Opened,
                            "roundtrip source must open");
                    const auto originalText = Text(roundtrip);
                    // The content is unchanged; an edit/delete moves away from the
                    // save point so the real close-Save path rewrites the bytes.
                    Append(roundtrip, "!");
                    roundtrip->Editor()->Send(Scintilla::Message::DeleteBack);
                    Require(Text(roundtrip) == originalText && roundtrip->Editor()->Send(Scintilla::Message::GetModify),
                            "roundtrip must exercise close Save with unchanged content");
                    FileCommandDriver closeSave(FileDialogProfile::Open, wxID_YES);
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
                FileCommandDriver utf16Pick(FileDialogProfile::Open, wxID_NO, Filename(utf16Path));
                wxCommandEvent utf16Open(wxEVT_MENU, utf16Menu);
                frame->GetEventHandler()->ProcessEvent(utf16Open);
                utf16Pick.Stop();
                if (utf16Pick.choosers != 1 || Text(frame) != "A中😀\r\n" ||
                    frame->DocumentEncoding() != FileEncoding::Utf16Le || frame->DocumentHasBom())
                    std::fprintf(
                        stderr,
                        "Open As state: choosers=%d prompts=%d errors=%d path='%s' bytes=%zu encoding=%d bom=%d\n",
                        utf16Pick.choosers, utf16Pick.prompts, utf16Pick.errors,
                        frame->DocumentPath().utf8_str().data(), Text(frame).size(),
                        static_cast<int>(frame->DocumentEncoding()), frame->DocumentHasBom());
                Require(utf16Pick.choosers == 1 && Text(frame) == "A中😀\r\n" &&
                            frame->DocumentEncoding() == FileEncoding::Utf16Le && !frame->DocumentHasBom(),
                        "native explicit Open As must decode no-BOM UTF-16LE");
                Require(frame->GetStatusBar() && frame->GetStatusBar()->GetStatusText().Contains("UTF-16LE"),
                        "status must identify current UTF-16LE encoding");
                FileCommandDriver utf16Cancel(FileDialogProfile::Open, wxID_CANCEL);
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
                FileCommandDriver utf16Invalid(FileDialogProfile::Open, wxID_CANCEL, Filename(invalidUtf16Path));
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(OpenNativeApp);
