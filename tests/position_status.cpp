#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include <wx/statusbr.h>
#include <wx/textctrl.h>
#include <wx/dialog.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <cstdio>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Status(EditorFrame* frame, const wxString& expected) {
    auto* status = frame->GetStatusBar();
    Require(status->GetFieldsCount() >= 2, "status must expose a position field");
    // Native Wayland configure and paint acknowledgements arrive asynchronously.
    wxStopWatch clock;
    do {
        NativeFold::Paint(frame->Editor());
        if (status->GetStatusText(1) == expected) return;
        wxMilliSleep(10);
    } while (clock.Time() < 2000);
    const auto actual = status->GetStatusText(1);
    if (actual != expected)
        std::fprintf(stderr, "Status: %s; expected %s\n", actual.utf8_str().data(), expected.utf8_str().data());
    Require(actual == expected, "status must describe the active logical caret");
}
void PressRight(EditorControl* editor, bool shift = false) {
#ifdef __WXMSW__
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read owned thread keyboard state");
    BYTE state[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(state));
    state[VK_SHIFT] = shift ? 0x80 : 0;
    state[VK_CONTROL] = 0;
    Require(SetKeyboardState(state), "set owned thread navigation modifiers");
    SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYDOWN, VK_RIGHT, 0);
    SetKeyboardState(previous);
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_Right;
    event->key.state = shift ? GDK_SHIFT_MASK : 0U;
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
void Mouse(EditorControl* editor, intptr_t position, bool shift = false) {
    const int x = static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position)) + 2;
    const int y = static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position) +
                                   editor->Send(Message::TextHeight, 0) / 2);
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(editor->GetHandle());
    const auto modifiers = shift ? MK_SHIFT : 0;
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON | modifiers, MAKELPARAM(x, y));
    SendMessageW(handle, WM_LBUTTONUP, modifiers, MAKELPARAM(x, y));
#elif defined(__WXGTK__)
    auto* widget = editor->GetConnectWidget();
    for (const auto type : {GDK_BUTTON_PRESS, GDK_BUTTON_RELEASE}) {
        auto* event = gdk_event_new(type);
        event->button.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
        event->button.send_event = TRUE;
        event->button.time = GDK_CURRENT_TIME;
        event->button.x = x;
        event->button.y = y;
        event->button.button = 1;
        // GDK accepts combined modifier and button flags.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        event->button.state = static_cast<GdkModifierType>((shift ? GDK_SHIFT_MASK : 0) |
                                                           (type == GDK_BUTTON_RELEASE ? GDK_BUTTON1_MASK : 0));
        gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget))));
        gtk_widget_event(widget, event);
        gdk_event_free(event);
    }
#endif
    wxYield();
}
class PositionStatusApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                Status(frame, "Ln 1, Col 1");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one\r\n\t\xE4\xB8\xAD\xF0\x9F\x98\x80"));
                editor->Send(Message::SetTabWidth, 4);
                editor->Send(Message::GotoPos, 13);
                Status(frame, "Ln 2, Col 7");
                editor->Send(Message::GotoPos, 6);
                Status(frame, "Ln 2, Col 5");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "8 Columns"));
                Status(frame, "Ln 2, Col 9");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "4 Columns"));
                Status(frame, "Ln 2, Col 5");
                Require(frame->GetStatusBar()->GetStatusText().Contains("UTF-8"), "metadata must remain visible");
                std::puts("PASS: live logical line and core column use tab stops and Unicode characters");
                const std::string selected = "\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80"
                                             "e\xCC\x81\t\r\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(selected.c_str()));
                editor->Send(Message::SetSel, 0, selected.size());
                Status(frame, "Ln 2, Col 1 | Sel 8");
                editor->Send(Message::SetSel, selected.size(), 0);
                Status(frame, "Ln 1, Col 1 | Sel 8");
                editor->Send(Message::SetTargetStart, 0);
                editor->Send(Message::SetTargetEnd, 3);
                editor->Send(Message::ReplaceTarget, 3, reinterpret_cast<intptr_t>("abc"));
                editor->Send(Message::SetSel, 0, selected.size());
                Status(frame, "Ln 2, Col 1 | Sel 10");
                editor->Send(Message::GotoPos, 0);
                Status(frame, "Ln 1, Col 1");
                std::puts(
                    "PASS: selection counts Unicode scalars, CR and LF separately and refreshes changed contents");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one\n\xE4\xB8\xAD\xE6\x96\x87"));
                editor->Send(Message::GotoPos, 0);
                NativeSearch::Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                Require(query, "Find must expose its native query input");
                query->SetValue(wxString::FromUTF8("\xE4\xB8\xAD\xE6\x96\x87"));
                NativeSearch::Command(frame, wxID_FORWARD);
                Status(frame, "Ln 2, Col 3 | Sel 2");
                Require(frame->GetStatusBar()->GetStatusText().Contains("UTF-8") &&
                            frame->GetStatusBar()->GetFieldsCount() == 3 &&
                            frame->GetStatusBar()->GetStatusText(2) == "Match found",
                        "Find must preserve document metadata alongside position and its result");
                query->GetParent()->Hide();
                std::puts("PASS: Find updates selection status while retaining encoding, language and search result");
                frame->SetSize(frame->FromDIP(wxSize(500, 400)));
                const std::string longLine(500, 'x');
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(longLine.c_str()));
                editor->Send(Message::SetSel, 0, 500);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "Python"));
                Status(frame, "Ln 1, Col 501 | Sel 500");
                for (int field : {0, 1}) {
                    auto* status = frame->GetStatusBar();
                    wxRect rectangle;
                    Require(status->GetFieldRect(field, rectangle), "status field must have a native rectangle");
                    Require(rectangle.width >= status->GetTextExtent(status->GetStatusText(field)).x,
                            "metadata and position fields must fit their native font after window resizing");
                }
                std::puts("PASS: resized status fields fit metadata and position at native font metrics");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("abc\nsecond\nlast"));
                editor->Send(Message::GotoPos, 0);
                editor->SetFocus();
                NativeFold::Paint(editor);
                PressRight(editor);
                Status(frame, "Ln 1, Col 2");
                PressRight(editor, true);
                Status(frame, "Ln 1, Col 3 | Sel 1");
                Mouse(editor, 5);
                Status(frame, "Ln 2, Col 2");
                Mouse(editor, 9, true);
                Status(frame, "Ln 2, Col 6 | Sel 4");
                std::puts("PASS: native Right, Shift+Right, mouse and Shift+click update live status");
                std::exception_ptr dialogFailure;
                CallAfter([&] {
                    auto* dialog = wxDynamicCast(wxWindow::FindWindowByName("Go to line", frame), wxDialog);
                    try {
                        Require(dialog && dialog->IsModal(), "Go To Line must open its native dialog");
                        auto* input =
                            wxDynamicCast(wxWindow::FindWindowByName("Go to line number", dialog), wxTextCtrl);
                        Require(input, "Go To Line input must exist");
                        input->SetValue("3");
                        NativeSearch::Click(dialog, wxID_OK);
                    } catch (...) {
                        dialogFailure = std::current_exception();
                        if (dialog && dialog->IsModal()) dialog->EndModal(wxID_CANCEL);
                    }
                });
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Go To Line..."));
                if (dialogFailure) std::rethrow_exception(dialogFailure);
                Status(frame, "Ln 3, Col 1");
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                Status(frame, "Ln 3, Col 2");
                NativeSearch::Command(frame, wxID_UNDO);
                Status(frame, "Ln 3, Col 1");
                NativeSearch::Command(frame, wxID_REDO);
                Status(frame, "Ln 3, Col 2");
                NativeSearch::Command(frame, wxID_UNDO);
                std::puts("PASS: Go To Line, edits and native Undo/Redo retain current position status");
                editor->Send(Message::SetSel, 9, 5);
                Status(frame, "Ln 2, Col 2 | Sel 4");
                Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled, "cancelled Open remains cancelled");
                Status(frame, "Ln 2, Col 2 | Sel 4");
                Require(frame->Open("/wxnotepad-missing-directory/absent.txt").status == OpenStatus::Failed,
                        "missing source must fail before replacing the editor");
                Status(frame, "Ln 2, Col 2 | Sel 4");
                Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled,
                        "cancelled Save As remains cancelled");
                Status(frame, "Ln 2, Col 2 | Sel 4");
                const auto source = wxFileName::CreateTempFileName("wxnotepad-position-");
                {
                    wxFFile file(source, "wb");
                    Require(file.IsOpened() && file.Write("\xEF\xBB\xBF"
                                                          "new\r\ntext",
                                                          12) == 12,
                            "write UTF-8 BOM fixture");
                }
                const auto opened = frame->Open(source);
                wxRemoveFile(source);
                Require(opened.status == OpenStatus::Opened, "valid Open must replace the document");
                Status(frame, "Ln 1, Col 1");
                Require(frame->GetStatusBar()->GetStatusText().Contains("UTF-8 (BOM)"),
                        "Open retains encoding/BOM metadata");
                Require(frame->New(), "clean New must succeed");
                Status(frame, "Ln 1, Col 1");
                std::puts("PASS: cancelled/failed operations preserve real status; successful Open/New reset it");
                const std::string wrapped = std::string(500, 'x') + "\nlast";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(wrapped.c_str()));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Word Wrap"));
                editor->Send(Message::GotoPos, 400);
                Status(frame, "Ln 1, Col 401");
                Require(editor->Send(Message::WrapCount, 0) > 1, "fixture must span soft-wrapped display rows");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Word Wrap"));
                const std::string folded = "void outer() {\n  call();\n}\nvoid other() {\n  untouched();\n}\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(folded.c_str()));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                NativeFold::Paint(editor);
                editor->Send(Message::GotoPos, 0);
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 3, 1);
                const auto revision = editor->DocumentRevision();
                const auto top = editor->Send(Message::GetFirstVisibleLine);
                const auto horizontal = editor->Send(Message::GetXOffset);
                for (int attempt = 0; attempt < 4; ++attempt)
                    Status(frame, "Ln 1, Col 2 | Sel 2");
                Require(NativeSearch::Text(editor) == folded && editor->DocumentRevision() == revision &&
                            editor->Send(Message::GetAnchor) == 3 && editor->Send(Message::GetCurrentPos) == 1 &&
                            editor->Send(Message::GetFirstVisibleLine) == top &&
                            editor->Send(Message::GetXOffset) == horizontal &&
                            !editor->Send(Message::GetFoldExpanded, 0) && !editor->Send(Message::GetFoldExpanded, 3) &&
                            !editor->Send(Message::GetLineVisible, 1) && !editor->Send(Message::GetLineVisible, 4) &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo),
                        "status refresh must preserve bytes, selection, viewport, folds and history/save point");
                editor->Send(Message::Redo);
                Require(NativeSearch::Text(editor) == folded + "!",
                        "observing status leaves the real redo action intact");
                editor->Send(Message::Undo);
                Require(!editor->Send(Message::GetModify), "observing status leaves the original save point intact");
                std::puts("PASS: wrapped lines remain logical; collapsed-text status refresh is observational");
                const std::string batch = std::string(100, 'p') + "\xE4\xB8\xAD\xE4\xB8\xAD";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(batch.c_str()));
                editor->Send(Message::SetSel, 100, 106);
                Status(frame, "Ln 1, Col 103 | Sel 2");
                editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("xxxxxxxxxxxxxxxxxxxx"));
                editor->Send(Message::SetTargetStart, 123);
                editor->Send(Message::SetTargetEnd, 126);
                editor->Send(Message::ReplaceTarget, 3, reinterpret_cast<intptr_t>("abc"));
                editor->Send(Message::SetTargetStart, 0);
                editor->Send(Message::SetTargetEnd, 20);
                editor->Send(Message::ReplaceTarget, 0, reinterpret_cast<intptr_t>(""));
                editor->Send(Message::SetSel, 100, 106);
                Status(frame, "Ln 1, Col 105 | Sel 4");
                std::puts(
                    "PASS: batched edits moving a selection and returning its endpoints refresh selected contents");
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
    int OnExit() override { return result_; }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(PositionStatusApp);
