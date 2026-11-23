#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include <cstdio>
#include <print>
#include <exception>
#include <memory>
#include <utility>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Position(EditorControl* editor, intptr_t anchor, intptr_t caret, const char* message) {
    using enum Scintilla::Message;
    if (editor->Send(GetAnchor) != anchor || editor->Send(GetCurrentPos) != caret) {
        std::println(stderr, "Anchor/caret: {:d}/{:d}; expected {:d}/{:d}",
                     static_cast<long long>(editor->Send(GetAnchor)),
                     static_cast<long long>(editor->Send(GetCurrentPos)), static_cast<long long>(anchor),
                     static_cast<long long>(caret));
        throw std::runtime_error(message);
    }
}
void Load(EditorControl* editor, const std::string& text, intptr_t anchor, intptr_t caret) {
    using enum Scintilla::Message;
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
    editor->Send(SetSel, anchor, caret);
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
}
void Case(EditorFrame* frame, int command, const std::string& text, intptr_t anchor, intptr_t caret,
          intptr_t expectedAnchor, intptr_t expectedCaret, const char* message) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    Load(editor, text, anchor, caret);
    NativeSearch::Command(frame, command);
    Position(editor, expectedAnchor, expectedCaret, message);
    Require(NativeSearch::Text(editor) == text && !editor->Send(GetModify) && !editor->Send(CanUndo) &&
                !editor->Send(CanRedo),
            "brace command must preserve document bytes, save point and empty edit history");
}
void Language(EditorFrame* frame, const char* name) {
    const int id = frame->GetMenuBar()->FindMenuItem("Scheme", name);
    Require(id != wxNOT_FOUND, "supported language must be present in its native menu");
    NativeSearch::Command(frame, id);
}
void Shortcut(EditorControl* editor, bool shift) {
#ifdef __WXMSW__
    Require(
        NativeSearch::WindowsShortcut(
            editor, 'B', {{VK_CONTROL, 0x80}, {VK_SHIFT, shift ? 0x80 : 0}, {VK_MENU, 0}, {VK_LWIN, 0}, {VK_RWIN, 0}}),
        "owned native editor must handle matching-brace accelerator");
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = shift ? GDK_KEY_B : GDK_KEY_b;
    event->key.state = GDK_CONTROL_MASK | (shift ? GDK_SHIFT_MASK : 0);
    GdkKeymapKey* keys = nullptr;
    gint count = 0;
    auto* keymap = gdk_keymap_get_for_display(gtk_widget_get_display(widget));
    if (gdk_keymap_get_entries_for_keyval(keymap, event->key.keyval, &keys, &count) && count > 0) {
        event->key.hardware_keycode = keys[0].keycode;
        event->key.group = keys[0].group;
    }
    g_free(keys);
    auto* top = GTK_WINDOW(wxGetTopLevelParent(editor)->GetHandle());
    if (!gtk_window_activate_key(top, &event->key)) gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
std::pair<int, int> BasicBraceCases(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const int find = frame->GetMenuBar()->FindMenuItem("Search", "Find Matching Brace");
    Require(find != wxNOT_FOUND, "native Edit menu must expose Find Matching Brace");
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>("(a(b)c)"));
    editor->Send(GotoPos, 2);
    NativeSearch::Command(frame, find);
    Require(editor->Send(GetCurrentPos) == 4 && editor->Send(GetAnchor) == 4,
            "Find Matching Brace navigates to the nested closing delimiter");
    std::puts("PASS: native menu navigates to the matching nested brace");
    const int select = frame->GetMenuBar()->FindMenuItem("Search", "Select to Matching Brace");
    Require(select != wxNOT_FOUND, "native Edit menu must expose Select to Matching Brace");
    editor->Send(SetSel, 0, 2);
    NativeSearch::Command(frame, select);
    Require(editor->Send(GetAnchor) == 0 && editor->Send(GetCurrentPos) == 5,
            "forward matching selection includes both nested braces and retains existing extent");
    editor->Send(SetSel, 7, 4);
    NativeSearch::Command(frame, select);
    Require(editor->Send(GetAnchor) == 7 && editor->Send(GetCurrentPos) == 2,
            "backward matching selection includes both nested braces and retains existing extent");
    std::puts("PASS: native menu expands selections in the matching direction");
    Language(frame, "Plain Text");
    Case(frame, find, "()[]", 2, 2, 3, 3, "current opening delimiter precedes previous closing delimiter");
    Case(frame, find, "()(", 2, 2, 2, 2, "unmatched current delimiter prevents previous-character fallback");
    Case(frame, find, "(x)", 1, 1, 2, 2, "non-brace caret checks the previous character");
    Case(frame, find, "(x)", 3, 3, 0, 0, "EOF checks the previous closing delimiter");
    Case(frame, find, "(x) word", 5, 5, 5, 5, "irrelevant caret must not search elsewhere on the line");
    Case(frame, find, "(x", 1, 1, 1, 1, "unmatched previous delimiter leaves caret unchanged");
    Case(frame, select, "(x", 0, 1, 0, 1, "unmatched delimiter preserves existing selection");
    Case(frame, find, "", 0, 0, 0, 0, "empty document is a no-op");
    for (const auto* text : {"(x)", "[x]", "{x}", "<x>"}) {
        Case(frame, find, text, 0, 0, 2, 2, "original recognized opening delimiters match");
        Case(frame, find, text, 2, 2, 0, 0, "original recognized closing delimiters match");
    }
    for (const auto* text : {"'x'", "\"x\"", "|x|", "«x»"})
        Case(frame, select, text, 0, 0, 0, 0, "unrecognized delimiter leaves selection unchanged");
    Case(frame, select, "..(x)..", 6, 2, 2, 6, "forward matching determines orientation of backward selection");
    Case(frame, select, "..(x)..", 0, 4, 5, 0, "backward matching determines orientation of forward selection");
    Case(frame, select, "..(x)..", 0, 3, 0, 5, "previous opening delimiter expands existing selection");
    Case(frame, select, "..(x)..", 7, 5, 7, 2, "previous closing delimiter expands existing selection");
    Case(frame, select, "..(x)..", 2, 2, 2, 5, "empty forward selection includes both delimiters");
    Case(frame, select, "..(x)..", 4, 4, 5, 2, "empty backward selection includes both delimiters");
    Case(frame, find, "中(a😀b)尾", 3, 3, 10, 10, "matching positions use UTF-8 byte offsets");
    Case(frame, select, "中(a😀b)尾", 11, 11, 11, 3, "previous ASCII delimiter after emoji matches backward");
    Case(frame, find, "中(a😀b)尾", 14, 14, 14, 14, "previous UTF-8 character is tested at its start");
    std::puts("PASS: original delimiter set, caret precedence, no-ops, selection extent/orientation and UTF-8");
    return {find, select};
}
void BraceShortcuts(EditorFrame* frame, int find, int select) {
    auto* editor = frame->Editor();
    const std::unique_ptr<wxAcceleratorEntry> findAccelerator(frame->GetMenuBar()->FindItem(find)->GetAccel());
    const std::unique_ptr<wxAcceleratorEntry> selectAccelerator(frame->GetMenuBar()->FindItem(select)->GetAccel());
    Require(findAccelerator && findAccelerator->GetKeyCode() == 'B' && findAccelerator->GetFlags() == wxACCEL_CTRL &&
                selectAccelerator && selectAccelerator->GetKeyCode() == 'B' &&
                selectAccelerator->GetFlags() == (wxACCEL_CTRL | wxACCEL_SHIFT),
            "native menu accelerator modifiers must exactly match the original");
    Load(editor, "(x)", 0, 0);
    Shortcut(editor, false);
    Position(editor, 2, 2, "native Ctrl+B must navigate through the same command");
    Shortcut(editor, true);
    Position(editor, 3, 0, "native Ctrl+Shift+B must select through the same command");
    for (const auto modifiers : {wxMOD_CONTROL | wxMOD_ALT, wxMOD_CONTROL | wxMOD_META}) {
        Load(editor, "(x)", 0, 0);
        wxKeyEvent key(wxEVT_CHAR_HOOK);
        key.m_keyCode = 'B';
        key.SetControlDown(true);
        key.SetAltDown((modifiers & wxMOD_ALT) != 0);
        key.SetMetaDown((modifiers & wxMOD_META) != 0);
        key.SetEventObject(editor);
        frame->GetEventHandler()->ProcessEvent(key);
        Position(editor, 0, 0, "extra Alt/Meta modifier must not invoke either command");
    }
    std::puts("PASS: native Ctrl+B/Ctrl+Shift+B and exact shortcut modifiers");
}
void LexerStyleMatching(EditorFrame* frame, int find, int select) {
    auto* editor = frame->Editor();
    for (const auto* language : {"Plain Text", "C/C++", "Python", "JSON"}) {
        Language(frame, language);
        Load(editor, "[\"]\"]", 0, 0);
        editor->Send(Message::Colourise, 0, -1);
        NativeFold::Paint(editor);
        NativeSearch::Command(frame, find);
        Position(editor, language == std::string("Plain Text") ? 2 : 4, language == std::string("Plain Text") ? 2 : 4,
                 "match must honor the current lexer's delimiter style");
        if (language != std::string("Plain Text")) {
            editor->Send(Message::SetSel, 2, 2);
            NativeSearch::Command(frame, select);
            Position(editor, 2, 2, "unmatched string-style delimiter must not match a code-style delimiter");
        }
    }
    std::puts("PASS: Plain Text/C++/Python/JSON preserve pinned-core lexer-style matching");
}
void BraceHistoryAndProtection(EditorFrame* frame, int find, int select) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    Language(frame, "Plain Text");
    Load(editor, "(x)", 0, 0);
    editor->Send(InsertText, 3, reinterpret_cast<intptr_t>("!"));
    NativeSearch::Command(frame, find);
    Require(NativeSearch::Text(editor) == "(x)!" && editor->Send(GetModify) && editor->Send(CanUndo),
            "navigation preserves existing modified state and Undo history");
    editor->Send(Undo);
    Require(NativeSearch::Text(editor) == "(x)" && !editor->Send(GetModify) && editor->Send(CanRedo),
            "one Undo after navigation still restores the preceding edit");
    editor->Send(SetSel, 0, 0);
    editor->Send(SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(find) && frame->GetMenuBar()->IsEnabled(select),
            "read-only documents retain both nonediting brace commands");
    NativeSearch::Command(frame, select);
    Position(editor, 0, 3, "read-only selection still includes both matching delimiters");
    Require(!editor->Send(GetModify), "read-only selection preserves the save point");
    editor->Send(SetReadOnly, 0);
    Require(editor->Send(CanRedo), "selection preserves existing Redo history");
    editor->Send(Redo);
    Require(NativeSearch::Text(editor) == "(x)!", "Redo still restores the edit after brace selection");
    std::puts("PASS: read-only commands preserve bytes, modified state and existing Undo/Redo history");
}
void CollapsedBraceTargets(EditorFrame* frame, int find, int select) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    Language(frame, "C/C++");
    const int collapse = frame->GetMenuBar()->FindMenuItem("View", "All Levels");
    Require(collapse != wxNOT_FOUND, "fold fixture must expose Collapse All");
    Load(editor, "{\n x\n}\n{\n y\n}\n", 0, 0);
    editor->Send(Colourise, 0, -1);
    NativeFold::Paint(editor);
    NativeSearch::Command(frame, collapse);
    Require(!editor->Send(GetFoldExpanded, 0) && !editor->Send(GetLineVisible, 2) && !editor->Send(GetFoldExpanded, 3),
            "both real lexer folds must start collapsed");
    NativeSearch::Command(frame, find);
    Position(editor, 5, 5, "navigation retains the original hidden closing-brace position");
    Require(!editor->Send(GetFoldExpanded, 0) && !editor->Send(GetLineVisible, 2) && !editor->Send(GetFoldExpanded, 3),
            "GotoPos must retain collapsed destination and unrelated folds");
    editor->Send(SetSel, 0, 0);
    NativeSearch::Command(frame, select);
    Position(editor, 0, 6, "selection includes the hidden matching closing brace");
    Require(!editor->Send(GetFoldExpanded, 0) && !editor->Send(GetLineVisible, 2) && !editor->Send(GetFoldExpanded, 3),
            "SetSel must retain destination and unrelated folds");
    std::puts("PASS: original GotoPos/SetSel visibility retains collapsed folds");
}
void WrappedBraceTargets(EditorFrame* frame, int find, int select) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    Language(frame, "Plain Text");
    const std::string wrapped = "(" + std::string(8000, 'x') + ")";
    Load(editor, wrapped, 0, 0);
    const int wrap = frame->GetMenuBar()->FindMenuItem("View", "Word Wrap");
    NativeSearch::Command(frame, wrap);
    NativeFold::Paint(editor);
    Require(editor->Send(WrapCount, 0) > 1, "long logical line must span multiple display rows");
    NativeSearch::Command(frame, find);
    Position(editor, 8001, 8001, "wrapped navigation retains the matching logical byte position");
    const auto caretY = editor->Send(PointYFromPosition, 0, 8001);
    Require(editor->Send(GetFirstVisibleLine) > 0 && caretY >= 0 && caretY < editor->GetClientSize().y,
            "original GotoPos scrolls the wrapped matching caret into the viewport");
    NativeSearch::Command(frame, select);
    Position(editor, 8002, 0, "wrapped backward selection includes both delimiters");
    Require(NativeSearch::Text(editor) == wrapped && !editor->Send(GetModify) && !editor->Send(CanUndo),
            "wrapped brace commands preserve document and edit history");
    std::puts("PASS: wrapped logical-line matching and inclusive selection");
}
class BraceApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                const auto [find, select] = BasicBraceCases(frame);
                BraceShortcuts(frame, find, select);
                LexerStyleMatching(frame, find, select);
                BraceHistoryAndProtection(frame, find, select);
                CollapsedBraceTargets(frame, find, select);
                WrappedBraceTargets(frame, find, select);
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
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
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(BraceApp);
