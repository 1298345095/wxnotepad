#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <wx/wx.h>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class NativeRendererApp final : public wxApp {
    int result_ = 1;

public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "Native renderer commands");
        auto* editor = new EditorControl(frame);
        CallAfter([this, frame, editor] {
            try {
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("alpha 中文😀\r\nbeta"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 2, 5);
                editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("XYZ"));
                editor->Send(Message::Undo);
                const auto before = editor->Send(Message::GetLength);
                for (const auto technology : {Scintilla::Technology::DirectWrite, Scintilla::Technology::Default}) {
                    editor->Send(Message::SetTechnology, static_cast<uintptr_t>(technology));
                    Require(editor->Send(Message::GetTechnology) == static_cast<intptr_t>(technology),
                            "the real editor selects and reports the requested native renderer");
                    Require(editor->Send(Message::GetBufferedDraw) == (technology == Scintilla::Technology::Default),
                            "renderer selection uses the original native buffering contract");
                    Require(editor->Send(Message::GetLength) == before && !editor->Send(Message::GetModify) &&
                                editor->Send(Message::CanRedo),
                            "renderer selection preserves the document, save point and redo");
                    Require(editor->Send(Message::TextWidth, 0, reinterpret_cast<intptr_t>("English 中文😀")) > 0,
                            "the active renderer measures real editor text");
                }
                result_ = 0;
                std::puts("PASS: real editor native renderer selection, measurement and history");
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
};
} // namespace
wxIMPLEMENT_APP(NativeRendererApp);
