#include "diagnostics/print.h"
#include "app/editor_frame.h"
#include <array>
#include "native_search_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/app.h>
#include <wx/ffile.h>
#include <wx/menu.h>
#include "steady_stopwatch.h"
#include <wx/textctrl.h>
#include <cstdio>
#include <print>
#include <utility>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Set(EditorControl* editor, const char* text, intptr_t anchor = 0, intptr_t caret = 0) {
    using enum Scintilla::Message;
    editor->Send(SetReadOnly, 0);
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Stream)));
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(SetSel, anchor, caret);
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
}
int Command(EditorFrame* frame, bool leading) {
    const int id =
        frame->GetMenuBar()->FindMenuItem("Edit", leading ? "Strip Leading Blanks" : "Strip Trailing Blanks");
    Require(id != wxNOT_FOUND, "Edit exposes both manual Strip Blanks commands");
    return id;
}
void Selections(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct Fixture {
        bool leading;
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        const char* after;
        intptr_t afterAnchor;
        intptr_t afterCaret;
    };
    // Literal results from the pinned regex-based selection command.
    const std::array<Fixture, 6> fixtures = {{
        {false, "A  \r\nB\t \nC \t", 11, 2, "A \r\nB\nC \t", 8, 2},
        {true, "  A\n\t B\n  C", 10, 1, "  A\nB\nC", 6, 1},
        {false, "A  \nB  ", 0, 2, "A  \nB  ", 0, 2},
        {true, "  A\n B", 0, 1, "  A\n B", 0, 1},
        {true, "  A\r\n \t\rB\n\t ", 0, 12, "A\r\n\rB\n", 0, 6},
        {false, "A \t\r\n \t\rB\n\t ", 0, 12, "A\r\n\rB\n", 0, 6},
    }};
    for (const auto& fixture : fixtures) {
        using enum Scintilla::Message;
        Set(editor, fixture.before, fixture.anchor, fixture.caret);
        NativeSearch::Command(frame, Command(frame, fixture.leading));
        if (TestEditor::Text(editor) != fixture.after)
            std::println(stderr, "selection leading={:d}: <{}>; expected <{}>", fixture.leading,
                         TestEditor::Text(editor).c_str(), fixture.after);
        Require(TestEditor::Text(editor) == fixture.after,
                "selected regex retains real line anchors and complete matches");
        Require(editor->Send(GetAnchor) == fixture.afterAnchor && editor->Send(GetCurrentPos) == fixture.afterCaret,
                "selection tracks original endpoints and direction");
        if (std::string(fixture.before) != fixture.after) {
            editor->Send(Undo);
            Require(TestEditor::Text(editor) == fixture.before && !editor->Send(CanUndo) && !editor->Send(GetModify),
                    "selected replacements form one Undo action");
            editor->Send(Redo);
            Require(TestEditor::Text(editor) == fixture.after, "one Redo restores selected replacements");
        } else {
            Require(!editor->Send(CanUndo) && !editor->Send(GetModify),
                    "no complete selected match leaves history and save point unchanged");
        }
    }
    std::puts("PASS: selection-limited regex, partial lines, endpoint direction and one-step history");
}
void NativeKey(EditorControl* editor, bool shift = false, bool control = false) {
#ifdef __WXMSW__
    const bool handled = NativeSearch::WindowsShortcut(
        editor, 'T', {{VK_MENU, 0x80}, {VK_CONTROL, control ? 0x80 : 0}, {VK_SHIFT, shift ? 0x80 : 0}});
    Require(handled || shift || control, "native Alt+T accelerator is handled");
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys use private display helper");
    bool released = false;
    wxEvtHandler observer;
    observer.Bind(wxEVT_KEY_UP, [&released](wxKeyEvent& event) {
        if (event.GetKeyCode() == 'T') released = true;
        event.Skip();
    });
    editor->PushEventHandler(&observer);
    {
        wxFFile request(focusFile, "wb");
        request.Write(wxString("key:alt+") + (shift ? "shift+" : "") + (control ? "ctrl+" : "") + "t");
    }
    TestClock::StopWatch wait;
    while (!released && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(released, "native private-display T release reaches editor");
#endif
    wxYield();
}
void NativeAndProtection(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    Set(editor, " A \t\r\n B\t", 1, 1);
    NativeKey(editor, true);
    NativeKey(editor, false, true);
    Require(TestEditor::Text(editor) == " A \t\r\n B\t" && !editor->Send(CanUndo),
            "extra Shift or Control does not invoke Alt+T");
    NativeKey(editor);
    Require(TestEditor::Text(editor) == " A\r\n B", "actual native Alt+T invokes manual trailing cleanup");
    for (const bool leading : {false, true}) {
        Set(editor, " A \n B \n C ");
        editor->Send(SetSelectionMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::SelectionMode::Rectangle)));
        editor->Send(SetRectangularSelectionAnchor, 1);
        editor->Send(SetRectangularSelectionCaret, 5);
        NativeSearch::Command(frame, Command(frame, leading));
        Require(TestEditor::Text(editor) == (leading ? "A \nB \nC " : " A\n B\n C") &&
                    editor->Send(SelectionIsRectangle),
                "rectangle trims the entire document without warning");
        editor->Send(Undo);
        Require(TestEditor::Text(editor) == " A \n B \n C " && !editor->Send(CanUndo) && !editor->Send(GetModify),
                "rectangle fallback remains one Undo action");
        for (const bool selected : {false, true}) {
            Set(editor, " A \n B ", 0, selected ? 7 : 0);
            editor->Send(SetReadOnly, 1);
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            Require(frame->GetMenuBar()->IsEnabled(Command(frame, leading)), "original read-only menu remains enabled");
            NativeSearch::Command(frame, Command(frame, leading));
            Require(TestEditor::Text(editor) == " A \n B " && !editor->Send(CanUndo) && !editor->Send(GetModify),
                    "core read-only protection rejects manual deletion");
        }
        Set(editor, "\vA\f\n\u{00a0}X\u{3000}", 0, 0);
        editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Undo);
        NativeSearch::Command(frame, Command(frame, leading));
        Require(TestEditor::Text(editor) == "\vA\f\n\u{00a0}X\u{3000}" && editor->Send(CanRedo) &&
                    !editor->Send(CanUndo) && !editor->Send(GetModify),
                "non-ASCII whitespace and other control bytes preserve no-change redo and save point");
        Set(editor, leading ? "  A\n B" : "A  \nB  ", 0, leading ? 1 : 2);
        editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Undo);
        NativeSearch::Command(frame, Command(frame, leading));
        Require(editor->Send(CanRedo) && !editor->Send(CanUndo) && !editor->Send(GetModify),
                "selection with no complete regex match retains pending redo");
    }
    std::puts("PASS: native Alt+T and exact modifiers, rectangular fallback, read-only core and no-change history");
}
void SearchState(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    Set(editor, "  Needle \r\nlater ");
    NativeSearch::Command(frame, wxID_REPLACE);
    auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
    auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
    auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", frame), wxCheckBox);
    Require(query && replacement && regex, "original search controls are available");
    query->SetValue("Needle");
    replacement->SetValue("replacement");
    NativeSearch::Toggle(regex, true);
    NativeSearch::Click(query->GetParent(), wxID_FORWARD);
    const auto flags = editor->Send(GetSearchFlags);
    editor->Send(SetSel, 0, 0);
    editor->StripLeadingBlanks();
    editor->Send(SetSel, 0, 7);
    editor->StripTrailingBlanks();
    Require(TestEditor::Text(editor) == "Needle\r\nlater " && query->GetValue() == "Needle" &&
                replacement->GetValue() == "replacement" && regex->GetValue() && editor->Send(GetSearchFlags) == flags,
            "manual regex leaves active query, replacement and flags intact");
    editor->Send(SetSel, 0, 0);
    NativeSearch::Command(frame, wxID_FORWARD);
    NativeSearch::Selection(editor, 0, 6, "Find Next continues using the current search request");
    query->GetParent()->Hide();
    std::puts("PASS: existing Find/Replace query, replacement, regex flags and Find Next survive manual cleanup");
}
void SavedBytes(EditorFrame* frame) {
    using namespace std::string_literals;
    using Fixture = TestFiles::EncodedFixture;
    const std::array<Fixture, 3> fixtures = {{
        {"utf8", FileEncoding::Utf8, "\x{ef}\x{bb}\x{bf} \t中 \t\r\n \tx \t\n \ty\t", "\x{ef}\x{bb}\x{bf}中\r\nx\ny"},
        {"gbk", FileEncoding::Gbk, " \t\x{d6}\x{d0} \t\r\n \tx \t\n \ty\t", "\x{d6}\x{d0}\r\nx\ny"},
        {"utf16", FileEncoding::Utf16Le, "\x{ff}\x{fe} \0\t\0\x{2d}\x{4e} \0\t\0\r\0\n\0 \0x\0 \0\n\0 \0y\0\t\0"s,
         "\x{ff}\x{fe}\x{2d}\x{4e}\r\0\n\0x\0\n\0y\0"s},
    }};
    auto* editor = frame->Editor();
    for (const auto& fixture : fixtures) {
        using enum Scintilla::Message;
        editor->Send(SetSavePoint);
        const auto path = TestFiles::TemporaryDirectory(std::string(fixture.name) + "-manual.txt");
        TestFiles::WriteBytes(path, fixture.before);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "independent encoded fixture opens");
        const auto encoding = frame->DocumentEncoding();
        const auto bom = frame->DocumentHasBom();
        const auto eol = editor->Send(GetEOLMode);
        editor->Send(SetSel, 0, 0);
        NativeSearch::Command(frame, Command(frame, true));
        NativeSearch::Command(frame, Command(frame, false));
        Require(TestEditor::Text(editor) == "中\r\nx\ny" && frame->DocumentPath() == TestFiles::Filename(path) &&
                    frame->DocumentEncoding() == encoding && frame->DocumentHasBom() == bom &&
                    editor->Send(GetEOLMode) == eol && TestFiles::Bytes(path) == fixture.before,
                "manual commands preserve encoding, BOM, EOL mode, path and disk bytes before Save");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after &&
                    !editor->Send(GetModify),
                "Save independently confirms encoding, BOM and exact mixed-EOL bytes");
    }
    DocumentSession document(*editor);
    document.SetSaveCleanup({true, true});
    Set(editor, "  A \t\r\n  B \n");
    editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(std::to_underlying(Scintilla::EndOfLine::Cr)));
    editor->StripLeadingBlanks();
    Require(TestEditor::Text(editor) == "A \t\r\nB \n" && document.SaveCleanup().fixLineEndings &&
                document.SaveCleanup().fixTrailingBlanks,
            "manual leading cleanup does not invoke or change active save policies");
    editor->StripTrailingBlanks();
    Require(TestEditor::Text(editor) == "A\r\nB\n",
            "manual trailing cleanup preserves EOLs with save normalization configured");
    std::puts("PASS: independently saved UTF-8 BOM, GBK and UTF-16 BOM bytes; metadata and save-policy isolation");
}
class StripBlanksApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                using enum Scintilla::Message;
                auto* editor = frame->Editor();
                Set(editor, " \ta \t\r\n\t \n中\t\r \t", 4, 4);
                NativeSearch::Command(frame, Command(frame, false));
                Require(TestEditor::Text(editor) == " \ta\r\n\n中\r",
                        "caret strips trailing ASCII blanks throughout document");
                editor->Send(Undo);
                Require(TestEditor::Text(editor) == " \ta \t\r\n\t \n中\t\r \t" && !editor->Send(CanUndo) &&
                            !editor->Send(GetModify),
                        "one Undo restores original mixed-EOL bytes and save point");
                std::puts("PASS: whole-document trailing blanks and grouped history");
                Selections(frame);
                NativeAndProtection(frame);
                SearchState(frame);
                SavedBytes(frame);
                result_ = 0;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
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
wxIMPLEMENT_APP(StripBlanksApp);
