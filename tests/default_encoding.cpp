#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "native_dialog_helpers.h"
#include <wx/wx.h>
#include <wx/checkbox.h>
#include <wx/treectrl.h>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Click(wxWindow* control) {
    Require(control, "native dialog control exists");
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(control->GetHandle()), BM_CLICK, 0, 0);
#elif defined(__WXGTK__)
    gtk_button_clicked(GTK_BUTTON(control->GetHandle()));
#endif
}
class DefaultsDriver final : public wxTimer {
public:
    bool nestedOk;
    bool outerOk;
    int stage = 0;
    wxString selected = "UTF-16BE with BOM";
    wxString initial;
    std::array<bool, 5> initialStates{false, true, true, true, false};
    std::array<bool, 5> finalStates{true, false, false, false, true};
    DefaultsDriver(bool nested, bool outer) : nestedOk(nested), outerOk(outer) { Start(40); }
    void SetOptions(wxDialog* dialog) {
        const std::array names{"SkipUnicodeDetection", "LoadANSIasUTF8", "LoadASCIIasUTF8", "LoadNFOasOEM",
                               "NoEncodingTags"};
        for (size_t i = 0; i < names.size(); ++i) {
            auto* check = wxDynamicCast(dialog->FindWindowByName(names[i], dialog), wxCheckBox);
            Require(check && check->GetValue() == initialStates[i], "original checkbox states and polarity");
            if (initialStates[i] != finalStates[i]) Click(check);
        }
        stage = 1;
        auto* choose = dialog->FindWindowByName("Choose default encoding", dialog);
        // GTK timeout sources do not re-enter while their callback
        // is displaying a nested modal dialog.
        wxTheApp->CallAfter([choose] { Click(choose); });
    }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal()) continue;
            if (dialog->GetTitle() == "Default Encoding") {
                if (stage == 0) {
                    SetOptions(dialog);
                    return;
                }
                if (stage == 2) {
                    stage = 3;
                    Click(dialog->FindWindow(outerOk ? wxID_OK : wxID_CANCEL));
                    return;
                }
            } else if (dialog->GetTitle() == "Select Encoding" && stage == 1) {
                auto* tree = wxDynamicCast(dialog->FindWindowByName("Default encoding", dialog), wxTreeCtrl);
                Require(tree, "default picker uses default-selection context");
                initial = tree->GetItemText(tree->GetSelection());
                Require(NativeDialogs::SelectTreeLabel(*tree, selected), "requested default encoding exists in picker");
                stage = 2;
                Click(dialog->FindWindow(nestedOk ? wxID_OK : wxID_CANCEL));
                return;
            }
        }
    }
};
void ShowDefaults(EditorFrame* frame) {
    auto* file = frame->GetMenuBar()->GetMenu(frame->GetMenuBar()->FindMenu("File"));
    auto* encoding = file->FindItem(file->FindItem("Encoding"))->GetSubMenu();
    const int command = encoding->FindItem("Default...");
    Require(command != wxNOT_FOUND, "File > Encoding > Default dialog is delivered");
    wxCommandEvent event(wxEVT_MENU, command);
    frame->GetEventHandler()->ProcessEvent(event);
}
void RunCommitCase(SettingsStore& settings, bool nested, bool outer) {
    using Scintilla::Message;
    settings.values.encoding = {};
    settings.values.encoding.skipUnicodeDetection = false;
    settings.values.warnLineEndings = false;
    settings.values.fileWatching.mode = FileWatchingMode::None;
    auto* frame = new EditorFrame(&settings);
    frame->Show();
    const auto source = TestFiles::TemporaryDirectory("defaults-live.txt");
    TestFiles::WriteBytes(source, "live text");
    Require(frame->Open(TestFiles::Filename(source), FileEncoding::Utf8).status == OpenStatus::Opened,
            "live fixture opens");
    TestEditor::Append(frame, "x");
    frame->Editor()->Send(Message::Undo);
    const auto actions = frame->Editor()->Send(Message::GetUndoActions);
    const auto undoCurrent = frame->Editor()->Send(Message::GetUndoCurrent);
    frame->Editor()->Send(Message::SetSel, 7, 2);
    frame->Editor()->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
    frame->Editor()->Send(Message::SetReadOnly, 1);
    DefaultsDriver driver(nested, outer);
    ShowDefaults(frame);
    driver.Stop();
    const auto& options = settings.values.encoding;
    Require(driver.stage == 3 && driver.initial == "UTF-8",
            "nested default picker initially selects configured default");
    Require(options.defaultEncoding == (nested ? 2 : -1),
            "nested acceptance commits encoding even when outer is cancelled");
    Require(options.skipUnicodeDetection == outer && options.loadANSIasUTF8 == !outer &&
                options.loadASCIIasUTF8 == !outer && options.loadNFOasOEM == !outer && options.noEncodingTags == outer,
            "five options commit only on outer OK");
    Require(TestEditor::Text(frame) == "live text" && !frame->DocumentHasBom() &&
                frame->DocumentEncoding() == FileEncoding::Utf8 && !frame->DocumentIsModified() &&
                frame->Editor()->Send(Message::GetUndoActions) == actions &&
                frame->Editor()->Send(Message::GetUndoCurrent) == undoCurrent &&
                frame->Editor()->Send(Message::GetAnchor) == 7 && frame->Editor()->Send(Message::GetCurrentPos) == 2 &&
                frame->Editor()->Send(Message::GetReadOnly) &&
                frame->Editor()->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::Lf) &&
                TestFiles::Bytes(source) == "live text",
            "default changes preserve live encoding, text, selection, redo, save point and disk");
    Require(frame->NewDocument(), "New consumes accepted defaults");
    Require(frame->DocumentEncoding() == (nested ? FileEncoding::Utf16Be : FileEncoding::Utf8) &&
                frame->DocumentHasBom() == nested && !frame->DocumentIsModified() &&
                !frame->Editor()->Send(Message::GetUndoActions),
            "New preserves the configured BOM and establishes a clean encoding identity without history");
    TestEditor::Append(frame, "中\r\n");
    const auto destination = TestFiles::TemporaryDirectory("defaults-new.txt");
    Require(frame->SaveAs(TestFiles::Filename(destination)).status == SaveStatus::Saved &&
                TestFiles::Bytes(destination) == (nested ? std::string("\xfe\xff\x4e\x2d\0\r\0\n", 8) : "中\r\n"),
            "New saves exact independently specified default-format bytes");
    const auto empty = TestFiles::TemporaryDirectory("defaults-empty.txt");
    TestFiles::WriteBytes(empty, "");
    Require(frame->Open(TestFiles::Filename(empty)).status == OpenStatus::Opened &&
                frame->DocumentEncoding() == (nested && outer ? FileEncoding::Utf16Be : FileEncoding::Utf8) &&
                frame->DocumentHasBom() == (nested && outer) && !frame->DocumentIsModified() &&
                !frame->Editor()->Send(Message::GetUndoActions),
            "later empty Open uses the separate ASCII-as-UTF8 gate and accepted default");
    const auto ascii = TestFiles::TemporaryDirectory("defaults-ascii.txt");
    TestFiles::WriteBytes(ascii, "plain");
    const auto native = NativeEncodingChoice();
    Require(frame->Open(TestFiles::Filename(ascii)).status == OpenStatus::Opened &&
                frame->DocumentEncoding() == (nested && outer && native ? native->encoding : FileEncoding::Utf8) &&
                !frame->DocumentHasBom() && TestEditor::Text(frame) == "plain",
            "ASCII Open retains original non-Unicode fallback decision order");
    frame->Destroy();
    std::printf("PASS: native nested=%d outer=%d commit matrix and live state\n", nested, outer);
}
void Run(SettingsStore& settings) {
    for (const bool nested : {false, true})
        for (const bool outer : {false, true})
            RunCommitCase(settings, nested, outer);
}
void Persist(SettingsStore& settings, const wxString& mode) {
    auto* frame = new EditorFrame(&settings);
    frame->Show();
    DefaultsDriver driver(true, true);
    wxString selection;
    if (wxGetEnv("WXNOTEPAD_DEFAULTS_CHOICE", &selection)) driver.selected = selection;
    ShowDefaults(frame);
    driver.Stop();
    Require(driver.stage == 3 && driver.initial == "UTF-8",
            "fresh process loads configured default into nested picker");
    if (mode == "explicit") {
        const int id = frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now");
        Require(id != wxNOT_FOUND, "Save Settings Now exists");
        wxCommandEvent save(wxEVT_MENU, id);
        frame->GetEventHandler()->ProcessEvent(save);
    }
    Require(frame->Close(), "clean application closes using exit settings policy");
    std::puts("PASS: native accepted preferences and actual settings command/exit");
}
void Transitions(SettingsStore& settings) {
    using Scintilla::Message;
    settings.values.encoding = {};
    settings.values.encoding.defaultEncoding = 936;
    auto* initial = new EditorFrame(&settings);
    initial->Show();
    Require(initial->DocumentEncoding() == FileEncoding::Gbk && !initial->DocumentIsModified(),
            "initial New uses configured non-BOM default even with ASCII-as-UTF8 enabled");
    const int python = initial->GetMenuBar()->FindMenuItem("Scheme", "Python");
    wxCommandEvent change(wxEVT_MENU, python);
    initial->GetEventHandler()->ProcessEvent(change);
    Require(initial->DocumentEncoding() == FileEncoding::Utf8 && !initial->DocumentIsModified(),
            "manual different-scheme selection applies ASCII-as-UTF8 independently of initial New");
    initial->Destroy();
    settings.values.encoding = {};
    settings.values.encoding.skipUnicodeDetection = false;
    auto* frame = new EditorFrame(&settings);
    frame->Show();
    {
        DefaultsDriver driver(true, true);
        driver.selected = "Simplified Chinese (GBK, GB2312)";
        driver.finalStates = {false, true, false, true, false};
        ShowDefaults(frame);
        driver.Stop();
    }
    Require(frame->DocumentEncoding() == FileEncoding::Utf8 && frame->NewDocument() &&
                frame->DocumentEncoding() == FileEncoding::Gbk && !frame->DocumentIsModified(),
            "only ASCII-as-UTF8 controls fresh default format; ANSI-as-UTF8 does not replace that gate");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf16Le));
    const auto scheme = [&](const char* name) {
        const int id = frame->GetMenuBar()->FindMenuItem("Scheme", name);
        Require(id != wxNOT_FOUND, "delivered scheme exists");
        wxCommandEvent change(wxEVT_MENU, id);
        frame->GetEventHandler()->ProcessEvent(change);
    };
    scheme("Python");
    Require(frame->DocumentEncoding() == FileEncoding::Gbk && !frame->DocumentIsModified() &&
                !frame->Editor()->Send(Message::GetUndoActions),
            "fresh non-BOM scheme transition adopts configured default as the original clean identity");
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf16Le, true));
    scheme("JSON");
    Require(frame->DocumentEncoding() == FileEncoding::Utf16Le && frame->DocumentHasBom(),
            "fresh scheme transitions preserve an existing BOM");
    TestEditor::Append(frame, "x");
    frame->Editor()->Send(Message::Undo);
    frame->SelectEncoding(EncodingChoice(FileEncoding::Utf16Le));
    frame->Editor()->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
    scheme("Python");
    Require(frame->DocumentEncoding() == FileEncoding::Utf16Le && !frame->DocumentHasBom() &&
                frame->Editor()->Send(Message::CanRedo) &&
                frame->Editor()->Send(Message::GetEOLMode) == static_cast<intptr_t>(Scintilla::EndOfLine::Lf),
            "empty-by-editing scheme changes retain encoding, insertion EOL and redo");
    Require(frame->NewDocument(), "fresh fixture resets retained history");
    const auto tagged = TestFiles::TemporaryDirectory("defaults-tag.txt");
    TestFiles::WriteBytes(tagged, "# coding: utf8\nplain");
    Require(frame->Open(TestFiles::Filename(tagged)).status == OpenStatus::Opened &&
                frame->DocumentEncoding() == FileEncoding::Utf8,
            "enabled declarations precede configured default on Open");
    {
        DefaultsDriver driver(false, true);
        driver.initialStates = {false, true, false, true, false};
        driver.finalStates = {false, true, false, true, true};
        ShowDefaults(frame);
        driver.Stop();
    }
    Require(frame->DocumentEncoding() == FileEncoding::Utf8 &&
                frame->Open(TestFiles::Filename(tagged)).status == OpenStatus::Opened &&
                frame->DocumentEncoding() == FileEncoding::Gbk && TestEditor::Text(frame) == "# coding: utf8\nplain",
            "changed encoding-tag preference affects the next Open without rewriting current text");
    frame->Destroy();
    std::puts("PASS: independent ASCII gate, clean empty scheme rules, BOM/history and next-Open tag preference");
}
void NativeDefaultPromotion(SettingsStore& settings) {
    const auto native = NativeEncodingChoice();
    if (!native) return; // Unsupported native pages retain their explicit refusal path.
    for (const bool loadAsUtf8 : {false, true}) {
        settings.values.encoding = {};
        settings.values.encoding.defaultEncoding = 0;
        settings.values.encoding.loadASCIIasUTF8 = loadAsUtf8;
        settings.values.encoding.loadANSIasUTF8 = loadAsUtf8;
        auto* frame = new EditorFrame(&settings);
        frame->Show();
        // Pinned Encoding_GetAnsiIndex always promotes native CP65001 to
        // explicit UTF-8; either load flag also promotes supported legacy ACP.
        const bool explicitEncoding = native->encoding == FileEncoding::Utf8 || loadAsUtf8;
        const auto check = [&] {
            Require(frame->DocumentEncoding() == native->encoding && !frame->DocumentIsModified() &&
                        !frame->Editor()->Send(Scintilla::Message::CanUndo) &&
                        frame->GetStatusBar()->GetStatusText().Contains("ANSI (") != explicitEncoding,
                    "fresh native default resolves original ANSI identity independently of scheme override");
        };
        check();
        const auto path = TestFiles::TemporaryDirectory(loadAsUtf8 ? "native-load-flag.txt" : "native-default.txt");
        TestFiles::WriteBytes(path, "");
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                "native default empty fixture opens");
        // ASCII-as-UTF8 governs empty Open separately from initial New.
        Require(!frame->DocumentIsModified() && TestEditor::Text(frame).empty() && TestFiles::Bytes(path).empty(),
                "native default promotion preserves empty document and disk state");
        if (!loadAsUtf8) check();
        frame->Destroy();
    }
}
class DefaultEncodingApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        auto* bootstrap = new wxFrame(nullptr, wxID_ANY, "Default encoding test");
        bootstrap->Show();
        CallAfter([this] {
            try {
                wxString mode;
                if (wxGetEnv("WXNOTEPAD_DEFAULTS_PERSIST", &mode))
                    Persist(settings_, mode);
                else {
                    Run(settings_);
                    NativeDefaultPromotion(settings_);
                    Transitions(settings_);
                }
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            for (auto* window : wxTopLevelWindows)
                window->Destroy();
            ExitMainLoop();
        });
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
wxIMPLEMENT_APP(DefaultEncodingApp);
