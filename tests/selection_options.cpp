#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <cstdio>
#include <stdexcept>
#include <tuple>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void CheckMenu(EditorFrame* frame, SettingsStore& settings, int option) {
    auto* bar = frame->GetMenuBar();
    for (const auto& [label, bit, message, enabled] :
         {std::tuple{"Enable Multiple Selection", 1, Message::GetMultipleSelection, 1},
          std::tuple{"Undo Redo Remember Selection", 8, Message::GetUndoSelectionHistory, 3}}) {
        const int id = bar->FindMenuItem("Settings", label);
        Require(id != wxNOT_FOUND, "original selection preference must be a native Settings menu command");
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(bar->FindItem(id)->IsCheckable() && bar->IsChecked(id) == ((option & bit) != 0),
                "selection preference checked state follows imported bit");
        NativeSearch::Command(frame, id);
        Require(settings.values.selectOption == (option ^ bit), "toggle preserves every unrelated selection bit");
        Require(frame->Editor()->Send(message) == ((option & bit) ? 0 : enabled),
                "native preference command applies to the existing core immediately");
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(bar->IsChecked(id) == ((option & bit) == 0), "toggled native menu check follows the preference");
        NativeSearch::Command(frame, id);
        Require(settings.values.selectOption == option, "second toggle restores imported raw bits");
    }
}
void Check(EditorControl& editor, int option) {
    Require(editor.Send(Message::GetMultipleSelection) == ((option & 1) != 0),
            "multiple selection must follow SelectOption bit 1");
    Require(editor.Send(Message::GetUndoSelectionHistory) == ((option & 8) ? 3 : 0),
            "undo selection and scrolling must follow SelectOption bit 8");
    editor.Send(Message::SetText, 0, reinterpret_cast<intptr_t>("01234567890123456789\n"));
    editor.Send(Message::EmptyUndoBuffer);
    editor.Send(Message::SetSel, 2, 5);
    editor.Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("X"));
    editor.Send(Message::SetSel, 10, 10);
    editor.Send(Message::Undo);
    Require(editor.Send(Message::GetLength) == 21, "Undo must restore edited bytes");
    if (option & 8)
        Require(editor.Send(Message::GetAnchor) == 2 && editor.Send(Message::GetCurrentPos) == 5,
                "enabled undo history must restore the replaced selection");
    else
        Require(editor.Send(Message::GetSelectionEmpty), "disabled undo history must leave a single caret");
    editor.Send(Message::SetSel, 2, 5);
    editor.Refresh(false);
    editor.Update();
    wxYield();
    const wxPoint point(
        static_cast<int>(editor.Send(Message::PointXFromPosition, 0, 10)),
        static_cast<int>(editor.Send(Message::PointYFromPosition, 0, 10) + editor.Send(Message::TextHeight, 0) / 2));
    wxMilliSleep(550);
    for (auto type : {wxEVT_LEFT_DOWN, wxEVT_LEFT_UP}) {
        wxMouseEvent event(type);
        event.SetPosition(point);
        event.SetControlDown(true);
        event.SetEventObject(&editor);
        editor.GetEventHandler()->ProcessEvent(event);
    }
    Require(editor.Send(Message::GetSelections) == ((option & 1) ? 2 : 1),
            "Ctrl-click adds a caret only with multiple selection enabled");
    editor.Send(Message::SetSavePoint);
}
class SelectionOptionsApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                const int option = wxAtoi(argv[1]);
                Require(settings_->values.selectOption == option, "raw imported bits must survive unchanged");
                CheckMenu(frame, *settings_, option);
                if (argc > 2) {
                    const wxString action = argv[2];
                    if (action != "keep") {
                        for (const char* label : {"Enable Multiple Selection", "Undo Redo Remember Selection"})
                            NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Settings", label));
                        Require(settings_->values.selectOption == (option ^ 9), "lifecycle toggles only target bits");
                        Require(frame->Editor()->Send(Message::GetMultipleSelection) == ((option & 1) ? 0 : 1) &&
                                    frame->Editor()->Send(Message::GetUndoSelectionHistory) == ((option & 8) ? 0 : 3),
                                "lifecycle commands apply both live options");
                    }
                    if (action == "disable-exit")
                        NativeSearch::Command(frame,
                                              frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings On Exit"));
                    if (action == "explicit")
                        NativeSearch::Command(frame,
                                              frame->GetMenuBar()->FindMenuItem("Settings", "Save Settings Now"));
                    std::printf("PASS: selection lifecycle imported=%d action=%s\n", option, action.utf8_str().data());
                    result_ = 0;
                    frame->Close(true);
                    ExitMainLoop();
                    return;
                }
                Check(*frame->Editor(), option);
                Require(frame->NewDocument(), "New must succeed");
                Check(*frame->Editor(), option);
                const auto path = wxFileName(wxFileName(settings_->Path()).GetPath(), "sample.txt").GetFullPath();
                Require(frame->Open(path).status == OpenStatus::Opened, "Open must succeed");
                Check(*frame->Editor(), option);
                Require(frame->Open(path).status == OpenStatus::Opened, "same-path Open must succeed");
                Check(*frame->Editor(), option);
                std::printf("PASS: SelectOption=%d at startup, New, Open and reopen\n", option);
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SelectionOptionsApp);
