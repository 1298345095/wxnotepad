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
    wxString selected = "UTF-16LE with BOM";
    wxString initial;
    DefaultsDriver(bool nested, bool outer) : nestedOk(nested), outerOk(outer) { Start(40); }
    void Notify() override {
        for (auto* window : wxTopLevelWindows) {
            auto* dialog = wxDynamicCast(window, wxDialog);
            if (!dialog || !dialog->IsModal()) continue;
            if (dialog->GetTitle() == "Default Encoding") {
                if (stage == 0) {
                    const std::array names{"SkipUnicodeDetection", "LoadANSIasUTF8", "LoadASCIIasUTF8",
                                           "LoadNFOasOEM", "NoEncodingTags"};
                    const std::array expected{false, true, true, true, false};
                    for (size_t i = 0; i < names.size(); ++i) {
                        auto* check = wxDynamicCast(dialog->FindWindowByName(names[i], dialog), wxCheckBox);
                        Require(check && check->GetValue() == expected[i], "original checkbox states and polarity");
                        Click(check);
                    }
                    stage = 1;
                    Click(dialog->FindWindowByName("Choose default encoding", dialog));
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
                const auto visit = [&](auto&& self, wxTreeItemId parent) -> void {
                    wxTreeItemIdValue cookie;
                    for (auto item = tree->GetFirstChild(parent, cookie); item.IsOk(); item = tree->GetNextChild(parent, cookie)) {
                        if (tree->GetItemText(item) == selected) tree->SelectItem(item);
                        self(self, item);
                    }
                };
                visit(visit, tree->GetRootItem());
                stage = 2;
                Click(dialog->FindWindow(nestedOk ? wxID_OK : wxID_CANCEL));
                return;
            }
        }
    }
};
void Run(SettingsStore& settings) {
    using Scintilla::Message;
    for (const bool nested : {false, true}) {
        for (const bool outer : {false, true}) {
            settings.values.encoding = {};
            settings.values.encoding.skipUnicodeDetection = false;
            settings.values.warnLineEndings = false;
            settings.values.fileWatching.mode = FileWatchingMode::None;
            auto* frame = new EditorFrame(&settings);
            frame->Show();
            const auto source = TestFiles::TemporaryDirectory("defaults-live.txt");
            TestFiles::WriteBytes(source, "live text");
            Require(frame->Open(TestFiles::Filename(source), FileEncoding::Utf8).status == OpenStatus::Opened, "live fixture opens");
            TestEditor::Append(frame, "x");
            frame->Editor()->Send(Message::Undo);
            frame->Editor()->Send(Message::SetSel, 7, 2);
            auto* file = frame->GetMenuBar()->GetMenu(frame->GetMenuBar()->FindMenu("File"));
            auto* encoding = file->FindItem(file->FindItem("Encoding"))->GetSubMenu();
            const int command = encoding->FindItem("Default...");
            Require(command != wxNOT_FOUND, "File > Encoding > Default dialog is delivered");
            DefaultsDriver driver(nested, outer);
            wxCommandEvent event(wxEVT_MENU, command);
            frame->GetEventHandler()->ProcessEvent(event);
            driver.Stop();
            const auto& options = settings.values.encoding;
            Require(driver.stage == 3 && driver.initial == "UTF-8", "nested default picker initially selects configured default");
            Require(options.defaultEncoding == (nested ? 1 : -1), "nested acceptance commits encoding even when outer is cancelled");
            Require(options.skipUnicodeDetection == outer && options.loadANSIasUTF8 == !outer &&
                        options.loadASCIIasUTF8 == !outer && options.loadNFOasOEM == !outer && options.noEncodingTags == outer,
                    "five options commit only on outer OK");
            Require(TestEditor::Text(frame) == "live text" && !frame->DocumentHasBom() &&
                        frame->DocumentEncoding() == FileEncoding::Utf8 && !frame->DocumentIsModified() &&
                        frame->Editor()->Send(Message::CanRedo) && frame->Editor()->Send(Message::GetAnchor) == 7 &&
                        frame->Editor()->Send(Message::GetCurrentPos) == 2 && TestFiles::Bytes(source) == "live text",
                    "default changes preserve live encoding, text, selection, redo, save point and disk");
            frame->Destroy();
            std::printf("PASS: native nested=%d outer=%d commit matrix and live state\n", nested, outer);
        }
    }
}
class DefaultEncodingApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        auto* bootstrap = new wxFrame(nullptr, wxID_ANY, "Default encoding test");
        bootstrap->Show();
        CallAfter([this, bootstrap] {
            try { Run(settings_); }
            catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); result_ = 1; }
            for (auto* window : wxTopLevelWindows) window->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override { wxApp::OnRun(); return result_; }
private:
    SettingsStore settings_;
    int result_ = 0;
};
}
wxIMPLEMENT_APP(DefaultEncodingApp);
