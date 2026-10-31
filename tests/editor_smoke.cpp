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

void Key(EditorControl* editor, int key, bool control = false, bool shift = false) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = key;
    event.m_controlDown = control;
    event.m_shiftDown = shift;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}

void Mouse(EditorControl* editor, wxEventType type, int position) {
    using Scintilla::Message;
    wxMouseEvent event(type);
    event.SetPosition(wxPoint(static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position)),
                              static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position)) + 3));
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}

void Scroll(EditorControl* editor, int orientation, int position) {
    wxScrollWinEvent event(wxEVT_SCROLLWIN_THUMBTRACK, position, orientation);
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
                for (int character : {0x41, 0x4e2d, 0x6587})
                    Character(editor, character);
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
                while (editor->Send(Message::CanUndo))
                    Key(editor, 'Z', true);
                Require(Text(editor).empty() && !editor->Send(Message::GetModify),
                        "Undo to initial save point must clear modified state");
                Require(!editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                        "Undo/Redo availability must follow history");
                while (editor->Send(Message::CanRedo))
                    Key(editor, 'Y', true);
                Require(Text(editor) == "A\r\n\tB" && editor->Send(Message::GetModify),
                        "Redo must restore edits and modified state");
                // A later save point also comes from the core, not a host edit counter.
                editor->Send(Message::SetSavePoint);
                Character(editor, 'C');
                Key(editor, 'Z', true);
                Require(Text(editor) == "A\r\n\tB" && !editor->Send(Message::GetModify),
                        "Undo to a later save point must clear modified state");
                Key(editor, 'Y', true);
                Require(Text(editor) == "AC\r\n\tB" && editor->Send(Message::GetModify),
                        "Redo across a save point must mark modified");
                std::puts("PASS: basic keys, Undo/Redo shortcuts and save points");
                wxYield();
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetTitle().StartsWith("*"), "title must show modified state");
                Require(frame->GetMenuBar()->IsEnabled(wxID_UNDO) && !frame->GetMenuBar()->IsEnabled(wxID_REDO),
                        "menu availability must follow history");
                wxCommandEvent undo(wxEVT_MENU, wxID_UNDO);
                frame->GetEventHandler()->ProcessEvent(undo);
                wxYield();
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetTitle().StartsWith("*") && frame->GetMenuBar()->IsEnabled(wxID_REDO),
                        "Undo menu must restore saved title and enable Redo");
                wxCommandEvent redo(wxEVT_MENU, wxID_REDO);
                frame->GetEventHandler()->ProcessEvent(redo);
                wxYield();
                Require(Text(editor) == "AC\r\n\tB" && frame->GetTitle().StartsWith("*"),
                        "Redo menu must restore text and modified title");
                std::puts("PASS: native menu commands, availability and modified title");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("A中文\r\n  B\r\nlast"));
                editor->Send(Message::GotoPos, 0);
                Key(editor, WXK_RIGHT);
                Require(editor->Send(Message::GetCurrentPos) == 1, "Right must move one character");
                Key(editor, WXK_RIGHT, false, true);
                Require(editor->Send(Message::GetCurrentPos) == 4 && editor->Send(Message::GetAnchor) == 1,
                        "Shift+Right must select a whole UTF-8 character");
                Character(editor, 'X');
                Require(Text(editor) == "AX文\r\n  B\r\nlast", "input must replace the keyboard selection");
                Key(editor, 'Z', true);
                Require(Text(editor) == "A中文\r\n  B\r\nlast" && editor->Send(Message::GetCurrentPos) == 4,
                        "Undo must restore the selected character and caret");
                std::puts("PASS: UTF-8 keyboard movement, Shift selection, replacement and Undo");
                Key(editor, WXK_END, true);
                Require(editor->Send(Message::GetCurrentPos) == 18, "Ctrl+End must reach document end");
                Key(editor, WXK_HOME, true, true);
                Require(editor->Send(Message::GetCurrentPos) == 0 && editor->Send(Message::GetAnchor) == 18,
                        "Ctrl+Shift+Home must extend to document start");
                Key(editor, WXK_HOME, true);
                Key(editor, WXK_DOWN);
                Key(editor, WXK_END);
                Require(editor->Send(Message::GetCurrentPos) == 12, "Down and End must reach the second line end");
                Key(editor, WXK_HOME);
                Require(editor->Send(Message::GetCurrentPos) == 11,
                        "Home must reach first non-whitespace like Notepad4");
                Key(editor, WXK_HOME);
                Require(editor->Send(Message::GetCurrentPos) == 9, "repeated Home must reach physical line start");
                Key(editor, WXK_UP);
                Key(editor, WXK_LEFT);
                Require(editor->Send(Message::GetCurrentPos) == 0, "Up and Left must clamp at document start");
                Key(editor, 'A', true);
                Require(editor->Send(Message::GetSelectionStart) == 0 && editor->Send(Message::GetSelectionEnd) == 18,
                        "Ctrl+A must select the whole document");
                Key(editor, WXK_DELETE);
                Require(Text(editor).empty(), "Delete must remove all selected text");
                Key(editor, 'Z', true);
                Require(Text(editor) == "A中文\r\n  B\r\nlast", "Undo must restore a deleted multiline selection");
                std::puts("PASS: Home/End, document boundaries, arrows, select-all and deletion");
                editor->Send(Message::GotoPos, 18);
                wxYield();
                Mouse(editor, wxEVT_LEFT_DOWN, 1);
                Mouse(editor, wxEVT_MOTION, 12);
                Mouse(editor, wxEVT_LEFT_UP, 12);
                Require(editor->Send(Message::GetAnchor) == 1 && editor->Send(Message::GetCurrentPos) == 12,
                        "mouse drag must select from the hit-tested anchor to the release position");
                Require(!editor->HasCapture(), "mouse release must finish dragging");
                Mouse(editor, wxEVT_MOTION, 18);
                Require(editor->Send(Message::GetCurrentPos) == 12, "motion after release must not extend selection");
                Character(editor, 'Q');
                Require(Text(editor) == "AQ\r\nlast", "input must replace the mouse selection");
                Key(editor, 'Z', true);
                Require(Text(editor) == "A中文\r\n  B\r\nlast", "Undo must restore mouse-selected text");
                std::puts("PASS: mouse hit, multiline drag, release, replacement and Undo");
                std::string large;
                for (int line = 0; line < 100; ++line)
                    large += std::string(240, 'x') + "中文\r\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(large.c_str()));
                frame->SetClientSize(420, 220);
                wxYield();
                Scroll(editor, wxVERTICAL, 30);
                Scroll(editor, wxHORIZONTAL, 600);
                Require(editor->Send(Message::GetFirstVisibleLine) == 30 && editor->Send(Message::GetXOffset) == 600,
                        "native scrollbar events must move the core viewport on both axes");
                Require(editor->GetScrollPos(wxVERTICAL) == 30 && editor->GetScrollPos(wxHORIZONTAL) == 600,
                        "native scrollbar positions must follow the core viewport");
                const auto target = editor->Send(Message::PositionFromLine, 32) + 100;
                Mouse(editor, wxEVT_LEFT_DOWN, static_cast<int>(target));
                Mouse(editor, wxEVT_LEFT_UP, static_cast<int>(target));
                Require(editor->Send(Message::GetCurrentPos) == target,
                        "mouse hit-testing must include both scroll offsets");
                frame->SetClientSize(600, 300);
                wxYield();
                Mouse(editor, wxEVT_LEFT_DOWN, static_cast<int>(target + 3));
                Mouse(editor, wxEVT_LEFT_UP, static_cast<int>(target + 3));
                Require(editor->Send(Message::GetCurrentPos) == target + 3,
                        "mouse hit-testing must remain consistent after resize");
                Key(editor, WXK_END, true);
                Key(editor, WXK_UP);
                Key(editor, WXK_END);
                const auto end = editor->Send(Message::GetCurrentPos);
                const auto x = editor->Send(Message::PointXFromPosition, 0, end);
                const auto y = editor->Send(Message::PointYFromPosition, 0, end);
                Require(editor->Send(Message::GetXOffset) > 0 && x >= 0 && x < editor->GetClientSize().x && y >= 0 &&
                            y < editor->GetClientSize().y,
                        "keyboard navigation must reveal the caret on both axes");
                Key(editor, WXK_HOME, true);
                Require(editor->Send(Message::GetFirstVisibleLine) == 0 && editor->Send(Message::GetXOffset) == 0,
                        "Ctrl+Home must reveal document start after scrolling");
                std::puts("PASS: horizontal/vertical scroll, resized mouse hit-testing and visible keyboard caret");
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
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SmokeApp);
