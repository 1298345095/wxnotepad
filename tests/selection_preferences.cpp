#include "native_selection_helpers.h"
#include "native_key_helpers.h"
#include <fstream>
#include <filesystem>
#include <sstream>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Command(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Settings", label);
    Require(id != wxNOT_FOUND, "native selection preference exists without a settings store");
    NativeSearch::Command(frame, id);
}
void Reset(EditorControl* editor, const char* text) {
    TestEditor::ResetStream(editor, text);
    editor->Send(Message::SetFirstVisibleLine, 0);
    editor->Send(Message::SetXOffset, 0);
    editor->Refresh(false);
    editor->Update();
    wxYield();
}
void CheckReference(EditorControl* editor, const std::string& name) {
    std::ifstream input{std::filesystem::path(SELECTION_PREFERENCES_REFERENCE)};
    Require(input.good(), "frozen original selection states are available");
    std::string row;
    std::getline(input, row);
    while (std::getline(input, row)) {
        std::istringstream fields(row);
        std::string key;
        fields >> key;
        if (key != name) continue;
        for (const auto message :
             {Message::GetAnchor, Message::GetCurrentPos, Message::GetModify, Message::CanUndo, Message::CanRedo,
              Message::GetSelections, Message::GetMultipleSelection, Message::GetUndoSelectionHistory,
              Message::GetFirstVisibleLine, Message::GetXOffset}) {
            intptr_t expected = 0;
            fields >> expected;
            const auto actual = editor->Send(message);
            if (!fields || actual != expected) {
                std::fprintf(stderr, "%s message=%u expected=%lld actual=%lld\n", name.c_str(),
                             static_cast<unsigned>(message), static_cast<long long>(expected),
                             static_cast<long long>(actual));
                throw std::runtime_error("public editor state matches frozen original runtime");
            }
        }
        return;
    }
    throw std::runtime_error("named frozen original state exists");
}
void History(EditorFrame* frame) {
    constexpr const char* longLine = "012345678901234567890123456789012345678901234567890123456789"
                                     "012345678901234567890123456789012345678901234567890123456789\n";
    auto* editor = frame->Editor();
    Reset(editor, "abcd\nefgh\nijkl");
    editor->Send(Message::SetSel, 2, 1);
    editor->Send(Message::AddSelection, 7, 6);
    CheckReference(editor, "multiple-before");
    Command(frame, "Enable Multiple Selection");
    CheckReference(editor, "multiple-disabled-active");
    editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("X"));
    Require(TestEditor::Text(editor) == "acd\neXgh\nijkl", "disabled active ranges retain original replacement result");
    CheckReference(editor, "multiple-disabled-replace");
    editor->Send(Message::Undo);
    CheckReference(editor, "multiple-disabled-undo");
    Command(frame, "Enable Multiple Selection");
    for (const bool enabled : {true, false}) {
        if (!enabled) Command(frame, "Undo Redo Remember Selection");
        for (const bool reverse : {false, true}) {
            std::string text;
            for (int line = 0; line < 80; ++line)
                text += longLine;
            Reset(editor, text.c_str());
            editor->Send(Message::SetSel, reverse ? 2426 : 2422, reverse ? 2422 : 2426);
            editor->Send(Message::SetFirstVisibleLine, 15);
            editor->Send(Message::SetXOffset, 20);
            editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("X"));
            editor->Send(Message::SetSel, 615, 615);
            editor->Send(Message::SetFirstVisibleLine, 5);
            const std::string prefix =
                std::string("history-") + (enabled ? "True-" : "False-") + (reverse ? "True-" : "False-");
            const auto changed = TestEditor::Text(editor);
            CheckReference(editor, prefix + "edited");
            editor->Send(Message::Undo);
            Require(TestEditor::Text(editor) == text, "Undo restores original bytes");
            CheckReference(editor, prefix + "undo");
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == changed, "Redo restores edited bytes");
            CheckReference(editor, prefix + "redo");
        }
    }
    Command(frame, "Undo Redo Remember Selection");
    Reset(editor, longLine);
    editor->Send(Message::SetXOffset, 20);
    editor->Send(Message::SetSel, 6, 2);
    editor->Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>("X"));
    CheckReference(editor, "history-toggle-before");
    Command(frame, "Undo Redo Remember Selection");
    CheckReference(editor, "history-toggle-off");
    Command(frame, "Undo Redo Remember Selection");
    CheckReference(editor, "history-toggle-on");
    editor->Send(Message::Undo);
    CheckReference(editor, "history-toggle-undo");
    editor->Send(Message::Redo);
    CheckReference(editor, "history-toggle-redo");
    std::puts("PASS: frozen original active ranges, direction, scroll, history clearing and save state");
}
wxPoint Point(EditorControl* editor, intptr_t position) {
    return {static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position)),
            static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position) +
                             editor->Send(Message::TextHeight, 0) / 2)};
}
void Drag(EditorControl* editor, intptr_t anchor, intptr_t caret, bool control = false, bool alt = false) {
    constexpr int motionSteps = 12;
    const auto start = Point(editor, anchor);
    const auto finish = Point(editor, caret);
    wxMilliSleep(550);
    NativeSelection::Mouse(editor, wxEVT_LEFT_DOWN, start, control, alt);
    wxYield();
    // wxMSW filters duplicate motion coordinates. Move through the actual path
    // so the core's normal drag throttling receives distinct native events.
    for (int motion = 1; motion <= motionSteps; ++motion) {
        const wxPoint point(start.x + (finish.x - start.x) * motion / motionSteps,
                            start.y + (finish.y - start.y) * motion / motionSteps);
        NativeSelection::Mouse(editor, wxEVT_MOTION, point, control, alt);
        wxYield();
    }
    NativeSelection::Mouse(editor, wxEVT_LEFT_UP, finish, control, alt);
    wxYield();
}
void NativeKey(EditorControl* editor, int key) {
#ifdef __WXMSW__
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read owned native typing thread state");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    for (const int modifier : {VK_CONTROL, VK_MENU, VK_SHIFT})
        modified[modifier] = 0;
    Require(SetKeyboardState(modified), "native typing uses released shortcut modifiers");
    MSG message{};
    message.hwnd = static_cast<HWND>(editor->GetHandle());
    message.message = WM_KEYDOWN;
    message.wParam = key == WXK_DELETE ? VK_DELETE : key;
    message.lParam = key == WXK_DELETE ? (LPARAM{KF_EXTENDED} << 16) | 1 : 1;
    if (!wxGetTopLevelParent(editor)->MSWTranslateMessage(&message)) DispatchMessageW(&message);
    if (key != WXK_DELETE) {
        message.message = WM_CHAR;
        message.wParam = key;
        DispatchMessageW(&message);
    }
    message.message = WM_KEYUP;
    message.lParam |= LPARAM{KF_UP | KF_REPEAT} << 16;
    DispatchMessageW(&message);
    SetKeyboardState(previous);
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->key.send_event = TRUE;
    event->key.time = GDK_CURRENT_TIME;
    event->key.keyval = key == WXK_DELETE ? GDK_KEY_Delete : static_cast<guint>(key);
    gdk_event_set_device(event, gdk_seat_get_keyboard(gdk_display_get_default_seat(gtk_widget_get_display(widget))));
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
void Gestures(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool enabled : {true, false}) {
        if (!enabled) Command(frame, "Enable Multiple Selection");
        Reset(editor, "abcd\r\nefgh\r\nijkl");
        Drag(editor, 1, 2);
        Drag(editor, 7, 8, true);
        Require(editor->Send(Message::GetSelections) == (enabled ? 2 : 1), "native Ctrl drag follows live preference");
        NativeKey(editor, 'X');
        Require(TestEditor::Text(editor) == (enabled ? "aXcd\r\neXgh\r\nijkl" : "abcd\r\neXgh\r\nijkl"),
                "native typing replaces selected ranges");
        editor->Send(Message::Undo);
        NativeKey(editor, WXK_DELETE);
        Require(TestEditor::Text(editor) == (enabled ? "acd\r\negh\r\nijkl" : "abcd\r\negh\r\nijkl"),
                "deletion reaches native selected ranges");
        Reset(editor, "abcd\r\nefgh\r\nijkl");
        Drag(editor, 1, 15, false, true);
        Require(editor->Send(Message::GetSelections) == 3 &&
                    editor->Send(Message::GetRectangularSelectionAnchor) == 1 &&
                    editor->Send(Message::GetRectangularSelectionCaret) == 15,
                "native Alt rectangle remains available with either preference");
        NativeKey(editor, 'N');
        Require(TestEditor::Text(editor) == "aNd\r\neNh\r\niNl", "native rectangle typing reaches each logical line");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "abcd\r\nefgh\r\nijkl", "rectangle replacement remains one Undo");
        Reset(editor, "abcd\r\nefgh\r\nijkl");
        editor->Send(Message::SetSel, 1, 8);
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Lines));
        NativeKey(editor, 'L');
        Require(TestEditor::Text(editor) == "L\r\nijkl",
                "native line selection typing preserves the original line ending");
    }
    Command(frame, "Enable Multiple Selection");
    std::puts("PASS: native Ctrl/Alt gestures, typing, deletion, rectangle and line selections with live toggles");
}
class SelectionPreferencesApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "owned native window is presented");
                if (frame->Editor()->Send(Message::GetWrapMode)) frame->Editor()->ToggleWordWrap();
                History(frame);
                Gestures(frame);
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
wxIMPLEMENT_APP(SelectionPreferencesApp);
