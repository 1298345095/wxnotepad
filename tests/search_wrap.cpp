#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "native_search_helpers.h"
#include "settings/settings_store.h"
#include <wx/modalhook.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <vector>
#include <memory>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
class Feedback final : public wxModalDialogHook {
public:
    Feedback() { Register(); }
    int count = 0;
    int response = wxID_CANCEL;
    bool suppress = false;
    bool suppressionEnabled = true;
    std::vector<wxString> messages;
    int Enter(wxDialog* dialog) override {
        if (dialog->GetName() != "Search feedback") return wxID_NONE;
        ++count;
        for (auto* child : dialog->GetChildren()) {
            if (wxDynamicCast(child, wxStaticText)) messages.push_back(child->GetName());
        }
        wxTheApp->CallAfter([this, dialog] {
            Require(dialog->IsShown(), "feedback must be presented as a native modal dialog");
            auto* checkbox = wxDynamicCast(wxWindow::FindWindowByName("Suppress search message", dialog), wxCheckBox);
            Require(checkbox, "feedback must offer the original message suppression choice");
            Require(checkbox->IsEnabled() == suppressionEnabled, "suppression mode 2 disables the original checkbox");
            if (suppress) NativeSearch::Toggle(checkbox, true);
            NativeSearch::Click(dialog, response);
        });
        return wxID_NONE;
    }
};
void SettingsCase(EditorFrame* frame, Feedback& feedback, const wxString& scenario) {
    using Scintilla::Message;
    auto* editor = frame->Editor();
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one"));
    NativeSearch::Command(frame, wxID_FIND);
    auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
    Require(query, "Find must expose the query");
    query->SetValue("one");
    editor->Send(Message::SetSel, 0, 3);
    if (scenario == "nowrap") {
        feedback.response = wxID_OK;
        NativeSearch::Click(query->GetParent(), wxID_FORWARD);
        Require(feedback.count == 1 && feedback.messages.back() == "MsgNotFound",
                "original FindReplaceOption bit 64 disables forward wrap before native search");
        NativeSearch::Click(query->GetParent(), wxID_BACKWARD);
        Require(feedback.count == 2 && feedback.messages.back() == "MsgNotFound",
                "original No Wrap disables backward wrap before native search");
        NativeSearch::Selection(editor, 0, 3, "loaded No Wrap retains the only match selection");
    } else if (scenario == "write") {
        feedback.suppress = true;
        NativeSearch::Click(query->GetParent(), wxID_FORWARD);
        Require(feedback.count == 1, "default suppression mode presents the wrap confirmation");
        NativeSearch::Selection(editor, 0, 3, "Cancel plus suppression keeps the match selected");
    } else {
        NativeSearch::Click(query->GetParent(), wxID_FORWARD);
        Require(feedback.count == 0, "loaded forward suppression accepts wrap without a dialog");
        NativeSearch::Selection(editor, 0, 3, "suppressed wrap finds the only match");
        feedback.suppressionEnabled = false;
        NativeSearch::Click(query->GetParent(), wxID_BACKWARD);
        Require(feedback.count == 1 && feedback.messages.back() == "MsgFindWrap2",
                "backward mode 2 still presents the confirmation");
        query->SetValue("missing");
        editor->Send(Message::GotoPos, 0);
        NativeSearch::Click(query->GetParent(), wxID_FORWARD);
        Require(feedback.count == 1, "loaded no-match suppression dismisses the warning");
    }
    std::puts("PASS: original suppression settings, independent fields and native disabled checkbox");
}
class WrapApp final : public wxApp {
public:
    bool OnInit() override {
        const wxString scenario = wxGetenv("WXNOTEPAD_SEARCH_SETTINGS");
        if (!scenario.empty()) settings_ = std::make_unique<SettingsStore>();
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        CallAfter([this, frame, scenario] {
            try {
                using Scintilla::Message;
                Feedback feedback;
                if (!scenario.empty()) {
                    SettingsCase(frame, feedback, scenario);
                    result_ = 0;
                    frame->Destroy();
                    ExitMainLoop();
                    return;
                }
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("oneone one"));
                NativeSearch::Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                Require(query, "Find must expose the query");
                query->SetValue("one");
                editor->Send(Message::SetSel, 3, 0);
                NativeSearch::Click(query->GetParent(), wxID_BACKWARD);
                Require(feedback.count == 1, "Previous at start must present the original wrap confirmation");
                Require(editor->Send(Message::GetAnchor) == 3 && editor->Send(Message::GetCurrentPos) == 0,
                        "cancelling wrap preserves selection direction");
                Require(feedback.messages.back() == "MsgFindWrap2", "Previous uses its own suppression field");
                std::puts("PASS: backward wrap confirmation and cancellation");
                feedback.response = wxID_OK;
                NativeSearch::Click(query->GetParent(), wxID_BACKWARD);
                NativeSearch::Selection(editor, 7, 10, "confirming Previous wraps to the final match");
                Require(feedback.count == 2, "successful wrap presents exactly one confirmation");
                feedback.response = wxID_CANCEL;
                editor->Send(Message::SetSel, 10, 7);
                NativeSearch::Click(query->GetParent(), wxID_FORWARD);
                Require(editor->Send(Message::GetAnchor) == 10 && editor->Send(Message::GetCurrentPos) == 7,
                        "cancelling Next retains its reversed selection");
                Require(feedback.messages.back() == "MsgFindWrap1", "Next uses the forward suppression field");
                feedback.response = wxID_OK;
                NativeSearch::Click(query->GetParent(), wxID_FORWARD);
                NativeSearch::Selection(editor, 0, 3, "confirming Next wraps to the first match");
                std::string lines;
                for (int line = 0; line < 100; ++line)
                    lines += std::string(150, 'x') + "\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(lines.c_str()));
                editor->Send(Message::SetWrapMode, 0);
                editor->Send(Message::SetSel, lines.size(), lines.size() - 20);
                editor->Send(Message::SetFirstVisibleLine, 20);
                editor->Send(Message::SetXOffset, 80);
                query->SetFocus();
                wxYield();
                const auto firstLine = editor->Send(Message::GetFirstVisibleLine);
                const auto xOffset = editor->Send(Message::GetXOffset);
                auto* priorFocus = wxWindow::FindFocus();
                feedback.response = wxID_CANCEL;
                NativeSearch::Click(query->GetParent(), wxID_FORWARD);
                Require(editor->Send(Message::GetFirstVisibleLine) == firstLine &&
                            editor->Send(Message::GetXOffset) == xOffset,
                        "cancelled wrap preserves vertical and horizontal view positions");
                Require(wxWindow::FindFocus() == priorFocus, "native feedback restores the invoking control focus");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("oneone one"));
                feedback.response = wxID_OK;
                auto* wrap = wxDynamicCast(wxWindow::FindWindowByName("Wrap around", frame), wxCheckBox);
                Require(wrap && wrap->GetValue(), "original No Wrap default is off");
                NativeSearch::Toggle(wrap, false);
                editor->Send(Message::SetSel, 10, 7);
                const int beforeNoWrap = feedback.count;
                NativeSearch::Click(query->GetParent(), wxID_FORWARD);
                Require(feedback.count == beforeNoWrap + 1 && feedback.messages.back() == "MsgNotFound",
                        "No Wrap reports not found without presenting wrap confirmation");
                Require(editor->Send(Message::GetAnchor) == 10 && editor->Send(Message::GetCurrentPos) == 7,
                        "No Wrap preserves selection direction");
                NativeSearch::Toggle(wrap, true);
                query->SetValue("missing");
                editor->Send(Message::GotoPos, 4);
                const int beforeMissing = feedback.count;
                NativeSearch::Click(query->GetParent(), wxID_FORWARD);
                Require(feedback.count == beforeMissing + 2 && feedback.messages.back() == "MsgNotFound",
                        "confirmed wrap without a match presents the original not-found warning");
                NativeSearch::Selection(editor, 4, 4, "failed search retains caret");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one"));
                editor->Send(Message::SetSel, 0, 3);
                query->SetValue("one");
                NativeSearch::Click(query->GetParent(), wxID_FORWARD);
                NativeSearch::Selection(editor, 0, 3, "only match wraps back to itself after confirmation");
                NativeSearch::Click(query->GetParent(), wxID_BACKWARD);
                NativeSearch::Selection(editor, 0, 3, "only match wraps backward back to itself");
                feedback.suppress = true;
                feedback.response = wxID_CANCEL;
                NativeSearch::Click(query->GetParent(), wxID_FORWARD);
                const int suppressedForward = feedback.count;
                feedback.suppress = false;
                NativeSearch::Click(query->GetParent(), wxID_FORWARD);
                Require(feedback.count == suppressedForward,
                        "suppressed forward wrap automatically returns OK after Cancel");
                feedback.response = wxID_OK;
                feedback.suppress = true;
                NativeSearch::Click(query->GetParent(), wxID_BACKWARD);
                const int suppressedBoth = feedback.count;
                feedback.suppress = false;
                NativeSearch::Click(query->GetParent(), wxID_BACKWARD);
                Require(feedback.count == suppressedBoth, "backward suppression is independent and accepts wrapping");
                std::puts("PASS: both directions, No Wrap, no match, only match and independent suppression");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one one one"));
                editor->Send(Message::SetSel, 5, 1);
                const int selectNext = frame->GetMenuBar()->FindMenuItem("Edit", "Select to Next");
                Require(selectNext != wxNOT_FOUND, "original Select to Next command must be available");
                NativeSearch::Command(frame, selectNext);
                Require(editor->Send(Message::GetAnchor) == 1 && editor->Send(Message::GetCurrentPos) == 11,
                        "extended Next retains the smaller original endpoint as anchor");
                const int beforeExtended = feedback.count;
                NativeSearch::Command(frame, selectNext);
                Require(feedback.count == beforeExtended + 1 && feedback.messages.back() == "MsgNotFound",
                        "extended search never wraps even when normal wrapping is suppressed");
                Require(editor->Send(Message::GetAnchor) == 1 && editor->Send(Message::GetCurrentPos) == 11,
                        "failed extended search retains the selection");
                const int selectPrevious = frame->GetMenuBar()->FindMenuItem("Edit", "Select to Previous");
                Require(selectPrevious != wxNOT_FOUND, "original Select to Previous command must be available");
                editor->Send(Message::SetSel, 10, 5);
                NativeSearch::Command(frame, selectPrevious);
                Require(editor->Send(Message::GetAnchor) == 10 && editor->Send(Message::GetCurrentPos) == 0,
                        "extended Previous retains the larger original endpoint as anchor");
                std::puts("PASS: original extended-selection anchors and no-wrap behavior");
                NativeSearch::Command(frame, wxID_REPLACE);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                Require(replacement, "Replace must expose its replacement text");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one one"));
                editor->Send(Message::SetSel, 7, 0);
                query->SetValue("one");
                replacement->SetValue("two");
                NativeSearch::Click(query->GetParent(), wxID_REPLACE);
                NativeSearch::Selection(editor, 0, 3,
                                        "Replace first selects a candidate inside a nonmatching selection");
                Require(NativeSearch::Text(editor) == "one one", "finding a candidate does not replace text");
                NativeSearch::Click(query->GetParent(), wxID_REPLACE);
                Require(NativeSearch::Text(editor) == "two one", "Replace updates the matched selection");
                NativeSearch::Selection(editor, 4, 7, "Replace selects the following match");
                NativeSearch::Click(query->GetParent(), wxID_REPLACE);
                Require(NativeSearch::Text(editor) == "two two", "Replace searches after the replaced bytes");
                NativeSearch::Selection(editor, 7, 7, "no following match collapses at replacement end");
                std::puts("PASS: Replace candidate selection, replacement and no-match caret retention");
                auto* replaceFrame = new EditorFrame;
                replaceFrame->Show();
                auto* replaceEditor = replaceFrame->Editor();
                replaceEditor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one"));
                NativeSearch::Command(replaceFrame, wxID_REPLACE);
                auto* replaceQuery = wxDynamicCast(wxWindow::FindWindowByName("Find query", replaceFrame), wxTextCtrl);
                auto* replaceText =
                    wxDynamicCast(wxWindow::FindWindowByName("Replacement text", replaceFrame), wxTextCtrl);
                Require(replaceQuery && replaceText, "Replace controls must be available");
                replaceQuery->SetValue("one");
                replaceText->SetValue("two");
                replaceEditor->Send(Message::SetSel, 0, 3);
                feedback.response = wxID_CANCEL;
                const int beforeReplaceCancel = feedback.count;
                NativeSearch::Click(replaceQuery->GetParent(), wxID_REPLACE);
                Require(feedback.count == beforeReplaceCancel + 1 && feedback.messages.back() == "MsgFindWrap1",
                        "Replace after editing offers forward wrap and Cancel suppresses not-found feedback");
                Require(NativeSearch::Text(replaceEditor) == "two", "cancelling subsequent search retains replacement");
                NativeSearch::Selection(replaceEditor, 3, 3,
                                        "cancelled search after Replace retains replacement-end caret");
                replaceEditor->Send(Message::SetSel, 2, 1);
                NativeSearch::Click(replaceQuery->GetParent(), wxID_REPLACE);
                Require(replaceEditor->Send(Message::GetAnchor) == 2 &&
                            replaceEditor->Send(Message::GetCurrentPos) == 1,
                        "cancelling Replace initial search retains its original selection");
                replaceEditor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one tail"));
                replaceEditor->Send(Message::SetSel, 8, 5);
                feedback.response = wxID_OK;
                NativeSearch::Click(replaceQuery->GetParent(), wxID_REPLACE);
                NativeSearch::Selection(replaceEditor, 0, 3,
                                        "Replace initial search can confirm a wrap to the first match");
                Require(NativeSearch::Text(replaceEditor) == "one tail",
                        "wrapped candidate is selected before replacing");
                replaceFrame->Destroy();
                std::puts(
                    "PASS: Replace confirms both initial and subsequent searches and preserves cancellation results");
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
    int OnExit() override { return result_; }

private:
    int result_ = 1;
    std::unique_ptr<SettingsStore> settings_;
};
} // namespace
wxIMPLEMENT_APP(WrapApp);
