#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Command(EditorFrame* frame, int id) {
    wxCommandEvent command(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(command);
}
class RevertApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                const int revert = frame->GetMenuBar()->FindMenuItem("File", "Revert");
                Require(revert != wxNOT_FOUND, "File menu must expose Revert");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetMenuBar()->IsEnabled(revert), "untitled documents disable Revert");
                const auto path = TestFiles::TemporaryDirectory("revert.cpp");
                TestFiles::WriteBytes(path, "first\nsecond\nthird\n");
                Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                        "the named fixture must open");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsEnabled(revert), "named documents enable Revert");
                frame->Editor()->Send(Message::SetSel, 9, 2);
                TestFiles::WriteBytes(path, "disk!\nsecond\nthird\n");
                Command(frame, revert);
                Require(TestEditor::Text(frame) == "disk!\nsecond\nthird\n" &&
                            frame->Editor()->Send(Message::GetAnchor) == 9 &&
                            frame->Editor()->Send(Message::GetCurrentPos) == 2 &&
                            !frame->Editor()->Send(Message::GetModify) && !frame->Editor()->Send(Message::CanUndo) &&
                            TestFiles::Bytes(path) == "disk!\nsecond\nthird\n",
                        "clean Revert reloads without prompting, preserves selection direction and never writes");
                std::puts("PASS: native Revert entry, enablement and clean same-file reload");
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
};
} // namespace
wxIMPLEMENT_APP(RevertApp);
