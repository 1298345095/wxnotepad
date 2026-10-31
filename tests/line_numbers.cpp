#include "native_search_helpers.h"
#include "editor/line_navigation.h"
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/menu.h>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Shortcut(wxWindow* window) {
#ifdef __WXMSW__
    Require(NativeSearch::WindowsShortcut(window, 'N', {{VK_CONTROL, 0x80}, {VK_SHIFT, 0x80}}),
            "the owned native window must translate Ctrl+Shift+N");
#elif defined(__WXGTK__)
    auto* widget = GTK_WIDGET(window->GetHandle());
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_N;
    event->key.state = GDK_CONTROL_MASK | GDK_SHIFT_MASK;
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
class LineNumbersApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                const int command = frame->GetMenuBar()->FindMenuItem("View", "Line Numbers");
                Require(command != wxNOT_FOUND, "View must expose the Line Numbers command");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsChecked(command) && editor->Send(Message::GetMarginWidthN, 0) > 0,
                        "a fresh window must show a checked line-number margin for logical line 1");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                const auto foldWidth = editor->Send(Message::GetMarginWidthN, 1);
                NativeSearch::Command(frame, command);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetMenuBar()->IsChecked(command) && editor->Send(Message::GetMarginWidthN, 0) == 0 &&
                            editor->Send(Message::GetMarginWidthN, 1) == foldWidth && foldWidth > 0,
                        "hiding line numbers must retain the independent fold margin");
                NativeSearch::Command(frame, command);
                Require(editor->Send(Message::GetMarginWidthN, 0) > 0, "showing line numbers must restore the margin");
                std::puts("PASS: native menu defaults and toggles preserve the fold margin");
                Shortcut(editor);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetMenuBar()->IsChecked(command) && editor->Send(Message::GetMarginWidthN, 0) == 0,
                        "native Ctrl+Shift+N must control the same setting as the menu");
                Shortcut(editor);
                Require(editor->Send(Message::GetMarginWidthN, 0) > 0, "the shortcut must restore line numbers");
                std::puts("PASS: native Ctrl+Shift+N shares the menu setting");
                const auto lines = [](int count) { return std::string(static_cast<size_t>(count - 1), '\n'); };
                for (const int boundary : {9, 99, 9999}) {
                    const auto source = lines(boundary);
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    editor->Send(Message::GotoPos, source.size());
                    const auto before = editor->Send(Message::GetMarginWidthN, 0);
                    editor->Send(Message::NewLine);
                    const auto after = editor->Send(Message::GetMarginWidthN, 0);
                    Require(editor->Send(Message::GetLineCount) == boundary + 1 && after > before,
                            "editing across a digit boundary must immediately grow the line-number margin");
                    editor->Send(Message::Undo);
                    Require(editor->Send(Message::GetMarginWidthN, 0) == before && !editor->Send(Message::GetModify),
                            "Undo across a digit boundary must shrink the margin and return to the save point");
                    editor->Send(Message::Redo);
                    Require(editor->Send(Message::GetMarginWidthN, 0) == after,
                            "Redo across a digit boundary must restore sufficient margin width");
                    std::printf("PASS: %d/%d line margin transition: %lld -> %lld pixels\n", boundary, boundary + 1,
                                static_cast<long long>(before), static_cast<long long>(after));
                }
                const auto normalWidth = editor->Send(Message::GetMarginWidthN, 0);
                editor->Send(Message::StyleSetSize, 33, 24);
                Require(editor->Send(Message::GetMarginWidthN, 0) > normalWidth &&
                            editor->Send(Message::StyleGetSize, 32) == 12,
                        "the margin must follow its current font metrics without changing the text font");
                editor->Send(Message::StyleSetSize, 33, 12);
                Require(editor->Send(Message::GetMarginWidthN, 0) == normalWidth,
                        "restoring the number font must restore the measured width");
                std::printf("PASS: font-dependent geometry at DPI %d, scale %g\n", editor->GetDPI().y,
                            editor->GetDPIScaleFactor());

                const std::string folded = "void outer() {\n  call();\n}\n" + std::string(300, 'x');
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(folded.c_str()));
                editor->Refresh(false);
                editor->Update();
                wxYield();
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                for (const char* suffix : {"first", "second"}) {
                    editor->Send(Message::BeginUndoAction);
                    editor->Send(Message::AppendText, std::char_traits<char>::length(suffix),
                                 reinterpret_cast<intptr_t>(suffix));
                    editor->Send(Message::EndUndoAction);
                }
                editor->Send(Message::Undo);
                const auto savedBytes = NativeSearch::Text(editor);
                const auto length = editor->Send(Message::GetLength);
                editor->Send(Message::SetSel, length - 3, length);
                editor->Send(Message::ScrollCaret);
                editor->Send(Message::FoldLine, 0, static_cast<intptr_t>(Scintilla::FoldAction::Contract));
                const auto revision = editor->DocumentRevision();
                for (int toggle = 0; toggle < 2; ++toggle) {
                    NativeSearch::Command(frame, command);
                    const auto caretX = editor->Send(Message::PointXFromPosition, 0, length);
                    const auto caretY = editor->Send(Message::PointYFromPosition, 0, length);
                    Require(caretX >= editor->Send(Message::GetMarginWidthN, 0) +
                                          editor->Send(Message::GetMarginWidthN, 1) &&
                                caretX < editor->GetClientSize().x && caretY >= 0 && caretY < editor->GetClientSize().y,
                            "changing the number margin must keep the horizontally scrolled caret visible");
                    Require(NativeSearch::Text(editor) == savedBytes && editor->DocumentRevision() == revision &&
                                editor->Send(Message::GetModify) && editor->Send(Message::CanUndo) &&
                                editor->Send(Message::CanRedo) && !editor->Send(Message::GetLineVisible, 1) &&
                                editor->Send(Message::GetLineCount) == 4 &&
                                editor->Send(Message::DocLineFromVisible, 1) == 3,
                            "view toggles must preserve text, history and logical line numbers after a folded block");
                    NativeSearch::Selection(editor, length - 3, length, "view toggles must retain the selection");
                }
                editor->Send(Message::Undo);
                Require(NativeSearch::Text(editor) == folded && !editor->Send(Message::GetModify),
                        "Undo after toggling must still reach the original save point");
                editor->Send(Message::Redo);
                editor->Send(Message::Redo);
                Require(NativeSearch::Text(editor) == folded + "firstsecond",
                        "Redo after toggling must retain both original edits");
                std::puts("PASS: caret geometry, folds, selection, bytes, save point and history survive toggles");
                NativeSearch::Command(frame, command);
                Require(GoToLogicalLine(*editor, "3") &&
                            editor->Send(Message::GetCurrentPos) == editor->Send(Message::PositionFromLine, 2) &&
                            editor->Send(Message::GetLineVisible, 1) && editor->Send(Message::GetMarginWidthN, 0) == 0,
                        "Go To Line must use logical lines and reveal folded targets while numbers are hidden");
                NativeSearch::Command(frame, command);
                Require(editor->Send(Message::GetLineCount) == 4 && editor->Send(Message::GetMarginWidthN, 0) > 0,
                        "showing numbers after Go To Line must retain the same logical document");
                std::puts("PASS: Go To Line and hidden line numbers share the original logical line model");
                const int whitespace = frame->GetMenuBar()->FindMenuItem("View", "Show Whitespace");
                NativeSearch::Command(frame, whitespace);
                NativeSearch::Command(frame, command);
                Require(editor->Send(Message::GetViewWS) != 0 && editor->Send(Message::GetMarginWidthN, 0) == 0 &&
                            editor->Send(Message::GetLineCount) == 4,
                        "visible whitespace must remain enabled when line numbers are hidden");
                NativeSearch::Command(frame, command);
                NativeSearch::Command(frame, whitespace);
                Require(editor->Send(Message::GetViewWS) == 0 && editor->Send(Message::GetMarginWidthN, 0) > 0,
                        "the two independent view choices must restore without affecting each other");
                std::puts("PASS: whitespace markers coexist with the independent number margin");

                const wxString originalPath = wxFileName::CreateTempFileName("wxnotepad-line-numbers-");
                const wxString savedPath = originalPath + ".json";
                struct RemoveFiles {
                    wxString original;
                    wxString saved;
                    ~RemoveFiles() {
                        wxRemoveFile(original);
                        if (wxFileExists(saved)) wxRemoveFile(saved);
                    }
                } removeFiles{originalPath, savedPath};
                const std::string openedBytes =
                    "\xef\xbb\xbf" + lines(100) + "\tUnicode: \xe4\xb8\xad\xe6\x96\x87 \r\n";
                {
                    wxFile file(originalPath, wxFile::write);
                    Require(file.IsOpened() &&
                                file.Write(openedBytes.data(), openedBytes.size()) == openedBytes.size() &&
                                file.Close(),
                            "must create the real file transition fixture");
                }
                editor->Send(Message::SetSavePoint);
                NativeSearch::Command(frame, command);
                Require(frame->New() && editor->Send(Message::GetLineCount) == 1 &&
                            editor->Send(Message::GetMarginWidthN, 0) == 0,
                        "New must retain the hidden choice and the empty document's logical line 1");
                Require(frame->Open(originalPath).status == OpenStatus::Opened &&
                            editor->Send(Message::GetLineCount) == 101 &&
                            editor->Send(Message::GetMarginWidthN, 0) == 0,
                        "Open must retain the window's hidden line-number setting");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "Python"));
                Require(editor->Send(Message::GetMarginWidthN, 0) == 0,
                        "language changes must retain hidden line numbers");
                Require(frame->SaveAs(savedPath).status == SaveStatus::Saved &&
                            editor->Send(Message::GetMarginWidthN, 0) == 0,
                        "Save As must retain hidden line numbers");
                {
                    wxFile file(savedPath, wxFile::read);
                    std::string bytes(static_cast<size_t>(file.Length()), '\0');
                    Require(file.Read(bytes.data(), bytes.size()) == bytes.size() && bytes == openedBytes,
                            "saving after view changes must preserve every UTF-8/BOM/EOL byte");
                }
                NativeSearch::Command(frame, command);
                const auto openedWidth = editor->Send(Message::GetMarginWidthN, 0);
                Require(openedWidth > 0 && frame->New() && editor->Send(Message::GetMarginWidthN, 0) < openedWidth &&
                            editor->Send(Message::GetMarginWidthN, 0) > 0,
                        "New must retain the shown choice and shrink to the empty document's digit count");
                NativeSearch::Command(frame, command);
                auto* fresh = new EditorFrame;
                fresh->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                const int freshCommand = fresh->GetMenuBar()->FindMenuItem("View", "Line Numbers");
                Require(fresh->GetMenuBar()->IsChecked(freshCommand) &&
                            fresh->Editor()->Send(Message::GetMarginWidthN, 0) > 0,
                        "a separate window must start enabled independently of this window");
                fresh->Destroy();
                std::puts("PASS: New/Open/Save As/language transitions retain the choice and disk bytes; fresh windows "
                          "reset");
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
wxIMPLEMENT_APP(LineNumbersApp);
