#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <cstdio>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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
