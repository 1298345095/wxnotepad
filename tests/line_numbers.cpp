#include "native_search_helpers.h"
#include <print>
#include "settings/settings_store.h"
#include <memory>
#include <utility>
#include "editor/go_to_location.h"
#include "syntax/syntax_scheme.h"
#include <wx/settings.h>
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
std::string Lines(int count) {
    return std::string(static_cast<size_t>(count - 1), '\n');
}
intptr_t ColourValue(const wxColour& colour) {
    return colour.Red() | (colour.Green() << 8U) | (colour.Blue() << 16U);
}
void NativeMarginColours(EditorFrame* frame) {
    using enum Scintilla::Message;
    constexpr auto numberStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::LineNumber);
    constexpr auto defaultStyle = static_cast<uintptr_t>(Scintilla::StylesCommon::Default);
    auto* editor = frame->Editor();
    const auto foreground = ColourValue(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
    const auto background = ColourValue(wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));
    std::println("Default native margin: expected={:06x}/{:06x} actual={:06x}/{:06x}",
                 static_cast<long long>(foreground), static_cast<long long>(background),
                 static_cast<long long>(editor->Send(StyleGetFore, numberStyle)),
                 static_cast<long long>(editor->Send(StyleGetBack, numberStyle)));
    Require(editor->Send(StyleGetFore, numberStyle) == foreground &&
                editor->Send(StyleGetBack, numberStyle) == background,
            "default line numbers use a coherent native foreground/background pair");
    const auto originalFonts = editor->Fonts();
    auto fonts = originalFonts;
    // A document may explicitly use a light palette while native chrome is dark.
    fonts.textStyle = "fore:#102030; back:#F0E0D0";
    fonts.codeStyle = "fore:#213141; back:#E1D1C1";
    fonts.Scheme(SyntaxLanguage::Cpp).defaultStyle = "fore:#324252; back:#D2C2B2";
    editor->SetFontSettings(fonts);
    for (const auto language : {SyntaxLanguage::PlainText, SyntaxLanguage::Cpp, SyntaxLanguage::Python}) {
        ApplySyntaxLanguage(*editor, language);
        std::println("Native margin colors: fore={:06x} back={:06x}; actual={:06x}/{:06x}",
                     static_cast<long long>(foreground), static_cast<long long>(background),
                     static_cast<long long>(editor->Send(StyleGetFore, numberStyle)),
                     static_cast<long long>(editor->Send(StyleGetBack, numberStyle)));
        Require(editor->Send(StyleGetFore, numberStyle) == foreground &&
                    editor->Send(StyleGetBack, numberStyle) == background,
                "line numbers retain the native foreground/background pair independently of document colors");
        const auto expectedFore = language == SyntaxLanguage::PlainText ? 0x302010
                                  : language == SyntaxLanguage::Cpp     ? 0x524232
                                                                        : 0x413121;
        const auto expectedBack = language == SyntaxLanguage::PlainText ? 0xD0E0F0
                                  : language == SyntaxLanguage::Cpp     ? 0xB2C2D2
                                                                        : 0xC1D1E1;
        Require(editor->Send(StyleGetFore, defaultStyle) == expectedFore &&
                    editor->Send(StyleGetBack, defaultStyle) == expectedBack,
                "native margin colors preserve explicit global and per-scheme document colors");
    }
    editor->SetFontSettings(originalFonts);
    ApplySyntaxLanguage(*editor, SyntaxLanguage::PlainText);
    TestEditor::UseFixedWidthFont(editor);
    std::puts("PASS: native margin pair and explicit document colors survive language changes");
}
void MenuAndShortcut(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const int command = frame->GetMenuBar()->FindMenuItem("View", "Line Numbers");
    Require(command != wxNOT_FOUND, "View must expose the Line Numbers command");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsChecked(command) && editor->Send(GetMarginWidthN, 0) > 0,
            "a fresh window must show a checked line-number margin for logical line 1");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++"));
    const auto foldWidth = editor->Send(GetMarginWidthN, 2);
    NativeSearch::Command(frame, command);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsChecked(command) && editor->Send(GetMarginWidthN, 0) == 0 &&
                editor->Send(GetMarginWidthN, 2) == foldWidth && foldWidth > 0,
            "hiding line numbers must retain the independent fold margin");
    NativeSearch::Command(frame, command);
    Require(editor->Send(GetMarginWidthN, 0) > 0, "showing line numbers must restore the margin");
    std::puts("PASS: native menu defaults and toggles preserve the fold margin");
    Shortcut(editor);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsChecked(command) && editor->Send(GetMarginWidthN, 0) == 0,
            "native Ctrl+Shift+N must control the same setting as the menu");
    Shortcut(editor);
    Require(editor->Send(GetMarginWidthN, 0) > 0, "the shortcut must restore line numbers");
    std::puts("PASS: native Ctrl+Shift+N shares the menu setting");
}
void MarginGeometry(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    for (const int boundary : {9, 99, 9999}) {
        const auto source = Lines(boundary);
        editor->Send(SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
        editor->Send(EmptyUndoBuffer);
        editor->Send(SetSavePoint);
        editor->Send(GotoPos, source.size());
        const auto before = editor->Send(GetMarginWidthN, 0);
        editor->Send(NewLine);
        const auto after = editor->Send(GetMarginWidthN, 0);
        Require(editor->Send(GetLineCount) == boundary + 1 && after > before,
                "editing across a digit boundary must immediately grow the line-number margin");
        editor->Send(Undo);
        Require(editor->Send(GetMarginWidthN, 0) == before && !editor->Send(GetModify),
                "Undo across a digit boundary must shrink the margin and return to the save point");
        editor->Send(Redo);
        Require(editor->Send(GetMarginWidthN, 0) == after,
                "Redo across a digit boundary must restore sufficient margin width");
        std::println("PASS: {:d}/{:d} line margin transition: {:d} -> {:d} pixels", boundary, boundary + 1,
                     static_cast<long long>(before), static_cast<long long>(after));
    }
    const auto normalWidth = editor->Send(GetMarginWidthN, 0);
    const auto numberStyle = static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::LineNumber));
    const auto linkStyle = static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Link));
    const auto defaultStyle = static_cast<uintptr_t>(std::to_underlying(Scintilla::StylesCommon::Default));
    const auto linkSize = editor->Send(StyleGetSize, linkStyle);
    editor->Send(StyleSetSize, linkStyle, 24);
    Require(editor->Send(GetMarginWidthN, 0) == normalWidth,
            "link font metrics must not affect the line-number margin");
    editor->Send(StyleSetSize, numberStyle, 24);
    Require(editor->Send(GetMarginWidthN, 0) > normalWidth && editor->Send(StyleGetSize, defaultStyle) == 12,
            "the margin must follow its current font metrics without changing the text font");
    editor->Send(StyleSetSize, numberStyle, 12);
    editor->Send(StyleSetSize, linkStyle, linkSize);
    Require(editor->Send(GetMarginWidthN, 0) == normalWidth,
            "restoring the number font must restore the measured width");
    std::println("PASS: font-dependent geometry at DPI {:d}, scale {:.6g}", editor->GetDPI().y,
                 editor->GetDPIScaleFactor());
}
void FoldedCaretAndHistory(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    const int command = frame->GetMenuBar()->FindMenuItem("View", "Line Numbers");
    const std::string folded = "void outer() {\n  call();\n}\n" + std::string(300, 'x');
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(folded.c_str()));
    editor->Refresh(false);
    editor->Update();
    wxYield();
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    for (const char* suffix : {"first", "second"}) {
        editor->Send(BeginUndoAction);
        editor->Send(AppendText, std::char_traits<char>::length(suffix), reinterpret_cast<intptr_t>(suffix));
        editor->Send(EndUndoAction);
    }
    editor->Send(Undo);
    const auto savedBytes = NativeSearch::Text(editor);
    const auto length = editor->Send(GetLength);
    editor->Send(SetSel, length - 3, length);
    editor->Send(ScrollCaret);
    editor->Send(FoldLine, 0, static_cast<intptr_t>(std::to_underlying(Scintilla::FoldAction::Contract)));
    const auto revision = editor->DocumentRevision();
    for (int toggle = 0; toggle < 2; ++toggle) {
        NativeSearch::Command(frame, command);
        const auto caretX = editor->Send(PointXFromPosition, 0, length);
        const auto caretY = editor->Send(PointYFromPosition, 0, length);
        Require(caretX >= editor->Send(GetMarginWidthN, 0) + editor->Send(GetMarginWidthN, 1) +
                              editor->Send(GetMarginWidthN, 2) &&
                    caretX < editor->GetClientSize().x && caretY >= 0 && caretY < editor->GetClientSize().y,
                "changing the number margin must keep the horizontally scrolled caret visible");
        Require(NativeSearch::Text(editor) == savedBytes && editor->DocumentRevision() == revision &&
                    editor->Send(GetModify) && editor->Send(CanUndo) && editor->Send(CanRedo) &&
                    !editor->Send(GetLineVisible, 1) && editor->Send(GetLineCount) == 4 &&
                    editor->Send(DocLineFromVisible, 1) == 3,
                "view toggles must preserve text, history and logical line numbers after a folded block");
        NativeSearch::Selection(editor, length - 3, length, "view toggles must retain the selection");
    }
    editor->Send(Undo);
    Require(NativeSearch::Text(editor) == folded && !editor->Send(GetModify),
            "Undo after toggling must still reach the original save point");
    editor->Send(Redo);
    editor->Send(Redo);
    Require(NativeSearch::Text(editor) == folded + "firstsecond",
            "Redo after toggling must retain both original edits");
    std::puts("PASS: caret geometry, folds, selection, bytes, save point and history survive toggles");
    NativeSearch::Command(frame, command);
    Require(GoToLogicalLine(*editor, "3") && editor->Send(GetCurrentPos) == editor->Send(PositionFromLine, 2) &&
                editor->Send(GetLineVisible, 1) && editor->Send(GetMarginWidthN, 0) == 0,
            "Go To Line must use logical lines and reveal folded targets while numbers are hidden");
    NativeSearch::Command(frame, command);
    Require(editor->Send(GetLineCount) == 4 && editor->Send(GetMarginWidthN, 0) > 0,
            "showing numbers after Go To Line must retain the same logical document");
    std::puts("PASS: Go To Line and hidden line numbers share the original logical line model");
    const int whitespace = frame->GetMenuBar()->FindMenuItem("View", "Show Whitespace");
    NativeSearch::Command(frame, whitespace);
    NativeSearch::Command(frame, command);
    Require(editor->Send(GetViewWS) != 0 && editor->Send(GetMarginWidthN, 0) == 0 && editor->Send(GetLineCount) == 4,
            "visible whitespace must remain enabled when line numbers are hidden");
    NativeSearch::Command(frame, command);
    NativeSearch::Command(frame, whitespace);
    Require(editor->Send(GetViewWS) == 0 && editor->Send(GetMarginWidthN, 0) > 0,
            "the two independent view choices must restore without affecting each other");
    std::puts("PASS: whitespace markers coexist with the independent number margin");
}
void FileTransitions(EditorFrame* frame, SettingsStore* settings) {
    auto* editor = frame->Editor();
    const int command = frame->GetMenuBar()->FindMenuItem("View", "Line Numbers");
    const wxString originalPath = wxFileName::CreateTempFileName("wxnotepad-line-numbers-");
    const wxString savedPath = originalPath + ".json";
    struct RemoveFiles {
        wxString original;
        wxString saved;
        RemoveFiles(wxString originalPath, wxString savedPath)
            : original(std::move(originalPath)), saved(std::move(savedPath)) {}
        RemoveFiles(const RemoveFiles&) = delete;
        RemoveFiles& operator=(const RemoveFiles&) = delete;
        ~RemoveFiles() {
            wxRemoveFile(original);
            if (wxFileExists(saved)) wxRemoveFile(saved);
        }
    };
    RemoveFiles removeFiles{originalPath, savedPath};
    const std::string openedBytes =
        "\x{ef}\x{bb}\x{bf}" + Lines(100) + "\tUnicode: \x{e4}\x{b8}\x{ad}\x{e6}\x{96}\x{87} \r\n";
    {
        wxFile file(originalPath, wxFile::write);
        Require(file.IsOpened() && file.Write(openedBytes.data(), openedBytes.size()) == openedBytes.size() &&
                    file.Close(),
                "must create the real file transition fixture");
    }
    editor->Send(Message::SetSavePoint);
    NativeSearch::Command(frame, command);
    Require(frame->NewDocument() && editor->Send(Message::GetLineCount) == 1 &&
                editor->Send(Message::GetMarginWidthN, 0) == 0,
            "New must retain the hidden choice and the empty document's logical line 1");
    Require(frame->Open(originalPath).status == OpenStatus::Opened && editor->Send(Message::GetLineCount) == 101 &&
                editor->Send(Message::GetMarginWidthN, 0) == 0,
            "Open must retain the window's hidden line-number setting");
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Scheme", "Python"));
    Require(editor->Send(Message::GetMarginWidthN, 0) == 0, "language changes must retain hidden line numbers");
    Require(frame->SaveAs(savedPath).status == SaveStatus::Saved && editor->Send(Message::GetMarginWidthN, 0) == 0,
            "Save As must retain hidden line numbers");
    {
        wxFile file(savedPath, wxFile::read);
        std::string bytes(static_cast<size_t>(file.Length()), '\0');
        Require(file.Read(bytes.data(), bytes.size()) == bytes.size() && bytes == openedBytes,
                "saving after view changes must preserve every UTF-8/BOM/EOL byte");
    }
    NativeSearch::Command(frame, command);
    const auto openedWidth = editor->Send(Message::GetMarginWidthN, 0);
    Require(openedWidth > 0 && frame->NewDocument() && editor->Send(Message::GetMarginWidthN, 0) < openedWidth &&
                editor->Send(Message::GetMarginWidthN, 0) > 0,
            "New must retain the shown choice and shrink to the empty document's digit count");
    NativeSearch::Command(frame, command);
    auto* fresh = new EditorFrame(settings);
    fresh->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    const int freshCommand = fresh->GetMenuBar()->FindMenuItem("View", "Line Numbers");
    Require(fresh->GetMenuBar()->IsChecked(freshCommand) && fresh->Editor()->Send(Message::GetMarginWidthN, 0) > 0,
            "a separate window must start enabled independently of this window");
    fresh->Destroy();
    std::puts("PASS: New/Open/Save As/language transitions retain the choice and disk bytes; fresh windows "
              "reset");
}
class LineNumbersApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        // Mixed-EOL warning choices are covered by mixed_line_ending_open.
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                NativeMarginColours(frame);
                MenuAndShortcut(frame);
                MarginGeometry(frame);
                FoldedCaretAndHistory(frame);
                FileTransitions(frame, settings_.get());
                result_ = 0;
            } catch (const std::exception& error) {
                std::println(stderr, "FAIL: {}", error.what());
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(LineNumbersApp);
