#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include <wx/wx.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/ffile.h>
#include <wx/stopwatch.h>
#include <cstdio>
#include <stdexcept>
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
void Reset(EditorControl* editor, const char* text, intptr_t caret) {
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::GotoPos, caret);
    editor->Send(Message::EmptyUndoBuffer);
}
void Key(EditorControl* editor, int key, bool control = false, bool shift = false, bool alt = false) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = key;
    event.m_controlDown = control;
    event.m_shiftDown = shift;
    event.m_altDown = alt;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void NativeKey(EditorControl* editor, int key, const char* gesture, bool control = false, bool shift = false) {
    std::printf("Native key: %s\n", gesture);
    std::fflush(stdout);
#ifdef __WXMSW__
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read native thread keyboard state");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    modified[VK_CONTROL] = control ? 0x80 : 0;
    modified[VK_SHIFT] = shift ? 0x80 : 0;
    Require(SetKeyboardState(modified), "set native thread modifiers");
    const int virtualKey = key == WXK_INSERT ? VK_INSERT : key == WXK_DELETE ? VK_DELETE : key;
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYDOWN, virtualKey, 0);
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYUP, virtualKey, 0);
    SetKeyboardState(previous);
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys require private display");
    bool received = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&received, key](wxKeyEvent& event) {
        if (event.GetKeyCode() == key) received = true;
        event.Skip();
    });
    editor->PushEventHandler(&observer);
    {
        wxFFile file(focusFile, "wb");
        file.Write("key:" + wxString(gesture));
    }
    wxStopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "actual private-display key reaches editor");
#endif
    wxYield();
}
class EditingKeysApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            auto* editor = frame->Editor();
            try {
                Reset(editor, "one two", 7);
                Key(editor, WXK_BACK, true);
                Require(Text(editor) == "one ", "Ctrl+Backspace deletes the preceding word");
                Key(editor, 'Z', true);
                Require(Text(editor) == "one two" && !editor->Send(Message::CanUndo),
                        "one Ctrl+Backspace is one undo action");
                Reset(editor, "one two", 0);
                Key(editor, WXK_DELETE, true);
                Require(Text(editor) == "two", "Ctrl+Delete deletes the following word");
                Key(editor, 'Z', true);
                Require(Text(editor) == "one two" && !editor->Send(Message::CanUndo),
                        "one Ctrl+Delete is one undo action");
                std::puts("PASS: original word deletion and undo grouping");
                for (const int key : {WXK_BACK, WXK_DELETE}) {
                    Reset(editor, "one two", 7);
                    editor->Send(Message::SetSel, 0, 7);
                    Key(editor, key, true);
                    Require(Text(editor) == "one two" && editor->Send(Message::GetSelectionEmpty) &&
                                editor->Send(Message::GetCurrentPos) == 7 && !editor->Send(Message::CanUndo),
                            "original Ctrl deletion collapses a selection without editing or history");
                }
                Reset(editor, "  word\r\nnext", 2);
                Key(editor, WXK_BACK, true);
                Require(Text(editor) == "word\r\nnext", "Ctrl+Backspace removes indentation to line start");
                Key(editor, 'Z', true);
                editor->Send(Message::GotoPos, 8);
                Key(editor, WXK_BACK, true);
                Require(Text(editor) == "  wordnext", "Ctrl+Backspace at line start joins the previous line");
                Reset(editor, "first\r\n\r\nlast", 7);
                Key(editor, WXK_DELETE, true);
                Require(Text(editor) == "first\r\nlast", "Ctrl+Delete removes an empty line");
                Reset(editor, "left right", 5);
                Key(editor, WXK_BACK, true, true);
                Require(Text(editor) == "right", "Ctrl+Shift+Backspace deletes to line start");
                Key(editor, 'Z', true);
                Key(editor, WXK_DELETE, true, true);
                Require(Text(editor) == "left ", "Ctrl+Shift+Delete deletes to line end");
                Key(editor, 'Z', true);
                Require(Text(editor) == "left right" && !editor->Send(Message::CanUndo),
                        "line fragment deletion is one undo action");
                Reset(editor, "abc", 1);
                Key(editor, WXK_INSERT);
                Require(editor->Send(Message::GetOvertype) == 1, "Insert toggles overtype");
                wxKeyEvent character(wxEVT_CHAR);
                character.m_keyCode = 'X';
                character.m_uniChar = 'X';
                character.SetEventObject(editor);
                editor->GetEventHandler()->ProcessEvent(character);
                Require(Text(editor) == "aXc", "character input obeys Insert overtype state");
                Key(editor, WXK_INSERT);
                Key(editor, WXK_RETURN, false, true);
                Require(Text(editor) == "aX\r\nc", "Shift+Enter inserts one original-mode newline");
                Key(editor, 'Z', true);
                Require(Text(editor) == "aXc", "Shift+Enter is one undo action");
                Key(editor, WXK_BACK, false, true);
                Require(Text(editor) == "ac", "Shift+Backspace retains core backspace behavior");
                std::puts("PASS: original selection, line deletion, Insert and Shift editing keys");
                Reset(editor, "alpha\r\nbeta", 2);
                editor->Send(Message::SetSel, 1, 3);
                Key(editor, 'D', true);
                Require(Text(editor) == "alpha\r\nalpha\r\nbeta",
                        "original Ctrl+D duplicates the line instead of the selected core range");
                Key(editor, 'Z', true);
                Require(Text(editor) == "alpha\r\nbeta" && !editor->Send(Message::CanUndo),
                        "the overlapping application accelerator executes once");
                Key(editor, 'Z', true, true);
                Require(Text(editor) == "alpha\r\nalpha\r\nbeta", "Ctrl+Shift+Z preserves original redo");
                Key(editor, 'Y', true, true);
                Require(Text(editor) == "alpha\r\nbeta", "Ctrl+Shift+Y is original undo, not core redo");
                Key(editor, 'Y', true);
                Key(editor, 'D', true, true);
                Require(Text(editor) == "alpha\r\nbeta", "Ctrl+Shift+D deletes the current line");
                Reset(editor, "AbCd", 0);
                editor->Send(Message::SetSel, 0, 4);
                Key(editor, 'U', true);
                Require(Text(editor) == "abcd", "Ctrl+U reaches original lowercase command");
                Key(editor, 'U', true, true);
                Require(Text(editor) == "ABCD", "Ctrl+Shift+U reaches original uppercase command");
                Reset(editor, "one\r\ntwo", 6);
                Key(editor, 'S', false, false, true);
                Require(Text(editor) == "two\r\none", "Alt+S uses original line transpose binding");
                Reset(editor, "abcdef", 4);
                editor->Send(Message::SetSel, 1, 4);
                Key(editor, 'D', false, false, true);
                Require(Text(editor) == "abcdbcdef", "Alt+D duplicates the selected range");
                Key(editor, WXK_BACK, false, false, true);
                Require(Text(editor) == "abcdef", "Alt+Backspace preserves original undo");
                for (const unsigned char key : {'L', 'T'}) {
                    Reset(editor, "keep\r\ntext", 0);
                    Key(editor, key, true);
                    Key(editor, key, true, true);
                    Require(Text(editor) == "keep\r\ntext" && !editor->Send(Message::CanUndo),
                            "reserved application bindings must not fall through to unrelated core edits");
                }
                std::puts("PASS: application precedence, exact modifiers and original editing accelerators");
                editor->SetFocus();
                wxYield();
#ifdef __WXGTK__
                wxString focusFile;
                Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile),
                        "native keys require private display");
                {
                    wxFFile file(focusFile, "wb");
                    file.Write("focus:" + frame->GetTitle());
                }
                wxStopWatch focusWait;
                while (focusWait.Time() < 3000) {
                    wxString pending;
                    {
                        wxFFile file(focusFile, "rb");
                        file.ReadAll(&pending);
                    }
                    if (pending.empty()) break;
                    wxYield();
                    wxMilliSleep(10);
                }
#endif
                Reset(editor, "abc tail", 0);
                editor->Send(Message::SetSel, 0, 3);
                NativeKey(editor, WXK_INSERT, "ctrl+Insert", true);
                Require(wxTheClipboard->Open(), "open native clipboard after Ctrl+Insert");
                wxTextDataObject copied;
                const bool copiedText = wxTheClipboard->GetData(copied);
                wxTheClipboard->Close();
                Require(copiedText && copied.GetText() == "abc" && !editor->Send(Message::CanUndo),
                        "Ctrl+Insert copies the selection without editing history");
                NativeKey(editor, WXK_DELETE, "shift+Delete", false, true);
                Require(Text(editor) == " tail", "Shift+Delete cuts selected text");
                NativeKey(editor, WXK_INSERT, "shift+Insert", false, true);
                Require(Text(editor) == "abc tail", "Shift+Insert pastes the original clipboard text once");
                Key(editor, 'Z', true);
                Require(Text(editor) == " tail", "one paste key has one undo action");
                Key(editor, 'Z', true);
                Require(Text(editor) == "abc tail" && !editor->Send(Message::CanUndo),
                        "one cut key has one undo action");
                Reset(editor, "line\r\nnext", 2);
                NativeKey(editor, 'D', "ctrl+d", true);
                Require(Text(editor) == "line\r\nline\r\nnext", "native Ctrl+D dispatches original command once");
                NativeKey(editor, 'Z', "ctrl+z", true);
                Require(Text(editor) == "line\r\nnext" && !editor->Send(Message::CanUndo),
                        "native overlapping accelerator creates only one history action");
                editor->Send(Message::SetSel, 0, 4);
                NativeKey(editor, 'C', "ctrl+shift+c", true, true);
                editor->Send(Message::GotoPos, editor->Send(Message::GetLength));
                NativeKey(editor, WXK_INSERT, "shift+Insert", false, true);
                Require(Text(editor) == "line\r\nline\r\nnext",
                        "Ctrl+Shift+C preserves line-paste meaning at the destination line start");
                Reset(editor, "first\r\nsecond", 2);
                NativeKey(editor, 'X', "ctrl+shift+x", true, true);
                Require(Text(editor) == "second", "Ctrl+Shift+X cuts a whole original line");
                std::puts("PASS: native clipboard alternatives, line accelerators and single dispatch");
                Reset(editor, "  text", 2);
                editor->Send(Message::SetUseTabs, 0);
                editor->Send(Message::SetTabIndents, 1);
                Key(editor, WXK_TAB, true);
                Require(Text(editor) == "  \ttext", "Ctrl+Tab inserts a literal tab using original host override");
                Require(!editor->Send(Message::GetUseTabs) && editor->Send(Message::GetTabIndents),
                        "Ctrl+Tab restores indentation options");
                Key(editor, 'Z', true);
                Require(Text(editor) == "  text" && !editor->Send(Message::CanUndo),
                        "literal tab insertion is one undo action");
                Reset(editor, "first\r\nsecond", 1);
                editor->Send(Message::SetSelection, 1, 0);
                editor->Send(Message::AddSelection, 8, 7);
                Key(editor, WXK_ESCAPE);
                Require(editor->Send(Message::GetSelections) == 2 && !editor->Send(Message::CanUndo),
                        "original Escape leaves selections intact when no completion popup is active");
                editor->Send(Message::ClearSelections);
                std::puts("PASS: literal tab override and original Escape selection preservation");
                Reset(editor, "first\r\n\r\nsecond", 0);
                Key(editor, ']', true);
                Require(editor->Send(Message::GetCurrentPos) == 9, "Ctrl+] reaches the next paragraph");
                Key(editor, ']', true, true);
                Require(editor->Send(Message::GetCurrentPos) == 15 && editor->Send(Message::GetAnchor) == 9,
                        "Ctrl+Shift+] extends to paragraph end");
                Key(editor, '[', true);
                Require(editor->Send(Message::GetCurrentPos) == 9, "Ctrl+[ reaches current paragraph start");
                Key(editor, '[', true, true);
                Require(editor->Send(Message::GetCurrentPos) == 0 && editor->Send(Message::GetAnchor) == 9 &&
                            Text(editor) == "first\r\n\r\nsecond" && !editor->Send(Message::CanUndo),
                        "Ctrl+Shift+[ extends to the previous paragraph without editing");
                Reset(editor, "keep", 2);
                for (const unsigned char key : {'A', 'C', 'V'}) {
                    Key(editor, key, true, true);
                    Require(Text(editor) == "keep" && editor->Send(Message::GetSelectionEmpty) &&
                                !editor->Send(Message::CanUndo),
                            "shifted application keys do not alias unrelated unshifted core edits");
                }
                std::puts("PASS: remaining unreserved core paragraph bindings and exact modifier matching");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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
wxIMPLEMENT_APP(EditingKeysApp);
