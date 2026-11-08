#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <wx/menu.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/timer.h>
#include "native_dialog_helpers.h"
#include "file_command_driver.h"
#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include <wx/statusbr.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <memory>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Untitled(EditorFrame* frame) {
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    auto* menus = frame->GetMenuBar();
    const auto withEncoding = menus->FindMenuItem("File", "With Encoding...");
    const auto asUtf8 = menus->FindMenuItem("File", "As UTF-8");
    Require(withEncoding != wxNOT_FOUND && asUtf8 != wxNOT_FOUND,
            "File Reload must expose With Encoding and As UTF-8 commands");
    Require(!menus->IsEnabled(withEncoding) && !menus->IsEnabled(asUtf8),
            "Untitled documents must disable both encoding reload commands");
    std::puts("PASS: Untitled disables both encoding reload commands");
}
class ReloadDialogs final : public wxTimer {
public:
    int choosers = 0;
    int initial = -1;
    int selected = 2;
    bool cancel = false;
    int warningChoice = wxID_OK;
    int warnings = 0;
    int errors = 0;
    int lineEndings = 0;
    bool validChoices = false;
    std::vector<wxString> order;
    ReloadDialogs() { Start(50); }
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<ReloadDialogs*>(data);
                wchar_t title[256]{};
                GetWindowTextW(window, title, 256);
                if (IsWindowVisible(window) && wxString(title) == "Reload Encoding") {
                    ++driver.warnings;
                    driver.order.push_back("warning");
                    SendMessageW(window, WM_USER + 102, driver.warningChoice == wxID_OK ? IDOK : IDCANCEL, 0);
                } else if (IsWindowVisible(window) && wxString(title) == "Could not reload file") {
                    ++driver.errors;
                    SendMessageW(window, WM_USER + 102, IDOK, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (gtk_widget_get_visible(widget) && GTK_IS_MESSAGE_DIALOG(widget) &&
                wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "Reload Encoding") {
                ++warnings;
                order.push_back("warning");
                NativeDialogs::Respond(widget, warningChoice == wxID_OK ? GTK_RESPONSE_OK : GTK_RESPONSE_CANCEL);
            } else if (gtk_widget_get_visible(widget) && GTK_IS_MESSAGE_DIALOG(widget) &&
                       wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "Could not reload file") {
                ++errors;
                NativeDialogs::Respond(widget, GTK_RESPONSE_OK);
            }
        }
        g_list_free(windows);
#endif
        for (auto* window : wxTopLevelWindows) {
            if (auto* dialog = wxDynamicCast(window, wxDialog);
                dialog && dialog->IsModal() && dialog->GetTitle() == "Inconsistent Line Endings") {
                ++lineEndings;
                wxCommandEvent response(wxEVT_BUTTON, wxID_CANCEL);
                dialog->GetEventHandler()->ProcessEvent(response);
                return;
            }
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != "Reload With Encoding") continue;
            ++choosers;
            order.push_back("picker");
            for (auto* child : dialog->GetChildren()) {
                auto* list = wxDynamicCast(child, wxListBox);
                if (list) {
                    initial = list->GetSelection();
                    validChoices = list->GetCount() == 3 && list->GetString(0) == "UTF-8" &&
                                   list->GetString(1) == "UTF-16LE" && list->GetString(2) == "GBK (CP936)";
                    list->SetSelection(selected);
                    wxCommandEvent changed(wxEVT_LISTBOX, list->GetId());
                    list->GetEventHandler()->ProcessEvent(changed);
                }
            }
            wxCommandEvent response(wxEVT_BUTTON, cancel ? wxID_CANCEL : wxID_OK);
            dialog->GetEventHandler()->ProcessEvent(response);
            return;
        }
    }
};
void ExplicitReload(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("reload.txt");
    TestFiles::WriteBytes(path, "\xef\xbb\xbf\xe4\xb8\xad");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "UTF-8 BOM fixture must open");
    TestFiles::WriteBytes(path, "\xd6\xd0");
    ReloadDialogs driver;
    wxCommandEvent command(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("File", "With Encoding..."));
    frame->GetEventHandler()->ProcessEvent(command);
    driver.Stop();
    Require(driver.choosers == 1 && driver.initial == 0 && TestEditor::Text(frame) == "中" &&
                frame->DocumentEncoding() == FileEncoding::Gbk && !frame->DocumentHasBom() &&
                !frame->Editor()->Send(Scintilla::Message::GetModify) && TestFiles::Bytes(path) == "\xd6\xd0",
            "F8 must normalize UTF-8 BOM selection and explicitly reload GBK without saving");
    std::puts("PASS: F8 normalizes UTF-8 BOM and reloads GBK from independent disk bytes");
}
void ModifiedPicker(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("reload.txt");
    TestEditor::Append(frame, "live");
    const auto before = TestEditor::Text(frame);
    {
        ReloadDialogs driver;
        driver.warningChoice = wxID_CANCEL;
        const auto result = frame->ReloadWithEncoding();
        driver.Stop();
        Require(result.status == OpenStatus::Cancelled && driver.warnings == 1 && driver.choosers == 0 &&
                    TestEditor::Text(frame) == before && frame->Editor()->Send(Scintilla::Message::GetModify) &&
                    TestFiles::Bytes(path) == "\xd6\xd0",
                "F8 warning Cancel must preserve dirty text and bytes before showing the picker");
    }
    {
        ReloadDialogs driver;
        driver.cancel = true;
        const auto result = frame->ReloadWithEncoding();
        driver.Stop();
        Require(result.status == OpenStatus::Cancelled && driver.order == std::vector<wxString>{"warning", "picker"} &&
                    TestEditor::Text(frame) == before && frame->Editor()->Send(Scintilla::Message::CanUndo) &&
                    TestFiles::Bytes(path) == "\xd6\xd0",
                "F8 picker Cancel must follow discard confirmation and preserve dirty history and bytes");
    }
    std::puts("PASS: F8 warning and picker cancellation preserve text, history and disk bytes in original order");
    frame->Editor()->Send(Scintilla::Message::Undo);
}
void QuickReload(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("quick.txt");
    for (const int choice : {wxID_YES, wxID_NO, wxID_CANCEL}) {
        TestFiles::WriteBytes(path, "disk");
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
                "quick reload GBK fixture must open");
        TestEditor::Append(frame, " live");
        // Save must occur before explicit UTF-8 decoding of the backing file.
        if (choice == wxID_YES) TestFiles::WriteBytes(path, "\xd6\xd0");
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, choice);
        wxCommandEvent command(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("File", "As UTF-8"));
        frame->GetEventHandler()->ProcessEvent(command);
        driver.Stop();
        Require(driver.prompts == 1 && driver.choosers == 0 && driver.errors == 0,
                "UTF-8 quick reload must use the normal Save/Discard/Cancel decision");
        const bool cancelled = choice == wxID_CANCEL;
        Require(TestEditor::Text(frame) == (choice == wxID_NO ? "disk" : "disk live") &&
                    TestFiles::Bytes(path) == (choice == wxID_YES ? "disk live" : "disk") &&
                    frame->DocumentEncoding() == (cancelled ? FileEncoding::Gbk : FileEncoding::Utf8) &&
                    bool(frame->Editor()->Send(Scintilla::Message::GetModify)) == cancelled &&
                    bool(frame->Editor()->Send(Scintilla::Message::CanUndo)) == cancelled,
                "UTF-8 Save must re-read saved bytes; Discard reloads disk; Cancel retains dirty text and format");
        if (cancelled) frame->Editor()->Send(Scintilla::Message::Undo);
    }
    std::puts("PASS: UTF-8 quick reload Save, Discard and Cancel retain their separate original prompt semantics");
}
void DeliveredCodecs(EditorFrame* frame) {
    struct Fixture {
        std::string bytes;
        FileEncoding encoding;
        bool bom;
        std::string text;
    };
    const Fixture fixtures[] = {{"\xe4\xb8\xad", FileEncoding::Utf8, false, "中"},
                                {"\xef\xbb\xbf\xe4\xb8\xad", FileEncoding::Utf8, true, "中"},
                                {"\x2d\x4e", FileEncoding::Utf16Le, false, "中"},
                                {"\xff\xfe\x2d\x4e", FileEncoding::Utf16Le, true, "中"},
                                {"\xd6\xd0", FileEncoding::Gbk, false, "中"},
                                {"# coding: utf-8\n\xd6\xd0", FileEncoding::Gbk, false, "# coding: utf-8\n中"},
                                {"a\r\nb\nc\r", FileEncoding::Utf8, false, "a\r\nb\nc\r"}};
    const auto path = TestFiles::TemporaryDirectory("codecs.txt");
    for (const auto& fixture : fixtures) {
        TestFiles::WriteBytes(path, "initial");
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
                "codec source fixture must open");
        TestFiles::WriteBytes(path, fixture.bytes);
        ReloadDialogs driver;
        driver.selected = static_cast<int>(fixture.encoding);
        const auto result = frame->ReloadWithEncoding();
        driver.Stop();
        const wxString label = fixture.encoding == FileEncoding::Utf8      ? "UTF-8"
                               : fixture.encoding == FileEncoding::Utf16Le ? "UTF-16LE"
                                                                           : "GBK (CP936)";
        Require(
            result.status == OpenStatus::Opened && driver.validChoices && driver.initial == 0 &&
                driver.lineEndings == (fixture.text == "a\r\nb\nc\r" ? 1 : 0) &&
                TestEditor::Text(frame) == fixture.text && TestFiles::Bytes(path) == fixture.bytes &&
                frame->DocumentEncoding() == fixture.encoding && frame->DocumentHasBom() == fixture.bom &&
                frame->GetStatusBar()->GetStatusText().StartsWith(label + (fixture.bom ? " (BOM)" : " |")) &&
                !frame->Editor()->Send(Scintilla::Message::GetModify) &&
                !frame->Editor()->Send(Scintilla::Message::CanUndo),
            "explicit reload must honor the selected codec, actual BOM, conflicting declarations and mixed EOL bytes");
        ReloadDialogs cancelled;
        cancelled.cancel = true;
        Require(frame->ReloadWithEncoding().status == OpenStatus::Cancelled, "codec normalization picker must cancel");
        cancelled.Stop();
        Require(cancelled.initial == static_cast<int>(fixture.encoding),
                "current source codec must normalize both BOM and BOM-less picker selections");
    }
    std::puts(
        "PASS: all three delivered codecs retain actual BOM/status metadata, explicit precedence and mixed EOL bytes");
}
void FailedReload(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("failure.txt");
    TestFiles::WriteBytes(path, "live");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "failure source must open");
    TestEditor::Append(frame, " edit");
    TestFiles::WriteBytes(path, "x");
    ReloadDialogs invalid;
    invalid.selected = 1;
    wxCommandEvent command(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("File", "With Encoding..."));
    frame->GetEventHandler()->ProcessEvent(command);
    invalid.Stop();
    Require(invalid.errors == 1 && invalid.order == std::vector<wxString>{"warning", "picker"} &&
                TestEditor::Text(frame) == "live edit" && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                frame->Editor()->Send(Scintilla::Message::GetModify) &&
                frame->Editor()->Send(Scintilla::Message::CanUndo) && TestFiles::Bytes(path) == "x",
            "decode failure must report through the menu UI and retain live text, undo and metadata");
    frame->Editor()->Send(Scintilla::Message::Undo);
    std::filesystem::remove(path);
    ReloadDialogs missing;
    frame->GetEventHandler()->ProcessEvent(command);
    missing.Stop();
    Require(missing.errors == 1 && TestEditor::Text(frame) == "live" &&
                frame->DocumentPath() == TestFiles::Filename(path) && !std::filesystem::exists(path),
            "read failure must retain the live document and report through the existing UI path");
    std::puts("PASS: decode/read failures preserve live state and report owned error dialogs");
}
void SameFileState(EditorFrame* frame) {
    using Scintilla::Message;
    const auto path = TestFiles::TemporaryDirectory("same-file.txt");
    std::string text;
    for (int line = 0; line < 80; ++line)
        text += std::string(140, 'a') + "\n";
    TestFiles::WriteBytes(path, text);
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
            "same-file source must open");
    wxCommandEvent cpp(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    frame->GetEventHandler()->ProcessEvent(cpp);
    auto* editor = frame->Editor();
    editor->Send(Message::SetSel, 4232, 4374);
    editor->Send(Message::SetFirstVisibleLine, 20);
    editor->Send(Message::SetXOffset, 40);
    editor->Send(Message::SetReadOnly, 1);
    const auto first = editor->Send(Message::GetFirstVisibleLine);
    const auto x = editor->Send(Message::GetXOffset);
    ReloadDialogs driver;
    driver.selected = 0;
    Require(frame->ReloadWithEncoding().status == OpenStatus::Opened, "same-file explicit reload must succeed");
    driver.Stop();
    Require(frame->GetMenuBar()->IsChecked(cpp.GetId()) && editor->Send(Message::GetAnchor) == 4232 &&
                editor->Send(Message::GetCurrentPos) == 4374 && editor->Send(Message::GetFirstVisibleLine) == first &&
                editor->Send(Message::GetXOffset) == x && editor->Send(Message::GetReadOnly) &&
                TestEditor::Text(frame) == text && TestFiles::Bytes(path) == text,
            "encoding reload must retain same-file scheme, directional selection, viewport and read-only mode");
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: encoding reload retains same-file scheme, selection, viewport and read-only mode");
}
void NativeShortcuts(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("keys.txt");
    TestFiles::WriteBytes(path, "\xef\xbb\xbf\xe4\xb8\xad");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "native key fixture must open");
    TestFiles::WriteBytes(path, "\xd6\xd0");
    ReloadDialogs driver;
    NativeKeys::Shortcut(frame, WXK_F8, false);
    driver.Stop();
    Require(driver.choosers == 1 && driver.initial == 0 && TestEditor::Text(frame) == "中" &&
                frame->DocumentEncoding() == FileEncoding::Gbk && TestFiles::Bytes(path) == "\xd6\xd0",
            "native F8 must share the explicit encoding reload menu implementation");
    TestFiles::WriteBytes(path, "quick");
    NativeKeys::Shortcut(frame, WXK_F8, false, false, true);
    Require(TestEditor::Text(frame) == "quick" && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                TestFiles::Bytes(path) == "quick" && !frame->Editor()->Send(Scintilla::Message::CanUndo),
            "native Shift+F8 must share the UTF-8 quick reload menu implementation");
    std::puts("PASS: native F8 and Shift+F8 share menu reload implementations and preserve disk bytes");
}
void NativeDefault() {
#ifdef __WXMSW__
    if (GetACP() != 936) return;
    SettingsStore settings;
    settings.values = OriginalSettings{};
    settings.values.fileWatching.mode = FileWatchingMode::None;
    settings.values.encoding = {.defaultEncoding = 0, .loadANSIasUTF8 = false, .loadASCIIasUTF8 = false};
    auto* frame = new EditorFrame(&settings);
    const auto path = TestFiles::TemporaryDirectory("native-default.txt");
    TestFiles::WriteBytes(path, "native");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "native CP936 fixture must open");
    ReloadDialogs driver;
    driver.cancel = true;
    const auto result = frame->ReloadWithEncoding();
    driver.Stop();
    const bool passed = result.status == OpenStatus::Cancelled && driver.initial == wxNOT_FOUND &&
                        TestEditor::Text(frame) == "native" && TestFiles::Bytes(path) == "native";
    frame->Destroy();
    Require(passed, "native ANSI identity must normalize to the original unselected source encoding");
    std::puts("PASS: native ANSI identity normalizes to the original unselected source choice");
#endif
}
void SaveCleanupView() {
    SettingsStore settings;
    settings.values = OriginalSettings{};
    settings.values.fileWatching.mode = FileWatchingMode::None;
    settings.values.fixTrailingBlanks = true;
    auto* frame = new EditorFrame(&settings);
    const auto path = TestFiles::TemporaryDirectory("cleanup-view.txt");
    TestFiles::WriteBytes(path, "a  \nb  \nc  \n");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "quick cleanup source must open");
    TestEditor::Append(frame, " ");
    frame->Editor()->Send(Scintilla::Message::SetSel, 9, 4);
    NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, wxID_YES);
    const auto result = frame->ReloadAsUtf8();
    driver.Stop();
    const bool passed = result.status == OpenStatus::Opened && driver.prompts == 1 &&
                        TestEditor::Text(frame) == "a\nb\nc\n" && TestFiles::Bytes(path) == "a\nb\nc\n" &&
                        frame->Editor()->Send(Scintilla::Message::GetAnchor) == 6 &&
                        frame->Editor()->Send(Scintilla::Message::GetCurrentPos) == 4;
    frame->Destroy();
    Require(passed,
            "quick reload must capture original selection before Save cleanup, then restore against saved bytes");
    std::puts("PASS: UTF-8 quick reload restores the original pre-Save selection after cleanup");
}
class ReloadEncodingApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.fileWatching.mode = FileWatchingMode::None;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        wxString nativeMode;
        const bool native = wxGetEnv("WXNOTEPAD_RELOAD_NATIVE_KEYS", &nativeMode) && nativeMode == "1";
        auto run = [this, frame, native](bool ready) {
            try {
                Require(ready, "native key fixture must be presented on the private display");
                if (native) {
                    NativeShortcuts(frame);
                } else {
                    Untitled(frame);
                    ExplicitReload(frame);
                    ModifiedPicker(frame);
                    QuickReload(frame);
                    DeliveredCodecs(frame);
                    FailedReload(frame);
                    SameFileState(frame);
                    NativeDefault();
                    SaveCleanupView();
                }
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Destroy();
            ExitMainLoop();
        };
        if (native)
            NativeKeys::AfterPresentation(frame, std::move(run));
        else
            CallAfter([run = std::move(run)] { run(true); });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(ReloadEncodingApp);
