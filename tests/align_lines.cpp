#include "native_search_helpers.h"
#include "settings/settings_store.h"
#include "encoded_file_helpers.h"
#include <wx/radiobut.h>
#include <wx/eventfilter.h>
#include <wx/ffile.h>
#include <wx/msgdlg.h>
#include <array>
#include <chrono>
#include <functional>
#include <vector>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Left(EditorControl* editor) {
    TestEditor::ResetStream(editor, "  one   two  \r\n    three\nTAIL", 1, 18);
    Require(editor->AlignLines(AlignmentMode::Left) == AlignmentResult::Complete, "left alignment succeeds");
    Require(TestEditor::Text(editor) == "  one two\r\n  three\nTAIL",
            "left uses minimum indentation and whole selected lines");
    TestEditor::RequireSelection(editor, 0, 19, "left expands the forward selection to complete lines");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "  one   two  \r\n    three\nTAIL" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "one Undo restores original bytes and save point");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "  one two\r\n  three\nTAIL", "Redo restores left alignment");
}
void Right(EditorControl* editor) {
    TestEditor::ResetStream(editor, "  a   b \n    c d\n  12345678", 26, 1);
    Require(editor->AlignLines(AlignmentMode::Right) == AlignmentResult::Complete, "right alignment succeeds");
    Require(TestEditor::Text(editor) == "       a b\n       c d\n  12345678",
            "right uses original extra-space and indentation calculation");
    TestEditor::RequireSelection(editor, 32, 0,
                                 "right preserves reversed selection direction through unterminated last line");
}
void Center(EditorControl* editor) {
    TestEditor::ResetStream(editor, " a b c\n a\n 12345678", 0, 19);
    Require(editor->AlignLines(AlignmentMode::Center) == AlignmentResult::Complete, "center alignment succeeds");
    Require(TestEditor::Text(editor) == "  a  b c\n    a\n 12345678",
            "center inserts odd space at the original word midpoint and drops it for one word");
    TestEditor::RequireSelection(editor, 0, 24, "center selects through unterminated EOF");
}
void Justify(EditorControl* editor) {
    TestEditor::ResetStream(editor, " a b c\n 12345678", 0, 17);
    Require(editor->AlignLines(AlignmentMode::Justify) == AlignmentResult::Complete, "justification succeeds");
    Require(TestEditor::Text(editor) == " a  b   c\n 12345678",
            "justification puts remainder spaces into rightmost gaps");
    TestEditor::RequireSelection(editor, 0, 19, "justification selects whole lines");
}
void Paragraph(EditorControl* editor) {
    TestEditor::ResetStream(editor, "a b\nc d\n\n12345678\none two", 0, 25);
    Require(editor->AlignLines(AlignmentMode::JustifyParagraph) == AlignmentResult::Complete,
            "paragraph justification succeeds");
    Require(TestEditor::Text(editor) == "a      b\nc d\n\n12345678\none two",
            "paragraph mode justifies continued lines and left-aligns short or exactly three-quarter final lines");
    TestEditor::ResetStream(editor, "123456789012\naaaa bbbbbb", 0, 24);
    editor->AlignLines(AlignmentMode::JustifyParagraph);
    Require(TestEditor::Text(editor) == "123456789012\naaaa  bbbbbb",
            "paragraph final line longer than three quarters is justified");
    TestEditor::ResetStream(editor, "a    b\n\ntail", 1, 5);
    editor->AlignLines(AlignmentMode::JustifyParagraph);
    Require(TestEditor::Text(editor) == "a    b\n\ntail",
            "single-line selection forces justification even at paragraph end");
    TestEditor::ResetStream(editor, "12345678\na b\nx y", 0, 13);
    editor->AlignLines(AlignmentMode::JustifyParagraph);
    Require(TestEditor::Text(editor) == "12345678\na      b\nx y",
            "paragraph continuation uses the actual next line outside selection");
}
void WaitFor(const std::function<bool()>& condition, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(condition(), message);
}
#ifndef __WXMSW__
class AlignmentKeyRelease final : public wxEventFilter {
public:
    explicit AlignmentKeyRelease(bool& released) : released_(released) { wxEvtHandler::AddFilter(this); }
    ~AlignmentKeyRelease() override { wxEvtHandler::RemoveFilter(this); }
    int FilterEvent(wxEvent& event) override {
        if (event.GetEventType() == wxEVT_KEY_UP && static_cast<wxKeyEvent&>(event).GetKeyCode() == 'J')
            released_ = true;
        return Event_Skip;
    }

private:
    bool& released_;
};
#endif
void NativeAltJ(EditorFrame* frame, bool& released) {
    auto* editor = frame->Editor();
    editor->SetFocus();
    wxYield();
#ifdef __WXMSW__
    released = true;
    Require(NativeSearch::WindowsShortcut(editor, 'J', {{VK_MENU, 0x80}, {VK_CONTROL, 0}, {VK_SHIFT, 0}}),
            "native Alt+J accelerator is handled");
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "Alt+J uses an isolated native display");
    {
        wxFFile file(focusFile, "wb");
        file.Write(frame->IsActive() ? "focus:wxnotepad" : "front:wxnotepad");
    }
    WaitFor(
        [&] {
            wxString pending;
            wxFFile file(focusFile, "rb");
            file.ReadAll(&pending);
            return pending.empty() && frame->IsActive() && editor->HasFocus();
        },
        "private display activates and focuses the owned editor");
    AlignmentKeyRelease observer(released);
    {
        wxFFile file(focusFile, "wb");
        file.Write("key:alt+j");
    }
    WaitFor([&] { return released; }, "actual Alt+J key release reaches owned editor or dialog");
#endif
}
class AlignmentDialogDriver final : public wxModalDialogHook {
public:
    AlignmentDialogDriver(AlignmentMode expected, AlignmentMode chosen, int button, bool* released = nullptr)
        : expected_(expected), chosen_(chosen), button_(button), released_(released) {
        Register();
    }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() != "Align Lines") return wxID_NONE;
        ++count;
        wxTheApp->CallAfter([this, dialog] {
            if (released_)
                WaitFor([this] { return *released_; }, "opening shortcut releases before the native dialog responds");
            const std::array<const char*, 5> names{"Left", "Right", "Center", "Justify", "Justify (Paragraph mode)"};
            bool valid = true;
            wxRadioButton* chosen = nullptr;
            for (size_t i = 0; i < names.size(); ++i) {
                auto* radio = wxDynamicCast(wxWindow::FindWindowByName(names[i], dialog), wxRadioButton);
                valid = valid && radio && radio->GetValue() == (i == static_cast<size_t>(expected_));
                if (i == static_cast<size_t>(chosen_)) chosen = radio;
            }
            observed = valid;
            if (chosen) {
#ifdef __WXMSW__
                SendMessageW(static_cast<HWND>(chosen->GetHandle()), BM_CLICK, 0, 0);
#elif defined(__WXGTK__)
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chosen->GetHandle()), TRUE);
#endif
            }
            NativeSearch::Click(dialog, button_);
        });
        return wxID_NONE;
    }
    bool observed = false;
    int count = 0;

private:
    AlignmentMode expected_;
    AlignmentMode chosen_;
    int button_;
    bool* released_;
};
void Dialog(EditorFrame* frame, AlignmentMode expected, AlignmentMode chosen, int button, bool native = false) {
    const int id = frame->GetMenuBar()->FindMenuItem("Edit", "Align Lines...");
    Require(id != wxNOT_FOUND, "Edit menu exposes original Align Lines dialog");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(id),
            "original alignment menu remains available for the current selection and protection state");
    bool released = false;
    AlignmentDialogDriver driver(expected, chosen, button, native ? &released : nullptr);
    if (native)
        NativeAltJ(frame, released);
    else
        NativeSearch::Command(frame, id);
    Require(driver.count == 1 && driver.observed,
            "native dialog exposes five exclusive choices and accepted-choice recall");
}
void NativeDialog(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "a b\n12345678", 0, 12);
    Dialog(frame, AlignmentMode::Left, AlignmentMode::Right, wxID_CANCEL, true);
    Require(TestEditor::Text(editor) == "a b\n12345678" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "Cancel preserves document state");
    Dialog(frame, AlignmentMode::Left, AlignmentMode::Right, wxID_OK);
    Require(TestEditor::Text(editor) == "     a b\n12345678", "native Right choice applies on OK");
    editor->Send(Message::Undo);
    Dialog(frame, AlignmentMode::Right, AlignmentMode::Center, wxID_CANCEL);
    Require(TestEditor::Text(editor) == "a b\n12345678" && editor->Send(Message::CanRedo) &&
                !editor->Send(Message::GetModify),
            "Cancel preserves previous accepted mode and pending Redo");
    Dialog(frame, AlignmentMode::Right, AlignmentMode::Left, wxID_OK);
}
class AlignmentFeedback final : public wxModalDialogHook {
public:
    AlignmentFeedback() { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() == "Align Lines") return wxID_NONE;
        if (auto* message = wxDynamicCast(dialog, wxMessageDialog)) messages.push_back(message->GetMessage());
        for (auto* window : wxTopLevelWindows) {
            if (auto* alignment = wxDynamicCast(window, wxDialog);
                alignment && alignment->GetTitle() == "Align Lines" && alignment->IsShown())
                alignmentVisible = true;
        }
        return wxID_OK;
    }
    std::vector<wxString> messages;
    bool alignmentVisible = false;
};
void NativeProtectionAndRecall(EditorFrame* frame) {
    auto* editor = frame->Editor();
    AlignmentFeedback feedback;
    const auto rejected = "x  y\n" + std::string(1024, 'z');
    TestEditor::ResetStream(editor, rejected.c_str(), 1028, 1);
    Dialog(frame, AlignmentMode::Left, AlignmentMode::Right, wxID_OK);
    Require(feedback.messages.size() == 1 &&
                feedback.messages[0] == "This operation can't be performed (lines too long)." &&
                !feedback.alignmentVisible && TestEditor::Text(editor) == rejected && !editor->Send(Message::CanUndo),
            "oversized input gives original feedback after the native choice dialog closes");
    TestEditor::RequireSelection(editor, 1029, 0,
                                 "native oversized path repairs selection despite rejected transformation");
    TestEditor::ResetStream(editor, "a b\nc d");
    editor->Send(Message::SetRectangularSelectionAnchor, 0);
    editor->Send(Message::SetRectangularSelectionCaret, 6);
    Dialog(frame, AlignmentMode::Right, AlignmentMode::Center, wxID_OK);
    Require(feedback.messages.size() == 2 &&
                feedback.messages[1] == "This operation can't be performed within a rectangular selection." &&
                !feedback.alignmentVisible && TestEditor::Text(editor) == "a b\nc d" &&
                editor->Send(Message::SelectionIsRectangle) &&
                editor->Send(Message::GetRectangularSelectionAnchor) == 0 &&
                editor->Send(Message::GetRectangularSelectionCaret) == 6,
            "rectangle keeps endpoints and gives original warning after dialog closes");
    TestEditor::ResetStream(editor, "a   b\nlongword", 1, 12);
    editor->Send(Message::SetReadOnly, 1);
    Dialog(frame, AlignmentMode::Center, AlignmentMode::Justify, wxID_OK);
    Require(TestEditor::Text(editor) == "a   b\nlongword" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify) && feedback.messages.size() == 2,
            "read-only native OK retains text, history and save point");
    TestEditor::RequireSelection(editor, 0, 14, "read-only native alignment repairs selection");
    const std::array<const char*, 5> results{"a b c\n12345678", "   a b c\n12345678", " a  b c\n12345678",
                                             "a  b   c\n12345678", "a  b   c\n12345678"};
    auto recalled = AlignmentMode::Justify;
    for (size_t i = 0; i < results.size(); ++i) {
        TestEditor::ResetStream(editor, "a b c\n12345678", 0, 14);
        const auto choice = static_cast<AlignmentMode>(i);
        Dialog(frame, recalled, choice, wxID_OK);
        Require(TestEditor::Text(editor) == results[i] && editor->Send(Message::CanUndo),
                "every native radio choice invokes its original alignment mode");
        editor->Send(Message::Undo);
        Require(TestEditor::Text(editor) == "a b c\n12345678" && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify),
                "every native choice is one Undo group");
        recalled = choice;
    }
    editor->Send(Message::SetSavePoint);
    Require(frame->New(), "new document opens in the current frame");
    Dialog(frame, AlignmentMode::JustifyParagraph, AlignmentMode::Right, wxID_CANCEL);
    Require(TestEditor::Text(editor).empty() && !editor->Send(Message::CanUndo),
            "empty document keeps alignment menu available and accepted recall survives New");
    auto* other = new EditorFrame;
    other->Show();
    Dialog(other, AlignmentMode::Left, AlignmentMode::Center, wxID_CANCEL);
    other->Destroy();
}
void OriginalEdges(EditorControl* editor) {
    editor->SetIndentationWidths(4, 4);
    editor->Send(Message::SetUseTabs, 0);
    TestEditor::ResetStream(editor, "\t😀\t中  \r\n  a b\nLONGEST\rx", 28, 1);
    const auto eol = editor->Send(Message::GetEOLMode);
    editor->AlignLines(AlignmentMode::Right);
    Require(TestEditor::Text(editor) == "     😀 中\r\n      a b\n  LONGEST\r        x",
            "Tabs, supplementary UTF-16 word lengths, Unicode and mixed EOLs retain original alignment");
    TestEditor::RequireSelection(editor, 44, 0, "Unicode reversed partial selection expands through unterminated EOF");
    Require(editor->Send(Message::GetEOLMode) == eol, "alignment preserves insertion EOL mode");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "\t😀\t中  \r\n  a b\nLONGEST\rx" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "mixed-EOL right alignment has one Undo");
    TestEditor::RequireSelection(editor, 28, 1, "Undo restores original partial reversed endpoints");
    editor->Send(Message::Redo);
    Require(TestEditor::Text(editor) == "     😀 中\r\n      a b\n  LONGEST\r        x",
            "mixed-EOL Redo restores literal bytes");
    TestEditor::ResetStream(editor, "😀 😀\n123", 0, 13);
    editor->AlignLines(AlignmentMode::Justify);
    Require(TestEditor::Text(editor) == "😀😀\n123",
            "original negative UTF-16 gap width concatenates supplementary words");
    TestEditor::ResetStream(editor, "x  y\nz   w", 1, 1);
    editor->AlignLines(AlignmentMode::Left);
    Require(TestEditor::Text(editor) == "x y\nz   w", "an empty stream selection aligns only its current logical line");
    TestEditor::RequireSelection(editor, 0, 4, "caret alignment selects its current complete line");
    editor->SetIndentationWidths(8, 8);
    editor->Send(Message::SetUseTabs, 1);
    TestEditor::ResetStream(editor, "\t a   b\n        x\n", 0, 18);
    editor->AlignLines(AlignmentMode::Left);
    Require(TestEditor::Text(editor) == "\ta b\n\tx\n",
            "effective tab width and core indentation policy produce original Tab indentation");
    editor->SetIndentationWidths(4, 4);
    editor->Send(Message::SetUseTabs, 0);

    TestEditor::ResetStream(editor, "  a b   c\n    中\nOUTSIDE", 1, 19);
    editor->AlignLines(AlignmentMode::Left);
    Require(TestEditor::Text(editor) == "  a b c\n  中\nOUTSIDE",
            "only ASCII spaces and Tabs split words; a next-line-start endpoint excludes its line");
    TestEditor::RequireSelection(editor, 0, 15, "excluded next line starts immediately after repaired selection");
    TestEditor::ResetStream(editor, "  \n\t\n", 0, 5);
    editor->AlignLines(AlignmentMode::Left);
    Require(TestEditor::Text(editor) == "\n\n", "space-only and Tab-only lines are emptied");
    TestEditor::RequireSelection(editor, 0, 2, "blank-line transformation retains whole-line selection");
    TestEditor::ResetStream(editor, "a   b\nlongword\nc  d", 1, 4);
    editor->Send(Message::AddSelection, 18, 15);
    editor->Send(Message::SetMainSelection, 0);
    editor->AlignLines(AlignmentMode::Left);
    Require(TestEditor::Text(editor) == "a b\nlongword\nc  d" && editor->Send(Message::GetSelections) == 1,
            "alignment uses the main stream selection and performs original single-selection repair");
    TestEditor::RequireSelection(editor, 0, 4, "main selection expands only its own logical line");
}
void HistoryAndProtection(EditorControl* editor) {
    TestEditor::ResetStream(editor, "a b\nc d", 1, 6);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 1, 6);
    const auto beforeRevision = editor->DocumentRevision();
    editor->AlignLines(AlignmentMode::Left);
    Require(TestEditor::Text(editor) == "a b\nc d" && editor->Send(Message::CanUndo) &&
                !editor->Send(Message::CanRedo) && editor->Send(Message::GetModify) &&
                editor->DocumentRevision() > beforeRevision,
            "equal-byte target replacement retains original history, revision and save-point behavior");
    TestEditor::RequireSelection(editor, 0, 7, "unchanged bytes still receive whole-line selection repair");
    editor->Send(Message::Undo);
    Require(TestEditor::Text(editor) == "a b\nc d" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "equal-byte replacement is one Undo group returning to save point");
    for (const auto mode : {AlignmentMode::Left, AlignmentMode::Right, AlignmentMode::Center, AlignmentMode::Justify,
                            AlignmentMode::JustifyParagraph}) {
        TestEditor::ResetStream(editor, "  a   b\nlongword", 1, 14);
        editor->Send(Message::SetReadOnly, 1);
        const auto revision = editor->DocumentRevision();
        Require(editor->AlignLines(mode) == AlignmentResult::Complete &&
                    TestEditor::Text(editor) == "  a   b\nlongword" && !editor->Send(Message::CanUndo) &&
                    !editor->Send(Message::GetModify) && editor->DocumentRevision() == revision,
                "original core protects read-only text for every alignment mode");
        TestEditor::RequireSelection(editor, 0, 16, "read-only command retains original selection repair");
    }
    TestEditor::ResetStream(editor, "a b\nc d", 0, 2);
    editor->Send(Message::SetRectangularSelectionAnchor, 0);
    editor->Send(Message::SetRectangularSelectionCaret, 6);
    const auto revision = editor->DocumentRevision();
    Require(editor->AlignLines(AlignmentMode::Left) == AlignmentResult::RectangularSelection &&
                TestEditor::Text(editor) == "a b\nc d" && editor->Send(Message::SelectionIsRectangle) &&
                editor->Send(Message::GetRectangularSelectionAnchor) == 0 &&
                editor->Send(Message::GetRectangularSelectionCaret) == 6 && !editor->Send(Message::CanUndo) &&
                editor->DocumentRevision() == revision,
            "rectangle rejects before text, selection or history changes");
    TestEditor::ResetStream(editor, "");
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    const auto emptyRevision = editor->DocumentRevision();
    editor->AlignLines(AlignmentMode::Left);
    Require(TestEditor::Text(editor).empty() && !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                !editor->Send(Message::GetModify) && editor->DocumentRevision() == emptyRevision,
            "empty alignment preserves pending Redo, revision and save point");
}
void BufferBoundary(EditorControl* editor) {
    const std::string accepted = "x  y\n" + std::string(1023, 'z');
    TestEditor::ResetStream(editor, accepted.c_str(), 1, 1027);
    Require(editor->AlignLines(AlignmentMode::Left) == AlignmentResult::Complete &&
                TestEditor::Text(editor) == "x y\n" + std::string(1023, 'z'),
            "original boundary accepts trimmed column 1023 without wrapping");
    TestEditor::RequireSelection(editor, 0, 1027, "accepted boundary repairs selection");
    const auto crlf = std::string(1023, 'a') + "\r\nx  y";
    TestEditor::ResetStream(editor, crlf.c_str(), 0, 1029);
    Require(editor->AlignLines(AlignmentMode::Left) == AlignmentResult::Complete &&
                TestEditor::Text(editor) == std::string(1023, 'a') + "\r\nx y",
            "fixed UTF-16 input boundary includes CRLF while trimmed column 1023 remains accepted");
    const std::string rejected = "x  y\n" + std::string(1024, 'z');
    TestEditor::ResetStream(editor, rejected.c_str(), 1028, 1);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 1028, 1);
    const auto revision = editor->DocumentRevision();
    Require(editor->AlignLines(AlignmentMode::Left) == AlignmentResult::LinesTooLong &&
                TestEditor::Text(editor) == rejected && !editor->Send(Message::CanUndo) &&
                editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify) &&
                editor->DocumentRevision() == revision,
            "column 1024 rejects the complete transformation and preserves pending Redo");
    TestEditor::RequireSelection(editor, 1029, 0,
                                 "original rejected boundary still repairs reversed partial selection");
    std::string wide;
    for (int i = 0; i < 600; ++i)
        wide += "😀";
    wide += "  x";
    TestEditor::ResetStream(editor, wide.c_str());
    Require(editor->AlignLines(AlignmentMode::Left) == AlignmentResult::Complete && TestEditor::Text(editor) == wide &&
                !editor->Send(Message::CanUndo),
            "original failed UTF-16 conversion skips a line above 1024 units despite lower core columns");
    const auto spaces = std::string(1100, ' ');
    TestEditor::ResetStream(editor, spaces.c_str());
    Require(editor->AlignLines(AlignmentMode::Left) == AlignmentResult::Complete && TestEditor::Text(editor).empty(),
            "space-only line does not enter the nonblank column limit");
}
void SavedBytes(EditorFrame* frame) {
    using namespace std::string_literals;
    const auto directory = TestFiles::TemporaryDirectory("align-lines");
    std::filesystem::create_directory(directory);
    const TestFiles::EncodedFixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "\xef\xbb\xbf  中   文\r\n    x\n", "\xef\xbb\xbf  中 文\r\n  x\n"},
        {"gbk", FileEncoding::Gbk, "  \xd6\xd0   \xce\xc4\r\n    x\n", "  \xd6\xd0 \xce\xc4\r\n  x\n"},
        {"utf16", FileEncoding::Utf16Le, "\xff\xfe \0 \0\x2d\x4e \0 \0 \0\x87\x65\r\0\n\0 \0 \0 \0 \0x\0\n\0"s,
         "\xff\xfe \0 \0\x2d\x4e \0\x87\x65\r\0\n\0 \0 \0x\0\n\0"s},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        frame->Editor()->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "opens original encoded alignment fixture");
        frame->Editor()->Send(Message::SelectAll);
        frame->Editor()->AlignLines(AlignmentMode::Left);
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"aligned document saves exact independent encoded bytes",
                                         "Undo saves original encoded bytes", "Redo saves aligned encoded bytes"});
    }
    std::filesystem::remove_all(directory);
}
class AlignmentApp final : public wxApp {
public:
    bool OnInit() override {
        settings_ = std::make_unique<SettingsStore>();
        settings_->values.warnLineEndings = false;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame] {
            try {
                Left(frame->Editor());
                Right(frame->Editor());
                Center(frame->Editor());
                Justify(frame->Editor());
                Paragraph(frame->Editor());
                NativeDialog(frame);
                NativeProtectionAndRecall(frame);
                OriginalEdges(frame->Editor());
                HistoryAndProtection(frame->Editor());
                BufferBoundary(frame->Editor());
                SavedBytes(frame);
                std::puts("PASS: original Align Lines");
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
wxIMPLEMENT_APP(AlignmentApp);
