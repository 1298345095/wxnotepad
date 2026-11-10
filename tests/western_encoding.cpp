#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "native_key_helpers.h"
#include "native_search_helpers.h"
#include "file_command_driver.h"
#include <wx/wx.h>
#include <wx/treectrl.h>
#include <wx/listbox.h>
#include <wx/statusbr.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Command(EditorFrame* frame, const char* menu, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem(menu, label);
    Require(id != wxNOT_FOUND, "encoding command exists");
    wxCommandEvent event(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(event);
}
void OriginalBytes(EditorFrame* frame) {
    using Scintilla::Message;
    const std::filesystem::path fixtures(WESTERN_ENCODING_FIXTURES);
    for (const auto& [name, encoding] :
         {std::pair{"cp1252", FileEncoding::Windows1252}, std::pair{"cp437", FileEncoding::Dos437}}) {
        const auto input = TestFiles::Bytes(fixtures / (std::string(name) + "-all256.bin"));
        const auto expected = TestFiles::Bytes(fixtures / (std::string(name) + "-all256.utf8"));
        const auto path = TestFiles::TemporaryDirectory(std::string(name) + ".txt");
        TestFiles::WriteBytes(path, input);
        Require(frame->Open(TestFiles::Filename(path), encoding).status == OpenStatus::Opened &&
                    TestEditor::Text(frame) == expected && frame->DocumentEncoding() == encoding,
                "all 256 explicit input bytes match independent original UTF-8 capture, including NUL");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == input,
                "all 256 original bytes save without warning or loss");
        TestEditor::Append(frame, "x");
        frame->Editor()->Send(Message::Undo);
        Require(frame->Editor()->Send(Message::CanRedo), "original reload fixture retains redo");
        frame->Editor()->Send(Message::SelectAll);
        Command(frame, "Edit", "Encoding Identifier");
        Require(TestEditor::Text(frame) == (encoding == FileEncoding::Windows1252 ? "Windows-1252" : "IBM437"),
                "Encoding Identifier uses original catalog identifier");
        Require(frame->Save().status == SaveStatus::Saved, "identifier saves");
    }
    std::puts("PASS: original all-256 decoding and exact save, NUL, C1 controls, box drawing and identifiers");
}
class Picker final : public wxTimer {
public:
    wxString selected;
    int shown = 0;
    bool reload = false;
    bool cancel = false;
    explicit Picker(wxString label, bool source = false) : selected(std::move(label)), reload(source) { Start(30); }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() ||
                dialog->GetTitle() != (reload ? "Reload With Encoding" : "Select Encoding"))
                continue;
            ++shown;
            bool found = false;
            if (reload) {
                auto* list = wxDynamicCast(dialog->FindWindowByName("Source encoding", dialog), wxListBox);
                const auto index = list ? list->FindString(selected) : wxNOT_FOUND;
                if (index != wxNOT_FOUND) {
                    list->SetSelection(index);
                    found = true;
                }
            } else {
                auto* tree = wxDynamicCast(dialog->FindWindowByName("Current encoding", dialog), wxTreeCtrl);
                if (tree) found = NativeDialogs::SelectTreeLabel(*tree, selected);
            }
            wxCommandEvent event(wxEVT_BUTTON, found && !cancel ? wxID_OK : wxID_CANCEL);
            dialog->GetEventHandler()->ProcessEvent(event);
        }
    }
};
void Pickers(EditorFrame* frame, bool native) {
    const auto path = TestFiles::TemporaryDirectory("picker-western.txt");
    TestFiles::WriteBytes(path, "€");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "western picker source opens");
    {
        Picker driver("Latin-1 (Windows-1252)");
        if (native)
            NativeKeys::Shortcut(frame, WXK_F9, false);
        else
            Command(frame, "File", "More...");
        driver.Stop();
        Require(driver.shown == 1 && frame->DocumentEncoding() == FileEncoding::Windows1252 &&
                    TestEditor::Text(frame) == "€" && TestFiles::Bytes(path) == "€" && frame->DocumentIsModified(),
                "F9 chooses original Windows-1252 destination without reading or writing source");
    }
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == "\x80",
            "selected Windows-1252 saves exact Euro byte");
    TestFiles::WriteBytes(path, "\xda\xc4\xbf");
    {
        Picker driver("DOS-437", true);
        if (native)
            NativeKeys::Shortcut(frame, WXK_F8, false);
        else
            Require(frame->ReloadWithEncoding().status == OpenStatus::Opened, "reload DOS-437 succeeds");
        driver.Stop();
        Require(driver.shown == 1 && frame->DocumentEncoding() == FileEncoding::Dos437 &&
                    TestEditor::Text(frame) == "┌─┐",
                "F8 reloads explicit DOS-437 source bytes");
    }
    {
        Picker driver("Latin-1 (Windows-1252)");
        driver.cancel = true;
        Command(frame, "File", "More...");
        driver.Stop();
        Require(driver.shown == 1 && frame->DocumentEncoding() == FileEncoding::Dos437 && !frame->DocumentIsModified(),
                "F9 Cancel preserves current codec and saved state");
    }
    Require(frame->GetStatusBar()->GetStatusText().Contains("DOS-437"), "status reports DOS-437");
    std::puts("PASS: native F9/F8 codec choices, cancellation and disk bytes");
}
void NativeOem(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("native-oem.txt");
    TestFiles::WriteBytes(path, "plain");
    EncodingOptions options;
    options.defaultEncoding = 5;
    options.loadASCIIasUTF8 = false;
    DocumentSession document(*frame->Editor(), options);
    Require(document.Open(TestFiles::Filename(path), [] { return true; }).status == OpenStatus::Opened,
            "delivered host-native OEM default opens");
    const auto choice = document.CurrentEncoding();
    Require(OriginalEncodingIdentifier(choice) == "OEM" && !document.IsDefaultEncoding() &&
                choice != EncodingChoice(choice.encoding),
            "OEM is a separate original semantic identity");
    TestEditor::Append(frame, "x");
    frame->Editor()->Send(Scintilla::Message::Undo);
    Require(document.Open(
                        TestFiles::Filename(path), [] { return true; }, {}, choice)
                        .status == OpenStatus::Opened &&
                document.CurrentEncoding() == choice && document.Save().status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == "plain",
            "weak reload and Save preserve native OEM identity and bytes");
    Require(document.SelectEncoding(EncodingChoice(choice.encoding)) && document.IsModified() &&
                OriginalEncodingIdentifier(document.CurrentEncoding()) != "OEM",
            "explicit codec differs from resolved OEM even when disk bytes are equal");
    Require(document.SelectEncoding(choice) && !document.IsModified(), "return to saved OEM clears format-only change");
    std::puts("PASS: host-native OEM identity remains distinct through default, reload and Save");
}
void LossAndSaveVariants(EditorFrame* frame) {
    const std::filesystem::path fixtures(WESTERN_ENCODING_FIXTURES);
    const auto source = TestFiles::TemporaryDirectory("western-loss.txt");
    const auto copy = TestFiles::TemporaryDirectory("western-copy.txt");
    const auto renamed = TestFiles::TemporaryDirectory("western-renamed.txt");
    for (const auto& [name, encoding] :
         {std::pair{"cp1252", FileEncoding::Windows1252}, std::pair{"cp437", FileEncoding::Dos437}}) {
        const std::string sample = "A€∞Ā中😀\ue000\r\n";
        TestFiles::WriteBytes(source, "unchanged");
        Require(frame->Open(TestFiles::Filename(source), FileEncoding::Utf8).status == OpenStatus::Opened,
                "loss source opens");
        frame->Editor()->Send(Scintilla::Message::SetSel, 0, 9);
        frame->Editor()->Send(Scintilla::Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(sample.c_str()));
        Require(frame->SelectEncoding(EncodingChoice(encoding)), "legacy destination selected");
        int warnings = 0;
        Require(frame->Save([&warnings] {
                         ++warnings;
                         return EncodingLossChoice::Cancel;
                     }).status == SaveStatus::Cancelled &&
                    warnings == 1 && TestFiles::Bytes(source) == "unchanged" && TestEditor::Text(frame) == sample &&
                    frame->DocumentIsModified() && frame->Editor()->Send(Scintilla::Message::CanUndo),
                "loss Cancel preserves independent disk bytes, live rich text, Undo and pending identity");
        const auto accept = [&warnings] {
            ++warnings;
            return EncodingLossChoice::Replace;
        };
        const auto expected = TestFiles::Bytes(fixtures / (std::string("save-") + name + ".bin"));
        Require(frame->SaveCopy(TestFiles::Filename(copy), accept).status == SaveStatus::Saved &&
                    TestFiles::Bytes(copy) == expected && TestFiles::Bytes(source) == "unchanged" &&
                    frame->DocumentPath() == TestFiles::Filename(source) && frame->DocumentIsModified() &&
                    TestEditor::Text(frame) == sample,
                "accepted Save Copy matches original substitution bytes and retains live identity/state");
        Require(frame->SaveAs(TestFiles::Filename(TestFiles::TemporaryDirectory()), accept).status ==
                        SaveStatus::Failed &&
                    frame->DocumentPath() == TestFiles::Filename(source) && frame->DocumentIsModified() &&
                    TestFiles::Bytes(source) == "unchanged",
                "failed safe-save retains source and pending codec");
        Require(frame->SaveAs(TestFiles::Filename(renamed), accept).status == SaveStatus::Saved &&
                    TestFiles::Bytes(renamed) == expected && !frame->DocumentIsModified() &&
                    TestEditor::Text(frame) == sample && frame->Editor()->Send(Scintilla::Message::CanUndo),
                "Save As accepts original loss bytes without replacing live text or clearing Undo");
        Require(frame->Save(accept, SaveTimeMode::Original).status == SaveStatus::Saved &&
                    TestFiles::Bytes(renamed) == expected,
                "Original Time Save preserves selected codec");
        Require(frame->NewDocument(), "saved document closes");
        frame->SelectEncoding(EncodingChoice(encoding));
        TestEditor::Append(frame, encoding == FileEncoding::Windows1252 ? "€" : "∞");
        const auto first = TestFiles::TemporaryDirectory(std::string(name) + "-first.txt");
        Require(frame->SaveFirst(TestFiles::Filename(first)).status == SaveStatus::Saved &&
                    TestFiles::Bytes(first) == (encoding == FileEncoding::Windows1252 ? "\x80" : "\xec"),
                "first Save writes selected single-byte codec");
    }
    std::puts("PASS: original loss Cancel/accept, safe-save failure, Save Copy and Save variants");
}
class WarningDriver final : public wxTimer {
public:
    int shown = 0;
    bool suppress = false;
    bool checkbox = false;
    bool enabled = true;
    bool yesNo = false;
    WarningDriver() { Start(30); }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal() ||
                dialog->GetTitle() != (yesNo ? "Encoding Conversion" : "Encoding character loss"))
                continue;
            ++shown;
            auto* check = wxDynamicCast(dialog->FindWindowByName("Suppress message", dialog), wxCheckBox);
            checkbox = check != nullptr;
            enabled = check && check->IsEnabled();
            if (suppress && check) NativeSearch::Toggle(check, true);
            NativeSearch::Click(dialog, yesNo ? wxID_NO : wxID_CANCEL);
            return;
        }
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL {
                wchar_t title[128]{};
                GetWindowTextW(window, title, 128);
                if (IsWindowVisible(window) && wxString(title).EndsWith("character loss")) {
                    ++reinterpret_cast<WarningDriver*>(data)->shown;
                    SendMessageW(window, TDM_CLICK_BUTTON, IDCANCEL, 0);
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
                ++shown;
                NativeDialogs::Respond(widget, GTK_RESPONSE_CANCEL);
            }
        }
        g_list_free(windows);
#endif
    }
};
void Suppression(EditorFrame* frame) {
    wxString mode;
    if (!wxGetEnv("WXNOTEPAD_WESTERN_SUPPRESSION", &mode)) return;
    const auto path = TestFiles::TemporaryDirectory("suppression.txt");
    TestFiles::WriteBytes(path, "😀");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "suppression source opens");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Windows1252));
    WarningDriver driver;
    driver.suppress = mode == "write";
    const auto result = frame->Save();
    driver.Stop();
    if (mode == "never") {
        Require(result.status == SaveStatus::Cancelled && driver.shown == 1 && driver.checkbox && !driver.enabled,
                "original MsgConv3 mode 2 leaves suppression disabled");
        return;
    } else if (mode == "write") {
        Require(result.status == SaveStatus::Cancelled && driver.shown == 1 && driver.checkbox &&
                    TestFiles::Bytes(path) == "😀" && frame->DocumentIsModified(),
                "original MsgConv3 Cancel plus suppression preserves disk and pending state");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == "??",
                "suppressed loss warning accepts next save");
    } else {
        Require(result.status == SaveStatus::Saved && driver.shown == 0 && TestFiles::Bytes(path) == "??",
                "fresh process restores original MsgConv3 suppression");
    }
    if (const auto native = NativeEncodingChoice()) {
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
                "ANSI suppression fixture opens");
        WarningDriver ansi;
        ansi.yesNo = true;
        ansi.suppress = mode == "write";
        const bool changed = frame->SelectEncoding(*native);
        ansi.Stop();
        Require(mode == "write" ? !changed && ansi.shown == 1 && ansi.checkbox : changed && ansi.shown == 0,
                "native ANSI uses shared original MsgConv1 No/suppression behavior");
        if (!changed)
            Require(frame->SelectEncoding(*native), "suppressed ANSI confirmation accepts the next transition");
        // The immediate native-ANSI path saves without destination-loss warning.
        Require(frame->Save().status == SaveStatus::Saved, "native ANSI conversion is immediate before Save");
        TestEditor::Append(frame, "x");
        frame->Editor()->Send(Scintilla::Message::SelectAll);
        frame->Editor()->Send(Scintilla::Message::Clear);
        WarningDriver empty;
        empty.yesNo = true;
        empty.suppress = mode == "write";
        const bool emptyChanged = frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8));
        empty.Stop();
        Require(mode == "write" ? !emptyChanged && empty.shown == 1 && empty.checkbox
                                : emptyChanged && empty.shown == 0,
                "empty native ANSI history uses distinct original MsgConv2 suppression");
        if (!emptyChanged)
            Require(frame->SelectEncoding(EncodingChoice(FileEncoding::Utf8)),
                    "suppressed empty ANSI transition accepts the next choice");
    }
    std::puts("PASS: shared original loss warning suppression and independent disk bytes");
}
wxString ExplicitOpenChooserState(const wxString& expected) {
#ifdef __WXGTK__
    const auto classify = [](const char* value, const wxString& target) {
        return !value ? "none" : wxString::FromUTF8(value) == target ? "match" : "other";
    };
    wxString state = "chooser=none";
    GList* windows = gtk_window_list_toplevels();
    for (auto* item = windows; item; item = item->next) {
        auto* widget = GTK_WIDGET(item->data);
        if (!gtk_widget_get_visible(widget)) continue;
        if (!GTK_IS_FILE_CHOOSER(widget)) continue;
        char* folder = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(widget));
        char* selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widget));
        auto* accept = gtk_dialog_get_widget_for_response(GTK_DIALOG(widget), GTK_RESPONSE_ACCEPT);
        state = "chooser=present folder=" + wxString(classify(folder, wxFileName(expected).GetPath())) +
                " selected=" + classify(selected, expected) + " accept=" +
                (!accept                           ? "absent"
                 : gtk_widget_is_sensitive(accept) ? "enabled"
                                                   : "disabled") +
                " filter='" + NativeDialogs::CurrentFileType(widget) + "'";
        g_free(folder);
        g_free(selected);
        break;
    }
    g_list_free(windows);
    return state;
#else
    (void)expected;
    return {};
#endif
}
void ExplicitOpen(EditorFrame* frame) {
    struct Fixture {
        const char* label;
        FileEncoding encoding;
        const char* input;
        const char* text;
    };
    constexpr Fixture fixtures[] = {{"Open As Windows-1252...", FileEncoding::Windows1252, "\x80", "€"},
                                    {"Open As DOS-437...", FileEncoding::Dos437, "\xec", "∞"}};
    for (const auto& fixture : fixtures) {
        const auto path = TestFiles::TemporaryDirectory("open-as-western.nfo");
        TestFiles::WriteBytes(path, fixture.input);
        wxString previousState;
        const auto trace = [label = std::string(fixture.label), expected = TestFiles::Filename(path),
                            &previousState](const char* transition) {
            const std::string_view phase(transition);
            if (phase.starts_with("driver callback")) {
                if (phase != "driver callback exit") return;
                const auto state = ExplicitOpenChooserState(expected);
                if (state.empty() || state == previousState) return;
                previousState = state;
                std::fprintf(stderr, "WESTERN-OPEN [%s] %s\n", label.c_str(), state.utf8_str().data());
            } else
                std::fprintf(stderr, "WESTERN-OPEN [%s] %s\n", label.c_str(), transition);
            std::fflush(stderr);
        };
        NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Open, wxID_OK,
                                                TestFiles::Filename(path), trace);
        trace("command begin");
        Command(frame, "File", fixture.label);
        trace("command returned");
        driver.Stop();
        Require(frame->DocumentEncoding() == fixture.encoding && TestEditor::Text(frame) == fixture.text,
                "explicit native Open As overrides filename preference and keeps original bytes");
    }
    std::puts("PASS: explicit native Open As Windows-1252 and DOS-437");
}
class WesternApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        settings_.values.warnLineEndings = false;
        settings_.values.fileWatching.mode = FileWatchingMode::None;
        auto* frame = new EditorFrame(&settings_);
        frame->Show();
        wxString mode;
        const bool native = wxGetEnv("WXNOTEPAD_WESTERN_NATIVE_KEYS", &mode) && mode == "1";
        auto run = [this, frame, native](bool ready) {
            try {
                Require(ready, "owned test window is presented");
                wxString suppression;
                if (!wxGetEnv("WXNOTEPAD_WESTERN_SUPPRESSION", &suppression)) {
                    OriginalBytes(frame);
                    Pickers(frame, native);
                    ExplicitOpen(frame);
                    LossAndSaveVariants(frame);
                    NativeOem(frame);
                }
                Suppression(frame);
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
wxIMPLEMENT_APP(WesternApp);
