#include "app/editor_frame.h"
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
#include <stdexcept>
#include <vector>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Declared(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("chinese-declared.txt");
    const std::string bytes = "# coding: GB18030\n\xd6\xd0\x81\x30\x81\x30\x94\x39\xfc\x36\r\n";
    TestFiles::WriteBytes(path, bytes);
    const auto opened = frame->Open(TestFiles::Filename(path));
    Require(opened.status == OpenStatus::Opened,
            "automatic Open recognizes original GB18030 declaration and four-byte forms");
    Require(TestEditor::Text(frame) == "# coding: GB18030\n中\xc2\x80😀\r\n",
            "GB18030 two/four-byte input matches independently captured original UTF-8");
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes,
            "GB18030 Save writes exact original bytes");
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
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != "Select Encoding") continue;
            ++shown;
            auto* tree = wxDynamicCast(dialog->FindWindowByName("Current encoding", dialog), wxTreeCtrl);
            if (tree) {
                initial = tree->GetItemText(tree->GetSelection());
                const auto visit = [&](auto&& self, wxTreeItemId parent) -> void {
                    wxTreeItemIdValue cookie;
                    for (auto item = tree->GetFirstChild(parent, cookie); item.IsOk();
                         item = tree->GetNextChild(parent, cookie)) {
                        if (tree->GetItemText(item) == selected) {
                            tree->SelectItem(item);
                            found = true;
                        }
                        self(self, item);
                    }
                };
                visit(visit, tree->GetRootItem());
            }
            wxCommandEvent response(wxEVT_BUTTON, cancel || !found ? wxID_CANCEL : wxID_OK);
            dialog->GetEventHandler()->ProcessEvent(response);
            return;
        }
    }
};
void Picker(EditorFrame* frame, bool native) {
    const auto path = TestFiles::TemporaryDirectory("chinese-picker.txt");
    TestFiles::WriteBytes(path, "live");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "picker UTF-8 fixture opens");
    EncodingPicker driver;
    if (native)
        NativeKeys::Shortcut(frame, WXK_F9, false);
    else {
        const int more = frame->GetMenuBar()->FindMenuItem("File", "More...");
        wxCommandEvent command(wxEVT_MENU, more);
        frame->GetEventHandler()->ProcessEvent(command);
    }
    driver.Stop();
    Require(driver.shown == 1 && driver.found && driver.initial == "UTF-8" &&
                frame->DocumentEncoding() == FileEncoding::Gb18030 && TestEditor::Text(frame) == "live" &&
                TestFiles::Bytes(path) == "live" && frame->DocumentIsModified(),
            "F9 original GB18030 catalog identity changes destination without text or disk mutation");
    Require(frame->Save().status == SaveStatus::Saved, "picker fixture saves its chosen format");
}
struct ChineseFixture {
    FileEncoding encoding;
    const char* label;
    const char* sourceLabel;
    const char* identifier;
};
constexpr ChineseFixture codecs[] = {
    {FileEncoding::Gb18030, "Simplified Chinese (GB18030)", "GB18030 (CP54936)", "GB18030"},
    {FileEncoding::Big5, "Traditional Chinese (Big5, Big5-HKSCS)", "Big5 (CP950)", "BIG5"},
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
            if (!dialog || !dialog->IsModal() || dialog->GetTitle() != "Reload With Encoding") continue;
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
void Fixtures(EditorFrame* frame) {
    using Scintilla::Message;
    // Literal Windows original-app observations: reference/README.md and
    // original-observations.json (89b8b36 source authority, executable limit recorded there).
    struct Fixture {
        FileEncoding encoding;
        std::string input, text, saved;
    };
    const Fixture fixtures[] = {
        {FileEncoding::Gb18030, std::string("A\xd6\xd0\x81\x30\x81\x30\x94\x39\xfc\x36\0B\r\n", 15),
         std::string("A中\xc2\x80😀\0B\r\n", 14), std::string("A\xd6\xd0\x81\x30\x81\x30\x94\x39\xfc\x36\0B\r\n", 15)},
        {FileEncoding::Gb18030, "A\x81\x30\x81\x42\x30\x81\x30\x81", "A?0丅0?0?",
         "A?0\x81\x42"
         "0?0?"},
        {FileEncoding::Big5, std::string("A\xa4\xa4\xa3\xe1\x80\x40\x81\x40\xfa\x40\0B\r\n", 15),
         std::string("A中€\xc2\x80@\uEEB8\uE000\0B\r\n", 20),
         std::string("A\xa4\xa4\xa3\xe1\x80\x40\x81\x40\xfa\x40\0B\r\n", 15)},
        {FileEncoding::Big5, "A\x81\x42\x81", "A\uEEBA?", "A\x81\x42?"},
        {FileEncoding::Gb18030, "\xa6\xd9\xa8\xbc\xfe\x59\x84\x31\x82\x36\x81\x35\xf4\x37\x82\x35\x90\x37",
         "\uE78D\uE7C7\uE81E\uFE10\u1E3F\u9FB4",
         "\xa6\xd9\xa8\xbc\xfe\x59\x84\x31\x82\x36\x81\x35\xf4\x37\x82\x35\x90\x37"},
        {FileEncoding::Big5, "\x8d\xfe\x8e\x40\xa0\xfe\xfe\xfe", "\uF6B0\uE311\uEEB7\uE310",
         "\x8d\xfe\x8e\x40\xa0\xfe\xfe\xfe"},
    };
    const auto path = TestFiles::TemporaryDirectory("chinese-literal.bin");
    for (const auto& fixture : fixtures) {
        TestFiles::WriteBytes(path, fixture.input);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened &&
                    TestEditor::Text(frame) == fixture.text && frame->DocumentEncoding() == fixture.encoding &&
                    !frame->DocumentHasBom() && !frame->DocumentIsModified() &&
                    !frame->Editor()->Send(Message::CanUndo),
                "explicit Chinese load matches original multibyte/PUA/replacement/NUL observations");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.saved,
                "Chinese Save retains original repertoire and replacement bytes");
    }
    for (const auto& codec : codecs) {
        for (const bool empty : {true, false}) {
            const std::string pair = codec.encoding == FileEncoding::Gb18030 ? "\xd6\xd0" : "\xa4\xa4";
            const std::string bytes = empty ? "" : pair + "\r\n" + pair + "\n" + pair + std::string("\r\0", 2) + pair;
            TestFiles::WriteBytes(path, bytes);
            Require(frame->Open(TestFiles::Filename(path), codec.encoding).status == OpenStatus::Opened &&
                        TestEditor::Text(frame) == (empty ? "" : std::string("中\r\n中\n中\r\0中", 17)),
                    "Chinese explicit empty or embedded-NUL mixed EOL file opens without normalization");
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == bytes,
                    "empty and mixed-EOL Chinese file bytes roundtrip unchanged");
        }
    }
    std::puts("PASS: original Chinese two/four-byte, supplementary, CP950 PUA, malformed, empty and NUL/EOL fixtures");
}
void Reload(EditorFrame* frame, bool native) {
    const auto path = TestFiles::TemporaryDirectory("chinese-reload.txt");
    for (const auto& codec : codecs) {
        TestFiles::WriteBytes(path, "live");
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
                "reload source begins as UTF-8");
        const std::string bytes =
            codec.encoding == FileEncoding::Gb18030 ? "\xd6\xd0\x94\x39\xfc\x36" : "\xa4\xa4\xfa\x40";
        TestFiles::WriteBytes(path, bytes);
        ReloadPicker driver;
        driver.selected = codec.sourceLabel;
        if (native)
            NativeKeys::Shortcut(frame, WXK_F8, false);
        else
            Require(frame->ReloadWithEncoding().status == OpenStatus::Opened, "source picker accepts Chinese codec");
        driver.Stop();
        const std::string expected = codec.encoding == FileEncoding::Gb18030 ? "中😀" : "中\uE000";
        Require(driver.shown == 1 && driver.found && driver.initial == "UTF-8" && TestEditor::Text(frame) == expected &&
                    frame->DocumentEncoding() == codec.encoding && !frame->DocumentIsModified() &&
                    TestFiles::Bytes(path) == bytes,
                "F8 reloads original Chinese bytes without saving the old live text");
        ReloadPicker cancelled;
        cancelled.selected = "UTF-8";
        cancelled.cancel = true;
        Require(frame->ReloadWithEncoding().status == OpenStatus::Cancelled, "Chinese reload picker cancels");
        cancelled.Stop();
        Require(cancelled.initial == codec.sourceLabel && frame->DocumentEncoding() == codec.encoding &&
                    TestEditor::Text(frame) == expected,
                "reload initialization and Cancel preserve Chinese format");
    }
    std::puts("PASS: F8 Chinese source selection, current initialization and cancellation");
}
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
                ++shown;
                NativeDialogs::Respond(widget, accept ? GTK_RESPONSE_OK : GTK_RESPONSE_CANCEL);
            }
        }
        g_list_free(windows);
#endif
    }
};
void Destination(EditorFrame* frame, bool native) {
    using Scintilla::Message;
    const auto path = TestFiles::TemporaryDirectory("chinese-destination.txt");
    for (const auto& codec : codecs) {
        TestFiles::WriteBytes(path, "中\r\n");
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
                "destination fixture opens as UTF-8");
        TestEditor::Append(frame, "x");
        frame->Editor()->Send(Message::Undo);
        frame->Editor()->Send(Message::SetSel, 0, 3);
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
            Require(driver.shown == 1 && driver.found && frame->DocumentEncoding() == codec.encoding &&
                        frame->GetStatusBar()->GetStatusText().Contains(codec.sourceLabel) &&
                        TestEditor::Text(frame) == "中\r\n" && frame->Editor()->Send(Message::CanRedo) &&
                        frame->Editor()->Send(Message::GetCurrentPos) == 3 && frame->DocumentIsModified(),
                    "F9 original Chinese destination retains rich text, selection, redo and distinct status");
        }
        {
            EncodingPicker driver;
            driver.cancel = true;
            driver.selected = "UTF-8";
            wxCommandEvent command(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("File", "More..."));
            frame->GetEventHandler()->ProcessEvent(command);
            driver.Stop();
            Require(driver.initial == codec.label && frame->DocumentEncoding() == codec.encoding &&
                        frame->Editor()->Send(Message::CanRedo),
                    "F9 cancellation keeps original Chinese identity/history");
        }
        wxCommandEvent identifier(wxEVT_MENU, frame->GetMenuBar()->FindMenuItem("Edit", "Encoding Identifier"));
        frame->GetEventHandler()->ProcessEvent(identifier);
        Require(TestEditor::Text(frame) == std::string(codec.identifier) + "\r\n",
                "Encoding Identifier inserts original GB18030/BIG5 catalog parse name");
        frame->Editor()->Send(Message::Undo);
        Require(TestEditor::Text(frame) == "中\r\n", "identifier insertion retains one-step Undo");
        Require(frame->Save().status == SaveStatus::Saved && !frame->DocumentIsModified() &&
                    TestFiles::Bytes(path) ==
                        (codec.encoding == FileEncoding::Gb18030 ? "\xd6\xd0\r\n" : "\xa4\xa4\r\n"),
                "Save commits selected Chinese destination and independently specified bytes");
    }
    std::puts("PASS: F9 GB18030/Big5 current choice, cancellation, identifier, status and retained Undo/Redo");
}
void Saves(EditorFrame* frame) {
    using Scintilla::Message;
    const auto source = TestFiles::TemporaryDirectory("chinese-loss-source.txt");
    const auto copy = TestFiles::TemporaryDirectory("chinese-loss-copy.txt");
    const auto target = TestFiles::TemporaryDirectory("chinese-loss-as.txt");
    const std::string sample = "A€∞Ā中😀\uE000\r\n";
    for (const auto& codec : codecs) {
        const bool lossy = codec.encoding == FileEncoding::Big5;
        const std::string expected = lossy ? "A\xa3\xe1\xa1\xdb?\xa4\xa4??\xfa\x40\r\n"
                                           : "A\xa2\xe3\xa1\xde\x81\x30\x8b\x38\xd6\xd0\x94\x39\xfc\x36\xaa\xa1\r\n";
        TestFiles::WriteBytes(source, "unchanged");
        Require(frame->Open(TestFiles::Filename(source), FileEncoding::Utf8).status == OpenStatus::Opened,
                "save fixture starts with independently observed original disk bytes");
        frame->Editor()->Send(Message::SelectAll);
        frame->Editor()->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(sample.c_str()));
        frame->Editor()->Send(Message::SetSel, 1, 4);
        frame->SelectEncoding(EncodingChoice(codec.encoding));
        if (lossy) {
            LossPrompt driver;
            Require(frame->Save().status == SaveStatus::Cancelled, "native Big5 loss warning Cancel cancels Save");
            driver.Stop();
            Require(driver.shown == 1 && TestFiles::Bytes(source) == "unchanged" && TestEditor::Text(frame) == sample &&
                        frame->DocumentIsModified() && frame->Editor()->Send(Message::CanUndo) &&
                        frame->Editor()->Send(Message::GetAnchor) == 1 &&
                        frame->Editor()->Send(Message::GetCurrentPos) == 4,
                    "native loss Cancel preserves disk, selected format, live text, selection and Undo");
        }
        const auto blocked = TestFiles::TemporaryDirectory();
        Require(frame->SaveAs(TestFiles::Filename(blocked), [] { return EncodingLossChoice::Replace; }).status ==
                        SaveStatus::Failed &&
                    frame->DocumentPath() == TestFiles::Filename(source) && frame->DocumentIsModified() &&
                    TestEditor::Text(frame) == sample && TestFiles::Bytes(source) == "unchanged",
                "Chinese write failure preserves original path, rich text, pending format and disk bytes");
        Require(frame->SaveCopy(TestFiles::Filename(copy), [] { return EncodingLossChoice::Replace; }).status ==
                        SaveStatus::Saved &&
                    TestFiles::Bytes(copy) == expected && TestFiles::Bytes(source) == "unchanged" &&
                    frame->DocumentPath() == TestFiles::Filename(source) && frame->DocumentIsModified(),
                "Save Copy writes exact original Chinese replacement bytes without committing identity/save point");
        {
            LossPrompt driver;
            driver.accept = true;
            Require(frame->SaveAs(TestFiles::Filename(target)).status == SaveStatus::Saved,
                    "native accepted Chinese Save As succeeds");
            driver.Stop();
            Require(driver.shown == (lossy ? 1 : 0) && TestFiles::Bytes(target) == expected &&
                        TestFiles::Bytes(source) == "unchanged" && TestEditor::Text(frame) == sample &&
                        !frame->DocumentIsModified() && frame->DocumentEncoding() == codec.encoding,
                    "accepted Save As matches original loss warning and exact bytes while retaining rich live text");
        }
        frame->Editor()->Send(Message::Undo);
        Require(TestEditor::Text(frame) == "unchanged" && frame->DocumentIsModified(),
                "post-save Undo restores preceding edit rather than converted placeholders");
        frame->Editor()->Send(Message::Redo);
        Require(TestEditor::Text(frame) == sample && !frame->DocumentIsModified(),
                "post-save Redo restores rich text at original save point");
        Require(frame->Save([] { return EncodingLossChoice::Replace; }, SaveTimeMode::Original).status ==
                        SaveStatus::Saved &&
                    TestFiles::Bytes(target) == expected,
                "Save with Original Time retains selected Chinese bytes");
        Require(frame->NewDocument(), "saved document can become Untitled");
        frame->SelectEncoding(EncodingChoice(codec.encoding));
        TestEditor::Append(frame, "中");
        Require(frame->SaveFirst(TestFiles::Filename(target)).status == SaveStatus::Saved &&
                    TestFiles::Bytes(target) == (lossy ? "\xa4\xa4" : "\xd6\xd0") && !frame->DocumentIsModified(),
                "first Save uses selected Chinese codec without BOM and commits its identity");
    }
    std::puts("PASS: original native loss Cancel/accept, exact save variants, failure and savepoint Undo/Redo");
}
void Detection(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("chinese-detection.txt");
    for (const auto& codec : codecs) {
        const std::vector<const char*> aliases =
            codec.encoding == FileEncoding::Big5
                ? std::vector<const char*>{"BIG5", "cn_big5", "csbig5", "xxbig5", "cp950", "ms950"}
                : std::vector<const char*>{"GB18030", "gb_18030"};
        for (const auto* name : aliases) {
            const std::string pair = codec.encoding == FileEncoding::Big5 ? "\xa4\xa4" : "\xd6\xd0";
            const std::string prefix = std::string("# coding: ") + name + "\n";
            TestFiles::WriteBytes(path, prefix + pair);
            const auto opened = frame->Open(TestFiles::Filename(path));
            if (opened.status != OpenStatus::Opened || frame->DocumentEncoding() != codec.encoding ||
                TestEditor::Text(frame) != prefix + "中")
                std::fprintf(stderr, "Alias %s: status=%d, codec=%d expected=%d, error=%s\n", name,
                             static_cast<int>(opened.status), static_cast<int>(frame->DocumentEncoding()),
                             static_cast<int>(codec.encoding), opened.error.utf8_str().data());
            Require(opened.status == OpenStatus::Opened && frame->DocumentEncoding() == codec.encoding &&
                        TestEditor::Text(frame) == prefix + "中",
                    "original catalog aliases select GB18030/CP950 without a new detector");
            TestFiles::WriteBytes(path, prefix + "中");
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                        frame->DocumentEncoding() == FileEncoding::Utf8,
                    "valid multibyte UTF-8 retains precedence over Chinese declarations");
        }
        SettingsStore settings;
        settings.values.warnLineEndings = false;
        settings.values.fileWatching.mode = FileWatchingMode::None;
        settings.values.encoding.defaultEncoding = codec.encoding == FileEncoding::Gb18030 ? 54936 : 950;
        auto* configured = new EditorFrame(&settings);
        const std::string pair = codec.encoding == FileEncoding::Big5 ? "\xa4\xa4" : "\xd6\xd0";
        TestFiles::WriteBytes(path, pair);
        Require(configured->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                    configured->DocumentEncoding() == codec.encoding && TestEditor::Text(configured) == "中",
                "original numeric DefaultEncoding selects Chinese fallback codec");
        configured->Destroy();
    }
    // This input decodes as GB18030 and cannot be UTF-8 or CP936. Without an
    // original declaration/default the port must not introduce a Chinese guess.
    TestFiles::WriteBytes(path, "\x94\x39\xfc\x36");
    const auto result = frame->Open(TestFiles::Filename(path));
    Require(result.status == OpenStatus::Failed ||
                (frame->DocumentEncoding() != FileEncoding::Gb18030 && frame->DocumentEncoding() != FileEncoding::Big5),
            "undesignated decodability does not guess a Chinese codec");
    TestFiles::WriteBytes(path, "# coding: gb2312\n\xd6\xd0");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened &&
                frame->DocumentEncoding() == FileEncoding::Gbk && TestEditor::Text(frame) == "# coding: gb2312\n中",
            "CP936 declaration compatibility remains distinct and unchanged");
    std::puts("PASS: original aliases, defaults, UTF-8 precedence, CP936 compatibility and no new encoding guesses");
}
void Big5BestFit(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory("big5-best-fit.txt");
    TestFiles::WriteBytes(path, "¢");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "original cent-sign fixture opens");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Big5));
    {
        LossPrompt driver;
        Require(frame->Save().status == SaveStatus::Cancelled,
                "original CP950 rejects best-fit-only cent with a loss warning");
        driver.Stop();
        Require(driver.shown == 1 && TestFiles::Bytes(path) == "¢" && frame->DocumentIsModified(),
                "best-fit warning Cancel preserves the UTF-8 disk and pending Big5 identity");
    }
    Require(frame->Save([] { return EncodingLossChoice::Replace; }).status == SaveStatus::Saved &&
                TestFiles::Bytes(path) == "?" && TestEditor::Text(frame) == "¢",
            "original accepted CP950 cent saves question mark while retaining live Unicode");
    TestFiles::WriteBytes(path, "═╞╡╪");
    Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
            "original duplicate box-character fixture opens");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Big5));
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == "\xf9\xf9\xf9\xe9\xf9\xeb\xf9\xea",
            "CP950 duplicates use independently captured original F9 byte choices");
    std::puts("PASS: original CP950 best-fit rejection and four duplicate-character byte choices");
}
class ChineseApp final : public wxApp {
public:
    bool OnInit() override {
        settings_.values.warnLineEndings = false;
        settings_.values.fileWatching.mode = FileWatchingMode::None;
        auto* frame = new EditorFrame(&settings_);
        frame->Show();
        wxString mode;
        const bool native = wxGetEnv("WXNOTEPAD_CHINESE_NATIVE_KEYS", &mode) && mode == "1";
        auto run = [this, frame, native](bool ready) {
            try {
                Require(ready, "Chinese native fixture presents on its owned display");
                Declared(frame);
                Picker(frame, native);
                Fixtures(frame);
                Reload(frame, native);
                Destination(frame, native);
                Saves(frame);
                Detection(frame);
                Big5BestFit(frame);
                std::puts("PASS: original Chinese file workflows");
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
wxIMPLEMENT_APP(ChineseApp);
