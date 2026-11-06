#include "native_search_helpers.h"
#include "settings/settings_store.h"
#include "sort_reference_fixtures.h"
#include "file_helpers.h"
#include <algorithm>
#include <vector>
#include <cstring>
#include <wx/radiobut.h>
#include <wx/ffile.h>
#include <wx/eventfilter.h>
#include <chrono>
#include <functional>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void WaitFor(const std::function<bool()>& condition, const char* message) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (!condition() && std::chrono::steady_clock::now() < end) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(condition(), message);
}
#ifndef __WXMSW__
class KeyRelease final : public wxEventFilter {
public:
    KeyRelease(int key, bool& released) : key_(key), released_(released) { wxEvtHandler::AddFilter(this); }
    ~KeyRelease() override { wxEvtHandler::RemoveFilter(this); }
    int FilterEvent(wxEvent& event) override {
        if (event.GetEventType() == wxEVT_KEY_UP && static_cast<wxKeyEvent&>(event).GetKeyCode() == key_)
            released_ = true;
        return Event_Skip;
    }

private:
    int key_;
    bool& released_;
};
#endif
void NativeKey(wxWindow* window, int key, const char* name, bool alt = false, bool* externalRelease = nullptr) {
#ifdef __WXMSW__
    Require(NativeSearch::WindowsShortcut(
                window, key, {{VK_MENU, static_cast<BYTE>(alt ? 0x80 : 0)}, {VK_SHIFT, 0}, {VK_CONTROL, 0}}),
            "native dialog key is handled");
#else
    wxString path;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &path), "native keys use a private display");
    if (auto* dialog = wxDynamicCast(window, wxDialog); dialog && !dialog->IsActive()) {
        {
            wxFFile file(path, "wb");
            file.Write("front:" + dialog->GetTitle());
        }
        WaitFor(
            [&] {
                wxString pending;
                wxFFile file(path, "rb");
                file.ReadAll(&pending);
                return pending.empty();
            },
            "private display focuses the owned modal dialog");
    }
    bool localReleased = false;
    bool& released = externalRelease ? *externalRelease : localReleased;
    KeyRelease observer(key, released);
    {
        wxFFile file(path, "wb");
        file.Write(wxString("key:") + name);
    }
    WaitFor([&] { return released; }, "native key release reaches the owned window");
#endif
}
class SortDialogDriver final : public wxModalDialogHook, private wxTimer {
public:
    explicit SortDialogDriver(std::function<void(wxDialog*)> action = {}) : action_(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Sort Lines") {
            ++count;
            dialog_ = dialog;
            StartOnce(40);
        }
        return wxID_NONE;
    }
    int count = 0;
    std::exception_ptr failure;

private:
    void Notify() override {
        try {
            if (action_)
                action_(dialog_);
            else
                NativeSearch::Click(dialog_, wxID_OK);
        } catch (...) {
            failure = std::current_exception();
            NativeSearch::Click(dialog_, wxID_CANCEL);
        }
    }
    wxDialog* dialog_ = nullptr;
    std::function<void(wxDialog*)> action_;
};
wxCheckBox* Check(wxDialog* dialog, const char* name) {
    auto* box = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxCheckBox);
    Require(box, "native Sort Lines option exists");
    return box;
}
wxRadioButton* Radio(wxDialog* dialog, const char* name) {
    auto* radio = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxRadioButton);
    Require(radio, "native Sort Lines mode exists");
    return radio;
}
void Choose(wxRadioButton* radio) {
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(radio->GetHandle()), BM_CLICK, 0, 0);
#elif defined(__WXGTK__)
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio->GetHandle()), TRUE);
#endif
    wxYield();
}
void Dialog(EditorFrame* frame, const std::function<void(wxDialog*)>& action, bool native = false) {
    bool openingReleased = false;
    SortDialogDriver driver([&](wxDialog* dialog) {
        if (native) WaitFor([&] { return openingReleased; }, "opening Alt+O is released before dialog navigation");
        action(dialog);
    });
    if (native) {
        frame->Editor()->SetFocus();
        wxYield();
#ifndef __WXMSW__
        wxString path;
        Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &path), "private focus control exists");
        {
            wxFFile file(path, "wb");
            file.Write(frame->IsActive() ? "focus:wxnotepad" : "front:wxnotepad");
        }
        WaitFor(
            [&] {
                wxString pending;
                wxFFile file(path, "rb");
                file.ReadAll(&pending);
                return pending.empty() && frame->IsActive() && frame->Editor()->HasFocus();
            },
            "private display focuses the editor");
#endif
#ifdef __WXMSW__
        openingReleased = true;
#endif
        NativeKey(frame->Editor(), 'O', "alt+o", true, &openingReleased);
    } else
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Sort Lines..."));
    if (driver.failure) std::rethrow_exception(driver.failure);
    Require(driver.count == 1, "Sort Lines opens one native modal dialog");
}
void DialogBehavior(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "b2\nb10\nb1", 0, 9, Scintilla::EndOfLine::Lf);
    Dialog(frame, [](wxDialog* dialog) { NativeSearch::Click(dialog, wxID_CANCEL); }, true);
    Dialog(
        frame,
        [](wxDialog* dialog) {
            Require(Radio(dialog, "Sort ascending.")->GetValue(), "ascending is the initial accepted mode");
            auto* merge = Check(dialog, "Merge duplicate lines.");
            auto* remove = Check(dialog, "Remove duplicate lines.");
            auto* unique = Check(dialog, "Remove unique lines.");
            Require(!Check(dialog, "Column sort (rectangular selection).")->IsEnabled(), "stream disables column sort");
            auto* previousFocus = wxWindow::FindFocus();
            NativeKey(dialog, WXK_TAB, "Tab");
            Require(wxWindow::FindFocus() && wxWindow::FindFocus() != previousFocus &&
                        wxWindow::FindFocus()->IsEnabled(),
                    "native Tab moves focus to an enabled dialog control");
            NativeSearch::Toggle(merge, true);
            NativeSearch::Toggle(remove, true);
            Require(!merge->IsEnabled() && merge->GetValue(),
                    "Remove disables Merge without clearing its accepted check");
            Choose(Radio(dialog, "Shuffle lines."));
            Require(!remove->IsEnabled() && !unique->IsEnabled() &&
                        !Check(dialog, "Logical number comparison.")->IsEnabled(),
                    "shuffle disables filters and comparisons");
            Choose(Radio(dialog, "Sort descending."));
            Require(merge->IsEnabled(), "original mode-change enablement depends on Remove Unique");
            NativeSearch::Toggle(unique, true);
            Require(merge->IsEnabled(), "original Remove Unique checkbox has no enablement handler");
            Choose(Radio(dialog, "Don't sort."));
            Require(!merge->IsEnabled(), "changing mode applies the original Remove Unique dependency");
            NativeSearch::Toggle(Check(dialog, "Logical number comparison."), true);
            NativeSearch::Click(dialog, wxID_CANCEL);
        },
        false);
    Require(TestEditor::Text(editor) == "b2\nb10\nb1" && !editor->Send(Scintilla::Message::CanUndo),
            "Cancel preserves bytes and history");
    TestEditor::RequireSelection(editor, 0, 9, "Cancel preserves selection");
    Dialog(frame, [](wxDialog* dialog) {
        Require(Radio(dialog, "Sort ascending.")->GetValue() &&
                    !Check(dialog, "Logical number comparison.")->GetValue() &&
                    !Check(dialog, "Merge duplicate lines.")->GetValue(),
                "Cancel preserves all accepted options");
        Choose(Radio(dialog, "Sort descending."));
        NativeSearch::Toggle(Check(dialog, "Logical number comparison."), true);
        NativeSearch::Click(dialog, wxID_OK);
    });
    Require(TestEditor::Text(editor) == "b10\nb2\nb1", "native accepted descending logical sort edits the document");
    editor->Send(Scintilla::Message::Undo);
    Dialog(frame, [](wxDialog* dialog) {
        Require(Radio(dialog, "Sort descending.")->GetValue() &&
                    Check(dialog, "Logical number comparison.")->GetValue(),
                "accepted mode and options are recalled within the session");
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    Require(editor->Send(Scintilla::Message::CanRedo), "Cancel preserves pending Redo");
    std::puts("PASS: Alt+O, native Tab/OK/Cancel, exact option interactions, accepted-option recall and Cancel state");
}
void Fixtures(EditorControl* editor) {
    struct Fixture {
        const char* before;
        unsigned options;
        const char* after;
    };
    const Fixture fixtures[] = {
        {"b\na\nc", SortOption::Descending, "c\nb\na"},
        {"a\nA\na\nB", SortOption::IgnoreCase, "a\nA\na\nB"},
        {"a\nA\na\nB", SortOption::IgnoreCase | SortOption::MergeDuplicate, "a\nB"},
        {"a\nA\na\nB", SortOption::IgnoreCase | SortOption::Descending | SortOption::MergeDuplicate, "B\na"},
        {"A\na\nb", SortOption::IgnoreCase | SortOption::MergeDuplicate, "A\nb"},
        {"A\na\nb", SortOption::IgnoreCase | SortOption::Descending | SortOption::MergeDuplicate, "b\na"},
        {"a\nA\nb", SortOption::IgnoreCase | SortOption::Descending, "b\nA\na"},
        {"B\na\nb\nC", SortOption::IgnoreCase | SortOption::RemoveDuplicate, "a\nC"},
        {"c\nb\na\nb", SortOption::RemoveUnique, "b\nb"},
        {"c\nb\na\nb", SortOption::RemoveUnique | SortOption::MergeDuplicate, "b"},
        {"c\nb\na\nb", SortOption::DontSort | SortOption::RemoveUnique, "b\nb"},
        {"\n😀\n中", SortOption::Ascending, "中\n😀\n"},
        {"é\né\nE\ne", SortOption::LogicalNumber, "E\ne\né\né"},
        {"a2\na02\na002\na10\na1\nA1\na01", SortOption::LogicalNumber, "a01\nA1\na1\na002\na02\na2\na10"},
        {"阿\n波\n中\n一\n丁", SortOption::LogicalNumber, "阿\n波\n丁\n一\n中"},
        {SortReference::logicalInput, SortOption::LogicalNumber, SortReference::logicalExpected},
        {SortReference::fileTypeInput, SortOption::LogicalNumber | SortOption::FileType,
         SortReference::fileTypeExpected},
        {"b.txt\na.cpp\nz.TXT\nfile\na.tar.gz\n.gitignore\nname.\ndir.ext/file\ndir.ext\\file\na "
         "b.ext\na.txt:stream\nfile..ext",
         SortOption::FileType,
         "dir.ext\\file\nfile\nname.\nz.TXT\na.cpp\na "
         "b.ext\nfile..ext\ndir.ext/file\n.gitignore\na.tar.gz\nb.txt\na.txt:stream"},
    };
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(editor, fixture.before, 0, 0, Scintilla::EndOfLine::Lf);
        editor->Send(Scintilla::Message::SetSel, 0, editor->Send(Scintilla::Message::GetLength));
        editor->SortLines(fixture.options);
        if (TestEditor::Text(editor) != fixture.after)
            std::fprintf(stderr, "options=%u actual=<%s> expected=<%s>\n", fixture.options,
                         TestEditor::Text(editor).c_str(), fixture.after);
        Require(TestEditor::Text(editor) == fixture.after, "literal original sorting/filtering fixture");
        TestEditor::RequireSelection(editor, 0, static_cast<intptr_t>(std::strlen(fixture.after)),
                                     "sort selects full replacement");
        editor->Send(Scintilla::Message::Undo);
        Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Scintilla::Message::CanUndo),
                "one Undo restores pre-sort bytes");
        editor->Send(Scintilla::Message::Redo);
        Require(TestEditor::Text(editor) == fixture.after, "one Redo restores sorted bytes");
    }
    std::puts("PASS: original literal ordering, filtering, UTF-16, linguistic/numeric ties and file-type precedence");
}
void SelectionAndHistory(EditorFrame* frame) {
    using Scintilla::Message;
    auto* editor = frame->Editor();
    for (bool reverse : {false, true}) {
        TestEditor::ResetStream(editor, "PRE\nb\na\nTAIL", reverse ? 8 : 5, reverse ? 5 : 8, Scintilla::EndOfLine::Lf);
        editor->SortLines();
        Require(TestEditor::Text(editor) == "PRE\na\nb\nTAIL",
                "partial selection expands to lines and excludes a next-line-start endpoint");
        TestEditor::RequireSelection(editor, reverse ? 8 : 4, reverse ? 4 : 8, "sorting restores stream direction");
    }
    for (const unsigned flags : {0u, unsigned(SortOption::Descending), unsigned(SortOption::DontSort),
                                 unsigned(SortOption::Shuffle), unsigned(SortOption::RemoveUnique)}) {
        for (intptr_t caret : {0, 1, 2}) {
            TestEditor::ResetStream(editor, "b\na", 0, caret, Scintilla::EndOfLine::Lf);
            editor->SortLines(flags);
            Require(TestEditor::Text(editor) == "b\na" && !editor->Send(Message::CanUndo),
                    "empty or effective single-line selection does not edit or create history");
            TestEditor::RequireSelection(editor, 0, caret, "early return preserves endpoints");
        }
        TestEditor::ResetStream(editor, "b\na", 1, 3, Scintilla::EndOfLine::Lf);
        editor->Send(Message::SetReadOnly, 1);
        const auto revision = editor->DocumentRevision();
        editor->SortLines(flags);
        Require(TestEditor::Text(editor) == "b\na" && !editor->Send(Message::CanUndo) &&
                    editor->DocumentRevision() == revision,
                "read-only core preserves bytes, history and revision for all modes");
        TestEditor::RequireSelection(editor, 0, flags == SortOption::RemoveUnique ? 0 : 3,
                                     "read-only original caller still repairs the computed selection");
        editor->Send(Message::SetReadOnly, 0);
    }
    TestEditor::ResetStream(editor, "a\nb", 0, 3, Scintilla::EndOfLine::Lf);
    editor->SortLines();
    Require(editor->Send(Message::CanUndo) && editor->Send(Message::GetModify),
            "unchanged multiline sort keeps original replacement history");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "a\nb" && !editor->Send(Message::GetModify),
            "Undo identical replacement restores save point");
    editor->Send(Message::SetSel, 0, 2);
    editor->SortLines();
    Require(editor->Send(Message::CanRedo), "effective single-line no-op keeps pending Redo");
    editor->Send(Message::SetSel, 0, 3);
    editor->SortLines();
    Require(!editor->Send(Message::CanRedo), "identical multiline sort consumes pending Redo");
    TestEditor::ResetStream(editor, "b\na\nb\n", 0, 6, Scintilla::EndOfLine::Lf);
    editor->SortLines(SortOption::Shuffle | SortOption::RemoveDuplicate | SortOption::RemoveUnique);
    const auto shuffled = TestEditor::Text(editor);
    Require(shuffled.size() == 6 && std::count(shuffled.begin(), shuffled.end(), 'b') == 2 &&
                std::count(shuffled.begin(), shuffled.end(), 'a') == 1 && shuffled.back() == '\n',
            "original shuffle preserves multiplicities and ignores filtering flags");
    TestEditor::RequireSelection(editor, 0, 6, "shuffle selects complete output");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "b\na\nb\n", "one Undo restores shuffled input");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == shuffled, "Redo restores the actual random permutation");
    TestEditor::ResetStream(editor, "", 0, 0, Scintilla::EndOfLine::Lf);
    const std::string embedded("b\0x\na\0y\nb\0z", 11);
    editor->Send(Message::AddText, embedded.size(), reinterpret_cast<intptr_t>(embedded.data()));
    editor->Send(Message::SetSel, 0, static_cast<intptr_t>(embedded.size()));
    editor->Send(Message::EmptyUndoBuffer);
    editor->SortLines(SortOption::MergeDuplicate);
    Require(TestEditor::Text(editor) == "a\nb",
            "original NUL-terminated conversions truncate every line before comparison and output");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == embedded, "Undo restores embedded NUL bytes");
    TestEditor::ResetStream(editor, "b\r\na\nb\rC", 0, 8, Scintilla::EndOfLine::CrLf);
    editor->SortLines();
    Require(TestEditor::Text(editor) == "C\r\na\r\nb\r\nb",
            "sorting reconstructs mixed endings using current mode and preserves unterminated EOF");
    std::puts(
        "PASS: stream bounds/direction, empty and single lines, read-only, unchanged history, NUL, EOL and shuffle");
}
void Rectangles(EditorFrame* frame) {
    using Scintilla::Message;
    auto* editor = frame->Editor();
    editor->SetIndentationWidths(4, 4);
    for (bool reverse : {false, true}) {
        TestEditor::ResetStream(editor, "b2\na1\nc0\nTAIL", 0, 0, Scintilla::EndOfLine::Lf);
        editor->Send(Message::SetRectangularSelectionAnchor, reverse ? 8 : 1);
        editor->Send(Message::SetRectangularSelectionCaret, reverse ? 1 : 8);
        editor->SortLines(SortOption::Column);
        Require(TestEditor::Text(editor) == "c0\na1\nb2\nTAIL",
                "rectangle sorts full padded lines at the left selected column");
        Require(editor->Send(Message::SelectionIsRectangle) &&
                    editor->Send(Message::GetRectangularSelectionAnchor) == (reverse ? 8 : 1) &&
                    editor->Send(Message::GetRectangularSelectionCaret) == (reverse ? 1 : 8),
                "rectangle keeps horizontal columns and vertical direction");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "b2\na1\nc0\nTAIL" && !editor->Send(Message::CanUndo),
                "one Undo restores full rectangle operation");
    }
    TestEditor::ResetStream(editor, "b\n\na\nTAIL", 0, 0, Scintilla::EndOfLine::Lf);
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 4);
    editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, 2);
    editor->SortLines(SortOption::Column);
    Require(TestEditor::Text(editor) == "\na\nb\nTAIL", "ordinary rectangle sort skips empty-line padding");
    Require(editor->Send(Message::GetRectangularSelectionCaretVirtualSpace) == 0,
            "restored rectangle clears virtual columns");
    TestEditor::ResetStream(editor, "x\n\nTAIL", 0, 0, Scintilla::EndOfLine::Lf);
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 2);
    editor->SortLines(SortOption::Shuffle);
    const auto shuffled = TestEditor::Text(editor);
    Require(shuffled.size() == 8 && shuffled.ends_with("TAIL") &&
                std::count(shuffled.begin(), shuffled.end(), ' ') == 1 &&
                std::count(shuffled.begin(), shuffled.end(), 'x') == 1,
            "rectangular shuffle pads empty lines while preserving complete-line multiplicities");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "x\n\nTAIL" && !editor->Send(Message::CanUndo),
            "one Undo restores empty-line padding and shuffle together");
    TestEditor::ResetStream(editor, "bX\naX\ncA\nTAIL", 0, 0, Scintilla::EndOfLine::Lf);
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 8);
    editor->SortLines(SortOption::Column);
    Require(TestEditor::Text(editor) == "cA\naX\nbX\nTAIL",
            "equal column entries use the complete transformed line before original-index ties");
    TestEditor::ResetStream(editor, "x\t2\ny\t1\nz\t0\nTAIL", 0, 0, Scintilla::EndOfLine::Lf);
    editor->Send(Message::SetRectangularSelectionAnchor, 2);
    editor->Send(Message::SetRectangularSelectionCaret, 10);
    editor->SortLines(SortOption::Column | SortOption::FileType);
    Require(TestEditor::Text(editor) == "z\t0\ny\t1\nx\t2\nTAIL",
            "column comparison takes precedence over file-type grouping and expands tab columns");
    Dialog(frame, [](wxDialog* dialog) {
        auto* column = Check(dialog, "Column sort (rectangular selection).");
        Require(column->IsEnabled() && column->GetValue(), "opening over a rectangle forces Column on");
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    std::puts("PASS: rectangle column precedence, tabs, padding, virtual columns, direction and grouped history");
}
void SavedBytes(EditorFrame* frame) {
    using Scintilla::Message;
    const auto directory = TestFiles::TemporaryDirectory("sort-lines");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string after;
    };
    const Fixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中\r\nB\nA",
         "\xef\xbb\xbf"
         "A\r\nB\r\n中"},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\r\nB\nA", "A\r\nB\r\n\xd6\xd0"},
        {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\r\0\n\0B\0\n\0A\0", 14),
         std::string("\xff\xfe"
                     "A\0\r\0\n\0B\0\r\0\n\0\x2d\x4e",
                     16)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        frame->Editor()->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "encoded sort fixture opens");
        auto* editor = frame->Editor();
        editor->Send(Message::SetSel, 0, editor->Send(Message::GetLength));
        editor->SortLines();
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "independently reopened bytes retain encoding/BOM and original EOL reconstruction");
        editor->Send(Message::Undo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "Undo and Save restore independent original encoded bytes");
        editor->Send(Message::Redo);
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.after,
                "Redo and Save restore independent sorted encoded bytes");
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: independently reopened UTF-8/GBK/UTF-16 bytes, BOM/EOL and Undo/Redo");
}
class SortApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        settings_ = std::make_unique<SettingsStore>();
        settings_->values = OriginalSettings{};
        settings_->values.wordWrap = false;
        settings_->values.warnLineEndings = false;
        settings_->values.fixLineEndings = false;
        settings_->values.fixTrailingBlanks = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                TestEditor::ResetStream(frame->Editor(), "b\na\nc", 0, 5, Scintilla::EndOfLine::Lf);
                const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Sort Lines...");
                Require(id != wxNOT_FOUND, "Edit menu exposes Sort Lines");
                {
                    SortDialogDriver driver;
                    NativeSearch::Command(frame, id);
                    Require(driver.count == 1 && TestEditor::Text(frame) == "a\nb\nc",
                            "default Sort Lines is ascending");
                }
                TestEditor::RequireSelection(frame->Editor(), 0, 5, "sorting selects the replacement");
                Fixtures(frame->Editor());
                DialogBehavior(frame);
                SelectionAndHistory(frame);
                Rectangles(frame);
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
wxIMPLEMENT_APP(SortApp);
