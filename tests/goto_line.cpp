#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/stopwatch.h>
#include <wx/textctrl.h>
#include <cstdio>
#include <exception>
#include <functional>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool WaitFor(const std::function<bool()>& ready) {
    wxStopWatch timer;
    while (!ready() && timer.Time() < 2000) {
        wxYield();
        wxMilliSleep(1);
    }
    return ready();
}
wxTextCtrl* Column(wxDialog* dialog) {
    auto* input = wxDynamicCast(wxWindow::FindWindowByName("Go to column number", dialog), wxTextCtrl);
    Require(input, "dialog must expose the original column/document-position field");
    return input;
}
void Key(wxWindow* window, int key, bool control = false) {
#ifdef __WXMSW__
    if (key == WXK_ESCAPE) {
        // The Windows keyboard hook converts Escape into this event. Target
        // the owned input directly instead of injecting desktop keyboard input.
        wxKeyEvent event(wxEVT_CHAR_HOOK);
        event.m_keyCode = key;
        event.SetEventObject(window);
        Require(window->GetEventHandler()->ProcessEvent(event), "the dialog must handle Escape from its input");
        wxYield();
        return;
    }
    Require(NativeSearch::WindowsShortcut(window, key, {{VK_CONTROL, static_cast<BYTE>(control ? 0x80 : 0)}}),
            "the owned native window must handle the requested key");
#elif defined(__WXGTK__)
    auto* widget = GTK_WIDGET(window->GetHandle());
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = key == WXK_ESCAPE ? GDK_KEY_Escape : GDK_KEY_g;
    event->key.state = control ? static_cast<guint>(GDK_CONTROL_MASK) : 0U;
    GdkKeymapKey* keys = nullptr;
    gint count = 0;
    auto* keymap = gdk_keymap_get_for_display(gtk_widget_get_display(widget));
    if (gdk_keymap_get_entries_for_keyval(keymap, event->key.keyval, &keys, &count) && count > 0) {
        event->key.hardware_keycode = keys[0].keycode;
        event->key.group = keys[0].group;
    }
    g_free(keys);
    auto* top = GTK_WINDOW(wxGetTopLevelParent(window)->GetHandle());
    if (!gtk_window_activate_key(top, &event->key)) gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
class GoToApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
#ifdef __WXGTK__
                Require(WaitFor([&] {
                            return gtk_widget_get_mapped(GTK_WIDGET(frame->GetHandle())) &&
                                   gtk_window_is_active(GTK_WINDOW(frame->GetHandle()));
                        }),
                        "the private native frame must finish mapping before dialog interaction");
#endif
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one\r\n\xE4\xB8\xAD\xE6\x96\x87\nlast"));
                editor->Send(Message::SetSel, 1, 7);
                const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Go To Line...");
                Require(id != wxNOT_FOUND, "Edit must expose Go To Line");
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    Require(input->GetValue() == "2", "dialog must start with the current logical line");
                    auto* range = wxDynamicCast(wxWindow::FindWindowByName("Go to line range", dialog), wxStaticText);
                    Require(range && range->GetLabel().Contains("1") && range->GetLabel().Contains("3"),
                            "dialog must show the valid range");
                    input->SetValue(" +3:ignored");
                    NativeSearch::Click(dialog, wxID_OK);
                });
                NativeSearch::Selection(editor, 12, 12,
                                        "confirmation must collapse selection at last logical line start");
                Require(WaitFor([&] { return wxWindow::FindFocus() == editor; }),
                        "confirmation must return editor focus");
                std::puts("PASS: native Go To Line opens at current logical line and navigates to last line");
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    auto* column = Column(dialog);
                    Require(column->GetValue().empty(), "column must initially be empty, meaning column one");
                    input->SetValue("2:");
                    column->SetValue("2characters");
                    NativeSearch::Click(dialog, wxID_OK);
                });
                NativeSearch::Selection(editor, 8, 8, "columns must count Unicode characters, not UTF-8 bytes");
                std::puts("PASS: original two-field numeric prefixes and Unicode columns");
                editor->Send(Message::GotoPos, 12);
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    for (const auto* invalid : {"", " ", "0", "-1", "4", "999999999999999999999999999"}) {
                        input->SetValue(invalid);
                        NativeSearch::Click(dialog, wxID_OK);
                        Require(dialog->IsModal(), "invalid input must keep the dialog open");
                        NativeSearch::Selection(editor, 12, 12, "invalid input must not navigate");
                        Require(wxWindow::FindFocus() == input,
                                "invalid line input must return focus to the line field");
                    }
                    // Even a modal dialog must validate against the current core document.
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("short"));
                    input->SetValue("3");
                    NativeSearch::Click(dialog, wxID_OK);
                    Require(dialog->IsModal(), "submission must reject a line removed after dialog opening");
                    input->SetValue("1");
                    NativeSearch::Click(dialog, wxID_OK);
                });
                NativeSearch::Selection(editor, 0, 0, "valid retry must navigate to the first line");
                std::puts("PASS: invalid input feedback and submission-time line count validation");
                Dialog(
                    frame, id,
                    [&](wxDialog* dialog, wxTextCtrl* input) {
                        input->SetValue("1");
                        NativeSearch::Click(dialog, wxID_CANCEL);
                    },
                    true);
                Require(WaitFor([&] { return wxWindow::FindFocus() == editor; }),
                        "Ctrl+G cancellation must return editor focus");
                std::puts("PASS: owned native Ctrl+G opens the same dialog");
                for (const auto* eol : {"\r\n", "\n", "\r"}) {
                    const std::string text = std::string("one") + eol + "\xE4\xB8\xAD\xE6\x96\x87" + eol + "last";
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
                    Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                        input->SetValue("2");
                        NativeSearch::Click(dialog, wxID_OK);
                    });
                    const auto position =
                        static_cast<intptr_t>(std::string("one").size() + std::char_traits<char>::length(eol));
                    NativeSearch::Selection(editor, position, position,
                                            "EOL navigation must use logical UTF-8 byte boundaries");
                    Require(NativeSearch::Text(editor) == text, "navigation must preserve every EOL byte");
                }
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(""));
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    Require(input->GetValue() == "1", "empty document must start at line one");
                    NativeSearch::Click(dialog, wxID_OK);
                });
                NativeSearch::Selection(editor, 0, 0, "empty document must accept line one");
                std::puts("PASS: CRLF/LF/CR, UTF-8 and empty document navigation");

                const std::string columns = "\tA\xE4\xB8\xAD\xF0\x9F\x98\x80Z\r\nnext\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(columns.c_str()));
                editor->Send(Message::SetTabWidth, 4);
                struct Destination {
                    const char* line;
                    const char* column;
                    intptr_t position;
                };
                for (const Destination target : {Destination{"1", "", 0},
                                                 {"1", "0", 0},
                                                 {"1", "2", 0},
                                                 {"1", "4", 0},
                                                 {"1", "5", 1},
                                                 {"1", "6", 2},
                                                 {"1", "7", 5},
                                                 {"1", "8", 9},
                                                 {"1", "99999999999999999999", 10},
                                                 {"1", "-2", 1},
                                                 {"1", "-3", 2},
                                                 {"1", "-4", 5},
                                                 {"1", "-5", 9},
                                                 {"1", "-9223372036854775808", 10},
                                                 {"1", "nonnumeric", 0},
                                                 {"1.5", "0x10", 0},
                                                 {"+2:12", "2suffix", 13},
                                                 {"3", "999", 17},
                                                 {"", "1", 0},
                                                 {"", "2", 1},
                                                 {"", "3", 2},
                                                 {"", "5", 2},
                                                 {"", "6", 5},
                                                 {"", "9", 5},
                                                 {"", "10", 9},
                                                 {"", "11", 10},
                                                 {"", "12", 10},
                                                 {"", "13", 12},
                                                 {"", "17", 16},
                                                 {"nonnumeric", "+6:byte", 5}}) {
                    editor->Send(Message::SetSel, 15, 1);
                    Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                        input->SetValue(target.line);
                        Column(dialog)->SetValue(target.column);
                        NativeSearch::Click(dialog, wxID_OK);
                    });
                    NativeSearch::Selection(editor, target.position, target.position,
                                            "original column/position semantics must replace the selection");
                    Require(NativeSearch::Text(editor) == columns, "Go To must preserve Unicode and EOL bytes");
                }
                std::puts("PASS: tab stops, signed character columns, clamping, numeric prefixes and byte-position "
                          "UTF-8/CRLF boundaries");
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    for (const auto& invalid : {Destination{"", "", 0},
                                                {"bad", "bad", 0},
                                                {"", "0", 1},
                                                {"", "-1", 1},
                                                {"", "18", 1},
                                                {"0", "5", 0},
                                                {"4", "5", 0},
                                                {"4", "0", 1},
                                                {"9223372036854775808", "", 0}}) {
                        input->SetValue(invalid.line);
                        auto* column = Column(dialog);
                        column->SetValue(invalid.column);
                        NativeSearch::Click(dialog, wxID_OK);
                        Require(dialog->IsModal(), "invalid original coordinates must keep the dialog open");
                        const bool focused =
                            WaitFor([&] { return wxWindow::FindFocus() == (invalid.position ? column : input); });
                        if (!focused)
                            std::fprintf(stderr, "Validation focus: Line='%s', Column='%s', expected %s\n",
                                         invalid.line, invalid.column, invalid.position ? "column" : "line");
                        Require(focused, "validation must focus the same field as the original dialog");
                        NativeSearch::Selection(editor, 5, 5, "invalid coordinates must retain the previous caret");
                    }
                    NativeSearch::Click(dialog, wxID_CANCEL);
                });
                std::puts("PASS: original validation priority, input focus and document-position limits");

                const std::string folded =
                    "void outer() {\n  if (true) {\n    call();\n  }\n}\nvoid other() {\n  untouched();\n}\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(folded.c_str()));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                NativeFold::Paint(editor);
                editor->Send(Message::GotoPos, 0);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
                Require(!editor->Send(Message::GetLineVisible, 2) && !editor->Send(Message::GetLineVisible, 6),
                        "fixture must hide nested and unrelated target blocks");
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                const auto revision = editor->DocumentRevision();
                const auto encoding = frame->DocumentEncoding();
                const auto bom = frame->DocumentHasBom();
                const auto lexer = editor->Send(Message::GetLexer);
                editor->Send(Message::SetSel, 5, 1);
                const auto top = editor->Send(Message::GetFirstVisibleLine);
                const auto horizontal = editor->Send(Message::GetXOffset);
                for (bool escape : {false, true}) {
                    Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                        input->SetValue("3");
                        Column(dialog)->SetValue("-4");
                        if (escape)
                            Key(input, WXK_ESCAPE);
                        else
                            NativeSearch::Click(dialog, wxID_CANCEL);
                    });
                    Require(editor->Send(Message::GetAnchor) == 5 && editor->Send(Message::GetCurrentPos) == 1 &&
                                editor->Send(Message::GetFirstVisibleLine) == top &&
                                editor->Send(Message::GetXOffset) == horizontal &&
                                !editor->Send(Message::GetFoldExpanded, 0) &&
                                !editor->Send(Message::GetFoldExpanded, 1) &&
                                !editor->Send(Message::GetFoldExpanded, 5),
                            "Cancel and Escape must preserve selection direction, viewport and all folds");
                }
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    input->SetValue("3");
                    NativeSearch::Click(dialog, wxID_OK);
                });
                NativeSearch::Selection(editor, 29, 29,
                                        "nested hidden target must become a collapsed caret at line start");
                Require(editor->Send(Message::GetFoldExpanded, 0) && editor->Send(Message::GetFoldExpanded, 1) &&
                            editor->Send(Message::GetLineVisible, 2) && !editor->Send(Message::GetFoldExpanded, 5) &&
                            !editor->Send(Message::GetLineVisible, 6),
                        "navigation must reveal only the target's folded ancestors");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    input->SetValue("");
                    Column(dialog)->SetValue("30");
                    NativeSearch::Click(dialog, wxID_OK);
                });
                NativeSearch::Selection(editor, 29, 29, "position-only navigation must reveal its folded target");
                Require(editor->Send(Message::GetFoldExpanded, 0) && editor->Send(Message::GetFoldExpanded, 1) &&
                            !editor->Send(Message::GetFoldExpanded, 5),
                        "position-only navigation must preserve unrelated folded blocks");
                const auto y = editor->Send(Message::PointYFromPosition, 0, 29);
                Require(y >= 0 && y < editor->GetClientSize().y, "navigated caret must be in the viewport");
                Require(NativeSearch::Text(editor) == folded && editor->DocumentRevision() == revision &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo) && frame->DocumentEncoding() == encoding &&
                            frame->DocumentHasBom() == bom && editor->Send(Message::GetLexer) == lexer,
                        "navigation and cancellation must preserve bytes, metadata, syntax and save/redo state");
                editor->Send(Message::Redo);
                Require(NativeSearch::Text(editor) == folded + "!",
                        "navigation must leave original redo action available");
                editor->Send(Message::Undo);
                Require(!editor->Send(Message::GetModify), "Undo must still reach the same save point");
                std::puts(
                    "PASS: cancellation/Escape, selective nested fold reveal and exact history/metadata preservation");
                std::string manyLines;
                for (int line = 0; line < 150; ++line)
                    manyLines += "row\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(manyLines.c_str()));
                NativeFold::Paint(editor);
                editor->Send(Message::GotoPos, 0);
                editor->Send(Message::SetFirstVisibleLine, 0);
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    input->SetValue("76:");
                    NativeSearch::Click(dialog, wxID_OK);
                });
                Require(editor->Send(Message::GetFirstVisibleLine) == 75 - editor->Send(Message::LinesOnScreen) / 2 + 1,
                        "an offscreen logical line must be centered with the original visible policy");
                editor->Send(Message::SetSel, 405, 401);
                editor->Send(Message::SetFirstVisibleLine, 90);
                const auto scrolledTop = editor->Send(Message::GetFirstVisibleLine);
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    input->SetValue("151");
                    NativeSearch::Click(dialog, wxID_CANCEL);
                });
                Require(editor->Send(Message::GetFirstVisibleLine) == scrolledTop &&
                            editor->Send(Message::GetAnchor) == 405 && editor->Send(Message::GetCurrentPos) == 401,
                        "cancelling a scrolled document must preserve viewport and reversed selection");
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    input->SetValue("151");
                    NativeSearch::Click(dialog, wxID_OK);
                });
                NativeSearch::Selection(editor, 600, 600, "trailing EOL must expose an empty final logical line");
                const auto lastY = editor->Send(Message::PointYFromPosition, 0, 600);
                Require(lastY >= 0 && lastY < editor->GetClientSize().y,
                        "off-screen destination must scroll into view");
                editor->Send(Message::SetText, 0,
                             reinterpret_cast<intptr_t>((std::string(500, 'x') + "\nsecond\nthird").c_str()));
                editor->Send(Message::SetWrapMode, static_cast<uintptr_t>(Scintilla::Wrap::Word));
                NativeFold::Paint(editor);
                Require(editor->Send(Message::WrapCount, 0) > 1, "wrapping fixture must have continuation rows");
                Dialog(frame, id, [&](wxDialog* dialog, wxTextCtrl* input) {
                    input->SetValue("3");
                    Column(dialog)->SetValue("4");
                    NativeSearch::Click(dialog, wxID_OK);
                });
                NativeSearch::Selection(editor, 511, 511,
                                        "wrapped navigation must count logical lines, not display rows");
                std::puts("PASS: scrolled cancellation, final empty line, caret reveal and wrapped logical lines");
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
    void Dialog(EditorFrame* frame, int id, const std::function<void(wxDialog*, wxTextCtrl*)>& action,
                bool shortcut = false) {
        std::exception_ptr failure;
        CallAfter([&] {
            auto* dialog = wxDynamicCast(wxWindow::FindWindowByName("Go to line", frame), wxDialog);
            try {
                Require(dialog && dialog->IsModal(), "command must open the modal Go To Line dialog");
                auto* input = wxDynamicCast(wxWindow::FindWindowByName("Go to line number", dialog), wxTextCtrl);
                Require(input, "dialog must expose a line number input");
                action(dialog, input);
            } catch (...) {
                failure = std::current_exception();
                if (dialog && dialog->IsModal()) dialog->EndModal(wxID_CANCEL);
            }
        });
        if (shortcut)
            Key(frame->Editor(), 'G', true);
        else
            NativeSearch::Command(frame, id);
        if (failure) std::rethrow_exception(failure);
        Require(!wxWindow::FindWindowByName("Go to line", frame),
                "the completed Go To command must remove its native dialog");
    }
};
} // namespace
wxIMPLEMENT_APP(GoToApp);
