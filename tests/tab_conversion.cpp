#include "app/editor_frame.h"
#include "native_search_helpers.h"
#include "file_helpers.h"
#include "settings/settings_store.h"
#include "tab_settings_helpers.h"
#include <wx/app.h>
#include <wx/ffile.h>
#include <wx/stopwatch.h>
#include <wx/modalhook.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    Require(id != wxNOT_FOUND, "Edit menu exposes original tab conversion command");
    NativeSearch::Command(frame, id);
}
void NativeKey(EditorControl* editor, char key, bool shift, bool alt) {
#ifdef __WXMSW__
    NativeSearch::WindowsShortcut(editor, key,
                                  {{VK_CONTROL, 0x80}, {VK_SHIFT, shift ? 0x80 : 0}, {VK_MENU, alt ? 0x80 : 0}});
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys require private display helper");
    bool received = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&received, key](wxKeyEvent& event) {
        if (event.GetKeyCode() == key || event.GetKeyCode() == key + ('a' - 'A')) received = true;
        event.Skip();
    });
    editor->PushEventHandler(&observer);
    {
        wxFFile request(focusFile, "wb");
        request.Write(wxString("key:ctrl+") + (shift ? "shift+" : "") + (alt ? "alt+" : "") +
                      wxString::Format("%c", key + ('a' - 'A')));
    }
    wxStopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "native conversion shortcut release reaches editor");
#endif
    wxYield();
}
void Set(EditorControl* editor, const char* text, intptr_t anchor = 0, intptr_t caret = 0) {
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::SetSel, anchor, caret);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
void Range(EditorControl* editor, intptr_t anchor, intptr_t caret) {
    Require(editor->Send(Message::GetAnchor) == anchor && editor->Send(Message::GetCurrentPos) == caret,
            "result selection matches original expanded range and direction");
}
void Conversions(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        const char* command;
        int width;
        const char* before;
        const char* after;
    };
    // Literal results from the pinned EditSpacesToTabs/EditTabsToSpaces rules.
    const Fixture fixtures[] = {
        {"Tabify Selection", 4, "abc d", "abc d"},
        {"Tabify Selection", 4, "abc  d", "abc\t d"},
        {"Tabify Selection", 4, " \t  X\t Y\r\n    Z\n \r  ", "\t  X\t Y\r\n\tZ\n \r  "},
        {"Tabify Selection", 4, "   ", "   "},
        {"Tabify Selection", 4, "    ", "\t"},
        {"Tabify Selection", 4, "\t ", "\t "},
        {"Tabify Selection", 4, "😀  X\n中   Y", "😀\tX\n中\tY"},
        {"Tabify Selection", 1, "  X", "\t X"},
        {"Tabify Indent", 4, "    A   B\r\n \tC\tD", "\tA   B\r\n\tC\tD"},
        {"Tabify Indent", 4, "😀  X\n中   Y", "😀  X\n中   Y"},
        {"Untabify Selection", 4, "\tA\tB\r\n \tC\n😀\tX\rD\tY", "    A   B\r\n    C\n😀  X\rD   Y"},
        {"Untabify Indent", 4, "\tA\tB\r\n \tC\n😀\tX\rD\tY", "    A\tB\r\n    C\n😀\tX\rD\tY"},
        {"Untabify Selection", 3, "😀\tX\n\tY", "😀 X\n   Y"},
    };
    for (const auto& fixture : fixtures) {
        for (bool backwards : {false, true}) {
            const auto beforeLength = static_cast<intptr_t>(std::char_traits<char>::length(fixture.before));
            const auto afterLength = static_cast<intptr_t>(std::char_traits<char>::length(fixture.after));
            Set(editor, fixture.before, backwards ? beforeLength : 0, backwards ? 0 : beforeLength);
            editor->Send(Message::SetTabWidth, fixture.width);
            Menu(frame, fixture.command);
            if (TestEditor::Text(editor) != fixture.after)
                std::fprintf(stderr, "%s width %d: expected <%s>, got <%s>\n", fixture.command, fixture.width,
                             fixture.after, TestEditor::Text(editor).c_str());
            Require(TestEditor::Text(editor) == fixture.after, "conversion matches literal original output");
            Range(editor, backwards ? afterLength : 0, backwards ? 0 : afterLength);
            if (std::string(fixture.before) != fixture.after) {
                Require(editor->Send(Message::GetModify), "changed conversion leaves save point");
                editor->Send(Message::Undo);
                Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::CanUndo) &&
                            !editor->Send(Message::GetModify),
                        "one Undo restores original bytes and save point");
                editor->Send(Message::Redo);
                Require(TestEditor::Text(editor) == fixture.after && editor->Send(Message::GetModify),
                        "one Redo restores the entire conversion");
            } else {
                Require(!editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                        "unchanged conversion retains history and save point");
            }
        }
    }
    editor->Send(Message::SetTabWidth, 4);
    for (bool backwards : {false, true}) {
        Set(editor, "xx\tA\n \tBtail", backwards ? 8 : 2, backwards ? 2 : 8);
        Menu(frame, "Untabify Selection");
        Require(TestEditor::Text(editor) == "xx  A\n    Btail",
                "first line expands but exact final byte boundary remains");
        Range(editor, backwards ? 11 : 0, backwards ? 0 : 11);
        Set(editor, "    A\n    Btail", backwards ? 9 : 2, backwards ? 2 : 9);
        Menu(frame, "Tabify Selection");
        Require(TestEditor::Text(editor) == "\tA\n    Btail", "partial final space run flushes literally");
        Range(editor, backwards ? 6 : 0, backwards ? 0 : 6);
        Set(editor, "xx\tA\n \tBtail", backwards ? 5 : 2, backwards ? 2 : 5);
        Menu(frame, "Untabify Selection");
        Require(TestEditor::Text(editor) == "xx  A\n \tBtail", "end at line start excludes all next-line text");
        Range(editor, backwards ? 6 : 0, backwards ? 0 : 6);
    }
    std::puts("PASS: literal tab stops, partial runs, existing Tabs, mixed EOLs, UTF-16 units, exact boundaries, "
              "direction and Undo/Redo");
}
class RectangleFeedback final : public wxModalDialogHook {
public:
    RectangleFeedback() { Register(); }
    int Enter(wxDialog*) override {
        ++count;
        return wxID_OK;
    }
    int count = 0;
};
void Protection(EditorFrame* frame) {
    auto* editor = frame->Editor();
    editor->Send(Message::SetTabWidth, 4);
    for (const char* command : {"Tabify Selection", "Untabify Selection", "Tabify Indent", "Untabify Indent"}) {
        Set(editor, "abc d", 1, 4);
        editor->Send(Message::InsertText, 5, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSel, 4, 1);
        Menu(frame, command);
        Require(TestEditor::Text(editor) == "abc d" && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                "true no-change conversion retains pending redo and save point");
        Range(editor, 4, 1);
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", command);
        editor->Send(Message::SetSel, 2, 2);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(!frame->GetMenuBar()->IsEnabled(id), "original empty-selection menu is disabled");
        wxCommandEvent empty(wxEVT_MENU, id);
        frame->GetEventHandler()->ProcessEvent(empty);
        Range(editor, 2, 2);
        Require(TestEditor::Text(editor) == "abc d" && editor->Send(Message::CanRedo),
                "empty command retains text and redo");
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, 2);
        editor->Send(Message::SetRectangularSelectionCaret, 2);
        RectangleFeedback feedback;
        frame->GetEventHandler()->ProcessEvent(empty);
        Require(feedback.count == 0, "empty rectangle returns before rectangular feedback");
        Set(editor, "    A\n\tB", 0, 5);
        editor->Send(Message::InsertText, 8, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, 1);
        editor->Send(Message::SetRectangularSelectionCaret, 8);
        const auto anchor = editor->Send(Message::GetRectangularSelectionAnchor);
        const auto caret = editor->Send(Message::GetRectangularSelectionCaret);
        Menu(frame, command);
        Require(feedback.count == 1 && TestEditor::Text(editor) == "    A\n\tB" &&
                    editor->Send(Message::GetRectangularSelectionAnchor) == anchor &&
                    editor->Send(Message::GetRectangularSelectionCaret) == caret && !editor->Send(Message::CanUndo) &&
                    editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                "nonempty rectangle warns and preserves bytes, endpoints, history and save point");
        const bool toTabs = std::string(command).starts_with("Tabify");
        Set(editor, toTabs ? "    A\nlast" : "\tA\nlast", 1, toTabs ? 5 : 2);
        const auto original = TestEditor::Text(editor);
        editor->Send(Message::SetReadOnly, 1);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(id),
                "original conversion menu remains enabled for a read-only selection");
        Menu(frame, command);
        Require(TestEditor::Text(editor) == original && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "protected core preserves read-only text, history and save point");
        Range(editor, 0, toTabs ? 2 : 5);
    }
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: no-change, empty/rectangular guard order, feedback, pending redo and literal read-only selection "
              "repair");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-tab-conversion");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        const char* command;
        FileEncoding encoding;
        std::string before;
        std::string after;
    };
    const Fixture fixtures[] = {
        {"utf8-bom", "Untabify Selection", FileEncoding::Utf8, "\xef\xbb\xbf😀\tX\r\n \t中\nlast\t",
         "\xef\xbb\xbf😀  X\r\n    中\nlast    "},
        {"gbk", "Untabify Selection", FileEncoding::Gbk, "\xd6\xd0\tX\rY\t\n", "\xd6\xd0   X\rY   \n"},
        {"utf16-bom", "Untabify Selection", FileEncoding::Utf16Le,
         std::string("\xff\xfe\x2d\x4e\x09\0X\0\r\0\n\0\x09\0B\0", 16),
         std::string("\xff\xfe\x2d\x4e \0 \0 \0X\0\r\0\n\0 \0 \0 \0 \0B\0", 26)},
        {"tabify-utf8", "Tabify Selection", FileEncoding::Utf8, "\xef\xbb\xbf    中\r\n  A  \n",
         "\xef\xbb\xbf\t中\r\n  A\t \n"},
        {"tabify-indent-gbk", "Tabify Indent", FileEncoding::Gbk, "    \xd6\xd0   X\rY   Z", "\t\xd6\xd0   X\rY   Z"},
        {"untabify-indent-utf8", "Untabify Indent", FileEncoding::Utf8, " \tA\tB\n\tC", "    A\tB\n    C"},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".cpp");
        TestFiles::WriteBytes(path, fixture.before);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(wxString::FromUTF8(path.string()), fixture.encoding).status == OpenStatus::Opened,
                "open encoded file through application");
        TabDialog::File(frame, 4, 2, false);
        Require(editor->Send(Message::GetTabWidth) == 4, "conversion uses effective document tab width");
        editor->Send(Message::SelectAll);
        Menu(frame, fixture.command);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "Save preserves independent encoding/BOM bytes, mixed EOLs and final newline");
        Require(frame->DocumentEncoding() == fixture.encoding &&
                    frame->GetStatusBar()->GetStatusText().Contains("C/C++"),
                "conversion preserves document encoding and language");
        editor->Send(Message::Undo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "one Undo and Save restore independent original bytes");
        editor->Send(Message::Redo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "one Redo and Save restore independent converted bytes");
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: effective file tab settings; independent UTF-8 BOM, GBK and UTF-16LE BOM saves; language and "
              "one-step Undo/Redo");
}
class TabConversionApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("a   b"));
                editor->Send(Message::SetTabWidth, 4);
                editor->Send(Message::SelectAll);
                Menu(frame, "Tabify Selection");
                Require(TestEditor::Text(editor) == "a\tb", "Tabify Selection uses original tab stops");
                std::puts("PASS: native Tabify Selection uses original tab stops");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("😀\tX\r\n \tY"));
                editor->Send(Message::SelectAll);
                Menu(frame, "Untabify Selection");
                Require(TestEditor::Text(editor) == "😀  X\r\n    Y",
                        "Untabify Selection counts original UTF-16 units and resets at each EOL");
                std::puts("PASS: native Untabify Selection uses original UTF-16 tab stops");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("    A   B\r\n \tC\tD"));
                editor->Send(Message::SelectAll);
                Menu(frame, "Tabify Indent");
                Require(TestEditor::Text(editor) == "\tA   B\r\n\tC\tD", "Tabify Indent preserves interior blanks");
                Menu(frame, "Untabify Indent");
                Require(TestEditor::Text(editor) == "    A   B\r\n    C\tD", "Untabify Indent preserves interior Tabs");
                std::puts("PASS: native leading-only menus preserve interior whitespace");
                editor->SetFocus();
                wxYield();
                for (bool indent : {false, true}) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("    A   B"));
                    editor->Send(Message::SelectAll);
                    NativeKey(editor, 'T', !indent, indent);
                    Require(TestEditor::Text(editor) == (indent ? "\tA   B" : "\tA\tB"),
                            "original exact Tabify shortcut selects the correct variant");
                    NativeKey(editor, 'S', !indent, indent);
                    Require(TestEditor::Text(editor) == "    A   B",
                            "original exact Untabify shortcut restores blanks");
                    NativeKey(editor, 'T', true, true);
                    NativeKey(editor, 'S', true, true);
                    Require(TestEditor::Text(editor) == "    A   B",
                            "additional modifiers do not dispatch conversions");
                }
                std::puts("PASS: all four actual native shortcuts and extra-modifier exclusion");
                Conversions(frame);
                Protection(frame);
                SavedBytes(frame);
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(TabConversionApp);
