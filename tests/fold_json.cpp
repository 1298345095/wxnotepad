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
    Require(width == editor->FromDIP(16), "JSON must expose a DPI-scaled fold margin");
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
class FoldJsonApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                const std::string source = "{\n  \"items\": [\n    {\n      \"text\": \"{ [ \\\" } ]\"\n    },\n    "
                                           "2\n  ],\n  \"tail\": true\n}\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "JSON"));
                Paint(editor);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 0);
                Require(Header(editor, 0) && Header(editor, 1) && Header(editor, 2) && !Header(editor, 3),
                        "JSON must fold nested objects and arrays while ignoring braces in quoted strings");
                MarginClick(editor, 2);
                Require(!editor->Send(Message::GetFoldExpanded, 2) && !editor->Send(Message::GetLineVisible, 3) &&
                            editor->Send(Message::GetLineVisible, 5),
                        "native object margin toggle must hide only its nested contents");
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "JSON folding must retain text, save point and history");
                std::puts("PASS: JSON native margin uses original object/array levels and ignores quoted braces");
                MarginClick(editor, 1);
                Require(!editor->Send(Message::GetLineVisible, 2) && editor->Send(Message::GetLineVisible, 7),
                        "native array toggle must hide its elements and leave the following property visible");
                MarginClick(editor, 1);
                Require(editor->Send(Message::GetLineVisible, 2) && !editor->Send(Message::GetLineVisible, 3) &&
                            !editor->Send(Message::GetFoldExpanded, 2),
                        "expanding an array must preserve its nested object's collapsed state");
                MarginClick(editor, 2);
                MarginClick(editor, 3);
                Require(editor->Send(Message::GetFoldExpanded, 2) && editor->Send(Message::GetLineVisible, 3),
                        "quoted braces must not make an ordinary property line toggle a containing fold");
                MarginClick(editor, 1);
                const auto array = source.find('[');
                editor->Send(Message::SetTargetStart, array);
                editor->Send(Message::SetTargetEnd, array + 1);
                editor->Send(Message::ReplaceTarget, 0, reinterpret_cast<intptr_t>(""));
                Paint(editor);
                Require(!Header(editor, 1) && editor->Send(Message::GetLineVisible, 2) &&
                            editor->Send(Message::GetLineVisible, 3),
                        "removing a collapsed array opener must recalculate its header and reveal former elements");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                Require(Header(editor, 1) && NativeSearch::Text(editor) == source &&
                            !editor->Send(Message::GetModify) && editor->Send(Message::GetLineVisible, 3),
                        "Undo must restore JSON levels, exact bytes and save point with reachable elements");
                NativeSearch::Command(frame, wxID_REDO);
                Paint(editor);
                Require(!Header(editor, 1) && editor->Send(Message::GetLineVisible, 3),
                        "Redo of a delimiter removal must recalculate levels without stranding lines");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                MarginClick(editor, 1);
                const auto close = source.rfind(']');
                editor->Send(Message::SetTargetStart, close);
                editor->Send(Message::SetTargetEnd, close + 1);
                editor->Send(Message::ReplaceTarget, 0, reinterpret_cast<intptr_t>(""));
                Paint(editor);
                Require(editor->Send(Message::GetLineVisible, 3) && editor->Send(Message::GetLineVisible, 6),
                        "editing a hidden closing delimiter must expose the edited block");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                            Header(editor, 1) && editor->Send(Message::GetLineVisible, 3),
                        "Undo of a closing delimiter edit must retain lexer levels and reachable original text");
                NativeSearch::Command(frame, wxID_REDO);
                Paint(editor);
                Require(editor->Send(Message::GetLineVisible, 6), "Redo must keep the edited closing line visible");
                NativeSearch::Command(frame, wxID_UNDO);
                Paint(editor);
                std::puts("PASS: array opener and hidden closing delimiter edits repair visibility through Undo/Redo");
                const auto directory = std::filesystem::temp_directory_path() /
                                       ("wxnotepad-json-fold-" + std::to_string(wxGetProcessId()));
                std::filesystem::create_directory(directory);
                const auto path = directory / "fold.json";
#ifdef __WXMSW__
                const wxString filename(path.wstring());
#else
                const auto filename = wxString::FromUTF8(path.string());
#endif
                Require(frame->SaveAs(filename).status == SaveStatus::Saved, "JSON fold fixture must save");
                editor->Send(Message::GotoPos, 0);
                const auto canUndo = editor->Send(Message::CanUndo);
                const auto canRedo = editor->Send(Message::CanRedo);
                const auto caret = editor->Send(Message::GetCurrentPos);
                const auto anchor = editor->Send(Message::GetAnchor);
                MarginClick(editor, 1);
                Require(frame->Save().status == SaveStatus::Saved && !editor->Send(Message::GetFoldExpanded, 1),
                        "unchanged-language Save must preserve JSON folds");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "JSON"));
                Require(!editor->Send(Message::GetFoldExpanded, 1),
                        "choosing JSON again must preserve its nested fold state");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "Plain Text"));
                Require(editor->Send(Message::GetMarginWidthN, 1) == 0 && !Header(editor, 0) && !Header(editor, 1) &&
                            editor->Send(Message::GetLineVisible, 3),
                        "Plain Text must clear JSON headers, hide the fold margin and reveal all text");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "JSON"));
                Paint(editor);
                Require(Header(editor, 0) && Header(editor, 1) && Header(editor, 2) &&
                            editor->Send(Message::GetFoldExpanded, 1) && editor->Send(Message::GetLineVisible, 3),
                        "returning to JSON must recolor original lexer levels and start expanded");
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::CanUndo) == canUndo && editor->Send(Message::CanRedo) == canRedo &&
                            editor->Send(Message::GetCurrentPos) == caret && editor->Send(Message::GetAnchor) == anchor,
                        "language changes and folding must preserve text, selection, save point and history");
                std::ifstream saved(path, std::ios::binary);
                Require(std::string(std::istreambuf_iterator<char>(saved), {}) == source,
                        "JSON fold operations and Save must retain independently read disk bytes");
                saved.close();
                MarginClick(editor, 1);
                Require(frame->Open(filename).status == OpenStatus::Opened, "JSON fixture must reopen");
                Paint(editor);
                Require(Header(editor, 1) && editor->Send(Message::GetFoldExpanded, 1) &&
                            editor->Send(Message::GetLineVisible, 3) && !editor->Send(Message::CanUndo),
                        "same-language Open must rebuild JSON fold levels and clear old contraction/history");
                NativeSearch::Command(frame, wxID_NEW);
                Require(editor->Send(Message::GetLength) == 0 && editor->Send(Message::GetMarginWidthN, 1) == 0,
                        "New must reset JSON folding to unnamed Plain Text");
                std::filesystem::remove_all(directory);
                std::puts("PASS: JSON language/reset transitions preserve editor history and saved bytes");
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
wxIMPLEMENT_APP(FoldJsonApp);
