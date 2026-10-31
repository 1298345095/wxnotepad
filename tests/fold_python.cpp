#include "editor_control.h"
#include "editor_frame.h"
#include "native_search_helpers.h"
#include <ScintillaTypes.h>
#include <wx/wx.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Paint(EditorControl* editor) {
    for (int attempt = 0; attempt < 40; ++attempt) {
        wxYield();
        editor->Refresh(false);
        editor->Update();
        wxYield();
        if (editor->Send(Message::GetEndStyled) == editor->Send(Message::GetLength)) return;
        wxMilliSleep(10);
    }
}
void MarginClick(EditorControl* editor, int line) {
    const auto width = editor->Send(Message::GetMarginWidthN, 1);
    Require(width == editor->FromDIP(16), "Python must expose a DPI-scaled fold margin");
    const auto position = editor->Send(Message::PositionFromLine, line);
    const int x = static_cast<int>(editor->Send(Message::GetMarginWidthN, 0) + width / 2);
    const int y = static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position) +
                                   editor->Send(Message::TextHeight, line) / 2);
    Require(y >= 0 && y < editor->GetClientSize().y, "native click must target an on-screen document line");
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(editor->GetHandle());
    const LPARAM point = MAKELPARAM(x, y);
    SendMessageW(handle, WM_LBUTTONDOWN, MK_LBUTTON, point);
    SendMessageW(handle, WM_LBUTTONUP, 0, point);
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
        if (type == GDK_BUTTON_RELEASE) event->button.state = GDK_BUTTON1_MASK;
        const auto* display = gtk_widget_get_display(widget);
        gdk_event_set_device(event,
                             gdk_seat_get_pointer(gdk_display_get_default_seat(const_cast<GdkDisplay*>(display))));
        gtk_widget_event(widget, event);
        gdk_event_free(event);
    }
#endif
    Paint(editor);
}
bool Header(EditorControl* editor, int line) {
    return (static_cast<uintptr_t>(editor->Send(Message::GetFoldLevel, line)) &
            static_cast<uintptr_t>(Scintilla::FoldLevel::HeaderFlag)) != 0;
}
void Reachable(EditorControl* editor) {
    for (intptr_t line = 0; line < editor->Send(Message::GetLineCount); ++line)
        Require(editor->Send(Message::GetLineVisible, static_cast<uintptr_t>(line)),
                "every line must remain reachable after removing or resetting a fold");
}
void Replace(EditorControl* editor, size_t start, size_t length, const char* text) {
    editor->Send(Message::SetTargetStart, start);
    editor->Send(Message::SetTargetEnd, start + length);
    editor->Send(Message::ReplaceTarget, std::char_traits<char>::length(text), reinterpret_cast<intptr_t>(text));
    Paint(editor);
}
class FoldApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                const std::string source = "class Sample:\n"
                                           "    # first\n"
                                           "    # second\n"
                                           "\n"
                                           "    def method(self):\n"
                                           "        if ready:\n"
                                           "            call()\n"
                                           "\n"
                                           "        # tail note\n"
                                           "        tail()\n"
                                           "    value = 1\n"
                                           "after()\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "Python"));
                Paint(editor);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 0);
                // Literal levels follow the pinned FoldPyDoc: blank lines adopt
                // the next nonempty indentation; consecutive comments form a fold.
                const int levels[] = {0, 4, 5, 4, 4, 8, 12, 8, 8, 8, 4, 0, 0};
                for (size_t line = 0; line < std::size(levels); ++line) {
                    Require((editor->Send(Message::GetFoldLevel, line) & 0xfff) == 0x400 + levels[line],
                            "Python suite, blank and comment levels must match the pinned lexer");
                    Require(Header(editor, static_cast<int>(line)) ==
                                (line == 0 || line == 1 || line == 4 || line == 5),
                            "only pinned class, method, nested suite and comment group headers may fold");
                }
                MarginClick(editor, 4);
                Require(!editor->Send(Message::GetFoldExpanded, 4) && !editor->Send(Message::GetLineVisible, 5) &&
                            editor->Send(Message::GetLineVisible, 10),
                        "native method header click must collapse only its lexer-defined suite");
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "folding must preserve bytes, save point and history");
                std::puts("PASS: native Python method fold uses original lexer without editing");
                MarginClick(editor, 0);
                MarginClick(editor, 0);
                Require(editor->Send(Message::GetLineVisible, 4) && !editor->Send(Message::GetLineVisible, 5) &&
                            !editor->Send(Message::GetFoldExpanded, 4),
                        "reopening a class must retain a collapsed method");
                MarginClick(editor, 10);
                Require(!editor->Send(Message::GetFoldExpanded, 4) && editor->Send(Message::GetFoldExpanded, 0),
                        "non-header native click must leave enclosing Python folds unchanged");
                MarginClick(editor, 4);
                MarginClick(editor, 1);
                Require(!editor->Send(Message::GetLineVisible, 2) && editor->Send(Message::GetLineVisible, 3),
                        "native comment-group toggle must follow the pinned comment range");
                MarginClick(editor, 1);
                const auto caret = editor->Send(Message::PositionFromLine, 6) + 12;
                editor->Send(Message::SetSel, caret + 2, caret);
                MarginClick(editor, 4);
                for (const auto endpoint : {Message::GetCurrentPos, Message::GetAnchor})
                    Require(editor->Send(Message::GetLineVisible,
                                         editor->Send(Message::LineFromPosition, editor->Send(endpoint))),
                            "native contraction must leave caret and anchor visible");
                MarginClick(editor, 4);
                std::puts("PASS: Python nested state, non-header clicks, comment folds and caret visibility");

                MarginClick(editor, 5);
                Replace(editor, source.find("            call()"), 4, "");
                Require(!Header(editor, 5) && editor->Send(Message::GetLineVisible, 6),
                        "dedenting the only suite body must remove its collapsed header and reveal the body");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                Require(Header(editor, 5) && NativeSearch::Text(editor) == source &&
                            !editor->Send(Message::GetModify) && editor->Send(Message::CanRedo),
                        "Undo of dedent must restore the original fold and save point");
                MarginClick(editor, 5);
                MarginClick(editor, 5);
                Require(editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                        "fold toggles must preserve redo and save point");
                NativeSearch::Command(frame, wxID_REDO);
                Paint(editor);
                Require(!Header(editor, 5) && editor->Send(Message::GetLineVisible, 6),
                        "Redo of dedent must recalculate the removed header with reachable text");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                Replace(editor, source.find("        tail()"), 0, "    ");
                Require((editor->Send(Message::GetFoldLevel, 7) & 0xfff) == 0x40c &&
                            (editor->Send(Message::GetFoldLevel, 8) & 0xfff) == 0x40c &&
                            editor->Send(Message::GetLastChild, 5, -1) == 9,
                        "indenting tail must extend the suite and move intervening blank/comment levels");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                Require(editor->Send(Message::GetLastChild, 5, -1) == 6 && NativeSearch::Text(editor) == source,
                        "Undo of indent must restore the pinned suite boundary");
                NativeSearch::Command(frame, wxID_REDO);
                Paint(editor);
                Require(editor->Send(Message::GetLastChild, 5, -1) == 9,
                        "Redo of indent must restore the extended suite boundary");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                MarginClick(editor, 0);
                Replace(editor, 0, source.find("after()"), "");
                Reachable(editor);
                Require(!Header(editor, 0), "removing a collapsed class must clear its header");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                Reachable(editor);
                Require(Header(editor, 0) && NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify),
                        "Undo of class removal must restore text, headers and save point");
                NativeSearch::Command(frame, wxID_REDO);
                Paint(editor);
                Reachable(editor);
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                std::puts("PASS: indent, dedent, header removal and Undo/Redo preserve pinned fold structure");

                const auto directory = std::filesystem::temp_directory_path() /
                                       ("wxnotepad-python-fold-" + std::to_string(wxGetProcessId()));
                std::filesystem::create_directory(directory);
                const auto path = directory / "fold.py";
#ifdef __WXMSW__
                const wxString filename(path.wstring());
#else
                const auto filename = wxString::FromUTF8(path.string());
#endif
                Require(frame->SaveAs(filename).status == SaveStatus::Saved, "Python fixture must save");
                MarginClick(editor, 0);
                Require(frame->Save().status == SaveStatus::Saved && !editor->Send(Message::GetFoldExpanded, 0),
                        "ordinary Save must retain Python contraction state");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "Python"));
                Require(!editor->Send(Message::GetFoldExpanded, 0), "repeated Python choice must retain folds");
                Require(frame->Open(std::nullopt).status == OpenStatus::Cancelled &&
                            frame->Open(filename + ".missing").status == OpenStatus::Failed &&
                            !editor->Send(Message::GetFoldExpanded, 0),
                        "cancelled and failed Open must retain Python folds");
                std::ifstream saved(path, std::ios::binary);
                Require(std::string(std::istreambuf_iterator<char>(saved), {}) == source,
                        "fold operations and Save must preserve independently read disk bytes");
                saved.close();
                for (const auto* language : {"Plain Text", "C/C++", "JSON"}) {
                    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language));
                    Paint(editor);
                    Reachable(editor);
                    if (std::string(language) == "Plain Text")
                        Require(editor->Send(Message::GetMarginWidthN, 1) == 0 && !Header(editor, 0),
                                "Plain Text must clear Python levels and hide its fold margin");
                    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "Python"));
                    Paint(editor);
                    Reachable(editor);
                    Require(Header(editor, 4) && editor->Send(Message::GetFoldExpanded, 4) &&
                                NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                                editor->Send(Message::CanRedo),
                            "returning to Python must start expanded and retain bytes, save point and redo");
                    MarginClick(editor, 0);
                }
                Require(frame->Open(filename).status == OpenStatus::Opened, "same-language Open must succeed");
                Paint(editor);
                Reachable(editor);
                Require(Header(editor, 4) && editor->Send(Message::GetFoldExpanded, 4),
                        "successful Python Open must reset contraction state");
                MarginClick(editor, 0);
                NativeSearch::Command(frame, wxID_NEW);
                Paint(editor);
                Require(editor->Send(Message::GetLength) == 0 && editor->Send(Message::GetMarginWidthN, 1) == 0,
                        "New must clear Python text and return to inactive Plain Text folding");
                std::filesystem::remove_all(directory);
                std::puts("PASS: Python language/document transitions and saved bytes retain view/history invariants");
                frame->Destroy();
                ExitMainLoop();
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
                frame->Destroy();
                ExitMainLoop();
            }
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(FoldApp);
