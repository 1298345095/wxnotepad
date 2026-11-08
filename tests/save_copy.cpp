#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "native_dialog_helpers.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/accel.h>
#include <wx/timer.h>
#include <filesystem>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
using Scintilla::Message;
using TestFiles::Bytes;
using TestFiles::Filename;

void FileShortcut(EditorFrame* frame, bool shift = false) {
    frame->Editor()->SetFocus();
#ifdef __WXMSW__
    NativeSearch::WindowsShortcut(frame->Editor(), VK_F6,
                                  {{VK_CONTROL, 0x80}, {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)}, {VK_MENU, 0}});
#elif defined(__WXGTK__)
    // The native Gtk chooser owns key release outside wx's event filter. Use
    // the same native GDK accelerator entry point as save_native.
    auto* widget = GTK_WIDGET(frame->GetHandle());
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_F6;
    event->key.state = GDK_CONTROL_MASK | (shift ? GDK_SHIFT_MASK : 0U);
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
#ifdef __WXGTK__
bool ClickOverwrite(GtkWidget* widget, const char* label) {
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
    for (auto* item = children; item && !clicked; item = item->next)
        clicked = ClickOverwrite(GTK_WIDGET(item->data), label);
    g_list_free(children);
    return clicked;
}
#endif

class CopyChooser final : public wxTimer {
public:
    explicit CopyChooser(wxString destination = {}, bool replace = true, GbkLossChoice loss = GbkLossChoice::Cancel)
        : destination_(std::move(destination)), replace_(replace), loss_(loss) {
        Start(100);
    }
    int choosers = 0;
    int overwrites = 0;
    int losses = 0;
    wxString folder;
    wxString filename;
    wxString selectedType;
    void AcceptDefaultFilename(const wxString& expected) {
        acceptDefault_ = true;
        destination_ = expected;
    }
    void Notify() override {
#ifdef __WXMSW__
        EnumThreadWindows(
            GetCurrentThreadId(),
            [](HWND window, LPARAM data) -> BOOL { return reinterpret_cast<CopyChooser*>(data)->HandleWindow(window); },
            reinterpret_cast<LPARAM>(this));
#elif defined(__WXGTK__)
        GList* windows = gtk_window_list_toplevels();
        for (GList* item = windows; item; item = item->next) {
            auto* widget = GTK_WIDGET(item->data);
            if (!gtk_widget_get_visible(widget)) continue;
            if (GTK_IS_MESSAGE_DIALOG(widget)) {
                HandleMessage(widget);
                continue;
            }
            if (GTK_IS_FILE_CHOOSER(widget)) HandleChooser(widget);
        }
        g_list_free(windows);
#endif
    }

private:
#ifdef __WXGTK__
    void HandleMessage(GtkWidget* widget) {
        if (wxString::FromUTF8(gtk_window_get_title(GTK_WINDOW(widget))) == "GBK character loss") {
            ++losses;
            NativeDialogs::Respond(widget, loss_ == GbkLossChoice::Replace ? GTK_RESPONSE_OK : GTK_RESPONSE_CANCEL);
        } else {
            ++overwrites;
            cancelChooser_ = !replace_;
            Require(ClickOverwrite(widget, replace_ ? "Replace" : "Cancel"),
                    "native overwrite must expose Replace/Cancel");
        }
    }
    void HandleChooser(GtkWidget* widget) {
        if (stage_ == 0) {
            stage_ = 1;
            ++choosers;
            char* directory = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(widget));
            char* name = gtk_file_chooser_get_current_name(GTK_FILE_CHOOSER(widget));
            folder = wxString::FromUTF8(directory ? directory : "");
            filename = wxString::FromUTF8(name ? name : "");
            g_free(directory);
            g_free(name);
            if (!selectedType.empty()) NativeDialogs::ChooseFileType(widget, selectedType);
            if (!acceptDefault_) NativeDialogs::ChooseFile(widget, destination_);
        } else if (cancelChooser_)
            NativeDialogs::Respond(widget, GTK_RESPONSE_CANCEL);
        else if (stage_ == 1 && !destination_.empty() && NativeDialogs::CanAcceptFile(widget, destination_)) {
            stage_ = 2;
            NativeDialogs::Respond(widget, GTK_RESPONSE_ACCEPT);
        }
    }
#endif
#ifdef __WXMSW__
    void ReadSuggestion(HWND window) {
        EnumChildWindows(
            window,
            [](HWND child, LPARAM data) -> BOOL {
                auto& driver = *reinterpret_cast<CopyChooser*>(data);
                wchar_t kind[64]{}, text[4096]{};
                GetClassNameW(child, kind, 64);
                GetWindowTextW(child, text, 4096);
                if (IsWindowVisible(child) && wxString(kind) == "Edit" && driver.filename.empty())
                    driver.filename = text;
                const wxString label(text);
                if (wxString(kind) == "ToolbarWindow32") {
                    const auto prefix = label.find(": ");
                    if (prefix != wxString::npos && wxFileName(label.Mid(prefix + 2)).IsAbsolute())
                        driver.folder = label.Mid(prefix + 2);
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(this));
    }
    BOOL HandleWindow(HWND window) {
        wchar_t title[256]{};
        GetWindowTextW(window, title, 256);
        if (!IsWindowVisible(window)) return TRUE;
        if (wxString(title) == "GBK character loss") {
            ++losses;
            SendMessageW(window, WM_USER + 102, loss_ == GbkLossChoice::Replace ? IDOK : IDCANCEL, 0);
            return TRUE;
        }
        if (wxString(title) != "Save Copy") {
            wchar_t owner[256]{};
            GetWindowTextW(GetWindow(window, GW_OWNER), owner, 256);
            if (wxString(owner) == "Save Copy") {
                ++overwrites;
                cancelChooser_ = !replace_;
                SendMessageW(window, WM_USER + 102, replace_ ? IDYES : IDNO, 0);
            }
            return TRUE;
        }
        if (stage_++) {
            if (cancelChooser_) SendMessageW(window, WM_COMMAND, IDCANCEL, 0);
            return TRUE;
        }
        ++choosers;
        ReadSuggestion(window);
        if (!selectedType.empty()) NativeDialogs::ChooseFileType(window, selectedType);
        if (acceptDefault_)
            PostMessageW(window, WM_COMMAND, IDOK, 0);
        else
            NativeDialogs::ChooseFile(window, destination_);
        return TRUE;
    }
#endif
    wxString destination_;
    int stage_ = 0;
    bool replace_;
    GbkLossChoice loss_;
    bool cancelChooser_ = false;
    bool acceptDefault_ = false;
};

void SaveSuggestedCopy(EditorFrame* frame, const std::filesystem::path& directory, const char* basename,
                       const std::string& expected) {
    const auto identity = frame->DocumentPath();
    const auto target = directory / basename;
    CopyChooser chooser;
    chooser.AcceptDefaultFilename(Filename(target));
    const auto result = frame->SaveCopy();
    chooser.Stop();
    // Windows can hide a registered extension in the filename edit. Accept the
    // untouched suggestion and independently verify its full effective name.
    if (result.status != SaveStatus::Saved || chooser.folder != Filename(directory) ||
        !std::filesystem::is_regular_file(target))
        std::fprintf(stderr, "Suggested copy folder='%s' display='%s' expected='%s' status=%d\n",
                     chooser.folder.utf8_str().data(), chooser.filename.utf8_str().data(),
                     Filename(target).utf8_str().data(), static_cast<int>(result.status));
    Require(result.status == SaveStatus::Saved && chooser.choosers == 1 && chooser.folder == Filename(directory) &&
                Bytes(target) == expected && frame->DocumentPath() == identity,
            "untouched native copy suggestion must save the exact ORIGINAL full basename and bytes");
}

void Identity(EditorFrame* frame, const std::filesystem::path& directory) {
    const auto source = directory / "source.txt";
    const auto copy = directory / u8"另存副本.cpp";
    const std::string original = "\xef\xbb\xbf"
                                 "one\r\ntwo\nthree\r";
    TestFiles::WriteBytes(source, original);
    Require(frame->Open(Filename(source)).status == OpenStatus::Opened, "copy source must open");
    const auto language = frame->GetMenuBar()->FindMenuItem("Language", "C/C++");
    NativeSearch::Command(frame, language);
    TestEditor::Append(frame, "中文!");
    auto* editor = frame->Editor();
    editor->Send(Message::SetSel, 7, 2);
    wxYield();
    const auto title = frame->GetTitle();
    const auto text = TestEditor::Text(frame);
    Require(frame->SaveCopy(Filename(copy)).status == SaveStatus::Saved, "named dirty Save Copy must succeed");
    Require(Bytes(copy) == "\xef\xbb\xbf"
                           "one\r\ntwo\nthree\r中文!" &&
                Bytes(source) == original,
            "copy must independently write BOM, Unicode and mixed EOL without saving the source");
    Require(frame->DocumentPath() == Filename(source) && frame->GetTitle() == title &&
                frame->GetMenuBar()->IsChecked(language) && TestEditor::Text(frame) == text &&
                frame->DocumentHasUtf8Bom() && editor->Send(Message::GetModify),
            "copy must retain original identity, scheme, encoding, text and dirty state");
    TestEditor::RequireSelection(editor, 7, 2, "copy must retain reversed selection");
    editor->Send(Message::Undo);
    Require(!editor->Send(Message::GetModify), "Undo after copy must return to the source save point");
    editor->Send(Message::Redo);
    Require(editor->Send(Message::GetModify) && TestEditor::Text(frame) == text,
            "Redo after copy must retain pending source changes");
    Require(frame->SaveCopy(std::nullopt).status == SaveStatus::Cancelled &&
                frame->SaveCopy(Filename(directory / "missing" / "fail.txt")).status == SaveStatus::Failed &&
                frame->DocumentPath() == Filename(source) && editor->Send(Message::GetModify),
            "copy cancellation and failed write must retain source state");
    Require(frame->SaveCopy(Filename(source)).status == SaveStatus::Saved && Bytes(source) == Bytes(copy) &&
                editor->Send(Message::GetModify),
            "copy onto current source path must write bytes without establishing a save point");
    Require(frame->Save().status == SaveStatus::Saved && !editor->Send(Message::GetModify),
            "ordinary Save after copy must retain its existing save-point contract");
    std::puts("PASS: Save Copy bytes, original identity/scheme, selection, save point and subsequent Save");
}

void RememberDirectory(EditorFrame* frame, const std::filesystem::path& directory) {
    const auto remembered = directory / "copies";
    std::filesystem::create_directory(remembered);
    Require(frame->SaveCopy(Filename(remembered / "different.txt")).status == SaveStatus::Saved,
            "successful copy must seed the next chooser");
    CopyChooser cancel;
    Require(frame->SaveCopy().status == SaveStatus::Cancelled && cancel.choosers == 1,
            "native Save Copy chooser must support cancellation");
    cancel.Stop();
    if (cancel.folder != Filename(remembered))
        std::fprintf(stderr, "Copy chooser folder='%s' name='%s'\n", cancel.folder.utf8_str().data(),
                     cancel.filename.utf8_str().data());
    Require(cancel.folder == Filename(remembered), "next copy chooser must remember successful directory");
    SaveSuggestedCopy(frame, remembered, "source.txt", Bytes(directory / "source.txt"));
    Require(frame->SaveCopy(Filename(directory / "missing" / "copy.txt")).status == SaveStatus::Failed,
            "failed copy must report failure");
    CopyChooser afterFailure;
    frame->SaveCopy();
    afterFailure.Stop();
    Require(afterFailure.folder == Filename(remembered),
            "cancelled and failed copies must retain the last SUCCESSFUL directory");
    SaveSuggestedCopy(frame, remembered, "source.txt", Bytes(directory / "source.txt"));
    const auto output = remembered / "menu.json";
    CopyChooser menu(Filename(output));
    menu.selectedType = "JSON";
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("File", "Save Copy..."));
    menu.Stop();
    Require(menu.choosers == 1 && Bytes(output) == Bytes(directory / "source.txt") &&
                frame->DocumentPath() == Filename(directory / "source.txt") &&
                frame->GetMenuBar()->IsChecked(frame->GetMenuBar()->FindMenuItem("Language", "C/C++")),
            "real Save Copy menu command must write through native chooser and retain original identity");
    const auto existing = remembered / "overwrite.txt";
    TestFiles::WriteBytes(existing, "keep");
    CopyChooser reject(Filename(existing), false);
    frame->SaveCopy();
    reject.Stop();
    Require(reject.overwrites == 1 && Bytes(existing) == "keep",
            "rejecting native overwrite must retain the existing destination");
    TestEditor::Append(frame, "pending");
    CopyChooser same(Filename(directory / "source.txt"));
    frame->SaveCopy();
    same.Stop();
    Require(same.overwrites == 1 && frame->Editor()->Send(Message::GetModify) &&
                Bytes(directory / "source.txt") == "\xef\xbb\xbf"
                                                   "one\r\ntwo\nthree\r中文!pending",
            "native overwrite of current source must preserve original dirty save point");
    Require(frame->Save().status == SaveStatus::Saved, "ordinary Save must clean up this native scenario");
    // Restore the remembered folder before the following shortcut and Untitled checks.
    Require(frame->SaveCopy(Filename(remembered / "final.txt")).status == SaveStatus::Saved,
            "successful copy must retain remembered directory ownership");
    CopyChooser shortcut;
    FileShortcut(frame);
    shortcut.Stop();
    Require(shortcut.choosers == 1, "native Ctrl+F6 must reach Save Copy chooser");
    CopyChooser unmatched;
    FileShortcut(frame, true);
    unmatched.Stop();
    Require(unmatched.choosers == 0, "Ctrl+Shift+F6 must not invoke Save Copy");
    const auto other = directory / "other.cpp";
    TestFiles::WriteBytes(other, "other");
    Require(frame->Open(Filename(other)).status == OpenStatus::Opened, "another named document must open");
    CopyChooser afterOpen;
    frame->SaveCopy();
    afterOpen.Stop();
    Require(afterOpen.folder == Filename(remembered), "Open must retain this window's successful copy directory");
    SaveSuggestedCopy(frame, remembered, "other.cpp", "other");
    Require(frame->Open(Filename(directory / "source.txt")).status == OpenStatus::Opened,
            "original source must reopen after directory lifetime check");
    std::puts("PASS: native Save Copy remembers successful directory and original basename");
}

void Formats(SettingsStore* settings, const std::filesystem::path& directory) {
    struct Sample {
        const char* name;
        FileEncoding encoding;
        std::string bytes;
    };
    // Literal samples also exercised by the independent original capture script
    // in evidence/native-utilities-2026-11-06/138.
    const Sample samples[] = {
        {"utf8", FileEncoding::Utf8, "中文\r\nx\nz\r"},
        {"utf8-bom", FileEncoding::Utf8,
         "\xef\xbb\xbf"
         "中文\r\nx\nz\r"},
        {"utf16", FileEncoding::Utf16Le, std::string("\x2d\x4e\x87\x65\r\0\n\0x\0\n\0z\0\r\0", 16)},
        {"utf16-bom", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\x87\x65\r\0\n\0x\0\n\0z\0\r\0", 18)},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\xce\xc4\r\nx\nz\r"},
        {"empty", FileEncoding::Utf8, {}},
        {"bom-only", FileEncoding::Utf8, "\xef\xbb\xbf"},
        {"utf16-empty", FileEncoding::Utf16Le, std::string("\xff\xfe", 2)},
    };
    for (const auto& sample : samples) {
        const auto source = directory / (std::string(sample.name) + ".txt");
        const auto copy = directory / (std::string(sample.name) + "-copy.txt");
        TestFiles::WriteBytes(source, sample.bytes);
        auto* frame = new EditorFrame(settings);
        Require(frame->Open(Filename(source), sample.encoding).status == OpenStatus::Opened,
                "encoded copy fixture must open");
        Require(frame->SaveCopy(Filename(copy)).status == SaveStatus::Saved && Bytes(copy) == sample.bytes &&
                    Bytes(source) == sample.bytes && frame->DocumentEncoding() == sample.encoding &&
                    !frame->Editor()->Send(Message::GetModify),
                "clean copy must preserve original codec/BOM/mixed-EOL/empty bytes and clean state");
        frame->Destroy();
    }
    std::puts("PASS: independent UTF-8, UTF-16LE, GBK, BOM, mixed-EOL and empty copy bytes");
}

void UntitledAfterCopy(EditorFrame* frame, const std::filesystem::path& directory) {
    Require(frame->NewDocument(), "clean source must permit New");
    CopyChooser cancel;
    frame->SaveCopy();
    cancel.Stop();
    Require(cancel.folder == Filename(directory), "original Untitled copy must seed remembered directory's PARENT");
    SaveSuggestedCopy(frame, directory, "copies.txt", {});
    const auto output = directory / "untitled-copy.txt";
    CopyChooser accept(Filename(output));
    frame->SaveCopy();
    accept.Stop();
    Require(accept.choosers == 1 && Bytes(output).empty() && frame->DocumentPath().empty() &&
                !frame->Editor()->Send(Message::GetModify),
            "empty Untitled native copy must write while remaining clean and Untitled");
    std::puts("PASS: original Untitled remembered-directory/default-name behavior and empty native copy");
}

void LegacyLoss(SettingsStore* settings, const std::filesystem::path& directory) {
    const auto source = directory / "loss-source.txt";
    TestFiles::WriteBytes(source, "\xd6\xd0\xce\xc4");
    auto* frame = new EditorFrame(settings);
    Require(frame->Open(Filename(source), FileEncoding::Gbk).status == OpenStatus::Opened,
            "legacy copy source must open");
    TestEditor::Append(frame, "😀\r\n!");
    frame->Editor()->Send(Message::SetSel, 3, 6);
    const auto pending = TestEditor::Text(frame);
    const auto cancelled = directory / "loss-new.txt";
    Require(frame->SaveCopy(Filename(cancelled), [] { return GbkLossChoice::Cancel; }).status ==
                    SaveStatus::Cancelled &&
                std::filesystem::exists(cancelled) && Bytes(cancelled).empty(),
            "original lossy copy Cancel must leave an empty newly created destination");
    const auto existing = directory / "loss-existing.txt";
    TestFiles::WriteBytes(existing, "keep existing");
    Require(frame->SaveCopy(Filename(existing), [] { return GbkLossChoice::Cancel; }).status == SaveStatus::Cancelled &&
                Bytes(existing) == "keep existing",
            "lossy copy Cancel must preserve an existing destination");
    const auto replaced = directory / "loss-replaced.txt";
    Require(frame->SaveCopy(Filename(replaced), [] { return GbkLossChoice::Replace; }).status == SaveStatus::Saved &&
                Bytes(replaced) == "\xd6\xd0\xce\xc4??\r\n!" && TestEditor::Text(frame) == pending &&
                frame->DocumentPath() == Filename(source) && frame->DocumentEncoding() == FileEncoding::Gbk &&
                frame->Editor()->Send(Message::GetModify),
            "confirmed lossy copy must write original CP936 placeholders while retaining Unicode source state");
    TestEditor::RequireSelection(frame->Editor(), 3, 6, "loss confirmation must retain live selection");
    const auto saveAs = directory / "loss-save-as.txt";
    Require(frame->SaveAs(Filename(saveAs), [] { return GbkLossChoice::Cancel; }).status == SaveStatus::Cancelled &&
                !std::filesystem::exists(saveAs),
            "Save Copy's original creation effect must not alter existing safe Save As cancellation");
    const auto native = directory / "loss-native.txt";
    CopyChooser cancelNative(Filename(native));
    Require(frame->SaveCopy().status == SaveStatus::Cancelled, "native GBK copy loss must support Cancel");
    cancelNative.Stop();
    Require(cancelNative.losses == 1 && Bytes(native).empty(),
            "native copy loss Cancel must leave the original newly created empty file");
    CopyChooser replaceNative(Filename(native), true, GbkLossChoice::Replace);
    Require(frame->SaveCopy().status == SaveStatus::Saved, "native GBK copy loss must support OK");
    replaceNative.Stop();
    Require(replaceNative.overwrites == 1 && replaceNative.losses == 1 && Bytes(native) == "\xd6\xd0\xce\xc4??\r\n!" &&
                TestEditor::Text(frame) == pending,
            "native overwrite and loss confirmations must preserve Unicode source and original CP936 bytes");
    frame->Destroy();
    settings->values.fixLineEndings = settings->values.fixTrailingBlanks = true;
    auto* cleanup = new EditorFrame(settings);
    Require(cleanup->Open(Filename(source), FileEncoding::Gbk).status == OpenStatus::Opened,
            "loss cleanup source must open");
    const char* full = "中文 \t\r\nx \t\n😀!";
    TestEditor::ResetStream(cleanup->Editor(), full, 19, 1, Scintilla::EndOfLine::Lf);
    const auto cleanupCancelled = directory / "cleanup-loss-copy.txt";
    Require(cleanup->SaveCopy(Filename(cleanupCancelled), [] { return GbkLossChoice::Cancel; }).status ==
                    SaveStatus::Cancelled &&
                Bytes(cleanupCancelled).empty() && TestEditor::Text(cleanup) == "中文\nx\n😀!" &&
                cleanup->Editor()->Send(Message::GetModify),
            "original loss cancellation must retain already-applied live cleanup and its old save point");
    TestEditor::RequireSelection(cleanup->Editor(), 14, 3,
                                 "loss-cancel cleanup must retain original split-Unicode position repair");
    cleanup->Destroy();
    settings->values.fixLineEndings = settings->values.fixTrailingBlanks = false;
    std::puts("PASS: independent original GBK copy Cancel/new-file and replacement effects");
}

void Cleanup(SettingsStore* settings, const std::filesystem::path& directory) {
    struct Policy {
        bool fix;
        bool strip;
        const char* expected;
    };
    const Policy policies[] = {{false, false, "a \t\r\nb \t\nc \t"},
                               {true, false, "a \t\nb \t\nc \t"},
                               {false, true, "a\r\nb\nc"},
                               {true, true, "a\nb\nc"}};
    const auto source = directory / "cleanup-source.txt";
    const auto copy = directory / "cleanup-copy.txt";
    const std::string before = "a \t\r\nb \t\nc \t";
    TestFiles::WriteBytes(source, before);
    for (const auto& policy : policies) {
        settings->values.fixLineEndings = policy.fix;
        settings->values.fixTrailingBlanks = policy.strip;
        auto* frame = new EditorFrame(settings);
        Require(frame->Open(Filename(source)).status == OpenStatus::Opened, "cleanup source must open");
        auto* editor = frame->Editor();
        editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
        editor->Send(Message::SetSel, 12, 1);
        Require(frame->SaveCopy(Filename(copy)).status == SaveStatus::Saved && Bytes(copy) == policy.expected &&
                    TestEditor::Text(frame) == policy.expected && Bytes(source) == before &&
                    frame->DocumentPath() == Filename(source) &&
                    bool(editor->Send(Message::GetModify)) == (policy.fix || policy.strip),
                "copy cleanup must change live text/disk while retaining source identity and old save point");
        TestEditor::RequireSelection(editor, static_cast<intptr_t>(std::string(policy.expected).size()), 1,
                                     "copy cleanup must retain original reversed selection movement");
        if (policy.strip) {
            editor->Send(Message::Undo);
            Require(TestEditor::Text(frame) == (policy.fix ? "a \t\nb \t\nc \t" : before),
                    "first Undo must restore original independent trailing-blank cleanup");
        }
        if (policy.fix) editor->Send(Message::Undo);
        Require(TestEditor::Text(frame) == before && !editor->Send(Message::GetModify),
                "Undo of copy cleanup must return to the ORIGINAL clean source save point");
        if (policy.fix) editor->Send(Message::Redo);
        if (policy.strip) editor->Send(Message::Redo);
        Require(TestEditor::Text(frame) == policy.expected &&
                    bool(editor->Send(Message::GetModify)) == (policy.fix || policy.strip),
                "Redo of copy cleanup must retain original dirty state without a copy save point");
        frame->Destroy();
    }
    settings->values.fixLineEndings = settings->values.fixTrailingBlanks = false;
    std::puts("PASS: four copy cleanup policies, live selection and original Undo/Redo save point");
}

void DirtyCleanup(SettingsStore* settings, const std::filesystem::path& directory) {
    struct Policy {
        bool fix;
        bool strip;
        const char* after;
        intptr_t anchor;
        const char* undone;
    };
    // Independent original after/first-Undo captures: original-formats.json.
    const Policy policies[] = {
        {false, false, "a \t\r\nb \t\nc \t!", 12, "a \t\r\nb \t\nc \t"},
        {true, false, "a \t\nb \t\nc \t!", 11, "a \t\r\nb \t\nc \t!"},
        {false, true, "a\r\nb\nc \t!", 8, "a \t\r\nb \t\nc \t!"},
        {true, true, "a\nb\nc \t!", 7, "a \t\nb \t\nc \t!"},
    };
    const auto source = directory / "dirty-cleanup.txt";
    TestFiles::WriteBytes(source, "a \t\r\nb \t\nc \t");
    for (const auto& policy : policies) {
        settings->values.fixLineEndings = policy.fix;
        settings->values.fixTrailingBlanks = policy.strip;
        auto* frame = new EditorFrame(settings);
        Require(frame->Open(Filename(source)).status == OpenStatus::Opened, "dirty cleanup source must open");
        auto* editor = frame->Editor();
        editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
        editor->Send(Message::SetSel, 12, 1);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        const auto copy = directory / "dirty-cleanup-copy.txt";
        Require(frame->SaveCopy(Filename(copy)).status == SaveStatus::Saved && Bytes(copy) == policy.after &&
                    TestEditor::Text(frame) == policy.after && editor->Send(Message::GetModify),
                "dirty copy cleanup must match independently captured text/bytes/modification");
        TestEditor::RequireSelection(editor, policy.anchor, 1, "dirty cleanup must match original selection");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(frame) == policy.undone &&
                    bool(editor->Send(Message::GetModify)) == (policy.fix || policy.strip),
                "dirty copy first Undo must match original cleanup grouping and save point");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(frame) == policy.after && editor->Send(Message::GetModify),
                "dirty copy Redo must retain original pending modification");
        frame->Destroy();
    }
    settings->values.fixLineEndings = settings->values.fixTrailingBlanks = true;
    auto* frame = new EditorFrame(settings);
    Require(frame->Open(Filename(source)).status == OpenStatus::Opened, "read-only cleanup source must open");
    auto* editor = frame->Editor();
    editor->Send(Message::SetSel, 12, 1);
    editor->Send(Message::SetReadOnly, 1);
    const auto readonly = directory / "readonly-copy.txt";
    Require(frame->SaveCopy(Filename(readonly)).status == SaveStatus::Saved &&
                Bytes(readonly) == "a \t\r\nb \t\nc \t" && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo),
            "read-only Save Copy must skip live cleanup as in the original");
    editor->Send(Message::SetReadOnly, 0);
    TestEditor::ResetStream(editor, "stable", 4, 1);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    Require(frame->SaveCopy(Filename(directory / "pending-copy.txt")).status == SaveStatus::Saved &&
                !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
            "no-op copy cleanup must preserve original pending Redo history");
    frame->Destroy();
    settings->values.fixLineEndings = settings->values.fixTrailingBlanks = false;
    std::puts("PASS: dirty cleanup policies, read-only cleanup gate and pending Redo retention");
}
class SaveCopyApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                const auto command = frame->GetMenuBar()->FindMenuItem("File", "Save Copy...");
                Require(command != wxNOT_FOUND, "File menu must expose original Save Copy");
                wxUpdateUIEvent state(command);
                frame->GetEventHandler()->ProcessEvent(state);
                Require(state.GetEnabled(), "empty Untitled must offer Save Copy");
                std::unique_ptr<wxAcceleratorEntry> accelerator(frame->GetMenuBar()->FindItem(command)->GetAccel());
                Require(accelerator && accelerator->GetKeyCode() == WXK_F6 && accelerator->GetFlags() == wxACCEL_CTRL,
                        "Save Copy must bind exact Ctrl+F6");
                std::puts("PASS: Save Copy menu availability and accelerator");
                for (const std::string_view text : {"", " \t", "content"}) {
                    TestEditor::ResetStream(frame->Editor(), "");
                    frame->Editor()->Send(Message::AppendText, text.size(), reinterpret_cast<intptr_t>(text.data()));
                    CopyChooser fresh;
                    Require(frame->SaveCopy().status == SaveStatus::Cancelled,
                            "fresh Untitled Save Copy must reach native chooser for empty/whitespace/dirty text");
                    fresh.Stop();
                    Require(fresh.filename.empty() && fresh.folder == wxGetCwd() && frame->DocumentPath().empty() &&
                                bool(frame->Editor()->Send(Message::GetModify)) == !text.empty(),
                            "first Untitled copy chooser must use working directory, blank name and retain state");
                }
                TestEditor::ResetStream(frame->Editor(), "");
                const auto directory = TestFiles::TemporaryDirectory("save-copy");
                std::filesystem::create_directory(directory);
                Identity(frame, directory);
                RememberDirectory(frame, directory);
                UntitledAfterCopy(frame, directory);
                Formats(settings_.get(), directory);
                Cleanup(settings_.get(), directory);
                DirtyCleanup(settings_.get(), directory);
                LegacyLoss(settings_.get(), directory);
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
    std::unique_ptr<SettingsStore> settings_;
};
} // namespace
wxIMPLEMENT_APP(SaveCopyApp);
