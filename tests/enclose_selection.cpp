#include "native_search_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/textctrl.h>
#include <wx/ffile.h>
#include "steady_stopwatch.h"
#include <wx/eventfilter.h>
#include <fstream>
#include <functional>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Menu(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
    Require(id != wxNOT_FOUND, "native Edit menu exposes original enclosure command");
    NativeSearch::Command(frame, id);
}
void Range(EditorControl* editor, intptr_t anchor, intptr_t caret) {
    Require(editor->Send(Message::GetAnchor) == anchor && editor->Send(Message::GetCurrentPos) == caret,
            "enclosure selects original content in original direction");
}
#ifndef __WXMSW__
class KeyRelease final : public wxEventFilter {
public:
    KeyRelease(int key, bool& received, bool* signal) : key_(key), received_(received), signal_(signal) {
        wxEvtHandler::AddFilter(this);
    }
    ~KeyRelease() override { wxEvtHandler::RemoveFilter(this); }
    int FilterEvent(wxEvent& event) override {
        if (event.GetEventType() == wxEVT_KEY_UP) {
            const int actual = static_cast<wxKeyEvent&>(event).GetKeyCode();
            if (actual == key_ || (key_ == '3' && actual == '#') || (key_ == '6' && actual == '^') ||
                (key_ == '9' && actual == '(')) {
                received_ = true;
                if (signal_) *signal_ = true;
            }
        }
        return Event_Skip;
    }

private:
    int key_;
    bool& received_;
    bool* signal_;
};
#endif
void NativeKey(EditorFrame* frame, int key, bool control, bool alt = false, bool shift = false,
               bool* releaseSignal = nullptr) {
#ifdef __WXMSW__
    NativeSearch::WindowsShortcut(frame->Editor(), key,
                                  {{VK_CONTROL, static_cast<BYTE>(control ? 0x80 : 0)},
                                   {VK_MENU, static_cast<BYTE>(alt ? 0x80 : 0)},
                                   {VK_SHIFT, static_cast<BYTE>(shift ? 0x80 : 0)}});
#else
    auto* editor = frame->Editor();
    editor->SetFocus();
    wxYield();
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys use private display");
    {
        wxFFile file(focusFile, "wb");
        file.Write(frame->IsActive() ? "focus:wxnotepad" : "front:wxnotepad");
    }
    TestClock::StopWatch focusWait;
    while (focusWait.Time() < 3000) {
        wxString pending;
        {
            wxFFile file(focusFile, "rb");
            file.ReadAll(&pending);
        }
        if (pending.empty()) break;
        wxYield();
        wxMilliSleep(10);
    }
    TestClock::StopWatch activation;
    while ((!frame->IsActive() || !editor->HasFocus()) && activation.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(frame->IsActive() && editor->HasFocus(), "private native input activates the owned editor surface");
    bool received = false;
    KeyRelease observer(key, received, releaseSignal);
    {
        wxFFile file(focusFile, "wb");
        file.Write(wxString("key:") + (control ? "ctrl+" : "") + (alt ? "alt+" : "") + (shift ? "shift+" : "") +
                   wxString::Format("%c", key == 'Q' ? 'q' : key));
    }
    TestClock::StopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    if (!received)
        std::fprintf(stderr, "Missing native key: %c ctrl=%d alt=%d shift=%d; text=%s\n", key, control, alt, shift,
                     TestEditor::Text(editor).c_str());
    Require(received, "native private-display enclosure key release reaches owned editor or dialog");
#endif
    wxYield();
}
void Shortcuts(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "x", 0, 1);
    NativeKey(frame, '3', true);
    Require(TestEditor::Text(editor) == "'''x'''", "native Ctrl+3 encloses with triple single quotes");
    TestEditor::ResetStream(editor, "x", 0, 1);
    NativeKey(frame, '6', true);
    Require(TestEditor::Text(editor) == "\"\"\"x\"\"\"", "native Ctrl+6 encloses with triple double quotes");
    TestEditor::ResetStream(editor, "x", 0, 1);
    NativeKey(frame, '9', true);
    Require(TestEditor::Text(editor) == "```x```", "native Ctrl+9 encloses with triple backticks");
    for (int key : {'3', '6', '9'}) {
        TestEditor::ResetStream(editor, "x", 0, 1);
        NativeKey(frame, key, true, false, true);
        Require(TestEditor::Text(editor) == "x" && !editor->Send(Message::CanUndo),
                "Ctrl+Shift does not invoke triple enclosure");
        NativeKey(frame, key, true, true);
        Require(TestEditor::Text(editor) == "x" && !editor->Send(Message::CanUndo),
                "Ctrl+Alt does not invoke triple enclosure");
    }
}
void Triple(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "A中文B", 7, 1);
    Menu(frame, "Triple Single Quotes");
    Require(TestEditor::Text(editor) == "A'''中文'''B", "original triple enclosure inserts around Unicode");
    Range(editor, 10, 4);
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "A中文B" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "one Undo restores enclosure and save point");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "A'''中文'''B", "one Redo restores both delimiters");
}
class DialogDriver final : public wxModalDialogHook {
public:
    explicit DialogDriver(std::function<void(wxDialog*)> action) : action_(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Enclose Selection") wxTheApp->CallAfter([this, dialog] { action_(dialog); });
        return wxID_NONE;
    }

private:
    std::function<void(wxDialog*)> action_;
};
void Dialog(EditorFrame* frame, const wxString& recalledBefore, const wxString& recalledAfter, const wxString& before,
            const wxString& after, int button, bool native = false) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "With...");
    Require(id != wxNOT_FOUND, "native enclosure submenu exposes With dialog");
    bool observed = false;
    bool released = false;
    DialogDriver driver([&](wxDialog* dialog) {
#ifndef __WXMSW__
        // Let the opening shortcut release in its owned dialog before clicking
        // a button, as it does during ordinary keyboard interaction.
        TestClock::StopWatch wait;
        while (native && !released && wait.Time() < 3000) {
            wxYield();
            wxMilliSleep(10);
        }
#endif
        auto* prefix = wxDynamicCast(wxWindow::FindWindowByName("Insert before selection", dialog), wxTextCtrl);
        auto* suffix = wxDynamicCast(wxWindow::FindWindowByName("Insert after selection", dialog), wxTextCtrl);
        observed = prefix && suffix && prefix->IsMultiLine() && suffix->IsMultiLine() &&
                   (dialog->GetWindowStyle() & wxRESIZE_BORDER) && prefix->GetValue() == recalledBefore &&
                   suffix->GetValue() == recalledAfter;
        if (prefix) prefix->SetValue(before);
        if (suffix) suffix->SetValue(after);
        NativeSearch::Click(dialog, button);
    });
    if (native)
        NativeKey(frame, 'Q', false, true, false, &released);
    else
        NativeSearch::Command(frame, id);
    Require(observed, "native resizable multiline dialog recalls current-window before/after values");
}
void CustomDialog(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "cat", 0, 3);
    editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Cr));
    const auto before = wxString::FromUTF8("前\n(");
    const auto after = wxString::FromUTF8(")\n後");
    Dialog(frame, "", "", before, after, wxID_OK, true);
    Require(TestEditor::Text(editor) == "前\r(cat)\r後", "multiline native inputs use current document CR EOL");
    Range(editor, 5, 8);
    editor->Send(Message::Undo);
    Dialog(frame, before, after, "discard", "discard", wxID_CANCEL);
    Require(TestEditor::Text(editor) == "cat" && !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                !editor->Send(Message::GetModify),
            "Cancel preserves document, save point and pending redo");
    Dialog(frame, before, after, "", "", wxID_OK);
    Require(TestEditor::Text(editor) == "cat" && !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
            "both-empty OK closes and preserves pending redo");
    Dialog(frame, "", "", "", "", wxID_CANCEL);
}
void DialogKeys(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "x", 0, 1);
    bool opened = false;
    {
        DialogDriver driver([&](wxDialog* dialog) {
            opened = true;
            NativeSearch::Click(dialog, wxID_CANCEL);
        });
        NativeKey(frame, 'Q', false, true, true);
        Require(!opened, "Alt+Shift+Q does not open the enclosure dialog");
        NativeKey(frame, 'Q', true, true);
        Require(!opened, "Ctrl+Alt+Q does not open the enclosure dialog");
    }
    bool multiline = false;
    DialogDriver driver([&](wxDialog* dialog) {
        multiline = true;
        for (const char* name : {"Insert before selection", "Insert after selection"}) {
            auto* input = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxTextCtrl);
            if (!input) {
                multiline = false;
                continue;
            }
            input->SetValue("line");
            input->SetInsertionPointEnd();
#ifdef __WXMSW__
            SendMessageW(static_cast<HWND>(input->GetHandle()), WM_CHAR, VK_RETURN, 0);
#else
            input->SetFocus();
            wxYield();
            wxString focusFile;
            if (!wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile)) {
                multiline = false;
                continue;
            }
            {
                wxFFile file(focusFile, "wb");
                file.Write("key:Return");
            }
            TestClock::StopWatch wait;
            while (input->GetValue() == "line" && dialog->IsShown() && wait.Time() < 3000) {
                wxYield();
                wxMilliSleep(10);
            }
#endif
            multiline = multiline && input->GetValue() == "line\n" && dialog->IsShown();
        }
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    Menu(frame, "With...");
    Require(multiline && TestEditor::Text(editor) == "x" && !editor->Send(Message::CanUndo),
            "native Return inserts a line break in each field and Cancel retains document");
    std::puts("PASS: exact Alt+Q modifiers and native Return in both multiline fields");
}
void PublicBehavior(EditorFrame* frame) {
    auto* editor = frame->Editor();
    struct EolFixture {
        Scintilla::EndOfLine mode;
        const char* expected;
        intptr_t prefixLength;
    };
    const EolFixture fixtures[] = {
        {Scintilla::EndOfLine::CrLf, "前\r\nA\rB\nxS\r\nT\rU\nV", 9},
        {Scintilla::EndOfLine::Lf, "前\nA\rB\nxS\nT\rU\nV", 8},
        {Scintilla::EndOfLine::Cr, "前\rA\rB\nxS\rT\rU\nV", 8},
    };
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(editor, "x", 1, 0);
        editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(fixture.mode));
        Require(editor->EncloseSelection(wxString::FromUTF8("前\r\nA\rB\n"), "S\r\nT\rU\nV"),
                "public enclosure accepts stream selection");
        Require(TestEditor::Text(editor) == fixture.expected,
                "original normalization converts CRLF pairs and preserves lone CR and LF");
        Range(editor, fixture.prefixLength + 1, fixture.prefixLength);
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "x" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                "normalization and both insertions form one undo group");
        editor->Send(Message::Redo);
        Require(TestEditor::Text(editor) == fixture.expected, "normalization redo retains literal EOL bytes");
    }
    TestEditor::ResetStream(editor, "ab", 1, 1);
    Require(editor->EncloseSelection(wxString::FromUTF8("😀"), ")"), "empty selection accepts Unicode prefix");
    Require(TestEditor::Text(editor) == "a😀)b", "empty selection inserts before/after at caret");
    Range(editor, 5, 5);
    TestEditor::ResetStream(editor, "ab", 1, 1);
    editor->EncloseSelection("", ")");
    Require(TestEditor::Text(editor) == "a)b", "suffix-only enclosure inserts at empty selection");
    Range(editor, 1, 1);
    TestEditor::ResetStream(editor, "ab", 0, 2);
    editor->EncloseSelection("(", "");
    Require(TestEditor::Text(editor) == "(ab", "prefix-only enclosure preserves selected content");
    Range(editor, 1, 3);
    editor->Send(Message::Undo);
    const auto anchor = editor->Send(Message::GetAnchor);
    const auto caret = editor->Send(Message::GetCurrentPos);
    editor->EncloseSelection("", "");
    Require(TestEditor::Text(editor) == "ab" && !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                !editor->Send(Message::GetModify),
            "both-empty public enclosure preserves history and save point");
    Range(editor, anchor, caret);
    std::puts("PASS: public Unicode, CRLF-pair-only EOL conversion, direction, caret and one-step history");
}
class RectangleFeedback final : public wxModalDialogHook {
public:
    RectangleFeedback() { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Enclose Selection") return wxID_NONE;
        ++count;
        for (auto* window : wxTopLevelWindows) {
            auto* enclosure = wxDynamicCast(window, wxDialog);
            if (enclosure && enclosure->GetTitle() == "Enclose Selection" && enclosure->IsShown())
                enclosureVisible = true;
        }
        return wxID_OK;
    }
    int count = 0;
    bool enclosureVisible = false;
};
void Protection(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "abcdefghi", 1, 3);
    editor->Send(Message::SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    for (const char* label : {"With...", "Triple Single Quotes", "Triple Double Quotes", "Triple Backticks"}) {
        const int id = frame->GetMenuBar()->FindMenuItem("Edit", label);
        Require(id != wxNOT_FOUND && frame->GetMenuBar()->IsEnabled(id),
                "original enclosure menus remain available on read-only documents");
    }
    Menu(frame, "Triple Single Quotes");
    Require(TestEditor::Text(editor) == "abcdefghi" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "core read-only protection rejects delimiter insertion");
    Range(editor, 4, 6);
    TestEditor::ResetStream(editor, "alpha\nbeta", 0, 5);
    Menu(frame, "Triple Backticks");
    editor->Send(Message::Undo);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 0);
    editor->Send(Message::SetRectangularSelectionCaret, 8);
    const auto anchor = editor->Send(Message::GetRectangularSelectionAnchor);
    const auto caret = editor->Send(Message::GetRectangularSelectionCaret);
    RectangleFeedback feedback;
    Menu(frame, "Triple Backticks");
    Require(feedback.count == 1, "rectangular triple enclosure reports original warning");
    Dialog(frame, "", "", "", "", wxID_OK);
    Require(feedback.count == 2 && feedback.enclosureVisible,
            "OK applies and reports rectangle warning before closing the enclosure dialog");
    Require(TestEditor::Text(editor) == "alpha\nbeta" && !editor->Send(Message::CanUndo) &&
                editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify) &&
                editor->Send(Message::GetRectangularSelectionAnchor) == anchor &&
                editor->Send(Message::GetRectangularSelectionCaret) == caret,
            "unsupported rectangle retains bytes, endpoints, save point and pending redo");
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Stream));
    std::puts("PASS: original read-only availability and rectangular warning order/history");
}
void Recall(EditorFrame* frame) {
    TestEditor::ResetStream(frame->Editor(), "cat");
    Dialog(frame, "", "", "[", "]", wxID_OK);
    frame->Editor()->Send(Message::SetSavePoint);
    Require(frame->NewDocument(), "new document opens in current window");
    Dialog(frame, "[", "]", "discard", "discard", wxID_CANCEL);
    auto* other = new EditorFrame;
    other->Show();
    Dialog(other, "", "", "other", "window", wxID_OK);
    other->Editor()->Send(Message::SetSavePoint);
    other->Destroy();
    Dialog(frame, "[", "]", "", "", wxID_OK);
    std::puts("PASS: recall survives New in current window and starts empty in another window");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("enclosure");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* extension;
        const char* language;
        const char* command;
        const char* after;
    };
    const Fixture fixtures[] = {
        {"cpp", "C/C++", "Triple Single Quotes", "\xef\xbb\xbf'''中文'''\r\nnext\nlast\r"},
        {"py", "Python", "Triple Double Quotes", "\xef\xbb\xbf\"\"\"中文\"\"\"\r\nnext\nlast\r"},
        {"json", "JSON", "Triple Backticks", "\xef\xbb\xbf```中文```\r\nnext\nlast\r"},
        {"txt", "Plain Text", "Triple Single Quotes", "\xef\xbb\xbf'''中文'''\r\nnext\nlast\r"},
    };
    const std::string before = "\xef\xbb\xbf中文\r\nnext\nlast\r";
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string("sample.") + fixture.extension);
        TestFiles::WriteBytes(path, before);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened, "open language disk fixture");
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", fixture.language));
        editor->Send(Message::SetSel, 0, 6);
        Menu(frame, fixture.command);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "all document languages save original enclosure with exact BOM, mixed EOL and final bytes");
        editor->Send(Message::Undo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == before,
                "one Undo and save restore independently read original disk bytes");
        editor->Send(Message::Redo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "one Redo and save restore independently read enclosed disk bytes");
    }
    using EncodedFixture = TestFiles::EncodedFixture;
    const EncodedFixture encoded[] = {
        {"gbk", FileEncoding::Gbk, "\xd6\xd0", "\xce\xc4\n\xd6\xd0\xd6\xd0"},
        {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e", 4),
         std::string("\xff\xfe\x87\x65\n\0\x2d\x4e\x2d\x4e", 10)},
    };
    for (const auto& fixture : encoded) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "open original encoded enclosure fixture");
        editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
        editor->Send(Message::SelectAll);
        editor->EncloseSelection(wxString::FromUTF8("文\r\n"), wxString::FromUTF8("中"));
        TestFiles::RequireSavedUndoRedo(
            frame, path, fixture,
            {"Unicode prefix/suffix and pair normalization save exact GBK and UTF-16LE BOM bytes",
             "encoded enclosure Undo saves original bytes", "encoded enclosure Redo saves enclosed bytes"});
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: C/C++, Python, JSON, Plain Text, GBK and UTF-16LE independent saved bytes and Undo/Redo");
}
class EnclosureApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                Triple(frame);
                CustomDialog(frame);
                Shortcuts(frame);
                DialogKeys(frame);
                PublicBehavior(frame);
                Protection(frame);
                Recall(frame);
                SavedBytes(frame);
                std::puts("PASS: original enclosure commands");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Editor()->Send(Message::SetSavePoint);
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
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(EnclosureApp);
