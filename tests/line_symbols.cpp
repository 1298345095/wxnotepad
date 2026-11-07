#include "native_search_helpers.h"
#include "native_fold_helpers.h"
#include "file_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <memory>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <tuple>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Shortcut(EditorControl* editor, int key) {
#ifdef __WXMSW__
    Require(NativeSearch::WindowsShortcut(editor, key, {{VK_CONTROL, 0x80}, {VK_SHIFT, 0x80}}),
            "native accelerator must translate the display shortcut");
#elif defined(__WXGTK__)
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(editor->GTKGetDrawingWindow()));
    event->key.send_event = TRUE;
    event->key.keyval = key == '9' ? GDK_KEY_parenleft : GDK_KEY_parenright;
    // GDK accepts combined modifier flags.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    event->key.state = static_cast<GdkModifierType>(GDK_CONTROL_MASK | GDK_SHIFT_MASK);
    gtk_widget_event(editor->GetConnectWidget(), event);
    gdk_event_free(event);
#endif
    wxYield();
}
auto DocumentState(EditorFrame* frame) {
    auto* editor = frame->Editor();
    return std::tuple(NativeSearch::Text(editor), editor->Send(Message::GetAnchor),
                      editor->Send(Message::GetCurrentPos), editor->Send(Message::GetEOLMode),
                      editor->Send(Message::GetModify), editor->Send(Message::CanUndo), editor->Send(Message::CanRedo),
                      editor->Send(Message::GetLineCount), frame->DocumentEncoding(), frame->DocumentHasBom(),
                      editor->Send(Message::StyleGetSizeFractional, 32));
}
void Combined(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const int eol = frame->GetMenuBar()->FindMenuItem("View", "Show Line Endings");
    const int symbols = frame->GetMenuBar()->FindMenuItem("View", "Show Wrap Symbols");
    NativeSearch::Command(frame, eol);
    NativeSearch::Command(frame, symbols);
    for (const auto* text : {"", "\r\n\n\r", "final line", "line\r\n"}) {
        TestEditor::ResetStream(editor, text);
        const auto before = DocumentState(frame);
        NativeFold::Paint(editor);
        NativeSearch::Command(frame, eol);
        NativeSearch::Command(frame, eol);
        Require(DocumentState(frame) == before && editor->Send(Message::GetViewEOL) == 1,
                "empty and final physical lines paint without changing document state");
    }
    TestEditor::RequireVirtualRectangleUnchanged(
        editor,
        [frame, eol, symbols] {
            for (int iteration = 0; iteration < 2; ++iteration) {
                NativeSearch::Command(frame, eol);
                NativeSearch::Command(frame, symbols);
            }
        },
        "visibility controls preserve rectangular selections and virtual space");
    std::string longLine = "// ";
    for (int repeat = 0; repeat < 50; ++repeat)
        longLine += "words\t中文😀 ";
    const std::string text = longLine + "\r\nvoid outer() {\n  target();\n}\n";
    TestEditor::ResetStream(editor, text.c_str());
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
    NativeFold::Paint(editor);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Collapse All"));
    Require(!editor->Send(Message::GetLineVisible, 2), "combined fixture contains a collapsed block");
    const auto before = DocumentState(frame);
    const int wrap = frame->GetMenuBar()->FindMenuItem("View", "Word Wrap");
    const int numbers = frame->GetMenuBar()->FindMenuItem("View", "Line Numbers");
    const int whitespace = frame->GetMenuBar()->FindMenuItem("View", "Show Whitespace");
    editor->Send(Message::SetXOffset, 100);
    Require(editor->Send(Message::GetXOffset) > 0, "unwrapped marker fixture has horizontal scrolling");
    NativeSearch::Command(frame, wrap);
    NativeSearch::Command(frame, numbers);
    NativeSearch::Command(frame, whitespace);
    NativeFold::Paint(editor);
    const auto rows = editor->Send(Message::WrapCount, 0);
    Require(rows > 1 && editor->Send(Message::GetXOffset) == 0 && editor->Send(Message::GetLineCount) == 5,
            "visual wrapping adds display rows while physical mixed-ending lines remain separate");
    frame->SetSize(300, 280);
    NativeFold::Paint(editor);
    Require(editor->Send(Message::WrapCount, 0) > rows, "resizing recomputes wrapped marker placement");
    editor->Send(Message::SetZoom, 150);
    NativeFold::Paint(editor);
    for (int iteration = 0; iteration < 2; ++iteration) {
        NativeSearch::Command(frame, eol);
        NativeSearch::Command(frame, symbols);
        NativeFold::Paint(editor);
    }
    Require(DocumentState(frame) == before && !editor->Send(Message::GetLineVisible, 2) &&
                editor->Send(Message::GetViewWS) == 1 && editor->Send(Message::GetViewEOL) == 1 &&
                editor->Send(Message::GetWrapVisualFlags) == 1,
            "combined marker toggles preserve folded text, font, selection, metadata and history");
    NativeSearch::Command(frame, wrap);
    editor->Send(Message::SetXOffset, 100);
    NativeFold::Paint(editor);
    Require(editor->Send(Message::GetXOffset) > 0, "marker visibility retains unwrapped horizontal scrolling");
    NativeSearch::Command(frame, numbers);
    NativeSearch::Command(frame, whitespace);
    editor->Send(Message::SetZoom, 100);
    Require(frame->New() && editor->Send(Message::GetViewEOL) == 1 && editor->Send(Message::GetWrapVisualFlags) == 1,
            "New preserves both current window preferences");
    for (const auto* language : {"C/C++", "Python", "JSON", "Plain Text", "Automatic"}) {
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language));
        Require(editor->Send(Message::GetViewEOL) == 1 && editor->Send(Message::GetWrapVisualFlags) == 1,
                "language and highlighting transitions preserve both preferences");
    }
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-line-symbols");
    std::filesystem::create_directory(directory);
    const std::string utf8 = "中文\r\n\r\nbeta\ngamma\rlast";
    const std::string utf16("\xff\xfe"
                            "A\0\r\0\n\0B\0\n\0C\0\r\0",
                            16);
    const std::string gbk = "\xd6\xd0\r\nA\rB\n";
    const TestFiles::EncodedFixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, utf8, utf8 + "!"},
        {"utf8bom", FileEncoding::Utf8, "\xef\xbb\xbf" + utf8, "\xef\xbb\xbf" + utf8 + "!"},
        {"utf16le", FileEncoding::Utf16Le, utf16, utf16 + std::string("!\0", 2)},
        {"gbk", FileEncoding::Gbk, gbk, gbk + "!"}};
    for (const auto& fixture : fixtures) {
#ifdef __WXMSW__
        const auto filename = std::filesystem::path(wxString::FromUTF8(fixture.name).ToStdWstring() + L"-中文.txt");
#else
        const auto filename = std::filesystem::path(std::string(fixture.name) + "-中文.txt");
#endif
        const auto path = directory / filename;
        {
            std::ofstream output(path, std::ios::binary);
            output << fixture.before;
        }
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "real Open loads the encoded marker fixture");
        Require(editor->Send(Message::GetViewEOL) == 1 && editor->Send(Message::GetWrapVisualFlags) == 1,
                "Open and automatic highlighting preserve visible markers");
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        const auto opened = DocumentState(frame);
        Shortcut(editor, '9');
        Shortcut(editor, '0');
        NativeSearch::Command(frame, eol);
        NativeSearch::Command(frame, symbols);
        Require(DocumentState(frame) == opened && frame->Save().status == SaveStatus::Saved &&
                    TestFiles::Bytes(path) == fixture.before,
                "marker toggles and an independent disk read preserve encoded bytes and pending Redo");
        editor->Send(Message::Redo);
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"view changes preserve the saved edit",
                                         "Undo preserves the original encoded bytes",
                                         "Redo preserves the encoded edit"});
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened &&
                    editor->Send(Message::GetViewEOL) == 1 && editor->Send(Message::GetWrapVisualFlags) == 1,
                "reopening a file retains both display preferences");
    }
    std::filesystem::remove_all(directory);
    std::puts(
        "PASS: mixed EOL rendering, wrapped rows, folding, resize, zoom, lifecycle and independent encoded disk bytes");
}
class LineSymbolsApp final : public wxApp {
public:
    bool OnInit() override {
        if (argc > 1)
            settings_ = std::make_unique<SettingsStore>();
        else {
            fixtureSettings_ = std::make_unique<SettingsStore>();
            fixtureSettings_->values = OriginalSettings{};
            fixtureSettings_->values.wordWrap = false;
            fixtureSettings_->values.warnLineEndings = false;
        }
        auto* frame = new EditorFrame(settings_ ? settings_.get() : fixtureSettings_.get());
        frame->SetSize(500, 400);
        frame->Show();
        CallAfter([this, frame] {
            try {
                if (settings_) {
                    Configuration(frame);
                    if (wxString(argv[1]) == "exit")
                        frame->Close();
                    else
                        frame->Destroy();
                    ExitMainLoop();
                    return;
                }
                auto* editor = frame->Editor();
                const int eol = frame->GetMenuBar()->FindMenuItem("View", "Show Line Endings");
                const int symbols = frame->GetMenuBar()->FindMenuItem("View", "Show Wrap Symbols");
                Require(eol != wxNOT_FOUND && symbols != wxNOT_FOUND,
                        "View must expose Show Line Endings and Show Wrap Symbols");
                auto* eolItem = frame->GetMenuBar()->FindItem(eol);
                auto* symbolsItem = frame->GetMenuBar()->FindItem(symbols);
                Require(eolItem->IsCheckable() && symbolsItem->IsCheckable() && !eolItem->IsChecked() &&
                            !symbolsItem->IsChecked() && editor->Send(Message::GetViewEOL) == 0 &&
                            editor->Send(Message::GetWrapVisualFlags) == 0,
                        "both original visibility preferences default to hidden");
                const std::string source = "alpha\r\n\r\nbeta\ngamma\rlast";
                TestEditor::ResetStream(editor, source.c_str(), 10, 2, Scintilla::EndOfLine::CrLf);
                editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 10, 2);
                NativeSearch::Command(frame, eol);
                NativeSearch::Command(frame, symbols);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(eolItem->IsChecked() && symbolsItem->IsChecked() && editor->Send(Message::GetViewEOL) == 1 &&
                            editor->Send(Message::GetWrapVisualFlags) == 1 &&
                            editor->Send(Message::GetWrapVisualFlagsLocation) == 0,
                        "native menu enables physical EOLs and the original default end-at-border wrap arrow");
                Require(NativeSearch::Text(editor) == source && editor->Send(Message::GetLineCount) == 5 &&
                            editor->Send(Message::GetEOLMode) == 0 && editor->Send(Message::GetAnchor) == 10 &&
                            editor->Send(Message::GetCurrentPos) == 2 && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                        "view toggles preserve mixed physical endings, selection, save point and pending Redo");
                Shortcut(editor, '9');
                Shortcut(editor, '0');
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!eolItem->IsChecked() && !symbolsItem->IsChecked() && !editor->Send(Message::GetViewEOL) &&
                            !editor->Send(Message::GetWrapVisualFlags),
                        "original native shortcuts disable the same checked commands");
                editor->Send(Message::Redo);
                Require(NativeSearch::Text(editor) == source + "!", "Redo retains the original edit");
                editor->Send(Message::Undo);
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify),
                        "Undo returns directly to the original save point");
                std::puts("PASS: native EOL/wrap-symbol menu, accelerators and unchanged mixed-ending history");
                Combined(frame);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
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
    int result_ = 0;
    std::unique_ptr<SettingsStore> settings_;
    std::unique_ptr<SettingsStore> fixtureSettings_;
    void Configuration(EditorFrame* frame) {
        Require(argc == 5, "configuration process requires mode, flags, location and numbers");
        const int flags = std::stoi(wxString(argv[2]).ToStdString());
        const int location = std::stoi(wxString(argv[3]).ToStdString());
        const bool numbers = wxString(argv[4]) == "1";
        auto* editor = frame->Editor();
        const int eol = frame->GetMenuBar()->FindMenuItem("View", "Show Line Endings");
        const int symbols = frame->GetMenuBar()->FindMenuItem("View", "Show Wrap Symbols");
        Require(eol != wxNOT_FOUND && symbols != wxNOT_FOUND, "configuration exposes both native controls");
        if (wxString(argv[1]) == "hidden-save") {
            Require(!editor->Send(Message::GetViewEOL) && !editor->Send(Message::GetWrapVisualFlags),
                    "hidden imported zero remains hidden before explicit save");
            NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Save Settings Now"));
            return;
        }
        if (wxString(argv[1]) != "restore") {
            Require(!editor->Send(Message::GetViewEOL) && !editor->Send(Message::GetWrapVisualFlags),
                    "hidden imported settings do not expose markers");
            NativeSearch::Command(frame, eol);
            NativeSearch::Command(frame, symbols);
        }
        Require(editor->Send(Message::GetViewEOL) == 1 && editor->Send(Message::GetWrapVisualFlags) == flags &&
                    editor->Send(Message::GetWrapVisualFlagsLocation) == location &&
                    editor->LineNumbersVisible() == numbers &&
                    (editor->Send(Message::GetMarginWidthN, 0) > 0) == numbers &&
                    editor->Send(Message::GetWrapMode) == 0,
                "imported placement must match independent original flags, location and line-number side effect");
        if (wxString(argv[1]) == "enable") {
            NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Save Settings Now"));
            NativeSearch::Command(frame, symbols);
            Require(!editor->Send(Message::GetWrapVisualFlags) &&
                        editor->Send(Message::GetWrapVisualFlagsLocation) == location &&
                        editor->LineNumbersVisible() == numbers,
                    "hiding symbols retains original location and enabled numbers");
            if (numbers) {
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Line Numbers"));
                Require(!editor->LineNumbersVisible(), "numbers can be hidden independently while symbols are hidden");
            }
            NativeSearch::Command(frame, symbols);
            Require(editor->Send(Message::GetWrapVisualFlags) == flags && editor->LineNumbersVisible() == numbers,
                    "re-enabling retained margin placement enables line numbers again");
            if (numbers) {
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("View", "Line Numbers"));
                const int wrap = frame->GetMenuBar()->FindMenuItem("View", "Word Wrap");
                NativeSearch::Command(frame, wrap);
                NativeSearch::Command(frame, wrap);
                Require(!editor->LineNumbersVisible() && editor->Send(Message::GetWrapVisualFlags) == flags,
                        "manual number hiding and ordinary wrap toggles do not reapply the margin side effect");
            }
        }
        std::puts("PASS: independent INI/frame native placement and settings command");
    }
};
} // namespace
wxIMPLEMENT_APP(LineSymbolsApp);
