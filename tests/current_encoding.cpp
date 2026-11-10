#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <wx/menu.h>
#include <wx/treectrl.h>
#include <wx/timer.h>
#include "native_key_helpers.h"
#include "native_dialog_helpers.h"
#include "native_search_helpers.h"
#include "file_command_driver.h"
#include <wx/statusbr.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Command(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("File", label);
    Require(id != wxNOT_FOUND, "File Encoding must expose the delivered destination choice");
    wxCommandEvent command(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(command);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
}
void FormatOnly(EditorFrame* frame) {
    using Scintilla::Message;
    const auto path = TestFiles::TemporaryDirectory("current-encoding.txt");
    TestFiles::WriteBytes(path, "a\r\nb\n");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "format fixture opens");
    auto* editor = frame->Editor();
    TestEditor::Append(frame, "x");
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 3, 1);
    Command(frame, "UTF-8 BOM");
    Require(frame->DocumentHasBom() && frame->GetTitle().StartsWith("*") && frame->GetMenuBar()->IsEnabled(wxID_SAVE) &&
                frame->GetStatusBar()->GetStatusText().Contains("UTF-8 (BOM)") && !editor->Send(Message::GetModify) &&
                editor->Send(Message::CanRedo) && editor->Send(Message::GetAnchor) == 3 &&
                editor->Send(Message::GetCurrentPos) == 1 && TestEditor::Text(frame) == "a\r\nb\n" &&
                TestFiles::Bytes(path) == "a\r\nb\n",
            "encoding-only change must enable Save and retain live text, selection, redo and disk");
    Command(frame, "UTF-8");
    Require(!frame->GetTitle().StartsWith("*") && !frame->GetMenuBar()->IsEnabled(wxID_SAVE),
            "returning to original encoding clears encoding-only modification");
    Command(frame, "UTF-8 BOM");
    Require(frame->Save().status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == "\xef\xbb\xbf"
                                          "a\r\nb\n" &&
                !frame->GetTitle().StartsWith("*") && editor->Send(Message::CanRedo),
            "Save writes the selected BOM and commits identity while retaining redo");
    std::puts("PASS: destination format changes preserve history and commit independent exact bytes");
}
class EncodingPicker final : public wxTimer {
public:
    int shown = 0;
    wxString initial;
    wxString selected = "UTF-16LE";
    bool cancel = false;
    EncodingPicker() { Start(40); }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != "Select Encoding") continue;
            ++shown;
            auto* tree = wxDynamicCast(dialog->FindWindowByName("Current encoding", dialog), wxTreeCtrl);
            if (!tree) {
                dialog->EndModal(wxID_CANCEL);
                return;
            }
            initial = tree->GetItemText(tree->GetSelection());
            const auto visit = [&](auto&& self, wxTreeItemId parent) -> void {
                wxTreeItemIdValue cookie;
                for (auto item = tree->GetFirstChild(parent, cookie); item.IsOk();
                     item = tree->GetNextChild(parent, cookie)) {
                    if (tree->GetItemText(item) == selected) tree->SelectItem(item);
                    self(self, item);
                }
            };
            visit(visit, tree->GetRootItem());
            wxCommandEvent response(wxEVT_BUTTON, cancel ? wxID_CANCEL : wxID_OK);
            dialog->GetEventHandler()->ProcessEvent(response);
            return;
        }
    }
};
void Picker(EditorFrame* frame, bool native) {
    using Scintilla::Message;
    const auto path = TestFiles::TemporaryDirectory("picker.txt");
    TestFiles::WriteBytes(path, "live\r\n");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "picker fixture opens");
    TestEditor::Append(frame, "x");
    frame->Editor()->Send(Message::Undo);
    TestFiles::WriteBytes(path, "external");
    {
        EncodingPicker driver;
        if (native)
            NativeKeys::Shortcut(frame, WXK_F9, false);
        else
            Command(frame, "More...");
        driver.Stop();
        Require(driver.shown == 1 && driver.initial == "UTF-8" && frame->DocumentEncoding() == FileEncoding::Utf16Le &&
                    !frame->DocumentHasBom() && TestEditor::Text(frame) == "live\r\n" &&
                    frame->Editor()->Send(Message::CanRedo) && TestFiles::Bytes(path) == "external",
                "More/F9 selects the live destination without reading or writing disk and keeps redo");
    }
    {
        EncodingPicker driver;
        driver.cancel = true;
        driver.selected = "Simplified Chinese (GBK, GB2312)";
        Command(frame, "More...");
        driver.Stop();
        Require(driver.shown == 1 && driver.initial == "UTF-16LE" &&
                    frame->DocumentEncoding() == FileEncoding::Utf16Le && frame->GetTitle().StartsWith("*"),
                "picker Cancel retains selected encoding and pending state");
    }
    Require(frame->Save().status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == std::string("l\0i\0v\0e\0\r\0\n\0", 12),
            "BOM-less UTF-16LE saves exact independently specified bytes");
    std::puts("PASS: More/F9 live destination picker, cancellation and exact UTF-16 bytes");
}
void BigEndian(EditorFrame* frame, bool native) {
    using Scintilla::Message;
    const auto path = TestFiles::TemporaryDirectory("big-endian-destination.txt");
    const auto copy = TestFiles::TemporaryDirectory("big-endian-copy.txt");
    const auto renamed = TestFiles::TemporaryDirectory("big-endian-renamed.txt");
    const std::string utf8 = "A中😀\r\nB\nC\r";
    const std::string bytes("\0A\x4e\x2d\xd8\x3d\xde\0\0\r\0\n\0B\0\n\0C\0\r", 20);
    TestFiles::WriteBytes(path, utf8);
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "UTF-16BE destination fixture opens");
    TestEditor::Append(frame, "x");
    frame->Editor()->Send(Message::Undo);
    Command(frame, "UTF-16BE BOM");
    Require(frame->DocumentEncoding() == FileEncoding::Utf16Be && frame->DocumentHasBom() &&
                frame->GetStatusBar()->GetStatusText().Contains("UTF-16BE (BOM)") &&
                frame->GetTitle().StartsWith("*") && frame->Editor()->Send(Message::CanRedo),
            "direct UTF-16BE BOM choice updates live identity and preserves redo");
    Require(frame->SaveCopy(TestFiles::Filename(copy)).status == SaveStatus::Saved &&
                TestFiles::Bytes(copy) == "\xfe\xff" + bytes && TestFiles::Bytes(path) == utf8 &&
                frame->DocumentIsModified(),
            "Save Copy writes exact UTF-16BE BOM bytes without committing the original identity");
    Require(frame->SaveAs(TestFiles::Filename(TestFiles::TemporaryDirectory())).status == SaveStatus::Failed &&
                frame->DocumentPath() == TestFiles::Filename(path) && frame->DocumentIsModified() &&
                frame->DocumentEncoding() == FileEncoding::Utf16Be && frame->DocumentHasBom() &&
                frame->Editor()->Send(Message::CanRedo) && TestEditor::Text(frame) == utf8 &&
                TestFiles::Bytes(path) == utf8,
            "failed UTF-16BE write retains path, pending encoding, live history and original bytes");
    Require(frame->Save({}, SaveTimeMode::Original).status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == "\xfe\xff" + bytes && !frame->DocumentIsModified(),
            "Save with Original Time commits exact UTF-16BE BOM bytes");
    {
        EncodingPicker driver;
        driver.selected = "UTF-16BE";
        if (native)
            NativeKeys::Shortcut(frame, WXK_F9, false);
        else
            Command(frame, "More...");
        driver.Stop();
        Require(driver.shown == 1 && driver.initial == "UTF-16BE with BOM" &&
                    frame->DocumentEncoding() == FileEncoding::Utf16Be && !frame->DocumentHasBom() &&
                    frame->Editor()->Send(Message::CanRedo) && TestEditor::Text(frame) == utf8,
                "F9 selects BOM-less UTF-16BE without changing live text or redo");
    }
    Require(frame->SaveAs(TestFiles::Filename(renamed)).status == SaveStatus::Saved &&
                TestFiles::Bytes(renamed) == bytes && !frame->DocumentIsModified(),
            "Save As writes selected BOM-less UTF-16BE");
    {
        EncodingPicker driver;
        driver.selected = "UTF-16BE with BOM";
        Command(frame, "More...");
        driver.Stop();
        Require(driver.initial == "UTF-16BE" && frame->DocumentHasBom(), "F9 offers both UTF-16BE destinations");
    }
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(renamed) == "\xfe\xff" + bytes,
            "Save adds exactly the selected UTF-16BE BOM");
    Require(frame->NewDocument(), "saved UTF-16BE document closes cleanly");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf16Be));
    TestEditor::Append(frame, utf8);
    Require(frame->SaveFirst(TestFiles::Filename(path)).status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes,
            "first Save writes BOM-less UTF-16BE");
    frame->Editor()->Send(Message::SelectAll);
    const int identifier = frame->GetMenuBar()->FindMenuItem("Edit", "Encoding Identifier");
    wxCommandEvent insert(wxEVT_MENU, identifier);
    frame->GetEventHandler()->ProcessEvent(insert);
    Require(TestEditor::Text(frame) == "UTF-16BE", "encoding identifier distinguishes BOM-less UTF-16BE");
    frame->Editor()->Send(Message::Undo);
    Command(frame, "UTF-16BE BOM");
    frame->Editor()->Send(Message::SelectAll);
    frame->GetEventHandler()->ProcessEvent(insert);
    Require(TestEditor::Text(frame) == "UTF-16BE BOM", "encoding identifier distinguishes UTF-16BE BOM");
    frame->Editor()->Send(Message::Undo);
    Require(frame->Save().status == SaveStatus::Saved, "finish UTF-16BE format-only change");
    std::puts("PASS: UTF-16BE direct/F9 identities, all save variants, EOL bytes, redo and encoding identifiers");
}
class ConversionPrompt final : public wxTimer {
public:
    int shown = 0;
    bool accept = false;
    wxString title = "Encoding Conversion";
    ConversionPrompt() { Start(40); }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != title) continue;
            ++shown;
            NativeSearch::Click(dialog, accept ? wxID_YES : wxID_NO);
            return;
        }
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<ConversionPrompt*>(data);
                wchar_t title[128]{};
                GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                if (IsWindowVisible(window) && wxString(title) == driver.title) {
                    ++driver.shown;
                    SendMessageW(window, TDM_CLICK_BUTTON, driver.accept ? IDYES : IDNO, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (GTK_IS_MESSAGE_DIALOG(widget) && gtk_widget_get_visible(widget) &&
                wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == title) {
                ++shown;
                NativeDialogs::Respond(widget, accept ? GTK_RESPONSE_YES : GTK_RESPONSE_NO);
            }
        }
        g_list_free(windows);
#endif
    }
};
void AnsiTransition(EditorFrame* frame) {
    using Scintilla::Message;
    const auto native = NativeEncodingChoice();
    if (!native) return;
    const auto path = TestFiles::TemporaryDirectory("ansi-transition.txt");
    TestFiles::WriteBytes(path, "a中😀\r\n");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "ANSI fixture opens");
    auto* editor = frame->Editor();
    TestEditor::Append(frame, "x");
    editor->Send(Message::Undo);
    editor->Send(Message::MarkerAdd, 0, 0);
    editor->Send(Message::SetSel, 1, 4);
    editor->Send(Message::SetReadOnly, 1);
    {
        ConversionPrompt driver;
        const bool changed = frame->SelectEncoding(*native);
        driver.Stop();
        Require(!changed && driver.shown == 1 && TestEditor::Text(frame) == "a中😀\r\n" &&
                    editor->Send(Message::GetReadOnly) && editor->Send(Message::GetUndoActions) &&
                    editor->Send(Message::MarkerGet, 0) && editor->Send(Message::GetAnchor) == 1 &&
                    editor->Send(Message::GetCurrentPos) == 4 && !frame->GetTitle().StartsWith("*"),
                "cancelled ANSI confirmation retains text, identity, selection, read-only, markers and history");
    }
    {
        ConversionPrompt driver;
        driver.accept = true;
        Require(frame->SelectEncoding(*native), "accepted ANSI conversion changes identity");
        driver.Stop();
        const std::string expected = native->encoding == FileEncoding::Gbk    ? "a中??\r\n"
                                     : native->encoding == FileEncoding::Utf8 ? "a中😀\r\n"
                                                                              : "a???\r\n";
        const bool converted = native->encoding != FileEncoding::Utf8;
        Require(driver.shown == 1 && TestEditor::Text(frame) == expected &&
                    bool(editor->Send(Message::GetReadOnly)) == !converted &&
                    bool(editor->Send(Message::GetUndoActions)) == !converted &&
                    bool(editor->Send(Message::MarkerGet, 0)) == !converted && frame->GetTitle().StartsWith("*"),
                "accepted ANSI conversion follows native loss and clears protection, markers and history");
    }
    {
        ConversionPrompt driver;
        driver.accept = true;
        frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8));
        driver.Stop();
        Require(driver.shown == 1 && frame->GetTitle().StartsWith("*"),
                "ANSI crossing invalidates original encoding even when returning to the original format");
    }
    Require(frame->Save().status == SaveStatus::Saved, "ANSI transition fixture saves before the next scenario");
    std::puts("PASS: ANSI confirmation, native conversion and original history reset");
}
void SaveVariants(EditorFrame* frame) {
    const auto source = TestFiles::TemporaryDirectory("save-source.txt");
    const auto copy = TestFiles::TemporaryDirectory("save-copy.txt");
    const auto renamed = TestFiles::TemporaryDirectory("save-as.txt");
    TestFiles::WriteBytes(source, "中\r\n");
    Require(frame->Open(TestFiles::Filename(source), FileEncoding::Utf8).status == OpenStatus::Opened,
            "save source opens");
    const int python = frame->GetMenuBar()->FindMenuItem("Scheme", "Python");
    Require(python != wxNOT_FOUND, "Python scheme menu is delivered");
    wxCommandEvent language(wxEVT_MENU, python);
    frame->GetEventHandler()->ProcessEvent(language);
    Require(frame->SelectEncoding(EncodingChoice(FileEncoding::Gbk)) &&
                !frame->SelectEncoding(EncodingChoice(FileEncoding::Gbk)),
            "selecting current format is a no-op");
    Require(frame->SaveCopy(TestFiles::Filename(copy)).status == SaveStatus::Saved &&
                TestFiles::Bytes(copy) == "\xd6\xd0\r\n" && TestFiles::Bytes(source) == "中\r\n" &&
                frame->DocumentPath() == TestFiles::Filename(source) && frame->GetTitle().StartsWith("*") &&
                frame->GetMenuBar()->IsChecked(python),
            "Save Copy writes pending GBK without committing identity, modification or language");
    Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled && frame->GetTitle().StartsWith("*") &&
                frame->SaveAs(TestFiles::Filename(TestFiles::TemporaryDirectory())).status == SaveStatus::Failed &&
                frame->GetTitle().StartsWith("*") && frame->DocumentEncoding() == FileEncoding::Gbk,
            "cancelled and failed saves retain pending encoding");
    Require(frame->SaveAs(TestFiles::Filename(renamed)).status == SaveStatus::Saved &&
                TestFiles::Bytes(renamed) == "\xd6\xd0\r\n" && !frame->GetTitle().StartsWith("*"),
            "Save As commits the pending format");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf16Le, true));
    Require(frame->Save({}, SaveTimeMode::Original).status == SaveStatus::Saved &&
                TestFiles::Bytes(renamed) == std::string("\xff\xfe\x2d\x4e\r\0\n\0", 8) &&
                !frame->GetTitle().StartsWith("*"),
            "Save with Original Time writes the selected BOM and codec");
    Require(frame->NewDocument(), "clean saved document can be replaced");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8, true));
    TestEditor::Append(frame, "😀");
    const auto first = TestFiles::TemporaryDirectory("first.txt");
    Require(frame->SaveFirst(TestFiles::Filename(first)).status == SaveStatus::Saved &&
                TestFiles::Bytes(first) == "\xef\xbb\xbf😀",
            "first Save writes chosen UTF-8 BOM");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Gbk));
    Require(frame->Save([] { return GbkLossChoice::Cancel; }).status == SaveStatus::Cancelled &&
                frame->GetTitle().StartsWith("*") && frame->DocumentEncoding() == FileEncoding::Gbk &&
                TestFiles::Bytes(first) == "\xef\xbb\xbf😀",
            "loss cancellation retains pending GBK and original bytes");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8, true));
    Require(!frame->GetTitle().StartsWith("*"), "loss cancellation did not commit original encoding");
    std::puts("PASS: all save variants commit or retain pending format at the original transaction boundary");
}
void EmptyRules(EditorFrame* frame) {
    using Scintilla::Message;
    Require(frame->NewDocument(), "empty cases start clean");
    TestEditor::Append(frame, "x");
    frame->Editor()->Send(Message::Undo);
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8, true));
    Require(!frame->GetTitle().StartsWith("*") && frame->Editor()->Send(Message::CanRedo),
            "empty Untitled adopts BOM choice while retaining redo");
    const auto path = TestFiles::TemporaryDirectory("empty.txt");
    TestFiles::WriteBytes(path, "");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "named empty opens");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf16Le));
    Require(!frame->GetTitle().StartsWith("*"), "empty named format without changed BOM stays unmodified");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf16Le, true));
    Require(frame->GetTitle().StartsWith("*"), "empty named added BOM is modified");
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL);
        Require(!frame->NewDocument(), "format-only pending change participates in New decision");
        driver.Stop();
        Require(driver.prompts == 1 && frame->DocumentPath() == TestFiles::Filename(path) && frame->DocumentHasBom(),
                "cancelled New keeps pending empty BOM");
    }
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == "\xff\xfe",
            "empty named Save writes the BOM alone");
    TestEditor::Append(frame, "x");
    frame->Editor()->Send(Message::SelectAll);
    frame->Editor()->Send(Message::Clear);
    frame->SelectEncoding(EncodingChoice(FileEncoding::Gbk));
    Require(frame->GetTitle().StartsWith("*") && frame->Editor()->Send(Message::CanUndo),
            "empty-by-editing retains text modification and Undo when changing format");
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path).empty(), "empty GBK saves no BOM");
    if (const auto native = NativeEncodingChoice()) {
        {
            ConversionPrompt driver;
            driver.accept = true;
            frame->SelectEncoding(*native);
            driver.Stop();
        }
        TestEditor::Append(frame, "x");
        frame->Editor()->Send(Message::Undo);
        ConversionPrompt driver;
        Require(!frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8)) && frame->Editor()->Send(Message::CanRedo),
                "empty ANSI document with history requires confirmation before history loss");
        driver.Stop();
        Require(driver.shown == 1, "empty ANSI conversion displays its separate confirmation");
    }
    std::puts("PASS: original named/Untitled/emptied document BOM and modification rules");
}
void FormatOnlyDecisions(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("watched-format.txt");
    TestFiles::WriteBytes(path, "kept live");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "watcher source opens");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8, true));
    {
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL);
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Cancelled,
                "Open asks before discarding a format-only change");
        frame->Close();
        driver.Stop();
        Require(driver.prompts == 2 && !frame->IsBeingDeleted() && frame->DocumentHasBom() &&
                    frame->DocumentIsModified(),
                "cancelled Open and Close retain the format-only modification");
    }
    FileWatchingOptions options{FileWatchingMode::AutoReload};
    options.checkInterval = 20;
    options.autoReloadDelay = 20;
    frame->SetFileWatchingOptions(options);
    ConversionPrompt driver;
    driver.title = "File Change Notification";
    TestFiles::WriteBytes(path, "external disk replacement");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (!driver.shown && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    driver.Stop();
    frame->SetFileWatchingOptions({FileWatchingMode::None});
    Require(driver.shown == 1 && TestEditor::Text(frame) == "kept live" && frame->DocumentHasBom() &&
                frame->DocumentIsModified() && !frame->Editor()->Send(Scintilla::Message::GetModify),
            "automatic watcher prompts and retains pending format when external reload is declined");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf16Le, true));
    frame->Editor()->Send(Scintilla::Message::SelectAll);
    const int identifier = frame->GetMenuBar()->FindMenuItem("Edit", "Encoding Identifier");
    Require(identifier != wxNOT_FOUND, "Encoding Identifier command is delivered");
    wxCommandEvent insert(wxEVT_MENU, identifier);
    frame->GetEventHandler()->ProcessEvent(insert);
    Require(TestEditor::Text(frame) == "UTF-16LE BOM" &&
                frame->GetStatusBar()->GetStatusText().Contains("UTF-16LE (BOM)"),
            "Encoding Identifier and status report the chosen live encoding and BOM");
    std::puts("PASS: Open, Close and automatic file watching use effective format-only modification");
}
class CurrentEncodingApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        settings_.values.warnLineEndings = false;
        settings_.values.fileWatching.mode = FileWatchingMode::None;
        auto* frame = new EditorFrame(&settings_);
        frame->Show();
        wxString mode;
        const bool native = wxGetEnv("WXNOTEPAD_CURRENT_ENCODING_NATIVE_KEYS", &mode) && mode == "1";
        auto run = [this, frame, native](bool ready) {
            try {
                Require(ready, "encoding fixture is presented on the private display");
                FormatOnly(frame);
                Picker(frame, native);
                BigEndian(frame, native);
                AnsiTransition(frame);
                SaveVariants(frame);
                EmptyRules(frame);
                FormatOnlyDecisions(frame);
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
    SettingsStore settings_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(CurrentEncodingApp);
