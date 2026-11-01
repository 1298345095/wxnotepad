#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>

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
void Reset(EditorControl* editor, const char* text) {
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::SetSel, 0, 0);
    editor->Send(Message::ScrollToStart);
    editor->Send(Message::SetXOffset, 0);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    editor->Refresh(false);
    editor->Update();
    wxYield();
}
wxPoint Point(EditorControl* editor, intptr_t position, int virtualColumns = 0) {
    const auto space = editor->Send(Message::TextWidth, 32, reinterpret_cast<intptr_t>(" "));
    return {static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position) + space * virtualColumns),
            static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position) +
                             editor->Send(Message::TextHeight, 0) / 2)};
}
void Mouse(EditorControl* editor, wxEventType type, wxPoint point, bool control = false, bool shift = false,
           bool alt = false) {
    wxMouseEvent event(type);
    event.SetPosition(point);
    event.SetControlDown(control);
    event.SetShiftDown(shift);
    event.SetAltDown(alt);
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Drag(EditorControl* editor, wxPoint anchor, wxPoint caret, bool control = false, bool shift = false,
          bool alt = false) {
    // Distinct clicks must not accidentally become double/triple clicks.
    wxMilliSleep(550);
    Mouse(editor, wxEVT_LEFT_DOWN, anchor, control, shift, alt);
    // The original core throttles drag motion after autoscrolling. Replay a
    // short motion sequence rather than assuming a single motion updates it.
    for (int motion = 0; motion < 12; ++motion)
        Mouse(editor, wxEVT_MOTION, caret, control, shift, alt);
    Mouse(editor, wxEVT_LEFT_UP, caret, control, shift, alt);
}
void Character(EditorControl* editor, int character) {
    wxKeyEvent event(wxEVT_CHAR);
    event.m_keyCode = character;
    event.m_uniChar = character;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Key(EditorControl* editor, int key, bool shift = false, bool alt = false) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = key;
    event.m_shiftDown = shift;
    event.m_altDown = alt;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Undo(EditorControl* editor, const char* before, const char* after) {
    if (Text(editor) != after) std::fprintf(stderr, "Expected <%s>, actual <%s>\n", after, Text(editor).c_str());
    Require(Text(editor) == after, "edit produces original-core bytes");
    editor->Send(Message::Undo);
    Require(Text(editor) == before && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
            "one Undo restores the complete edit and save point");
    editor->Send(Message::Redo);
    Require(Text(editor) == after, "one Redo restores the complete edit");
}
class SelectionApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                Require(editor->Send(Message::GetMultipleSelection) == 1, "original enables multiple selection");
                Require(editor->Send(Message::GetAdditionalSelectionTyping) == 1,
                        "original types into additional selections");
                Require(editor->Send(Message::GetMultiPaste) == static_cast<intptr_t>(Scintilla::MultiPaste::Each),
                        "original pastes into each selection");
                Require(editor->Send(Message::GetVirtualSpaceOptions) ==
                            static_cast<intptr_t>(Scintilla::VirtualSpace::RectangularSelection),
                        "original permits virtual space only for rectangles");
                Reset(editor, "abcd\r\nefgh\r\nijkl");
                Drag(editor, Point(editor, 1), Point(editor, 15), false, false, true);
                Require(editor->Send(Message::GetSelectionMode) ==
                                static_cast<intptr_t>(Scintilla::SelectionMode::Rectangle) &&
                            editor->Send(Message::GetSelections) == 3 &&
                            editor->Send(Message::GetRectangularSelectionAnchor) == 1 &&
                            editor->Send(Message::GetRectangularSelectionCaret) == 15,
                        "Alt drag retains rectangle endpoints and one range per line");
                Character(editor, 'X');
                Undo(editor, "abcd\r\nefgh\r\nijkl", "aXd\r\neXh\r\niXl");
                std::puts("PASS: original selection defaults, Alt rectangle replacement and grouped Undo/Redo");
                Reset(editor, "abcd\r\nefgh\r\nijkl");
                Drag(editor, Point(editor, 1), Point(editor, 1));
                Drag(editor, Point(editor, 7), Point(editor, 7), true);
                Drag(editor, Point(editor, 13), Point(editor, 13), true);
                Require(editor->Send(Message::GetSelections) == 3,
                        "Ctrl click adds independent carets instead of replacing the main selection");
                editor->IndentLines(true);
                Require(editor->Send(Message::GetSelections) == 3,
                        "explicit Indent retains all empty selection ranges");
                Undo(editor, "abcd\r\nefgh\r\nijkl", "\tabcd\r\n\tefgh\r\n\tijkl");
                std::puts("PASS: Ctrl click multiple carets, explicit Indent and grouped Undo/Redo");
                Reset(editor, "abcd\r\nefgh\r\nijkl");
                Drag(editor, Point(editor, 1), Point(editor, 2));
                Drag(editor, Point(editor, 7), Point(editor, 8), true);
                Character(editor, 'Y');
                Undo(editor, "abcd\r\nefgh\r\nijkl", "aYcd\r\neYgh\r\nijkl");
                editor->Send(Message::Undo);
                Key(editor, WXK_DELETE);
                Undo(editor, "abcd\r\nefgh\r\nijkl", "acd\r\negh\r\nijkl");
                std::puts("PASS: multiple selection typing and Delete preserve ranges and undo grouping");
                editor->Send(Message::SetTabWidth, 4);
                Reset(editor, "\tabcd\r\nx\r\n\tqrst");
                Drag(editor, Point(editor, 2), Point(editor, 14), false, false, true);
                Require(editor->Send(Message::GetSelectionNAnchorVirtualSpace, 1) == 4 &&
                            editor->Send(Message::GetSelectionNCaretVirtualSpace, 1) == 6,
                        "rectangle across tabs retains short-line virtual columns five through seven");
                Character(editor, 'X');
                Undo(editor, "\tabcd\r\nx\r\n\tqrst", "\taXd\r\nx    X\r\n\tqXt");
                editor->Send(Message::Undo);
                Key(editor, WXK_DELETE);
                Undo(editor, "\tabcd\r\nx\r\n\tqrst", "\tad\r\nx\r\n\tqt");
                Reset(editor, "\tabcd\r\nx\r\n\tqrst");
                Drag(editor, Point(editor, 2), Point(editor, 12), false, false, true);
                editor->IndentLines(true);
                Require(editor->Send(Message::GetSelections) == 3 &&
                            editor->Send(Message::GetSelectionMode) !=
                                static_cast<intptr_t>(Scintilla::SelectionMode::Stream),
                        "explicit Indent preserves a thin rectangle with virtual space");
                Undo(editor, "\tabcd\r\nx\r\n\tqrst", "\t\tabcd\r\n\tx\r\n\t\tqrst");
                std::puts("PASS: tabs, short-line virtual columns, rectangle typing/Delete/Indent and one-step undo");
                Reset(editor, "abcd\r\nefgh\r\nijkl");
                Drag(editor, Point(editor, 1), Point(editor, 1));
                Drag(editor, Point(editor, 8), Point(editor, 8), false, true);
                Require(editor->Send(Message::GetAnchor) == 1 && editor->Send(Message::GetCurrentPos) == 8 &&
                            editor->Send(Message::GetSelectionMode) ==
                                static_cast<intptr_t>(Scintilla::SelectionMode::Stream),
                        "Shift click extends the ordinary stream anchor");
                Character(editor, 'S');
                Undo(editor, "abcd\r\nefgh\r\nijkl", "aSgh\r\nijkl");
                editor->Send(Message::SetMultipleSelection, 0);
                Reset(editor, "abcd\r\nefgh");
                Drag(editor, Point(editor, 1), Point(editor, 1));
                Drag(editor, Point(editor, 7), Point(editor, 7), true);
                Require(editor->Send(Message::GetSelections) == 1 && editor->Send(Message::GetCurrentPos) == 7,
                        "disabling the original multiple-selection option makes Ctrl click a single caret");
                editor->Send(Message::SetMultipleSelection, 1);
                std::puts("PASS: Shift stream selection and disabled multiple-selection option");
                frame->SetClientSize(420, 240);
                std::string wrapped(120, 'a');
                wrapped += "\r\n" + std::string(120, 'b');
                Reset(editor, wrapped.c_str());
                editor->ToggleWordWrap();
                wxYield();
                editor->Refresh(false);
                editor->Update();
                wxYield();
                Require(editor->Send(Message::WrapCount, 0) > 1, "wrapped fixture has multiple display rows");
                Drag(editor, Point(editor, 1), Point(editor, 125), false, false, true);
                Require(editor->Send(Message::GetSelections) == 2 &&
                            editor->Send(Message::GetRectangularSelectionAnchor) == 1 &&
                            editor->Send(Message::GetRectangularSelectionCaret) == 125,
                        "wrapped rectangle uses original logical-line ranges and hit-tested endpoints");
                editor->ToggleWordWrap();
                Reset(editor, "head\r\none\r\ntwo\r\nlast");
                editor->Send(Message::SetFoldLevel, 0, 0x2400);
                editor->Send(Message::SetFoldLevel, 1, 0x401);
                editor->Send(Message::SetFoldLevel, 2, 0x401);
                editor->Send(Message::SetFoldLevel, 3, 0x400);
                editor->Send(Message::ToggleFold, 0);
                Require(!editor->Send(Message::GetLineVisible, 1) && !editor->Send(Message::GetLineVisible, 2),
                        "fold fixture contracts two document lines");
                editor->Refresh(false);
                editor->Update();
                wxYield();
                Drag(editor, Point(editor, 1), Point(editor, 19), false, false, true);
                Require(editor->Send(Message::GetSelections) == 4 &&
                            editor->Send(Message::GetRectangularSelectionCaret) == 19,
                        "rectangle across a fold retains original hidden logical-line ranges");
                Key(editor, WXK_DELETE);
                Undo(editor, "head\r\none\r\ntwo\r\nlast", "hd\r\no\r\nt\r\nlt");
                std::puts("PASS: wrapped display rows and folded logical-line rectangle endpoints/editing");
                std::string scrolled;
                for (int line = 0; line < 40; ++line)
                    scrolled += std::string(100, 'a') + "\r\n";
                Reset(editor, scrolled.c_str());
                editor->Send(Message::ShowLines, 0, 40);
                wxScrollWinEvent vertical(wxEVT_SCROLLWIN_THUMBTRACK, 15, wxVERTICAL);
                editor->GetEventHandler()->ProcessEvent(vertical);
                wxScrollWinEvent horizontal(wxEVT_SCROLLWIN_THUMBTRACK, 200, wxHORIZONTAL);
                editor->GetEventHandler()->ProcessEvent(horizontal);
                Require(editor->Send(Message::GetFirstVisibleLine) == 15 && editor->Send(Message::GetXOffset) == 200,
                        "rectangle fixture scrolls both viewport axes");
                Drag(editor, Point(editor, 17 * 102 + 40), Point(editor, 19 * 102 + 42), false, false, true);
                Require(editor->Send(Message::GetSelections) == 3 &&
                            editor->Send(Message::GetRectangularSelectionAnchor) == 17 * 102 + 40 &&
                            editor->Send(Message::GetRectangularSelectionCaret) == 19 * 102 + 42,
                        "scrolled rectangle hit-testing includes both native scrollbar offsets");
                std::puts("PASS: horizontal/vertical scrolling preserves rectangle endpoints");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Editor()->Send(Message::SetSavePoint);
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }
    int OnExit() override { return result_; }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(SelectionApp);
