#include "diagnostics/print.h"
#include "status_helpers.h"
#include "app/editor_frame.h"
#include "app/localization.h"
#include <array>
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "native_key_helpers.h"
#include "native_dialog_helpers.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/menu.h>
#include <wx/treectrl.h>
#include <wx/timer.h>
#include <wx/listbox.h>
#include <wx/statusbr.h>
#include <cstdio>
#include <print>
#include <stdexcept>
#include <vector>
#include <utility>
#include <filesystem>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class EncodingPicker final : public wxTimer {
public:
    bool found = false;
    int shown = 0;
    wxString initial;
    wxString selected = "Simplified Chinese (GB18030)";
    bool cancel = false;
    EncodingPicker() { Start(30); }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != wxGetTranslation("Select Encoding")) continue;
            ++shown;
            auto* tree = wxDynamicCast(dialog->FindWindowByName("Current encoding", dialog), wxTreeCtrl);
            if (tree) {
                initial = tree->GetItemText(tree->GetSelection());
                found = NativeDialogs::SelectTreeLabel(*tree, selected);
            }
            wxCommandEvent response(wxEVT_BUTTON, cancel || !found ? wxID_CANCEL : wxID_OK);
            dialog->GetEventHandler()->ProcessEvent(response);
            return;
        }
    }
};
class ReloadPicker final : public wxTimer {
public:
    wxString selected;
    wxString initial;
    bool cancel = false;
    bool found = false;
    int shown = 0;
    ReloadPicker() { Start(30); }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != wxGetTranslation("Reload With Encoding"))
                continue;
            ++shown;
            auto* list = wxDynamicCast(dialog->FindWindowByName("Source encoding", dialog), wxListBox);
            if (list) {
                if (list->GetSelection() != wxNOT_FOUND) initial = list->GetStringSelection();
                const int index = list->FindString(selected);
                if (index != wxNOT_FOUND) {
                    found = true;
                    list->SetSelection(index);
                    wxCommandEvent changed(wxEVT_LISTBOX, list->GetId());
                    list->GetEventHandler()->ProcessEvent(changed);
                }
            }
            wxCommandEvent response(wxEVT_BUTTON, cancel || !found ? wxID_CANCEL : wxID_OK);
            dialog->GetEventHandler()->ProcessEvent(response);
            return;
        }
    }
};
class LossPrompt final : public wxTimer {
public:
    bool accept = false;
    int shown = 0;
    LossPrompt() { Start(30); }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() || !dialog->GetTitle().EndsWith("character loss")) continue;
            auto* button = dialog->FindWindow(accept ? wxID_OK : wxID_CANCEL);
            if (!button) continue;
            Stop();
            ++shown;
            NativeSearch::Click(dialog, accept ? wxID_OK : wxID_CANCEL);
            return;
        }
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<LossPrompt*>(data);
                wchar_t title[128]{};
                GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                if (IsWindowVisible(window) && wxString(title).EndsWith("character loss")) {
                    driver.Stop();
                    ++driver.shown;
                    SendMessageW(window, TDM_CLICK_BUTTON, driver.accept ? IDOK : IDCANCEL, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (GTK_IS_MESSAGE_DIALOG(widget) && gtk_widget_get_visible(widget) &&
                wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))).EndsWith("character loss")) {
                Stop();
                ++shown;
                NativeDialogs::Respond(widget, accept ? GTK_RESPONSE_OK : GTK_RESPONSE_CANCEL);
            }
        }
        g_list_free(windows);
#endif
    }
};

struct Codec {
    FileEncoding encoding;
    int page;
    const char* label;
    const char* sourceLabel;
    const char* identifier;
    std::string bytes;
    std::string text;
};
const std::array<Codec, 2> codecs = {{
    {FileEncoding::Windows932, 932, "Japanese (Shift-JIS)", "Shift-JIS (CP932)", "Shift-JIS", "\xb6\xfb\xfc", "ｶ髙"},
    {FileEncoding::Windows949, 949, "Korean (Unified Hangul Code)", "Windows-949 (UHC)", "Windows-949",
     "\xb0\xa1\x81\x41", "가갂"},
}};
void Fixtures(EditorFrame* frame) {
    using Scintilla::Message;
    const auto references = std::filesystem::path(JAPANESE_KOREAN_FIXTURES);
    const auto complete = TestFiles::TemporaryDirectory("japanese-korean-complete.bin");
    for (const auto& codec : codecs) {
        TestFiles::WriteBytes(complete, TestFiles::Bytes(references / "all-single-pairs.bin"));
        Require(frame->Open(TestFiles::Filename(complete), codec.encoding).status == OpenStatus::Opened &&
                    TestEditor::Text(frame) ==
                        TestFiles::Bytes(references / ("cp" + std::to_string(codec.page) + ".utf8")),
                "every original single-byte and two-byte input produces the captured original application Unicode");
        Require(frame->Save().status == SaveStatus::Saved &&
                    TestFiles::Bytes(complete) ==
                        TestFiles::Bytes(references / ("cp" + std::to_string(codec.page) + ".saved.bin")),
                "complete original byte repertoire saves exactly the original application's canonical bytes");
    }
    struct Fixture {
        FileEncoding encoding;
        std::string input, text, saved;
    };
    // Independent Windows conversion observations, not values made by this codec.
    // Original-app captures and API exhaustive comparison: docs/encoding.md.
    const std::array<Fixture, 8> fixtures = {{
        {FileEncoding::Windows932, "\xb6\xfb\xfc\x5c\x7e\x81\x60\xfa\x40", "ｶ髙\\~～ⅰ",
         "\xb6\xfb\xfc\x5c\x7e\x81\x60\xfa\x40"},
        {FileEncoding::Windows932, "A\x81\x20\x81\x7f\x81", "A・・・", "A\x81\x45\x81\x45\x81\x45"},
        {FileEncoding::Windows932, std::string("A\x81\0B", 4), std::string("A・\0B", 6),
         std::string("A\x81\x45\0B", 5)},
        {FileEncoding::Windows932, "\x80\xa0\xfd\xfe\xff\xf0\x40", "\u0080\uf8f0\uf8f1\uf8f2\uf8f3\ue000",
         "\x80\xa0\xfd\xfe\xff\xf0\x40"},
        {FileEncoding::Windows949, "\xb0\xa1\x81\x41\x80\xc9\xa1\xfe\xfe\xff", "가갂\u0080\ue000\ue0bb\uf8f7",
         "\xb0\xa1\x81\x41\x80\xc9\xa1\xfe\xfe\xff"},
        {FileEncoding::Windows949, "A\x81\x20\x81\x7f\x81", "A???", "A???"},
        {FileEncoding::Windows949, std::string("A\x81\0B", 4), std::string("A?\0B", 4), std::string("A?\0B", 4)},
        {FileEncoding::Windows932, "\x87\x90\x81\xe0", "≒≒", "\x81\xe0\x81\xe0"},
    }};
    const auto path = TestFiles::TemporaryDirectory("japanese-korean-literals.bin");
    for (const auto& fixture : fixtures) {
        TestFiles::WriteBytes(path, fixture.input);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened &&
                    TestEditor::Text(frame) == fixture.text && frame->DocumentEncoding() == fixture.encoding &&
                    !frame->DocumentHasBom() && !frame->DocumentIsModified() &&
                    !frame->Editor()->Send(Message::CanUndo),
                "explicit Japanese/Korean file reads independent original Unicode and source identity");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.saved,
                "Japanese/Korean Save writes independently observed replacement/duplicate bytes");
    }
    for (const auto& codec : codecs) {
        for (const auto& suffix : {std::string{}, std::string("ASCII"),
                                   codec.bytes + "\r\n" + codec.bytes + "\n" + codec.bytes + std::string("\r\0", 2)}) {
            const auto expected = suffix.empty() || suffix == "ASCII"
                                      ? suffix
                                      : codec.text + "\r\n" + codec.text + "\n" + codec.text + std::string("\r\0", 2);
            TestFiles::WriteBytes(path, suffix);
            Require(frame->Open(TestFiles::Filename(path), codec.encoding).status == OpenStatus::Opened &&
                        TestEditor::Text(frame) == expected && frame->Save().status == SaveStatus::Saved &&
                        TestFiles::Bytes(path) == suffix,
                    "explicit Japanese/Korean ASCII, empty, NUL and mixed-EOL files roundtrip exactly");
        }
    }
}
void DestinationAndReload(EditorFrame* frame, bool native) {
    using enum Scintilla::Message;
    const auto path = TestFiles::TemporaryDirectory("japanese-korean-picker.bin");
    for (const auto& codec : codecs) {
        TestFiles::WriteBytes(path, codec.text + "\r\n");
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
                "UTF-8 source opens");
        TestEditor::Append(frame, "x");
        frame->Editor()->Send(Undo);
        frame->Editor()->Send(SetSel, 0, static_cast<intptr_t>(codec.text.size()));
        {
            EncodingPicker driver;
            driver.selected = codec.label;
            if (native)
                NativeKeys::Shortcut(frame, WXK_F9, false);
            else {
                wxCommandEvent command(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("File", "More..."));
                frame->GetEventHandler()->ProcessEvent(command);
            }
            driver.Stop();
            Require(driver.shown == 1 && driver.found && driver.initial == "UTF-8" &&
                        frame->DocumentEncoding() == codec.encoding &&
                        StatusMetadata(frame).Contains(codec.sourceLabel) &&
                        TestEditor::Text(frame) == codec.text + "\r\n" && frame->Editor()->Send(CanRedo) &&
                        frame->Editor()->Send(GetCurrentPos) == static_cast<intptr_t>(codec.text.size()) &&
                        frame->DocumentIsModified(),
                    "F9 selects exact current codec and preserves text, selection and Redo");
        }
        {
            EncodingPicker driver;
            driver.selected = "UTF-8";
            driver.cancel = true;
            wxCommandEvent command(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("File", "More..."));
            frame->GetEventHandler()->ProcessEvent(command);
            driver.Stop();
            Require(driver.initial == codec.label && frame->DocumentEncoding() == codec.encoding &&
                        frame->Editor()->Send(CanRedo),
                    "cancel current selection retains codec and Redo");
        }
        wxCommandEvent identifier(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("Edit", "Encoding Identifier"));
        frame->GetEventHandler()->ProcessEvent(identifier);
        Require(TestEditor::Text(frame) == std::string(codec.identifier) + "\r\n",
                "identifier uses original parse name");
        frame->Editor()->Send(Undo);
        Require(TestEditor::Text(frame) == codec.text + "\r\n", "identifier insertion has one-step Undo");
        Require(frame->Save().status == SaveStatus::Saved && !frame->DocumentIsModified() &&
                    TestFiles::Bytes(path) == codec.bytes + "\r\n",
                "current conversion Save writes exact bytes");
        TestFiles::WriteBytes(path, "live");
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
                "reload begins UTF-8");
        TestFiles::WriteBytes(path, codec.bytes);
        {
            ReloadPicker driver;
            driver.selected = codec.sourceLabel;
            if (native)
                NativeKeys::Shortcut(frame, WXK_F8, false);
            else
                Require(frame->ReloadWithEncoding().status == OpenStatus::Opened, "source selection reloads");
            driver.Stop();
            Require(driver.shown == 1 && driver.found && driver.initial == "UTF-8" &&
                        frame->DocumentEncoding() == codec.encoding && TestEditor::Text(frame) == codec.text &&
                        !frame->DocumentIsModified() && !frame->Editor()->Send(CanUndo) &&
                        TestFiles::Bytes(path) == codec.bytes,
                    "F8 reinterprets disk bytes and establishes new source save point without saving live text");
        }
        ReloadPicker cancel;
        cancel.selected = "UTF-8";
        cancel.cancel = true;
        Require(frame->ReloadWithEncoding().status == OpenStatus::Cancelled,
                "source picker cancellation returns Cancelled");
        cancel.Stop();
        Require(cancel.initial == codec.sourceLabel && TestEditor::Text(frame) == codec.text &&
                    frame->DocumentEncoding() == codec.encoding,
                "cancel source selection preserves loaded identity and text");
    }
}
void Saves(EditorFrame* frame) {
    using enum Scintilla::Message;
    const auto source = TestFiles::TemporaryDirectory("japanese-korean-source.txt");
    const auto copy = TestFiles::TemporaryDirectory("japanese-korean-copy.txt");
    const auto target = TestFiles::TemporaryDirectory("japanese-korean-as.txt");
    for (const auto& codec : codecs) {
        const auto sample = codec.text + "¥😀\r\n";
        const auto expected = codec.bytes + "???\r\n";
        TestFiles::WriteBytes(source, "unchanged");
        Require(frame->Open(TestFiles::Filename(source), FileEncoding::Utf8).status == OpenStatus::Opened,
                "save source opens");
        frame->Editor()->Send(SelectAll);
        frame->Editor()->Send(ReplaceSel, 0, reinterpret_cast<intptr_t>(sample.c_str()));
        frame->Editor()->Send(SetSel, 1, 4);
        frame->SelectEncoding(EncodingChoice(codec.encoding));
        {
            LossPrompt driver;
            Require(frame->Save().status == SaveStatus::Cancelled, "native no-best-fit loss prompt cancels Save");
            driver.Stop();
            Require(driver.shown == 1 && TestFiles::Bytes(source) == "unchanged" && TestEditor::Text(frame) == sample &&
                        frame->DocumentIsModified() && frame->Editor()->Send(CanUndo) &&
                        frame->Editor()->Send(GetAnchor) == 1 && frame->Editor()->Send(GetCurrentPos) == 4,
                    "loss Cancel preserves rich text, disk, selection and history");
        }
        Require(frame->SaveCopy(TestFiles::Filename(copy), [] { return EncodingLossChoice::Cancel; }).status ==
                        SaveStatus::Cancelled &&
                    std::filesystem::exists(copy) && TestFiles::Bytes(copy).empty(),
                "original cancelled Save Copy retains its newly created empty target");
        Require(frame->SaveAs(
                         TestFiles::Filename(TestFiles::TemporaryDirectory()),
                         [] {
                             return EncodingLossChoice::Replace;
                         }).status == SaveStatus::Failed &&
                    frame->DocumentPath() == TestFiles::Filename(source) && frame->DocumentIsModified() &&
                    TestFiles::Bytes(source) == "unchanged",
                "failed Save As retains identity and original disk bytes");
        Require(frame->SaveCopy(TestFiles::Filename(copy), [] { return EncodingLossChoice::Replace; }).status ==
                        SaveStatus::Saved &&
                    TestFiles::Bytes(copy) == expected && TestFiles::Bytes(source) == "unchanged" &&
                    frame->DocumentPath() == TestFiles::Filename(source) && frame->DocumentIsModified(),
                "Save Copy writes exact bytes without committing identity");
        std::filesystem::remove(copy);
        {
            LossPrompt driver;
            driver.accept = true;
            Require(frame->SaveAs(TestFiles::Filename(target)).status == SaveStatus::Saved,
                    "accepted native loss Save As succeeds");
            driver.Stop();
            Require(driver.shown == 1 && TestFiles::Bytes(target) == expected && TestEditor::Text(frame) == sample &&
                        !frame->DocumentIsModified() && frame->DocumentEncoding() == codec.encoding,
                    "Save As keeps rich text at the save point");
        }
        frame->Editor()->Send(Undo);
        Require(TestEditor::Text(frame) == "unchanged" && frame->DocumentIsModified(),
                "post-Save Undo restores original edit");
        frame->Editor()->Send(Redo);
        Require(TestEditor::Text(frame) == sample && !frame->DocumentIsModified(),
                "post-Save Redo restores rich text at save point");
        Require(frame->Save([] { return EncodingLossChoice::Replace; }, SaveTimeMode::Original).status ==
                        SaveStatus::Saved &&
                    TestFiles::Bytes(target) == expected,
                "Save with Original Time uses current codec");
        Require(frame->NewDocument(), "saved file becomes Untitled");
        frame->SelectEncoding(EncodingChoice(codec.encoding));
        TestEditor::Append(frame, codec.text);
        Require(frame->SaveFirst(TestFiles::Filename(target)).status == SaveStatus::Saved &&
                    TestFiles::Bytes(target) == codec.bytes && !frame->DocumentIsModified(),
                "first Save uses explicit codec");
    }
}
void Detection(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("japanese-korean-detection.txt");
    for (const auto& codec : codecs) {
        const std::vector<const char*> aliases =
            codec.page == 932
                ? std::vector<const char*>{"Shift-JIS", "shiftjis", "csshiftjis", "cswindows31j", "mskanji",
                                           "xmscp932",  "xsjis",    "cp932",      "sjis",         "ms932"}
                : std::vector<const char*>{"Windows-949", "windows949", "ksx1001", "ksc56011987",
                                           "csksc5601",   "isoir149",   "korean",  "ksc56011989",
                                           "cp949",       "ms949",      "uhc"};
        for (const auto* alias : aliases) {
            const std::string prefix = std::string("# coding: ") + alias + "\n";
            TestFiles::WriteBytes(path, prefix + codec.bytes);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                        frame->DocumentEncoding() == codec.encoding && TestEditor::Text(frame) == prefix + codec.text,
                    "original aliases select real CP932/949 decoding");
            TestFiles::WriteBytes(path, prefix + codec.text);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                        frame->DocumentEncoding() == FileEncoding::Utf8,
                    "valid UTF-8 retains declaration precedence");
        }
        TestFiles::WriteBytes(path, codec.bytes);
        const auto opened = frame->Open(TestFiles::Filename(path));
        Require(opened.status != OpenStatus::Opened || (frame->DocumentEncoding() != FileEncoding::Windows932 &&
                                                        frame->DocumentEncoding() != FileEncoding::Windows949),
                "no Japanese/Korean decodability guess is introduced");
        SettingsStore settings;
        settings.values.warnLineEndings = false;
        settings.values.fileWatching.mode = FileWatchingMode::None;
        settings.values.encoding.defaultEncoding = codec.page;
        auto* configured = new EditorFrame(&settings);
        Require(configured->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                    configured->DocumentEncoding() == codec.encoding && TestEditor::Text(configured) == codec.text,
                "numeric original default selects CP932/949");
        configured->Destroy();
    }
}
void Backups() {
    using enum Scintilla::Message;
    constexpr unsigned BackupPeriodMilliseconds = 125;
    constexpr unsigned BackupDeadlineMilliseconds = 2000;
    // Match the existing save_backup/periodic_backup fixture boundary:
    // Windows' .tmp reservation would invoke the original first-dot fallback.
    struct BackupRoot {
        std::filesystem::path path = TestFiles::TemporaryDirectory();
        BackupRoot() {
            path.replace_extension();
            path += "-japanese-korean-backup";
            Require(std::filesystem::create_directory(path), "creates private dot-free backup fixture root");
        }
        ~BackupRoot() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } root;
    for (const auto& codec : codecs) {
        const auto folder = root.path / ("backup-" + std::to_string(codec.page));
        std::filesystem::create_directory(folder);
        const auto path = folder / "source.txt";
        TestFiles::WriteBytes(path, codec.bytes);
        SettingsStore settings;
        settings.values.warnLineEndings = false;
        settings.values.fileWatching.mode = FileWatchingMode::None;
        settings.values.autoSave.flags = AutoSaveOption::Periodic | AutoSaveOption::ManuallyDelete;
        settings.values.autoSave.periodMilliseconds = BackupPeriodMilliseconds;
        auto* frame = new EditorFrame(&settings);
        Require(frame->Open(TestFiles::Filename(path), codec.encoding).status == OpenStatus::Opened,
                "backup loads explicit Japanese/Korean source");
        TestEditor::Append(frame, "!");
        const auto backups = [&] {
            std::vector<std::filesystem::path> result;
            for (const auto& item : std::filesystem::directory_iterator(folder))
                if (item.path() != path) result.push_back(item.path());
            return result;
        };
        wxStopWatch clock;
        while (backups().empty() && clock.Time() < BackupDeadlineMilliseconds) {
            wxYield();
            wxMilliSleep(5);
        }
        Require(backups().size() == 1 && TestFiles::Bytes(backups().front()) == codec.text + "!",
                "real automatic backup retains the original explicit-legacy UTF-8 editor buffer boundary");
        wxCommandEvent command(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("File", "Save Backup"));
        frame->GetEventHandler()->ProcessEvent(command);
        Require(backups().size() == 2, "manual Save Backup creates another complete file");
        for (const auto& backup : backups())
            Require(TestFiles::Bytes(backup) == codec.text + "!",
                    "manual and automatic explicit-legacy backups share the original UTF-8 buffer bytes");
        Require(TestFiles::Bytes(path) == codec.bytes && frame->DocumentPath() == TestFiles::Filename(path) &&
                    frame->DocumentEncoding() == codec.encoding && frame->DocumentIsModified() &&
                    frame->Editor()->Send(CanUndo) && TestEditor::Text(frame) == codec.text + "!",
                "backup retains original disk file, identity, live text, save point and Undo");
        frame->Destroy();
    }
}
void LocalizedPickers(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("localized-japanese-korean.txt");
    for (const auto& codec : codecs) {
        const auto label = wxGetTranslation(codec.label);
        Require(label != codec.label, "release Chinese catalog translates the new original encoding choice");
        TestFiles::WriteBytes(path, codec.text);
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
                "localized workflow opens independent Unicode source");
        {
            EncodingPicker driver;
            driver.selected = label;
            NativeKeys::Shortcut(frame, WXK_F9, false);
            driver.Stop();
            Require(driver.shown == 1 && driver.found && frame->DocumentEncoding() == codec.encoding &&
                        TestEditor::Text(frame) == codec.text && StatusMetadata(frame).Contains(codec.sourceLabel),
                    "translated F9 picker selects the actual original codec and distinct status");
        }
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == codec.bytes,
                "translated workflow Save writes exact original file bytes");
        {
            ReloadPicker driver;
            driver.selected = codec.sourceLabel;
            NativeKeys::Shortcut(frame, WXK_F8, false);
            driver.Stop();
            Require(driver.shown == 1 && driver.found && driver.initial == codec.sourceLabel &&
                        TestEditor::Text(frame) == codec.text && frame->DocumentEncoding() == codec.encoding &&
                        !frame->DocumentIsModified(),
                    "translated F8 source reload preserves Unicode, codec and save point");
        }
    }
}
class JapaneseKoreanApp final : public wxApp {
public:
    bool OnInit() override {
        wxString language;
        const bool localized = wxGetEnv("WXNOTEPAD_JAPANESE_KOREAN_LANGUAGE", &language);
        if (localized) AppLocalization::Initialize(static_cast<uint16_t>(wxAtoi(language)));
        settings_.values.warnLineEndings = false;
        settings_.values.fileWatching.mode = FileWatchingMode::None;
        auto* frame = new EditorFrame(&settings_);
        frame->Show();
        wxString mode;
        const bool native = wxGetEnv("WXNOTEPAD_JAPANESE_KOREAN_NATIVE_KEYS", &mode) && mode == "1";
        auto run = [this, frame, native, localized](bool ready) {
            try {
                Require(ready, "Japanese/Korean fixture presents on its owned display");
                if (localized)
                    LocalizedPickers(frame);
                else {
                    Fixtures(frame);
                    DestinationAndReload(frame, native);
                    Saves(frame);
                    Detection(frame);
                    Backups();
                }
                std::puts("PASS: original Japanese/Korean file workflows");
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
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
wxIMPLEMENT_APP(JapaneseKoreanApp);
