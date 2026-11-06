#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/textctrl.h>
#include <wx/hyperlink.h>
#include <wx/msgdlg.h>
#include <chrono>
#include <functional>
#ifdef __WXMSW__
#include <commctrl.h>
#endif

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void NumberedLines(EditorControl* editor) {
    TestEditor::ResetStream(editor, "zero\none\n\nthree\nfour", 6, 16);
    Require(editor->ModifyLines("$(L):", ":$(N)", true), "numbered stream lines are supported");
    Require(TestEditor::Text(editor) == "zero\n2:one:1\n\n4:three:2\nfour",
            "document numbering advances across skipped lines while continuous counters advance on insertion");
    TestEditor::RequireSelection(editor, 5, 24, "partial selection expands to participating whole lines");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "zero\none\n\nthree\nfour" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "prefix and suffix undo together to save point");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "zero\n2:one:1\n\n4:three:2\nfour", "redo restores numbered lines");
}
void Substitutions(EditorControl* editor) {
    struct Fixture {
        const char* token;
        const char* expected;
    };
    const Fixture fixtures[] = {
        {"$(L):", " 1:a\n\n 3:b\n 4:c\n 5:d\n 6:e\n 7:f\n 8:g\n 9:h\n10:i\n11:j"},
        {"$(0L):", "01:a\n\n03:b\n04:c\n05:d\n06:e\n07:f\n08:g\n09:h\n10:i\n11:j"},
        {"$(N):", " 1:a\n\n 2:b\n 3:c\n 4:d\n 5:e\n 6:f\n 7:g\n 8:h\n 9:i\n10:j"},
        {"$(0N):", "01:a\n\n02:b\n03:c\n04:d\n05:e\n06:f\n07:g\n08:h\n09:i\n10:j"},
        {"$(I):", " 0:a\n\n 1:b\n 2:c\n 3:d\n 4:e\n 5:f\n 6:g\n 7:h\n 8:i\n 9:j"},
        {"$(0I):", "00:a\n\n01:b\n02:c\n03:d\n04:e\n05:f\n06:g\n07:h\n08:i\n09:j"},
    };
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(editor, "a\n\nb\nc\nd\ne\nf\ng\nh\ni\nj", 0, 20);
        editor->ModifyLines(fixture.token, "", true);
        Require(TestEditor::Text(editor) == fixture.expected,
                "all six tokens retain original padding and skipped-line width");
    }
    TestEditor::ResetStream(editor, "a\nb\nc", 0, 3);
    editor->ModifyLines("$(bad):$(N):$(0L)", "$(I):$(N)");
    Require(
        TestEditor::Text(editor) == "$(bad):1:$(0L)a0:$(N)\n$(bad):2:$(0L)b1:$(N)\nc",
        "only the first recognized token in each field substitutes; remaining tokens and unknown text stay literal");
    TestEditor::ResetStream(editor, " \n\t\n", 0, 4);
    editor->ModifyLines("[", "]", true);
    Require(TestEditor::Text(editor) == "[ ]\n[\t]\n", "Skip Empty Lines still modifies whitespace-only lines");
    std::puts("PASS: six substitutions, padding, first recognized token and empty-line counter rules");
}
void ScopeAndMultiline(EditorControl* editor) {
    TestEditor::ResetStream(editor, "head\ntail", 7, 7);
    editor->ModifyLines("[", "]");
    Require(TestEditor::Text(editor) == "head\n[tail]", "caret-only command modifies just the current logical line");
    TestEditor::RequireSelection(editor, 8, 8, "caret follows original-core insertion adjustment");
    TestEditor::ResetStream(editor, "ab\ncd\nend", 6, 1, Scintilla::EndOfLine::Lf);
    editor->ModifyLines("P\r\n", "\r\nS");
    Require(TestEditor::Text(editor) == "P\nab\nS\nP\ncd\nS\nend",
            "multiline prefix and suffix advance destination lines without revisiting inserted lines");
    TestEditor::RequireSelection(editor, 5, 0,
                                 "multiline result retains original unadjusted whole-line extent and reversal");
    struct EolFixture {
        Scintilla::EndOfLine mode;
        const char* expected;
        intptr_t selectionEnd;
    };
    const EolFixture fixtures[] = {
        {Scintilla::EndOfLine::CrLf, "P\r\nA\rB\nx\nP\r\nA\rB\ny", 5},
        {Scintilla::EndOfLine::Lf, "P\nA\rB\nx\nP\nA\rB\ny", 4},
        {Scintilla::EndOfLine::Cr, "P\rA\rB\nx\nP\rA\rB\ny", 4},
    };
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(editor, "x\ny", 0, 3, fixture.mode);
        editor->ModifyLines("P\r\nA\rB\n", "");
        Require(TestEditor::Text(editor) == fixture.expected,
                "only CRLF pairs convert, and lone CR/LF count as inserted lines");
        TestEditor::RequireSelection(editor, 0, fixture.selectionEnd,
                                     "selection extent uses original participating line count");
    }
    TestEditor::ResetStream(editor, "a\n", 0, 2);
    editor->ModifyLines("[", "]");
    Require(TestEditor::Text(editor) == "[a]\n", "line-start endpoint excludes the final empty line");
    TestEditor::RequireSelection(editor, 0, 4, "selected terminated line includes its EOL");
    TestEditor::ResetStream(editor, "a\n", 2, 2);
    editor->ModifyLines("[", "]");
    Require(TestEditor::Text(editor) == "a\n[]", "caret at EOF modifies the final empty logical line");
    TestEditor::ResetStream(editor, "");
    editor->ModifyLines("$(L)", "$(N)");
    Require(TestEditor::Text(editor) == "11", "an empty document has one participating caret line");
    std::puts(
        "PASS: caret, partial/reversed selections, line-start exclusion, multiline destination and EOL accounting");
}
class DialogDriver final : public wxModalDialogHook {
public:
    explicit DialogDriver(std::function<void(wxDialog*)> action) : action_(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Modify Lines") wxTheApp->CallAfter([this, dialog] { action_(dialog); });
        return wxID_NONE;
    }

private:
    std::function<void(wxDialog*)> action_;
};
void Visit(EditorFrame* frame, const std::function<void(wxDialog*)>& action, bool native = false) {
    const auto id = frame->GetMenuBar()->FindMenuItem("Edit", "Modify Lines...");
    Require(id != wxNOT_FOUND, "Edit menu exposes original Modify Lines dialog");
    bool opened = false;
    bool released = false;
    DialogDriver driver([&](wxDialog* dialog) {
#ifndef __WXMSW__
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (native && !released && std::chrono::steady_clock::now() < deadline) {
            wxYield();
            wxMilliSleep(10);
        }
        Require(!native || released, "opening native shortcut releases before interacting with the dialog");
#endif
        opened = true;
        action(dialog);
    });
    if (native)
        NativeKeys::Shortcut(frame, 'M', false, true, false, &released);
    else
        NativeSearch::Command(frame, id);
    Require(opened, "requested native Modify Lines dialog opens");
}
wxTextCtrl* Field(wxDialog* dialog, const char* name) {
    auto* field = wxDynamicCast(wxWindow::FindWindowByName(name, dialog), wxTextCtrl);
    Require(field, "native multiline field exists");
    return field;
}
wxCheckBox* Skip(wxDialog* dialog) {
    auto* option = wxDynamicCast(wxWindow::FindWindowByName("Skip empty lines", dialog), wxCheckBox);
    Require(option, "native Skip Empty Lines option exists");
    return option;
}
void InputReturn(wxTextCtrl* field, bool shift = false, bool control = false, bool keypad = false) {
    Require(field->IsShownOnScreen(), "Return starts in an open native multiline field");
    field->SetFocus();
    wxYield();
#ifdef __WXMSW__
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read owned test-thread keyboard state for multiline input");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    modified[VK_SHIFT] = shift ? 0x80 : 0;
    modified[VK_CONTROL] = control ? 0x80 : 0;
    modified[VK_MENU] = 0;
    Require(SetKeyboardState(modified), "set owned multiline input modifiers");
    const auto handle = static_cast<HWND>(field->GetHandle());
    const LPARAM extended = keypad ? (LPARAM{KF_EXTENDED} << 16) : 0;
    PostMessageW(handle, WM_KEYDOWN, VK_RETURN, extended | 1);
    PostMessageW(handle, WM_KEYUP, VK_RETURN, extended | 0xc0000001);
    wxYield();
    SetKeyboardState(previous);
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "multiline input uses private native keys");
    bool received = false;
    NativeKeys::KeyRelease observer(keypad ? WXK_NUMPAD_ENTER : WXK_RETURN, received, nullptr);
    {
        wxFFile file(focusFile, "wb");
        file.Write(wxString("key:") + (control ? "ctrl+" : "") + (shift ? "shift+" : "") +
                   (keypad ? "KP_Enter" : "Return"));
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    // Unshifted Return closes this modal receiver before its parent is enabled
    // again. Shift+Return keeps the field open and must deliver its release.
    const auto closed = [&] { return !shift && !field->IsShownOnScreen(); };
    while (!received && !closed() && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(received || closed(), "native Return releases or unshifted Return closes its dialog");
#endif
    wxYield();
}
void ClickSubstitution(wxDialog* dialog, const char* token) {
    auto* link = wxDynamicCast(wxWindow::FindWindowByName(token, dialog), wxHyperlinkCtrl);
    Require(link, "all six original substitution links exist");
    link->SetFocus();
    wxYield();
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(link->GetHandle());
    NMLINK notification{};
    notification.hdr.hwndFrom = handle;
    notification.hdr.idFrom = GetDlgCtrlID(handle);
    notification.hdr.code = NM_CLICK;
    SendMessageW(static_cast<HWND>(dialog->GetHandle()), WM_NOTIFY, notification.hdr.idFrom,
                 reinterpret_cast<LPARAM>(&notification));
#else
    gtk_button_clicked(GTK_BUTTON(link->GetHandle()));
#endif
    wxYield();
}
void Dialog(EditorFrame* frame) {
    const auto id = frame->GetMenuBar()->FindMenuItem("Edit", "Modify Lines...");
    Require(id != wxNOT_FOUND, "Edit menu exposes original Modify Lines dialog");
    bool observed = false;
    DialogDriver driver([&](wxDialog* dialog) {
        auto* prefix = wxDynamicCast(wxWindow::FindWindowByName("Prefix text to lines", dialog), wxTextCtrl);
        auto* suffix = wxDynamicCast(wxWindow::FindWindowByName("Append text to lines", dialog), wxTextCtrl);
        auto* skip = wxDynamicCast(wxWindow::FindWindowByName("Skip empty lines", dialog), wxCheckBox);
        observed = prefix && suffix && skip && prefix->IsMultiLine() && suffix->IsMultiLine() &&
                   prefix->GetValue().empty() && suffix->GetValue().empty() && !skip->GetValue() &&
                   (dialog->GetWindowStyle() & wxRESIZE_BORDER);
        if (prefix) prefix->SetValue("$(N):");
        if (suffix) suffix->SetValue(":$(I)");
        NativeSearch::Click(dialog, wxID_OK);
    });
    TestEditor::ResetStream(frame->Editor(), "x", 1, 1);
    NativeSearch::Command(frame, id);
    Require(observed && TestEditor::Text(frame) == "1:x:0", "native multiline dialog applies independent counters");
}
void NativeControls(EditorFrame* frame) {
    TestEditor::ResetStream(frame->Editor(), "x\n\nz", 0, 4, Scintilla::EndOfLine::Lf);
    bool tabReachedSkip = false;
    Visit(
        frame,
        [&](wxDialog* dialog) {
            auto* prefix = Field(dialog, "Prefix text to lines");
            auto* suffix = Field(dialog, "Append text to lines");
            Require(prefix->GetValue() == "$(N):" && suffix->GetValue() == ":$(I)" && !Skip(dialog)->GetValue() &&
                        prefix->HasFocus(),
                    "Alt+M restores accepted values and initially focuses the prefix");
            for (auto* field : {prefix, suffix}) {
                field->SetFocus();
                wxYield();
                for (const bool keypad : {false, true}) {
                    field->SetValue("line");
                    field->SetInsertionPointEnd();
                    InputReturn(field, true, false, keypad);
                    Require(field->GetValue() == "line\n" && dialog->IsShown(),
                            "native Shift+Return and Shift+keypad Enter insert a line break in each field");
                }
                auto* other = field == prefix ? suffix : prefix;
                for (const char* token : {"$(L)", "$(0L)", "$(N)", "$(0N)", "$(I)", "$(0I)"}) {
                    field->SetFocus();
                    wxYield();
                    field->SetValue("replace all this");
                    field->SetSelection(2, 4);
                    other->SetValue("keep other field");
                    ClickSubstitution(dialog, token);
                    Require(field->GetValue() == token && other->GetValue() == "keep other field" && field->HasFocus(),
                            "native substitution link replaces the entire focused field and restores its focus");
                }
            }
            suffix->SetFocus();
            wxYield();
#ifdef __WXMSW__
            NativeKeys::DialogNavigation(dialog, VK_TAB);
            wxYield();
#else
            wxString focusFile;
            Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "Tab uses private native input");
            {
                wxFFile file(focusFile, "wb");
                file.Write("key:Tab");
            }
            const auto tabDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!Skip(dialog)->HasFocus() && std::chrono::steady_clock::now() < tabDeadline) {
                wxYield();
                wxMilliSleep(10);
            }
#endif
            tabReachedSkip = Skip(dialog)->HasFocus();
            if (!tabReachedSkip) {
                NativeSearch::Click(dialog, wxID_CANCEL);
                return;
            }
            prefix->SetValue(wxString::FromUTF8("中\n$(N):"));
            suffix->SetValue(wxString::FromUTF8(":$(I)\n文"));
            NativeSearch::Toggle(Skip(dialog), true);
            NativeSearch::Click(dialog, wxID_OK);
        },
        true);
    Require(tabReachedSkip, "native Tab skips substitution links as in the original dialog");
    Require(TestEditor::Text(frame) == "中\n1:x:0\n文\n\n中\n2:z:1\n文",
            "native multiline fields preserve inserted-line accounting, skipped empties and independent counters");
    Require(frame->Editor()->HasFocus(), "closing Modify Lines returns focus to the editor");
    TestEditor::RequireSelection(frame->Editor(), 0, 14, "native multiline selection uses original line extent");
    bool opened = false;
    DialogDriver modifierDriver([&](wxDialog* dialog) {
        opened = true;
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    NativeKeys::Shortcut(frame, 'M', false, true, true);
    NativeKeys::Shortcut(frame, 'M', true, true);
    Require(!opened, "Alt+Shift+M and Ctrl+Alt+M do not invoke Modify Lines");
    std::puts("PASS: native Alt+M modifiers, Shift+Return/keypad Enter in both fields, six links and focus");
}
void ReturnAndCancel(EditorFrame* frame) {
    auto* editor = frame->Editor();
    for (const bool keypad : {false, true}) {
        for (const char* name : {"Prefix text to lines", "Append text to lines"}) {
            for (const bool control : {false, true}) {
                TestEditor::ResetStream(editor, "x", 1, 1, Scintilla::EndOfLine::Lf);
                bool closed = false;
                Visit(frame, [&](wxDialog* dialog) {
                    Field(dialog, "Prefix text to lines")->SetValue("[");
                    Field(dialog, "Append text to lines")->SetValue("]");
                    NativeSearch::Toggle(Skip(dialog), false);
                    InputReturn(Field(dialog, name), false, control, keypad);
                    closed = !dialog->IsShown();
                    if (!closed) NativeSearch::Click(dialog, wxID_CANCEL);
                });
                Require(closed && TestEditor::Text(editor) == "[x]",
                        "native Return or keypad Enter without Shift applies and closes from either field");
                TestEditor::RequireSelection(editor, 2, 2, "Return acceptance preserves original caret adjustment");
                Require(editor->HasFocus(), "Return acceptance restores native editor focus");
                editor->Send(Message::Undo);
                Require(TestEditor::Text(editor) == "x" && !editor->Send(Message::CanUndo) &&
                            !editor->Send(Message::GetModify),
                        "Return acceptance is one undo action back to the save point");
                Visit(frame, [&](wxDialog* dialog) {
                    Require(Field(dialog, "Prefix text to lines")->GetValue() == "[" &&
                                Field(dialog, "Append text to lines")->GetValue() == "]" && !Skip(dialog)->GetValue(),
                            "Return accepts prefix, suffix and checkbox recall");
                    Field(dialog, name)->SetValue("discard");
                    NativeSearch::Click(dialog, wxID_CANCEL);
                    closed = !dialog->IsShown();
                    if (!closed) NativeSearch::Click(dialog, wxID_CANCEL);
                });
                Require(closed && TestEditor::Text(editor) == "x" && editor->Send(Message::CanRedo) &&
                            !editor->Send(Message::CanUndo),
                        "native cancellation closes from either field and retains document and pending redo");
            }
        }
    }
    std::puts("PASS: native Return/keypad Enter with or without Ctrl apply and recall; cancellation preserves history");
}
void NativeEolAndRecall(EditorFrame* frame) {
    struct Fixture {
        Scintilla::EndOfLine mode;
        const char* expected;
    };
    const Fixture fixtures[] = {{Scintilla::EndOfLine::CrLf, "中\r\nx\r\n文"},
                                {Scintilla::EndOfLine::Lf, "中\nx\n文"},
                                {Scintilla::EndOfLine::Cr, "中\rx\r文"}};
    bool first = true;
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(frame->Editor(), "x", 1, 1, fixture.mode);
        Visit(frame, [&](wxDialog* dialog) {
            if (first) {
                Require(Skip(dialog)->GetValue() &&
                            Field(dialog, "Prefix text to lines")->GetValue() == wxString::FromUTF8("中\n$(N):") &&
                            Field(dialog, "Append text to lines")->GetValue() == wxString::FromUTF8(":$(I)\n文"),
                        "accepted multiline values and enabled Skip Empty Lines are recalled");
                first = false;
            }
            Field(dialog, "Prefix text to lines")->SetValue(wxString::FromUTF8("中\n"));
            Field(dialog, "Append text to lines")->SetValue(wxString::FromUTF8("\n文"));
            NativeSearch::Toggle(Skip(dialog), false);
            NativeSearch::Click(dialog, wxID_OK);
        });
        Require(TestEditor::Text(frame) == fixture.expected,
                "native boundary constructs document CRLF/LF/CR from multiline inputs");
        frame->Editor()->Send(Message::Undo);
        Require(TestEditor::Text(frame) == "x" && !frame->Editor()->Send(Message::GetModify) &&
                    !frame->Editor()->Send(Message::CanUndo),
                "native OK forms one undo action");
    }
    Visit(frame, [&](wxDialog* dialog) {
        Require(Field(dialog, "Prefix text to lines")->GetValue() == wxString::FromUTF8("中\n") &&
                    Field(dialog, "Append text to lines")->GetValue() == wxString::FromUTF8("\n文") &&
                    !Skip(dialog)->GetValue(),
                "accepted multiline values and checkbox are recalled");
        Field(dialog, "Prefix text to lines")->SetValue("discard");
        Field(dialog, "Append text to lines")->SetValue("discard");
        NativeSearch::Toggle(Skip(dialog), true);
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    Require(TestEditor::Text(frame) == "x" && !frame->Editor()->Send(Message::CanUndo) &&
                frame->Editor()->Send(Message::CanRedo) && !frame->Editor()->Send(Message::GetModify),
            "Cancel preserves document, save point and pending redo");
    Visit(frame, [&](wxDialog* dialog) {
        Require(Field(dialog, "Prefix text to lines")->GetValue() == wxString::FromUTF8("中\n") &&
                    !Skip(dialog)->GetValue(),
                "Cancel retains previously accepted values");
        Field(dialog, "Prefix text to lines")->Clear();
        Field(dialog, "Append text to lines")->Clear();
        NativeSearch::Click(dialog, wxID_OK);
    });
    Require(TestEditor::Text(frame) == "x" && !frame->Editor()->Send(Message::CanUndo) &&
                frame->Editor()->Send(Message::CanRedo),
            "both-empty OK closes and retains pending redo");
    Require(frame->New(), "New retains current-window dialog recall");
    Visit(frame, [&](wxDialog* dialog) {
        Require(Field(dialog, "Prefix text to lines")->IsEmpty() && Field(dialog, "Append text to lines")->IsEmpty(),
                "accepted empty fields replace prior recall across New");
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    std::puts("PASS: native EOL boundary, accepted recall, Cancel, both-empty OK, New and history");
}
class RectangleFeedback final : public wxModalDialogHook {
public:
    RectangleFeedback() { Register(); }
    int Enter(wxDialog* dialog) override {
        auto* warning = wxDynamicCast(dialog, wxMessageDialog);
        if (!warning) return wxID_NONE;
        Require(warning->GetMessage() == "This operation can't be performed within a rectangular selection.",
                "rectangle feedback retains the original message");
        ++count;
        auto* modify = wxDynamicCast(dialog->GetParent(), wxDialog);
        applyBeforeClose = modify && modify->GetTitle() == "Modify Lines" && modify->IsShown();
        return wxID_OK;
    }
    int count = 0;
    bool applyBeforeClose = false;
};
void Protection(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "alpha\nbeta", 0, 5);
    editor->ModifyLines("[", "]");
    editor->Send(Message::Undo);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 0);
    editor->Send(Message::SetRectangularSelectionCaret, 8);
    Require(!editor->ModifyLines("", ""), "rectangle rejection precedes the both-empty shortcut");
    {
        RectangleFeedback feedback;
        Visit(frame, [&](wxDialog* dialog) {
            Field(dialog, "Prefix text to lines")->Clear();
            Field(dialog, "Append text to lines")->Clear();
            InputReturn(Field(dialog, "Prefix text to lines"));
        });
        Require(feedback.count == 1 && feedback.applyBeforeClose,
                "Return invokes OK and reports rectangle feedback before dialog closes");
    }
    Require(TestEditor::Text(editor) == "alpha\nbeta" && !editor->Send(Message::CanUndo) &&
                editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify) &&
                editor->Send(Message::GetRectangularSelectionAnchor) == 0 &&
                editor->Send(Message::GetRectangularSelectionCaret) == 8,
            "rectangle rejection preserves document, endpoints, history and save point");
    TestEditor::ResetStream(editor, "ab\ncd", 1, 4);
    editor->Send(Message::SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    const auto id = frame->GetMenuBar()->FindMenuItem("Edit", "Modify Lines...");
    Require(frame->GetMenuBar()->IsEnabled(id), "Modify Lines remains available for original core read-only behavior");
    Visit(frame, [&](wxDialog* dialog) {
        Field(dialog, "Prefix text to lines")->SetValue("[");
        Field(dialog, "Append text to lines")->SetValue("]");
        NativeSearch::Click(dialog, wxID_OK);
    });
    Require(TestEditor::Text(editor) == "ab\ncd" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "core read-only protection rejects insertion and preserves save point");
    TestEditor::RequireSelection(editor, 0, 5, "read-only command retains original whole-line selection repair");
    TestEditor::ResetStream(editor, "\n\n", 0, 2);
    editor->ModifyLines("[", "]", true);
    Require(TestEditor::Text(editor) == "\n\n" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
            "skipping every empty line leaves history at its save point");
    editor->Send(Message::SetSavePoint);
    Require(frame->New(), "New succeeds after read-only fixture reset");
    Visit(frame, [&](wxDialog* dialog) {
        Require(Field(dialog, "Prefix text to lines")->GetValue() == "[" &&
                    Field(dialog, "Append text to lines")->GetValue() == "]",
                "read-only OK accepts recall across New");
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    auto* other = new EditorFrame;
    other->Show();
    Visit(other, [&](wxDialog* dialog) {
        Require(Field(dialog, "Prefix text to lines")->IsEmpty() && Field(dialog, "Append text to lines")->IsEmpty() &&
                    !Skip(dialog)->GetValue(),
                "another window starts with original empty defaults");
        NativeSearch::Click(dialog, wxID_CANCEL);
    });
    other->Destroy();
    std::puts(
        "PASS: rectangle feedback before close, read-only availability/selection, empty lines and recall lifetime");
}
void SavedBytes(EditorFrame* frame) {
    const auto directory = TestFiles::TemporaryDirectory("modify-lines");
    std::filesystem::create_directory(directory);
    const TestFiles::EncodedFixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf中\r\n文",
         "\xef\xbb\xbf"
         "1:中:0\r\n2:文:1"},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\r\n\xce\xc4", "1:\xd6\xd0:0\r\n2:\xce\xc4:1"},
        {"utf16", FileEncoding::Utf16Le, std::string("\xff\xfe\x2d\x4e\r\0\n\0\x87\x65", 10),
         std::string("\xff\xfe"
                     "1\0:\0\x2d\x4e:\0"
                     "0\0\r\0\n\0"
                     "2\0:\0\x87\x65:\0"
                     "1\0",
                     26)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        frame->Editor()->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "open original encoded Modify Lines fixture");
        frame->Editor()->Send(Message::SelectAll);
        Visit(frame, [&](wxDialog* dialog) {
            Field(dialog, "Prefix text to lines")->SetValue("$(N):");
            Field(dialog, "Append text to lines")->SetValue(":$(I)");
            NativeSearch::Toggle(Skip(dialog), false);
            NativeSearch::Click(dialog, wxID_OK);
        });
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"native dialog save preserves exact UTF-8/GBK/UTF-16LE bytes and BOM",
                                         "one Undo and save restore independently read original bytes",
                                         "one Redo and save restore independently read numbered bytes"});
    }
    std::puts("PASS: independently reopened UTF-8, GBK and UTF-16LE saved bytes with Undo/Redo");
}
class ModifyLinesApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                NumberedLines(frame->Editor());
                Substitutions(frame->Editor());
                ScopeAndMultiline(frame->Editor());
                Dialog(frame);
                NativeControls(frame);
                NativeEolAndRecall(frame);
                ReturnAndCancel(frame);
                Protection(frame);
                SavedBytes(frame);
                std::puts("PASS: original Modify Lines");
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
wxIMPLEMENT_APP(ModifyLinesApp);
