#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/app.h>
#include <wx/menu.h>
#include <wx/modalhook.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <cstdio>
#include <stdexcept>
#include <chrono>
#include <functional>
#include <memory>
#include <tuple>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Split(EditorFrame* frame) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Split Lines");
    Require(id != wxNOT_FOUND, "native Edit menu exposes Split Lines");
    NativeSearch::Command(frame, id);
}
void SplitFixture(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "one two three", 0, 13, Scintilla::EndOfLine::Lf);
    // A very wide view leaves this target unchanged, preserving history/endpoints.
    Split(frame);
    Require(TestEditor::Text(editor) == "one two three" && !editor->Send(Message::CanUndo), "wide split is a no-op");
    TestEditor::RequireSelection(editor, 0, 13, "split retains selection");
    editor->SetLineNumbersVisible(false);
    for (int margin = 0; margin < 5; ++margin)
        editor->Send(Message::SetMarginWidthN, margin, 0);
    editor->Send(Message::SetMarginLeft, 0, 0);
    editor->Send(Message::SetMarginRight, 0, 0);
    const auto width = editor->Send(Message::TextWidth, 32, reinterpret_cast<intptr_t>("one two ")) + 1;
    editor->SetClientSize(static_cast<int>(width), 200);
    wxYield();
    Split(frame);
    Require(TestEditor::Text(editor) == "one two \nthree", "split inserts current EOL at rendered word boundary");
    TestEditor::RequireSelection(editor, 0, 14, "split adjusts original caret for inserted EOL");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "one two three" && !editor->Send(Message::CanUndo), "split is one Undo action");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "one two \nthree", "split Redo restores breaks");
    frame->SetClientSize(900, 600);
}
void WaitFor(const std::function<bool()>& condition, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(condition(), message);
}
void DialogKey(wxDialog* dialog, int key, const char* nativeName) {
#ifdef __WXMSW__
    static_cast<void>(nativeName);
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read test-thread keyboard state for native dialog navigation");
    BYTE released[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(released));
    for (const int modifier : {VK_CONTROL, VK_MENU, VK_SHIFT})
        released[modifier] = 0;
    Require(SetKeyboardState(released), "release shortcut modifiers for native dialog navigation");
    MSG message{};
    auto* focused = wxWindow::FindFocus();
    message.hwnd = static_cast<HWND>((focused ? focused : dialog)->GetHandle());
    message.message = WM_KEYDOWN;
    message.wParam = key;
    // Reuse the native dialog-navigation seam established by modify_lines.
    dialog->MSWProcessMessage(&message);
    SetKeyboardState(previous);
#else
    bool released = false;
    auto* previousFocus = wxWindow::FindFocus();
    NativeKeys::KeyRelease observer(key, released, nullptr);
    wxString focus;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focus), "dialog keys use private display");
    if (!dialog->IsActive()) {
        {
            wxFFile request(focus, "wb");
            request.Write("front:Column Wrap");
        }
        WaitFor(
            [&] {
                wxString pending;
                wxFFile request(focus, "rb");
                request.ReadAll(&pending);
                return pending.empty();
            },
            "private compositor activates owned boundary dialog");
    }
    {
        wxFFile request(focus, "wb");
        request.Write(wxString("key:") + nativeName);
    }
    WaitFor(
        [&] { return released || !dialog->IsShown() || (key == WXK_TAB && wxWindow::FindFocus() != previousFocus); },
        "native dialog key reaches its modal control");
#endif
    wxYield();
}
enum class DialogAction : uint8_t { Accept, Cancel, Return, Escape, Invalid, Tab };
class WrapDriver final : public wxModalDialogHook {
public:
    WrapDriver(wxString expected = "80", wxString chosen = "6", DialogAction action = DialogAction::Accept)
        : expected_(std::move(expected)), chosen_(std::move(chosen)), action_(action) {
        Register();
    }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() != "Column Wrap") return wxID_NONE;
        wxTheApp->CallAfter([this, dialog] {
            try {
                auto* input = wxDynamicCast(wxWindow::FindWindowByName("Boundary", dialog), wxTextCtrl);
                observed = input && input->GetValue() == expected_;
                if (input) input->SetValue(chosen_);
                if (action_ == DialogAction::Invalid) {
                    NativeSearch::Click(dialog, wxID_OK);
                    Require(dialog->IsShown() && input->HasFocus(), "invalid input stays open and refocuses Boundary");
                    NativeSearch::Click(dialog, wxID_CANCEL);
                } else if (action_ == DialogAction::Return) {
                    input->SetFocus();
                    DialogKey(dialog, WXK_RETURN, "Return");
                    Require(!dialog->IsShown(), "native Return accepts and closes Column Wrap");
                } else if (action_ == DialogAction::Escape) {
                    DialogKey(dialog, WXK_ESCAPE, "Escape");
                    Require(!dialog->IsShown(), "native Escape cancels and closes Column Wrap");
                } else if (action_ == DialogAction::Tab) {
                    input->SetFocus();
                    bool ok = false, cancel = false;
                    for (int i = 0; i < 5; ++i) {
                        DialogKey(dialog, WXK_TAB, "Tab");
                        auto* focused = wxWindow::FindFocus();
                        ok |= focused && focused->GetId() == wxID_OK;
                        cancel |= focused && focused->GetId() == wxID_CANCEL;
                        if (ok && cancel && input->HasFocus()) break;
                    }
                    Require(ok && cancel && input->HasFocus(), "native Tab traverses Boundary, OK and Cancel");
                    NativeSearch::Click(dialog, wxID_CANCEL);
                } else {
                    NativeSearch::Click(dialog, action_ == DialogAction::Cancel ? wxID_CANCEL : wxID_OK);
                    if (action_ == DialogAction::Accept && dialog->IsShown()) {
                        NativeSearch::Click(dialog, wxID_CANCEL);
                        throw std::runtime_error(("original accepted boundary must close the native dialog: " + chosen_)
                                                     .ToStdString(wxConvUTF8));
                    }
                }
            } catch (const std::exception& error) {
                failure = error.what();
                if (dialog->IsShown()) NativeSearch::Click(dialog, wxID_CANCEL);
            }
        });
        return wxID_NONE;
    }
    bool observed = false;
    std::string failure;

private:
    wxString expected_;
    wxString chosen_;
    DialogAction action_;
};
void Dialog(EditorFrame* frame, const wxString& expected, const wxString& chosen, DialogAction action) {
    if (action == DialogAction::Return || action == DialogAction::Escape || action == DialogAction::Tab)
        NativeKeys::Shortcut(frame, 'I', true, true, true);
    WrapDriver driver(expected, chosen, action);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Column Wrap..."));
    if (!driver.failure.empty()) throw std::runtime_error(driver.failure);
    Require(driver.observed, "native Boundary shows original effective default/accepted recall");
}
void ColumnFixture(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "one two", 0, 7, Scintilla::EndOfLine::Lf);
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Column Wrap...");
    Require(id != wxNOT_FOUND, "native Edit menu exposes Column Wrap");
    WrapDriver driver;
    NativeSearch::Command(frame, id);
    if (!driver.failure.empty()) throw std::runtime_error(driver.failure);
    Require(driver.observed && TestEditor::Text(editor) == "one\ntwo", "native boundary wraps original words");
    TestEditor::RequireSelection(editor, 0, 7, "column wrap replacement endpoints");
}
void ColumnCases(EditorControl* editor) {
    struct Fixture {
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        int column;
        const char* after;
        intptr_t newAnchor;
        intptr_t newCaret;
        bool modified = true;
        Scintilla::EndOfLine mode = Scintilla::EndOfLine::Lf;
    };
    // Literal results from Edit.cpp::EditWrapToColumn (89b8b36), including
    // its bModified condition rather than a comparison of output bytes.
    const Fixture fixtures[] = {
        {"one two three", 2, 13, 6, "one\ntwo\nthree", 0, 13},
        {"one two three", 13, 2, 6, "one\ntwo\nthree", 13, 0},
        {"one two three", 1, 5, 4, "one\ntwo three", 0, 5},
        {"a  b\nTAIL", 1, 5, 80, "a b\nTAIL", 0, 4},
        {"a\tb", 0, 3, 80, "a\tb", 0, 3, false},
        {" a", 0, 2, 80, " a", 0, 2, false},
        {"a ", 0, 2, 80, "a ", 0, 2, false},
        {" a  b ", 0, 6, 80, "a b", 0, 3},
        {"a\t\tb", 0, 4, 80, "a b", 0, 3},
        {"abcdefgh", 0, 8, 1, "abcdefgh", 0, 8, false},
        {"a abcdefgh", 0, 10, 3, "a\nabcdefgh", 0, 10},
        {" abcdefgh", 0, 9, 3, "\nabcdefgh", 0, 9},
        {"a\r\nb c\rd e\nf g", 0, 14, 2, "a\r\nb\r\nc\rd\r\ne\nf\r\ng", 0, 17, true, Scintilla::EndOfLine::CrLf},
        {"a b\r\n\r\nc d", 0, 10, 2, "a\rb\r\n\r\nc\rd", 0, 10, true, Scintilla::EndOfLine::Cr},
        {"a  \r\n\t\tb\n\n", 0, 10, 80, "a\r\nb\n\n", 0, 6},
        {"中 😀 尾", 0, 12, 3, "中\n😀\n尾", 0, 12},
        {"😀 x", 0, 6, 3, "😀\nx", 0, 6},
        {"中 x", 0, 5, 3, "中 x", 0, 5, false},
        {"a b  c", 0, 7, 4, "a b\nc", 0, 6},
        {"a\vlong  b", 0, 9, 80, "a\vlong b", 0, 8},
        {"a  b", 2, 2, 1, "a  b", 2, 2, false},
    };
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(editor, fixture.before, fixture.anchor, fixture.caret, fixture.mode);
        Require(editor->WrapToColumn(fixture.column), "stream column wrap succeeds");
        if (TestEditor::Text(editor) != fixture.after)
            std::fprintf(stderr, "column=%d before=<%s> expected=<%s> actual=<%s>\n", fixture.column, fixture.before,
                         fixture.after, TestEditor::Text(editor).c_str());
        Require(TestEditor::Text(editor) == fixture.after, "original literal column-wrap bytes");
        TestEditor::RequireSelection(editor, fixture.newAnchor, fixture.newCaret, "original wrap endpoints");
        Require(!!editor->Send(Message::CanUndo) == fixture.modified &&
                    !!editor->Send(Message::GetModify) == fixture.modified,
                "original replacement condition and save point");
        Require(editor->Send(Message::GetEOLMode) == static_cast<intptr_t>(fixture.mode),
                "wrap retains insertion EOL mode");
        if (fixture.modified) {
            editor->Send(Message::Undo);
            Require(TestEditor::Text(editor) == fixture.before && !editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::GetModify),
                    "one Undo restores original text and save point");
            editor->Send(Message::Redo);
            Require(TestEditor::Text(editor) == fixture.after && !editor->Send(Message::CanRedo),
                    "one Redo restores wrap");
        }
    }
    std::puts(
        "PASS: literal Column Wrap Unicode/UTF-16, whitespace, long-word, EOL, partial/reversed and no-op fixtures");
    TestEditor::ResetStream(editor, "");
    const std::string nulText("a  \0b c", 7);
    editor->Send(Message::AddText, nulText.size(), reinterpret_cast<intptr_t>(nulText.data()));
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    editor->Send(Message::SetSel, 0, 7);
    editor->WrapToColumn(80);
    Require(TestEditor::Text(editor) == std::string("a\0b c", 5),
            "explicit-length wrapping preserves embedded NUL and following text");
}
void DialogCases(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "a  b", 1, 4, Scintilla::EndOfLine::Lf);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 1, 4);
    for (const auto action : {DialogAction::Cancel, DialogAction::Escape, DialogAction::Tab}) {
        Dialog(frame, "6", "2", action);
        Require(TestEditor::Text(editor) == "a  b" && editor->Send(Message::CanRedo) &&
                    !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                "Cancel/Esc/Tab keep text, history, accepted recall and save point");
        TestEditor::RequireSelection(editor, 1, 4, "cancel keeps partial endpoints");
    }
    for (const auto* invalid : {"", "abc", "-1", "+1", "12abc", "12 ", "4294967296", "\t12", "Ⅻ", "⑫", "½", "零"})
        Dialog(frame, "6", wxString::FromUTF8(invalid), DialogAction::Invalid);
    Dialog(frame, "6", "0", DialogAction::Return);
    Require(TestEditor::Text(editor) == "a\nb", "Return accepts and zero clamps to one");
    Dialog(frame, "1", "513", DialogAction::Accept);
    Dialog(frame, "512", "4294967295", DialogAction::Accept);
    Dialog(frame, "1", " 12", DialogAction::Accept);
    Dialog(frame, "12", "000000000000006", DialogAction::Accept);
    Dialog(frame, "6", "42", DialogAction::Cancel);
    Dialog(frame, "6", wxString::FromUTF8("１２"), DialogAction::Accept);
    Dialog(frame, "12", wxString::FromUTF8("٢"), DialogAction::Accept);
    Dialog(frame, "2", wxString::FromUTF8("²"), DialogAction::Accept);
    Dialog(frame, "2", wxString::FromUTF8("𝟞"), DialogAction::Accept);
    std::puts("PASS: native OK/Return/Cancel/Esc/Tab, invalid refocus, unsigned conversion, bounds and recall");
}
void SelectionAndProtection(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (bool reverse : {false, true}) {
        TestEditor::ResetStream(editor, "a  b", reverse ? 4 : 1, reverse ? 1 : 4);
        editor->Send(Message::SetReadOnly, 1);
        Dialog(frame, reverse ? "2" : "6", "2", DialogAction::Accept);
        Require(TestEditor::Text(editor) == "a  b" && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "read-only accepts boundary while core protects text and history");
        TestEditor::RequireSelection(editor, reverse ? 3 : 0, reverse ? 0 : 3,
                                     "read-only retains original replacement endpoint repair");
    }
    TestEditor::ResetStream(editor, "a\tb", 1, 3);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 1, 3);
    editor->WrapToColumn(80);
    Require(!editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
            "unmodified wrap preserves pending Redo");
    TestEditor::RequireSelection(editor, 1, 3, "unmodified wrap preserves partial selection");
    TestEditor::ResetStream(editor, "a  b\nc  d", 1, 4);
    editor->Send(Message::AddSelection, 9, 6);
    editor->Send(Message::SetMainSelection, 0);
    editor->WrapToColumn(80);
    Require(TestEditor::Text(editor) == "a b\nc  d" && editor->Send(Message::GetSelections) == 1,
            "wrap uses main selection and repairs to one selection");
    TestEditor::RequireSelection(editor, 0, 3, "main wrap expands first line only");
    struct Feedback final : wxModalDialogHook {
        Feedback() { Register(); }
        int Enter(wxDialog* dialog) override {
            if (dialog->GetTitle() == "Column Wrap") return wxID_NONE;
            ++warnings;
            for (auto* window : wxTopLevelWindows) {
                auto* wrap = wxDynamicCast(window, wxDialog);
                if (wrap && wrap->GetTitle() == "Column Wrap" && wrap->IsShown()) wrapVisible = true;
            }
            return wxID_OK;
        }
        int warnings = 0;
        bool wrapVisible = false;
    } feedback;
    for (bool reverse : {false, true}) {
        TestEditor::ResetStream(editor, "a  b\nc  d");
        editor->Send(Message::SetRectangularSelectionAnchor, reverse ? 9 : 1);
        editor->Send(Message::SetRectangularSelectionCaret, reverse ? 1 : 9);
        Dialog(frame, "2", "2", DialogAction::Accept);
        Require(TestEditor::Text(editor) == "a  b\nc  d" && editor->Send(Message::SelectionIsRectangle) &&
                    !editor->Send(Message::CanUndo),
                "rectangle wrap warns without editing");
        Require(editor->Send(Message::GetRectangularSelectionAnchor) == (reverse ? 9 : 1) &&
                    editor->Send(Message::GetRectangularSelectionCaret) == (reverse ? 1 : 9),
                "rectangle wrap keeps original endpoints");
    }
    Require(feedback.warnings == 2 && !feedback.wrapVisible, "rectangle warning follows closure of boundary dialog");
    TestEditor::ResetStream(editor, "a b", 1, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    for (const auto* name : {"Split Lines", "Column Wrap..."})
        Require(!frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", name)),
                "original wrap menus require a selection");
    editor->Send(Message::SetSavePoint);
    Require(frame->New(), "new document resets wrap recall");
    TestEditor::ResetStream(editor, "a b", 0, 3);
    Dialog(frame, "80", "2", DialogAction::Cancel);
    std::puts(
        "PASS: read-only, rectangle warnings/order, multiple selections, empty selection, pending Redo and New recall");
}
void SplitCases(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto width = editor->Send(Message::TextWidth, 32, reinterpret_cast<intptr_t>("one two ")) + 1;
    editor->SetClientSize(static_cast<int>(width), 200);
    wxYield();
    TestEditor::ResetStream(editor, "one two three", 5, 1, Scintilla::EndOfLine::CrLf);
    Split(frame);
    Require(TestEditor::Text(editor) == "one two \r\nthree",
            "split expands partial target to measured logical line using CRLF");
    TestEditor::RequireSelection(editor, 5, 1, "break beyond partial selection leaves reversed endpoints");
    TestEditor::ResetStream(editor, "one two three\none two three", 0, 14, Scintilla::EndOfLine::Cr);
    Split(frame);
    Require(TestEditor::Text(editor) == "one two \rthree\none two \rthree",
            "split includes logical line at selected end-at-line-start");
    TestEditor::RequireSelection(editor, 0, 15, "only breaks preceding caret shift selection end");
    TestEditor::ResetStream(editor, "one two three\none two three", 1, 5, Scintilla::EndOfLine::Lf);
    editor->Send(Message::AddSelection, 27, 14);
    editor->Send(Message::SetMainSelection, 0);
    Split(frame);
    Require(TestEditor::Text(editor) == "one two \nthree\none two three" && editor->Send(Message::GetSelections) == 2,
            "split uses only main target and keeps multiple selections");
    TestEditor::RequireSelection(editor, 1, 5, "split retains main partial endpoints");
    Require(editor->Send(Message::GetSelectionNAnchor, 1) == 15 && editor->Send(Message::GetSelectionNCaret, 1) == 28,
            "split adjusts secondary selection for preceding insertion");
    TestEditor::ResetStream(editor, "one two three", 0, 13);
    editor->Send(Message::SetReadOnly, 1);
    Split(frame);
    Require(TestEditor::Text(editor) == "one two three" && !editor->Send(Message::CanUndo),
            "read-only core split protects text");
    TestEditor::RequireSelection(editor, 0, 13, "read-only split retains endpoints");
    TestEditor::ResetStream(editor, "one two three", 1, 1);
    editor->SplitLines();
    Require(TestEditor::Text(editor) == "one two \nthree", "direct empty-target core split processes caret line");
    TestEditor::ResetStream(editor, "one two three", 0, 13);
    NativeKeys::Shortcut(frame, 'I', true);
    Require(TestEditor::Text(editor) == "one two \nthree", "native Ctrl+I reaches Split Lines");
    for (const auto modifiers : {std::pair{false, true}, std::pair{true, true}}) {
        TestEditor::ResetStream(editor, "one two three", 0, 13);
        NativeKeys::Shortcut(frame, 'I', true, modifiers.first, modifiers.second);
        Require(TestEditor::Text(editor) == "one two three" && !editor->Send(Message::CanUndo),
                "extra shortcut modifiers do not invoke Split Lines");
    }
    TestEditor::ResetStream(editor, "one two three", 0, 13);
    NativeKeys::Shortcut(frame, 'I', true, true);
    Require(TestEditor::Text(editor) == "One Two Three", "Ctrl+Alt+I retains original Title Case routing");
    TestEditor::ResetStream(editor, "one two three", 0, 13);
    editor->SetClientSize(static_cast<int>(width * 2), 200);
    Split(frame);
    Require(TestEditor::Text(editor) == "one two three", "resizing changes rendered split width");
    editor->Send(Message::SetZoom, 200);
    editor->Refresh();
    editor->Update();
    wxYield();
    Require(editor->Send(Message::TextWidth, 32, reinterpret_cast<intptr_t>("one two ")) > width,
            "zoom updates original core text measurement");
    Split(frame);
    Require(TestEditor::Text(editor) == "one two \nthree", "zoom changes core measurement for split");
    editor->Send(Message::SetZoom, 100);
    editor->Refresh();
    editor->Update();
    for (const auto& [before, prefix, after] : {
             std::tuple{"中 中 中 中", "中 中 ", "中 中 \n中 中"},
             std::tuple{"😀 😀 😀 😀", "😀 😀 ", "😀 😀 \n😀 😀"},
         }) {
        TestEditor::ResetStream(editor, before, 0, static_cast<intptr_t>(std::string_view(before).size()),
                                Scintilla::EndOfLine::Lf);
        const auto measured = editor->Send(Message::TextWidth, 32, reinterpret_cast<intptr_t>(prefix));
        editor->SetClientSize(static_cast<int>(measured + 1), 200);
        wxYield();
        Split(frame);
        Require(TestEditor::Text(editor) == after,
                "rendered Unicode/emoji split preserves complete characters and literal word breaks");
        TestEditor::RequireSelection(editor, 0, static_cast<intptr_t>(std::string_view(after).size()),
                                     "Unicode split retains selected result endpoints");
    }
    TestEditor::ResetStream(editor, "one two three\none two three", 0, 0);
    editor->SetClientSize(static_cast<int>(width), 200);
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 19);
    Split(frame);
    Require(TestEditor::Text(editor) == "one two three\none two \nthree" && editor->Send(Message::SelectionIsRectangle),
            "rectangle split forwards main line target without warning");
    Require(editor->Send(Message::GetRectangularSelectionAnchor) == 1 &&
                editor->Send(Message::GetRectangularSelectionCaret) == 19,
            "rectangle split retains endpoints before inserted break");
    frame->SetClientSize(900, 600);
    std::puts("PASS: native Ctrl+I exact modifiers, rendered resizing/zoom, CRLF/CR, partial/end-at-line-start, "
              "multiple and read-only targets");
}
void SavedBytes(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("line-wrap");
    std::filesystem::create_directory(directory);
    struct Fixture {
        const char* name;
        FileEncoding encoding;
        std::string before;
        std::string split;
        std::string wrap;
    };
    const Fixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbfone two three\n中  文", "\xef\xbb\xbfone two \nthree\n中  文",
         "\xef\xbb\xbfone\ntwo\nthree\n中 文"},
        {"gbk", FileEncoding::Gbk, "one two three\n\xd6\xd0  \xce\xc4", "one two \nthree\n\xd6\xd0  \xce\xc4",
         "one\ntwo\nthree\n\xd6\xd0 \xce\xc4"},
        {"utf16", FileEncoding::Utf16Le,
         std::string("\xff\xfe"
                     "o\0n\0e\0 \0t\0w\0o\0 \0t\0h\0r\0e\0e\0",
                     28),
         std::string("\xff\xfe"
                     "o\0n\0e\0 \0t\0w\0o\0 \0\n\0t\0h\0r\0e\0e\0",
                     30),
         std::string("\xff\xfe"
                     "o\0n\0e\0\n\0t\0w\0o\0\n\0t\0h\0r\0e\0e\0",
                     28)},
    };
    for (const auto& fixture : fixtures) {
        for (bool split : {false, true}) {
            const auto path = directory / (std::string(fixture.name) + (split ? "-split.txt" : "-wrap.txt"));
            TestFiles::WriteBytes(path, fixture.before);
            editor->Send(Message::SetSavePoint);
            Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                    "open encoded fixture via application");
            editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
            editor->Send(Message::SelectAll);
            if (split) {
                const auto width = editor->Send(Message::TextWidth, 32, reinterpret_cast<intptr_t>("one two ")) + 1;
                editor->SetClientSize(static_cast<int>(width), 200);
                wxYield();
                Split(frame);
            } else
                Dialog(frame, "80", "4", DialogAction::Accept);
            const auto& after = split ? fixture.split : fixture.wrap;
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == after,
                    "independent saved bytes retain original encoding/BOM and generated EOLs");
            editor->Send(Message::Undo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                    "saved Undo restores independent input bytes");
            editor->Send(Message::Redo);
            Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == after,
                    "saved Redo restores independent result bytes");
            Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                    "independently reopen saved encoded file");
        }
    }
    std::puts("PASS: saved/reopened UTF-8 BOM, GBK, UTF-16LE BOM literal bytes, saved Undo/Redo for both commands");
}
void EffectiveBoundary(EditorFrame* frame) {
    const auto path = TestFiles::TemporaryDirectory() / "wrap-file-variables.txt";
    for (const auto& [text, expected] :
         {std::pair{"fill-column: 7\na  b", "7"}, std::pair{"fill-column: -1\na  b", "0"},
          std::pair{"fill-column: 9999\na  b", "4096"}}) {
        TestFiles::WriteBytes(path, text);
        frame->Editor()->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                "open effective file boundary fixture");
        frame->Editor()->Send(Message::SelectAll);
        Dialog(frame, expected, "8", DialogAction::Accept);
        Dialog(frame, "8", "9", DialogAction::Cancel);
    }
    SettingsStore settings;
    settings.values.longLinesLimit = 91;
    auto* other = new EditorFrame(&settings);
    other->Show();
    TestEditor::ResetStream(other->Editor(), "a  b", 0, 4);
    Dialog(other, "91", "2", DialogAction::Cancel);
    other->Destroy();
    std::puts("PASS: default/effective LongLinesLimit, bounded fill-column, accepted recall and Open reset");
}
class WrapApp final : public wxApp {
public:
    bool OnInit() override {
        const bool probe = argc == 3 && argv[1] == "--boundary-probe";
        const int expected = probe ? wxAtoi(argv[2]) : 80;
        if (probe) settings_ = std::make_unique<SettingsStore>();
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame, probe, expected] {
            try {
                if (probe) {
                    TestEditor::ResetStream(frame->Editor(), "a b", 0, 3);
                    Dialog(frame, wxString::Format("%d", expected), "2", DialogAction::Cancel);
                    Require(settings_->Save(true), "save imported long-line boundary through original settings store");
                } else {
                    SplitFixture(frame);
                    ColumnFixture(frame);
                    ColumnCases(frame->Editor());
                    DialogCases(frame);
                    SelectionAndProtection(frame);
                    SplitCases(frame);
                    SavedBytes(frame);
                    EffectiveBoundary(frame);
                }
                std::puts("PASS: original Split Lines");
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
wxIMPLEMENT_APP(WrapApp);
