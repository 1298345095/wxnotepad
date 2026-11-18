#include "app/editor_frame.h"
#include <array>
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
#include <tuple>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct QuickCommand {
    const char* label;
    int key;
    bool alt = false;
};
constexpr QuickCommand asAnsi{"As ANSI", 'A'};
constexpr QuickCommand asOem{"As OEM", 'O'};
constexpr QuickCommand asDefault{"Default Encoding", 'F', true};

void NativeQuickCommand(EditorFrame* frame, const QuickCommand& command, bool useShortcut) {
    const auto menuId = frame->GetMenuBar()->FindMenuItem("File", command.label);
    Require(menuId != wxNOT_FOUND, "File Reload must contain the quick encoding command");
    if (useShortcut)
        NativeKeys::Shortcut(frame, command.key, true, command.alt, !command.alt);
    else
        NativeSearch::Command(frame, menuId);
}
struct NativeQuickSamples {
    std::string_view text;
    std::string_view signatureText;
};
NativeQuickSamples QuickSamples(FileEncoding encoding) {
    using enum FileEncoding;
    switch (encoding) {
    case Utf8:
        return {"é\n", "\x{ef}\x{bb}\x{bf}text\n"};
    case Gbk:
    case Gb18030:
        return {"茅\n", "\x{e9}\x{94}\x{98}\x{e7}\x{b8}\x{af}ext\n"};
    case Big5:
        return {"矇\n", "\x{e5}\x{9a}\x{9c}\x{e7}\x{af}\x{99}ext\n"};
    case Windows1252:
        return {"Ã©\n", "ï»¿text\n"};
    case Dos437:
        return {"├⌐\n", "∩╗┐text\n"};
    default:
        throw std::runtime_error("fixture must use a supported native code page");
    }
}
void DefaultQuickReload(EditorFrame* frame, bool useShortcut, FileEncoding expected, std::string_view bytes,
                        std::string_view text, bool bom = false) {
    const auto path = TestFiles::TemporaryDirectory("default-quick.txt");
    TestFiles::WriteBytes(path, "before");
    const auto previous = expected == FileEncoding::Utf8 ? FileEncoding::Windows1252 : FileEncoding::Utf8;
    Require(frame->Open(TestFiles::Filename(path), previous).status == OpenStatus::Opened,
            "default quick reload fixture must start in a different encoding");
    TestFiles::WriteBytes(path, bytes);
    NativeQuickCommand(frame, asDefault, useShortcut);
    Require(frame->DocumentEncoding() == expected && frame->DocumentHasBom() == bom &&
                TestEditor::Text(frame) == text && TestFiles::Bytes(path) == bytes,
            "quick default must decode using configured source and measure BOM identity from disk");
}
void ConfiguredQuickReload() {
    using enum FileEncoding;
    struct Fixture {
        int setting;
        FileEncoding encoding;
        std::string_view bytes;
        std::string_view bomBytes;
        std::string_view text;
    };
    constexpr auto fixtures = std::to_array<Fixture>({
        {1252, Windows1252, "\x{c3}\x{a9}\n", {}, "Ã©\n"},
        {4, Utf8, "\x{c3}\x{a9}\n", "\x{ef}\x{bb}\x{bf}\x{c3}\x{a9}\n", "é\n"},
        {1, Utf16Le, {"\x{e9}\0\n\0", 4}, {"\x{ff}\x{fe}\x{e9}\0\n\0", 6}, "é\n"},
        {2, Utf16Be, {"\0\x{e9}\0\n", 4}, {"\x{fe}\x{ff}\0\x{e9}\0\n", 6}, "é\n"},
    });
    for (const auto& fixture : fixtures) {
        SettingsStore settings;
        settings.values.encoding.defaultEncoding = fixture.setting;
        settings.values.fileWatching.mode = FileWatchingMode::None;
        const auto frame = std::make_unique<EditorFrame>(&settings);
        DefaultQuickReload(frame.get(), false, fixture.encoding, fixture.bytes, fixture.text);
        if (!fixture.bomBytes.empty())
            DefaultQuickReload(frame.get(), false, fixture.encoding, fixture.bomBytes, fixture.text, true);
        Require(settings.values.encoding.defaultEncoding == fixture.setting,
                "default quick reload must leave the persisted encoding choice alone");
    }
}
void Untitled(EditorFrame* frame) {
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    const auto* menus = frame->GetMenuBar();
    const auto withEncoding = menus->FindMenuItem("File", "With Encoding...");
    const auto asUtf8 = menus->FindMenuItem("File", "As UTF-8");
    Require(withEncoding != wxNOT_FOUND && asUtf8 != wxNOT_FOUND,
            "File Reload must expose With Encoding and As UTF-8 commands");
    Require(!menus->IsEnabled(withEncoding) && !menus->IsEnabled(asUtf8),
            "Untitled documents must disable both encoding reload commands");
    for (const auto& command : {asAnsi, asOem, asDefault}) {
        const auto id = menus->FindMenuItem("File", command.label);
        Require(id != wxNOT_FOUND && !menus->IsEnabled(id), "Untitled must disable each new quick reload");
    }
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
                GetWindowTextW(window, title, static_cast<int>(std::size(title)));
                if (IsWindowVisible(window) && wxString(title) == "Reload Encoding") {
                    ++driver.warnings;
                    driver.order.push_back("warning");
                    SendMessageW(window, TDM_CLICK_BUTTON, driver.warningChoice == wxID_OK ? IDOK : IDCANCEL, 0);
                } else if (IsWindowVisible(window) && wxString(title) == "Could not reload file") {
                    ++driver.errors;
                    SendMessageW(window, TDM_CLICK_BUTTON, IDOK, 0);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (auto* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget)) continue;
            if (!GTK_IS_MESSAGE_DIALOG(widget)) continue;
            const auto title = wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget)));
            if (title == "Reload Encoding") {
                ++warnings;
                order.push_back("warning");
                NativeDialogs::Respond(widget, warningChoice == wxID_OK ? GTK_RESPONSE_OK : GTK_RESPONSE_CANCEL);
            } else if (title == "Could not reload file") {
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
            SelectEncoding(dialog);
            wxCommandEvent response(wxEVT_BUTTON, cancel ? wxID_CANCEL : wxID_OK);
            dialog->GetEventHandler()->ProcessEvent(response);
            return;
        }
    }
    void SelectEncoding(wxDialog* dialog) {
        for (auto* child : dialog->GetChildren()) {
            auto* list = wxDynamicCast(child, wxListBox);
            if (list) {
                initial = list->GetSelection();
                validChoices = list->GetCount() >= 4 && list->GetString(0) == "UTF-8" &&
                               list->GetString(1) == "UTF-16LE" && list->GetString(2) == "GBK (CP936)" &&
                               list->GetString(3) == "UTF-16BE";
                list->SetSelection(selected);
                wxCommandEvent changed(wxEVT_LISTBOX, list->GetId());
                list->GetEventHandler()->ProcessEvent(changed);
            }
        }
    }
};
void NativeAliasQuickReload(EditorFrame* frame, bool useShortcut, bool oem) {
    const auto path = TestFiles::TemporaryDirectory("native-quick.txt");
    TestFiles::WriteBytes(path, "before");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "native quick reload fixture must open as explicit UTF-8");
    const std::string bytes = "\x{c3}\x{a9}\n";
    TestFiles::WriteBytes(path, bytes);
    const auto invoke = [&] { NativeQuickCommand(frame, oem ? asOem : asAnsi, useShortcut); };
    const auto native = oem ? NativeOemEncodingChoice() : NativeEncodingChoice();
    ReloadDialogs dialogs;
    invoke();
    if (!native) {
        Require(dialogs.errors == 1 && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                    TestEditor::Text(frame) == "before" && TestFiles::Bytes(path) == bytes,
                "unsupported native source must show the reload error and preserve both live text and disk bytes");
        return;
    }
    const auto samples = QuickSamples(native->encoding);
    const wxString identity = oem ? "OEM (" : "ANSI (";
    Require(dialogs.errors == 0 && frame->DocumentEncoding() == native->encoding && !frame->DocumentHasBom() &&
                frame->GetStatusBar()->GetStatusText().StartsWith(identity) &&
                TestEditor::Text(frame) == samples.text && TestFiles::Bytes(path) == bytes,
            "quick reload must decode disk bytes as the requested native source and retain its identity");
    const std::string signature = "\x{ef}\x{bb}\x{bf}text\n";
    TestFiles::WriteBytes(path, signature);
    invoke();
    Require(!frame->DocumentHasBom() && frame->GetStatusBar()->GetStatusText().StartsWith(identity) &&
                TestEditor::Text(frame) == samples.signatureText && TestFiles::Bytes(path) == signature,
            "native ANSI and OEM must interpret a UTF-8 signature as document content");
    TestFiles::WriteBytes(path, "ASCII\n");
    invoke();
    Require(TestEditor::Text(frame) == "ASCII\n" && frame->DocumentEncoding() == native->encoding &&
                !frame->GetStatusBar()->GetStatusText().StartsWith("ANSI (") &&
                frame->GetStatusBar()->GetStatusText().StartsWith("OEM (") == oem,
            "for ASCII under the default load options OEM remains native and ANSI uses its explicit codec identity");
}
auto QuickReloadState(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    return std::make_tuple(TestEditor::Text(frame), frame->DocumentPath(), frame->GetStatusBar()->GetStatusText(),
                           editor->Send(GetAnchor), editor->Send(GetCurrentPos), editor->Send(GetFirstVisibleLine),
                           editor->Send(GetXOffset), editor->Send(GetEOLMode), frame->DocumentIsModified(),
                           editor->Send(CanUndo), editor->Send(CanRedo));
}
void QuickReloadDecisions(EditorFrame* frame, const QuickCommand& command) {
    using enum Scintilla::Message;
    const auto path = TestFiles::TemporaryDirectory("quick-decision.txt");
    TestFiles::WriteBytes(path, "live");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "quick decision fixture must open");
    TestEditor::Append(frame, " edit");
    frame->Editor()->Send(SetSel, 5, 2);
    const auto dirty = QuickReloadState(frame);
    Require(std::filesystem::remove(path), "remove the backing file before deciding quick reload");
    const auto decide = [&](int button, int loadErrors) {
        NativeDialogs::FileCommandDriver savePrompt(NativeDialogs::FileDialogProfile::Open, button);
        ReloadDialogs reload;
        NativeQuickCommand(frame, command, false);
        savePrompt.Stop();
        reload.Stop();
        Require(
            savePrompt.prompts == 1 && savePrompt.choosers == 0 && savePrompt.errors == 0 && reload.choosers == 0 &&
                reload.errors == loadErrors,
            "quick reload must confirm before reading, report read failure only after Discard and never pick a path");
    };
    decide(wxID_CANCEL, 0);
    Require(QuickReloadState(frame) == dirty && !std::filesystem::exists(path),
            "Cancel must leave the dirty document, original view and missing file alone");
    decide(wxID_NO, 1);
    Require(QuickReloadState(frame) == dirty && !std::filesystem::exists(path),
            "failed reload after Discard must leave the live dirty document, original view and undo history intact");
    if ((command.key == asAnsi.key && !NativeEncodingChoice()) ||
        (command.key == asOem.key && !NativeOemEncodingChoice())) {
        frame->Editor()->Send(Undo);
        return;
    }
    TestFiles::WriteBytes(path, "disk");
    decide(wxID_NO, 0);
    Require(TestEditor::Text(frame) == "disk" && TestFiles::Bytes(path) == "disk" && !frame->DocumentIsModified() &&
                !frame->Editor()->Send(CanUndo),
            "Discard must read disk bytes and reset modified state and history");
    TestEditor::Append(frame, " saved");
    TestFiles::WriteBytes(path, "stale disk");
    decide(wxID_YES, 0);
    Require(TestEditor::Text(frame) == "disk saved" && TestFiles::Bytes(path) == "disk saved" &&
                !frame->DocumentIsModified() && !frame->Editor()->Send(CanUndo),
            "Save must write live text before the quick reload reads the file and resets history");
}
void ExplicitReload(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("reload.txt");
    TestFiles::WriteBytes(path, "\x{ef}\x{bb}\x{bf}\x{e4}\x{b8}\x{ad}");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "UTF-8 BOM fixture must open");
    TestFiles::WriteBytes(path, "\x{d6}\x{d0}");
    ReloadDialogs driver;
    wxCommandEvent command(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("File", "With Encoding..."));
    frame->GetEventHandler()->ProcessEvent(command);
    driver.Stop();
    Require(driver.choosers == 1 && driver.initial == 0 && TestEditor::Text(frame) == "中" &&
                frame->DocumentEncoding() == FileEncoding::Gbk && !frame->DocumentHasBom() &&
                !frame->Editor()->Send(Scintilla::Message::GetModify) && TestFiles::Bytes(path) == "\x{d6}\x{d0}",
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
                    TestFiles::Bytes(path) == "\x{d6}\x{d0}",
                "F8 warning Cancel must preserve dirty text and bytes before showing the picker");
    }
    {
        ReloadDialogs driver;
        driver.cancel = true;
        const auto result = frame->ReloadWithEncoding();
        driver.Stop();
        Require(result.status == OpenStatus::Cancelled && driver.order == std::vector<wxString>{"warning", "picker"} &&
                    TestEditor::Text(frame) == before && frame->Editor()->Send(Scintilla::Message::CanUndo) &&
                    TestFiles::Bytes(path) == "\x{d6}\x{d0}",
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
        if (choice == wxID_YES) TestFiles::WriteBytes(path, "\x{d6}\x{d0}");
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
    using enum FileEncoding;
    struct Fixture {
        std::string bytes;
        FileEncoding encoding;
        int picker;
        const char* label;
        bool bom;
        std::string text;
    };
    const std::array<Fixture, 9> fixtures = {
        {{"\x{e4}\x{b8}\x{ad}", Utf8, 0, "UTF-8", false, "中"},
         {"\x{ef}\x{bb}\x{bf}\x{e4}\x{b8}\x{ad}", Utf8, 0, "UTF-8", true, "中"},
         {"\x{2d}\x{4e}", Utf16Le, 1, "UTF-16LE", false, "中"},
         {"\x{ff}\x{fe}\x{2d}\x{4e}", Utf16Le, 1, "UTF-16LE", true, "中"},
         {"\x{d6}\x{d0}", Gbk, 2, "GBK (CP936)", false, "中"},
         {"# coding: utf-8\n\x{d6}\x{d0}", Gbk, 2, "GBK (CP936)", false, "# coding: utf-8\n中"},
         {"a\r\nb\nc\r", Utf8, 0, "UTF-8", false, "a\r\nb\nc\r"},
         {std::string("\x{fe}\x{ff}\x{4e}\x{2d}", 4), Utf16Be, 3, "UTF-16BE", true, "中"},
         {std::string("\0a\0\r\0\n\0b\0\n\0c\0\r", 14), Utf16Be, 3, "UTF-16BE", false, "a\r\nb\nc\r"}}};
    const auto path = TestFiles::TemporaryDirectory("codecs.txt");
    for (const auto& fixture : fixtures) {
        TestFiles::WriteBytes(path, "initial");
        Require(frame->Open(TestFiles::Filename(path), Utf8).status == OpenStatus::Opened,
                "codec source fixture must open");
        TestFiles::WriteBytes(path, fixture.bytes);
        ReloadDialogs driver;
        driver.selected = fixture.picker;
        const auto result = frame->ReloadWithEncoding();
        driver.Stop();
        Require(
            result.status == OpenStatus::Opened && driver.validChoices && driver.initial == 0 &&
                driver.lineEndings == (fixture.text == "a\r\nb\nc\r" ? 1 : 0) &&
                TestEditor::Text(frame) == fixture.text && TestFiles::Bytes(path) == fixture.bytes &&
                frame->DocumentEncoding() == fixture.encoding && frame->DocumentHasBom() == fixture.bom &&
                frame->GetStatusBar()->GetStatusText().StartsWith(wxString(fixture.label) +
                                                                  (fixture.bom ? " (BOM)" : " |")) &&
                !frame->Editor()->Send(Scintilla::Message::GetModify) &&
                !frame->Editor()->Send(Scintilla::Message::CanUndo),
            "explicit reload must honor the selected codec, actual BOM, conflicting declarations and mixed EOL bytes");
        ReloadDialogs cancelled;
        cancelled.cancel = true;
        Require(frame->ReloadWithEncoding().status == OpenStatus::Cancelled, "codec normalization picker must cancel");
        cancelled.Stop();
        Require(cancelled.initial == fixture.picker,
                "current source codec must normalize both BOM and BOM-less picker selections");
    }
    std::puts("PASS: delivered codecs retain actual BOM/status metadata, explicit precedence and mixed EOL bytes");
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
    using enum Scintilla::Message;
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
    editor->Send(SetSel, 4232, 4374);
    editor->Send(SetFirstVisibleLine, 20);
    editor->Send(SetXOffset, 40);
    editor->Send(SetReadOnly, 1);
    const auto first = editor->Send(GetFirstVisibleLine);
    const auto x = editor->Send(GetXOffset);
    ReloadDialogs driver;
    driver.selected = 0;
    Require(frame->ReloadWithEncoding().status == OpenStatus::Opened, "same-file explicit reload must succeed");
    driver.Stop();
    Require(frame->GetMenuBar()->IsChecked(cpp.GetId()) && editor->Send(GetAnchor) == 4232 &&
                editor->Send(GetCurrentPos) == 4374 && editor->Send(GetFirstVisibleLine) == first &&
                editor->Send(GetXOffset) == x && editor->Send(GetReadOnly) && TestEditor::Text(frame) == text &&
                TestFiles::Bytes(path) == text,
            "encoding reload must retain same-file scheme, directional selection, viewport and read-only mode");
    editor->Send(SetReadOnly, 0);
    std::puts("PASS: encoding reload retains same-file scheme, selection, viewport and read-only mode");
}
void QuickReloadView(EditorFrame* frame) {
    using enum Scintilla::Message;
    const auto path = TestFiles::TemporaryDirectory("quick-view.py");
    constexpr std::string_view beforeTag = "# -*- tab-width: 3; indent-tabs-mode: nil -*-\n";
    constexpr std::string_view afterTag = "# -*- tab-width: 7; indent-tabs-mode: yes -*-\n";
    std::string body;
    for (int line = 0; line < 80; ++line)
        body += std::string(180, 'a') + "\n";
    TestFiles::WriteBytes(path, std::string(beforeTag) + body);
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "quick reload view fixture must open");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    const auto* menus = frame->GetMenuBar();
    wxMenu* reload = nullptr;
    menus->FindItem(menus->FindMenuItem("File", "As UTF-8"), &reload);
    Require(reload != nullptr, "File must have a Reload submenu");
    const std::array<const char*, 6> expected = {"As UTF-8", asAnsi.label,    asOem.label,
                                                 nullptr,    asDefault.label, "With Encoding..."};
    const auto& items = reload->GetMenuItems();
    Require(items.GetCount() == expected.size(), "Reload must expose the scoped original entries");
    size_t index = 0;
    for (const auto* item : items) {
        const auto* label = expected[index++];
        Require(label ? item->GetItemLabelText() == label && item->IsEnabled() : item->IsSeparator(),
                "named document must enable Reload commands in the original order");
    }
    const int language = menus->FindMenuItem("Scheme", "C/C++");
    NativeSearch::Command(frame, language);
    auto* editor = frame->Editor();
    editor->SetWordWrapMode(Scintilla::Wrap::None);
    editor->Send(SetSel, 4240, 4232);
    editor->Send(SetFirstVisibleLine, 20);
    editor->Send(SetXOffset, 40);
    editor->Send(SetReadOnly, 1);
    const auto top = editor->Send(GetFirstVisibleLine);
    const auto horizontal = editor->Send(GetXOffset);
    Require(top > 0 && horizontal > 0 && editor->Send(GetTabWidth) == 3 && !editor->Send(GetUseTabs),
            "quick reload fixture must start scrolled with original indentation and tabs");
    const auto replacement = std::string(afterTag) + body;
    TestFiles::WriteBytes(path, replacement);
    NativeQuickCommand(frame, asDefault, false);
    Require(TestEditor::Text(frame) == replacement && TestFiles::Bytes(path) == replacement &&
                menus->IsChecked(language) && editor->Send(GetReadOnly) && editor->Send(GetAnchor) == 4240 &&
                editor->Send(GetCurrentPos) == 4232 && editor->Send(GetFirstVisibleLine) == top &&
                editor->Send(GetXOffset) == horizontal && editor->Send(GetTabWidth) == 7 && editor->Send(GetUseTabs) &&
                editor->Send(GetEOLMode) == std::to_underlying(Scintilla::EndOfLine::Lf) && !editor->Send(CanUndo),
            "quick reload must reapply file variables and retain scheme, reverse selection, viewport and protection");
    editor->Send(SetReadOnly, 0);
}
void NativeShortcuts(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("keys.txt");
    TestFiles::WriteBytes(path, "\x{ef}\x{bb}\x{bf}\x{e4}\x{b8}\x{ad}");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "native key fixture must open");
    TestFiles::WriteBytes(path, "\x{d6}\x{d0}");
    ReloadDialogs driver;
    NativeKeys::Shortcut(frame, WXK_F8, false);
    driver.Stop();
    Require(driver.choosers == 1 && driver.initial == 0 && TestEditor::Text(frame) == "中" &&
                frame->DocumentEncoding() == FileEncoding::Gbk && TestFiles::Bytes(path) == "\x{d6}\x{d0}",
            "native F8 must share the explicit encoding reload menu implementation");
    TestFiles::WriteBytes(path, std::string("\x{fe}\x{ff}\x{4e}\x{2d}", 4));
    ReloadDialogs bigEndian;
    bigEndian.selected = 3;
    NativeKeys::Shortcut(frame, WXK_F8, false);
    bigEndian.Stop();
    Require(bigEndian.choosers == 1 && bigEndian.validChoices && TestEditor::Text(frame) == "中" &&
                frame->DocumentEncoding() == FileEncoding::Utf16Be && frame->DocumentHasBom(),
            "native F8 selects the UTF-16BE source and reports its actual BOM");
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
                    NativeAliasQuickReload(frame, true, false);
                    NativeAliasQuickReload(frame, true, true);
                    DefaultQuickReload(frame, true, FileEncoding::Utf8, "\x{c3}\x{a9}\n", "é\n");
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
                    NativeAliasQuickReload(frame, false, false);
                    NativeAliasQuickReload(frame, false, true);
                    ConfiguredQuickReload();
                    QuickReloadView(frame);
                    for (const auto& command : {asAnsi, asOem, asDefault})
                        QuickReloadDecisions(frame, command);
                    ReloadDialogs endings;
                    DefaultQuickReload(frame, false, FileEncoding::Utf8, "a\r\nb\nc\r", "a\r\nb\nc\r");
                    Require(endings.lineEndings == 1 && frame->Editor()->Send(Scintilla::Message::GetEOLMode) ==
                                                            std::to_underlying(Scintilla::EndOfLine::CrLf),
                            "quick default reload must present the mixed EOL choice without rewriting declined bytes");
                }
                std::puts("PASS: quick ANSI, OEM and configured default reload commands match original load decisions");
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
