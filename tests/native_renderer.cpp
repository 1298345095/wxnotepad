#include "editor/editor_control.h"
#include <ScintillaTypes.h>
#include <wx/wx.h>
#include <string>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Text(EditorControl* editor) {
    std::string text(static_cast<size_t>(editor->Send(Message::GetLength)) + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
class NativeRendererApp final : public wxApp {
    int result_ = 1;

public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "Native renderer commands");
        auto* editor = new EditorControl(frame);
        auto* other = new EditorControl(frame);
        CallAfter([this, frame, editor, other] {
            try {
                const std::string original = "alpha 中文😀\r\nbeta";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(original.c_str()));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 2, 5);
                editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("XYZ"));
                editor->Send(Message::Undo);
                const auto anchor = editor->Send(Message::GetAnchor);
                const auto caret = editor->Send(Message::GetCurrentPos);
                for (const auto technology : {Scintilla::Technology::DirectWrite, Scintilla::Technology::Default}) {
                    editor->Send(Message::SetTechnology, static_cast<uintptr_t>(technology));
                    Require(editor->Send(Message::GetTechnology) == static_cast<intptr_t>(technology),
                            "the real editor selects and reports the requested native renderer");
                    Require(editor->Send(Message::GetBufferedDraw) == (technology == Scintilla::Technology::Default),
                            "renderer selection uses the original native buffering contract");
                    Require(Text(editor) == original && !editor->Send(Message::GetModify) &&
                                editor->Send(Message::CanRedo) && editor->Send(Message::GetAnchor) == anchor &&
                                editor->Send(Message::GetCurrentPos) == caret,
                            "renderer selection preserves exact UTF-8 bytes, selection, save point and redo");
                    Require(other->Send(Message::GetTechnology) ==
                                static_cast<intptr_t>(Scintilla::Technology::Default),
                            "changing one editor leaves another editor's renderer unchanged");
                    editor->Send(Message::SetTechnology, static_cast<uintptr_t>(Scintilla::Technology::DirectWriteDC));
                    Require(editor->Send(Message::GetTechnology) == static_cast<intptr_t>(technology),
                            "an unsupported request preserves a usable truthful native renderer");
                    Require(editor->Send(Message::TextWidth, 0, reinterpret_cast<intptr_t>("English 中文😀")) > 0,
                            "the active renderer measures real editor text");
                }
                editor->Send(Message::Redo);
                Require(Text(editor) == "alXYZ 中文😀\r\nbeta" && editor->Send(Message::GetModify),
                        "redo after renderer switching applies the original edit");
                editor->Send(Message::Undo);
                Require(Text(editor) == original && !editor->Send(Message::GetModify),
                        "undo after renderer switching restores original bytes and the save point");
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
