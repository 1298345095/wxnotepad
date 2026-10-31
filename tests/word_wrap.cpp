#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include <wx/dialog.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/textctrl.h>
#include <cstdio>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Layout(EditorFrame* frame) {
    // Wayland configure/paint acknowledgements arrive asynchronously.
    for (int attempt = 0; attempt < 5; ++attempt) {
        wxYield();
        frame->Editor()->Refresh();
        frame->Editor()->Update();
        wxMilliSleep(10);
    }
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    wxYield();
}
void Shortcut(EditorFrame* frame) {
#ifdef __WXMSW__
    Require(NativeSearch::WindowsShortcut(frame, 'W', {{VK_CONTROL, 0x80}, {VK_SHIFT, 0x80}}),
            "native window translates Ctrl+Shift+W");
#elif defined(__WXGTK__)
    auto* widget = GTK_WIDGET(frame->GetHandle());
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_W;
    // GDK modifier values are combined bit flags.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    event->key.state = static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK);
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    Layout(frame);
}
void MouseSelect(EditorControl* editor, intptr_t position, bool extend) {
    const int x = static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position)) + 2;
    const int y = static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position) +
                                   editor->Send(Message::TextHeight, 0) / 2);
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(editor->GetHandle());
    const auto modifiers = extend ? MK_SHIFT : 0;
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
        // GDK accepts zero and combined modifier/button flags.
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
        event->button.state = static_cast<GdkModifierType>((extend ? GDK_SHIFT_MASK : 0) |
                                                           (type == GDK_BUTTON_RELEASE ? GDK_BUTTON1_MASK : 0));
        gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gtk_widget_get_display(widget))));
        gtk_widget_event(widget, event);
        gdk_event_free(event);
    }
#endif
    wxYield();
}
struct Files {
    wxString source = wxFileName::CreateTempFileName("wxnotepad-wrap-");
    wxString saved = source + ".saved";
    ~Files() {
        for (const auto* path : {&source, &saved})
            if (wxFileExists(*path)) wxRemoveFile(*path);
    }
};
std::string Bytes(const wxString& path) {
    wxFFile file(path, "rb");
    Require(file.IsOpened(), "independently open saved bytes");
    std::string bytes(static_cast<size_t>(file.Length()), '\0');
    Require(file.Read(bytes.data(), bytes.size()) == bytes.size(), "read all saved bytes");
    return bytes;
}
class WordWrapApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->SetSize(500, 400);
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                Layout(frame);
                const int wrap = frame->GetMenuBar()->FindMenuItem("View", "Word Wrap");
                Require(wrap != wxNOT_FOUND, "View must expose Word Wrap");
                Require(!frame->GetMenuBar()->IsChecked(wrap) && editor->Send(Message::GetWrapMode) == 0,
                        "a fresh window starts with wrapping off");
                std::string text;
                for (int i = 0; i < 50; ++i)
                    text += "words\t中文😀 ";
                const auto textEnd = static_cast<intptr_t>(text.size());
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::InsertText, text.size(), reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 6, 18);
                editor->Send(Message::SetXOffset, 200);
                NativeSearch::Command(frame, wrap);
                Layout(frame);
                Require(frame->GetMenuBar()->IsChecked(wrap) &&
                            editor->Send(Message::GetWrapMode) == static_cast<intptr_t>(Scintilla::Wrap::Word),
                        "native menu enables core word wrapping");
                Require(editor->Send(Message::WrapCount, 0) > 1 && editor->Send(Message::GetXOffset) == 0,
                        "wrapped text uses continuation rows without stale horizontal offset");
                NativeSearch::Selection(editor, 6, 18, "wrapping retains selection byte positions");
                Require(editor->Send(Message::GetLineCount) == 1 && NativeSearch::Text(editor) == text,
                        "wrapping preserves logical lines and bytes");
                editor->Send(Message::SetSel, 6, textEnd);
                editor->Send(Message::ScrollCaret);
                Layout(frame);
                const auto rows = editor->Send(Message::WrapCount, 0);
                frame->SetSize(280, 240);
                Layout(frame);
                Require(editor->Send(Message::WrapCount, 0) > rows, "resizing recomputes continuation rows");
                const auto caretY = editor->Send(Message::PointYFromPosition, 0, textEnd);
                Require(caretY >= 0 && caretY + editor->Send(Message::TextHeight, 0) <= editor->GetClientSize().y,
                        "resizing wrapped text keeps the caret visible");
                NativeSearch::Selection(editor, 6, textEnd, "resizing retains the selection byte range");
                NativeSearch::Command(frame, wrap);
                Layout(frame);
                Require(!frame->GetMenuBar()->IsChecked(wrap) && editor->Send(Message::WrapCount, 0) == 1,
                        "native menu disables wrapping");
                const auto unwrappedX = editor->Send(Message::PointXFromPosition, 0, textEnd);
                const auto unwrappedY = editor->Send(Message::PointYFromPosition, 0, textEnd);
                Require(unwrappedX >= 0 && unwrappedX < editor->GetClientSize().x && unwrappedY >= 0 &&
                            unwrappedY < editor->GetClientSize().y,
                        "turning wrapping off keeps the selected caret visible");
                editor->Send(Message::SetXOffset, 100);
                Require(editor->Send(Message::GetXOffset) > 0, "unwrapped text restores horizontal scrolling");
                Require(!editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo),
                        "view changes retain the save point and redo history");
                editor->Send(Message::Redo);
                Require(NativeSearch::Text(editor) == text + "!", "view changes retain the original redo action");
                editor->Send(Message::Undo);
                std::puts("PASS: native word-wrap toggle, continuation rows, selection and logical text");

                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(std::string(400, 'x').c_str()));
                editor->Send(Message::SetSel, 0, 0);
                Shortcut(frame);
                Require(editor->Send(Message::GetWrapMode) == static_cast<intptr_t>(Scintilla::Wrap::Word) &&
                            frame->GetMenuBar()->IsChecked(wrap) && editor->Send(Message::WrapCount, 0) > 1,
                        "native shortcut enables character fallback for an unbroken line");
                const auto secondRow =
                    editor->Send(Message::PositionFromPointClose, 70, editor->Send(Message::TextHeight, 0) + 2);
                Require(secondRow > 0 && secondRow < 400, "hit testing on a continuation row returns a byte position");
                MouseSelect(editor, 0, false);
                MouseSelect(editor, secondRow, true);
                NativeSearch::Selection(editor, 0, secondRow, "selection spans continuation rows in core positions");
                editor->Send(Message::SetSel, 0, 0);
                editor->Send(Message::LineDown);
                Require(editor->Send(Message::GetCurrentPos) > 0 &&
                            editor->Send(Message::LineFromPosition, editor->Send(Message::GetCurrentPos)) == 0,
                        "keyboard down navigates a display row within the same logical line");
                SearchSession search(*editor);
                search.Configure({wxString(std::string(20, 'x')), "", {}, false});
                editor->Send(Message::GotoPos, 300);
                const auto found = search.Find(SearchDirection::Forward);
                Require(found.status == FindStatus::Found && found.start == 300 && found.end == 320,
                        "search reveal uses wrapped core byte positions");
                const auto foundY = editor->Send(Message::PointYFromPosition, 0, found.end);
                Require(foundY >= 0 && foundY < editor->GetClientSize().y,
                        "search results in continuation rows are visible");
                Shortcut(frame);
                Require(!frame->GetMenuBar()->IsChecked(wrap), "native shortcut and menu share the disabled state");
                std::puts("PASS: shortcut, unbroken fallback, hit testing, display-row navigation and search reveal");

                Files files;
                const std::string disk = "\xef\xbb\xbf" + text + "\r\nsecond 中文\r\n";
                {
                    wxFFile output(files.source, "wb");
                    Require(output.IsOpened() && output.Write(disk.data(), disk.size()) == disk.size(),
                            "write the UTF-8 BOM/CRLF fixture");
                }
                editor->Send(Message::SetSavePoint);
                NativeSearch::Command(frame, wrap);
                Require(frame->New(), "New succeeds for a clean document");
                Require(editor->Send(Message::GetWrapMode) != 0, "New retains wrapping in this window");
                Require(frame->Open(files.source).status == OpenStatus::Opened, "Open the Unicode fixture");
                const auto eol = editor->Send(Message::GetEOLMode);
                Require(editor->Send(Message::GetWrapMode) != 0 && frame->DocumentHasUtf8Bom(),
                        "Open retains wrapping and the document BOM");
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                Require(editor->Send(Message::GetWrapMode) != 0, "language changes retain wrapping");
                Require(frame->SaveAs(files.saved).status == SaveStatus::Saved, "Save As succeeds while wrapping");
                Require(Bytes(files.saved) == disk && editor->Send(Message::GetEOLMode) == eol &&
                            editor->Send(Message::GetLineCount) == 3 && frame->DocumentHasUtf8Bom() &&
                            !editor->Send(Message::GetModify) && editor->Send(Message::GetWrapMode) != 0,
                        "wrapping preserves saved bytes, EOL mode, logical lines, BOM and save point");
                auto* fresh = new EditorFrame;
                Require(fresh->Editor()->Send(Message::GetWrapMode) == 0, "a separate window starts unwrapped");
                fresh->Destroy();
                std::puts("PASS: per-window lifetime, language transitions and byte-identical BOM/CRLF saving");
                Combined(frame);
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
    void Combined(EditorFrame* frame) {
        auto* editor = frame->Editor();
        Files files;
        std::string comment = "// ";
        for (int repeat = 0; repeat < 40; ++repeat)
            comment += "space\t中文😀 ";
        const auto commentEnd = static_cast<intptr_t>(comment.size());
        const std::string source = comment + "\nvoid outer() {\n  if (true) {\n    target();\n  }\n}\n"
                                             "void other() {\n  untouched();\n}\n";
        {
            wxFFile output(files.source, "wb");
            Require(output.IsOpened() && output.Write(source.data(), source.size()) == source.size(),
                    "write the combined navigation/view fixture");
        }
        Require(frame->Open(files.source).status == OpenStatus::Opened, "Open the combined fixture");
        const int numbers = frame->GetMenuBar()->FindMenuItem("View", "Line Numbers");
        const int whitespace = frame->GetMenuBar()->FindMenuItem("View", "Show Whitespace");
        const int wrap = frame->GetMenuBar()->FindMenuItem("View", "Word Wrap");
        const int goTo = frame->GetMenuBar()->FindMenuItem("Edit", "Go To Line...");
        Require(numbers != wxNOT_FOUND && whitespace != wxNOT_FOUND && goTo != wxNOT_FOUND,
                "the combined scenario requires every sibling native control");
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
        NativeFold::Paint(editor);
        Layout(frame);
        editor->Send(Message::GotoPos, 0);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
        Require(!editor->Send(Message::GetLineVisible, 3) && !editor->Send(Message::GetLineVisible, 7),
                "the combined fixture hides nested and unrelated blocks");
        NativeSearch::Command(frame, whitespace);
        frame->SetSize(500, 400);
        Layout(frame);
        const auto foldWidth = editor->Send(Message::GetMarginWidthN, 1);
        editor->Send(Message::SetSel, 2, commentEnd);
        editor->Send(Message::ScrollCaret);
        NativeSearch::Command(frame, numbers);
        Layout(frame);
        Require(editor->Send(Message::GetMarginWidthN, 0) == 0 &&
                    editor->Send(Message::GetMarginWidthN, 1) == foldWidth,
                "hiding numbers while wrapped preserves the fold margin");
        NativeSearch::Command(frame, numbers);
        Layout(frame);
        NativeSearch::Selection(editor, 2, commentEnd, "number visibility preserves wrapped selection bytes");
        const auto marginCaretY = editor->Send(Message::PointYFromPosition, 0, commentEnd);
        Require(marginCaretY >= 0 && marginCaretY < editor->GetClientSize().y,
                "changing number margin width keeps the wrapped caret visible");
        Require(editor->Send(Message::GetMarginWidthN, 0) > 0 && editor->Send(Message::GetViewWS) == 1,
                "numbers and whitespace remain enabled together");
        editor->Send(Message::GotoPos, 0);
        NativeSearch::Command(frame, wrap);
        NativeSearch::Command(frame, wrap);
        const auto wideRows = editor->Send(Message::WrapCount, 0);
        frame->SetSize(300, 280);
        Layout(frame);
        Require(editor->Send(Message::WrapCount, 0) > wideRows && editor->Send(Message::GetLineCount) == 10 &&
                    !editor->Send(Message::GetLineVisible, 3) && !editor->Send(Message::GetLineVisible, 7),
                "resize and view toggles retain logical numbering and existing folds");
        editor->Send(Message::EmptyUndoBuffer);
        editor->Send(Message::SetSavePoint);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        const auto encoding = frame->DocumentEncoding();
        const auto eol = editor->Send(Message::GetEOLMode);
        const auto lexer = editor->Send(Message::GetLexer);
        const auto font = editor->Send(Message::StyleGetSizeFractional, 32);
        std::exception_ptr failure;
        CallAfter([&] {
            auto* dialog = wxDynamicCast(wxWindow::FindWindowByName("Go to line", frame), wxDialog);
            try {
                Require(dialog && dialog->IsModal(), "native Go To Line opens in the combined view");
                auto* input = wxDynamicCast(wxWindow::FindWindowByName("Go to line number", dialog), wxTextCtrl);
                Require(input, "Go To Line exposes its logical line input");
                input->SetValue("4");
                NativeSearch::Click(dialog, wxID_OK);
            } catch (...) {
                failure = std::current_exception();
                if (dialog && dialog->IsModal()) dialog->EndModal(wxID_CANCEL);
            }
        });
        NativeSearch::Command(frame, goTo);
        if (failure) std::rethrow_exception(failure);
        const auto target = editor->Send(Message::PositionFromLine, 3);
        NativeSearch::Selection(editor, target, target, "Go To Line counts logical lines past wrapped rows");
        Require(editor->Send(Message::GetFoldExpanded, 1) && editor->Send(Message::GetFoldExpanded, 2) &&
                    editor->Send(Message::GetLineVisible, 3) && !editor->Send(Message::GetFoldExpanded, 6) &&
                    !editor->Send(Message::GetLineVisible, 7),
                "Go To Line reveals only the requested nested ancestors with all view options active");
        SearchSession search(*editor);
        search.Configure({"target", "", {}, false});
        const auto found = search.Find(SearchDirection::Forward);
        Require(found.status == FindStatus::Found && found.start == target + 4 && found.end == target + 10,
                "search reveals core byte positions in the combined view");
        const auto y = editor->Send(Message::PointYFromPosition, 0, found.end);
        Require(y >= 0 && y < editor->GetClientSize().y && editor->Send(Message::GetWrapMode) != 0 &&
                    editor->Send(Message::GetViewWS) == 1 && editor->Send(Message::GetMarginWidthN, 0) > 0,
                "all view options remain effective while navigation keeps the caret visible");
        Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                    !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                    frame->DocumentEncoding() == encoding && editor->Send(Message::GetEOLMode) == eol &&
                    editor->Send(Message::GetLexer) == lexer &&
                    editor->Send(Message::StyleGetSizeFractional, 32) == font,
                "combined presentation and navigation preserve bytes, history, encoding, EOL, syntax and font");
        Require(frame->SaveAs(files.saved).status == SaveStatus::Saved && Bytes(files.saved) == source,
                "the combined view saves byte-identical text");
        std::puts("PASS: combined line numbers, whitespace, wrapping, resize, folded Go To Line, search and saving");
    }
};
} // namespace
wxIMPLEMENT_APP(WordWrapApp);
