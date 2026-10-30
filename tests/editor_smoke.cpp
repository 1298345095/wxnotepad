#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string Text(EditorControl* editor) {
    using Scintilla::Message;
    const auto length = editor->Send(Message::GetLength);
    std::string text(static_cast<size_t>(length) + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.resize(static_cast<size_t>(length));
    return text;
}

void Character(EditorControl* editor, int character) {
    wxKeyEvent event(wxEVT_CHAR);
    event.m_keyCode = character;
    event.m_uniChar = character;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}

void Key(EditorControl* editor, int key, bool control = false) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = key;
    event.m_controlDown = control;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}

class SmokeApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        auto* editor = frame->Editor();
        frame->Show();
        CallAfter([this, frame, editor] {
            try {
                using Scintilla::Message;
                Require(Text(editor).empty(), "a new document must start empty");
                Require(editor->Send(Message::GetModify) == 0, "a new document must be at its save point");
                // Replay the actual character event entry, not ReplaceSel/AddText.
                // Temporary conversion storage must remain alive during core insertion.
                for (int character : {0x41, 0x4e2d, 0x6587}) Character(editor, character);
                Require(Text(editor) == "A中文", "character events must own valid UTF-8 through insertion");
                Require(editor->Send(Message::GetCurrentPos) == 7, "caret must follow UTF-8 byte positions");
                Require(editor->Send(Message::GetModify) == 1, "input must leave the save point");
                std::puts("PASS: empty document and real UTF-8 character events");
                Key(editor, WXK_BACK);
                Require(Text(editor) == "A中", "Backspace must delete one UTF-8 character");
                Key(editor, WXK_RETURN);
                Key(editor, WXK_TAB);
                Character(editor, 'B');
                Require(Text(editor) == "A中\r\n\tB", "Enter and Tab must use original-core commands");
                editor->Send(Message::GotoPos, 1);
                Key(editor, WXK_DELETE);
                Require(Text(editor) == "A\r\n\tB", "Delete must remove the character after the caret");
                Require(editor->Send(Message::GetCurrentPos) == 1, "Delete must retain the caret position");
                while (editor->Send(Message::CanUndo)) Key(editor, 'Z', true);
                Require(Text(editor).empty() && !editor->Send(Message::GetModify), "Undo to initial save point must clear modified state");
                Require(!editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo), "Undo/Redo availability must follow history");
                while (editor->Send(Message::CanRedo)) Key(editor, 'Y', true);
                Require(Text(editor) == "A\r\n\tB" && editor->Send(Message::GetModify), "Redo must restore edits and modified state");
                // A later save point also comes from the core, not a host edit counter.
                editor->Send(Message::SetSavePoint);
                Character(editor, 'C');
                Key(editor, 'Z', true);
                Require(Text(editor) == "A\r\n\tB" && !editor->Send(Message::GetModify), "Undo to a later save point must clear modified state");
                Key(editor, 'Y', true);
                Require(Text(editor) == "AC\r\n\tB" && editor->Send(Message::GetModify), "Redo across a save point must mark modified");
                std::puts("PASS: basic keys, Undo/Redo shortcuts and save points");
                wxYield();
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetTitle().StartsWith("*"), "title must show modified state");
                Require(frame->GetMenuBar()->IsEnabled(wxID_UNDO) && !frame->GetMenuBar()->IsEnabled(wxID_REDO), "menu availability must follow history");
                wxCommandEvent undo(wxEVT_MENU, wxID_UNDO);
                frame->GetEventHandler()->ProcessEvent(undo);
                wxYield();
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetTitle().StartsWith("*") && frame->GetMenuBar()->IsEnabled(wxID_REDO), "Undo menu must restore saved title and enable Redo");
                wxCommandEvent redo(wxEVT_MENU, wxID_REDO);
                frame->GetEventHandler()->ProcessEvent(redo);
                wxYield();
                Require(Text(editor) == "AC\r\n\tB" && frame->GetTitle().StartsWith("*"), "Redo menu must restore text and modified title");
                std::puts("PASS: native menu commands, availability and modified title");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnExit() override { return result_; }
    int OnRun() override { wxApp::OnRun(); return result_; }
private:
    int result_ = 1;
};
}
wxIMPLEMENT_APP(SmokeApp);
