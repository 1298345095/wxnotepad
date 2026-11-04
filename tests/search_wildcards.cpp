#include "native_search_helpers.h"
#include "app/find_replace_dialog.h"
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <memory>

namespace {
using Scintilla::Message;
using namespace NativeSearch;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class WildcardApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad wildcard search");
        auto* editor = new EditorControl(frame);
        frame->Show();
        CallAfter([this, frame, editor] {
            try {
                SearchSession search(*editor);
                auto dialog = std::make_unique<FindReplaceDialog>(
                    frame, *editor, search, [](const FindResult&) {}, [](wxKeyEvent& event) { event.Skip(); }, 0);
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("a.c abc a+c"));
                editor->Send(Message::SetSel, 0, 0);
                dialog->Present(false, 0);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* wildcard = wxDynamicCast(wxWindow::FindWindowByName("Wildcard search", frame), wxCheckBox);
                Require(query && wildcard, "native Find exposes original wildcard search mode");
                Require(!wildcard->GetValue(), "wildcard search defaults off");
                Toggle(wildcard, true);
                query->SetValue("a?c");
                Click(query->GetParent(), wxID_FORWARD);
                Selection(editor, 0, 3, "question mark matches one original-core character");
                const auto load = [&](const char* text) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    editor->Send(Message::SetSel, 0, 0);
                    wxYield();
                };
                const auto next = [&] { Click(query->GetParent(), wxID_FORWARD); };
                load("a.c abc a+c\nac\nabbbc");
                query->SetValue("a*c");
                next();
                Selection(editor, 0, 11, "star is greedy within the original line matcher");
                next();
                Selection(editor, 12, 14, "star also matches zero bytes");
                editor->Send(Message::SetSel, 20, 20);
                Click(query->GetParent(), wxID_BACKWARD);
                Selection(editor, 15, 20, "Previous shares wildcard preparation");
                load("(a)|{2}.^$\\[]+");
                query->SetValue("(a)|{2}.^$\\[]+");
                next();
                Selection(editor, 0, 14, "original escaped metacharacters and builtin literal syntax remain literal");
                load("中😀文 中a文");
                query->SetValue(wxString::FromUTF8("中?文"));
                next();
                Selection(editor, 11, 18, "original question mark consumes one byte, not one Unicode code point");
                editor->Send(Message::SetSel, 0, 0);
                query->SetValue(wxString::FromUTF8("中????文"));
                next();
                Selection(editor, 0, 10, "four question marks consume the original UTF-8 emoji bytes");
                std::puts("PASS: original star greediness, Previous, literal metacharacters and Unicode byte grammar");
                auto* matchCase = wxDynamicCast(wxWindow::FindWindowByName("Match case", frame), wxCheckBox);
                auto* wholeWord = wxDynamicCast(wxWindow::FindWindowByName("Whole word", frame), wxCheckBox);
                auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", frame), wxCheckBox);
                Require(matchCase && wholeWord && regex && wildcard->GetValue(),
                        "original options coexist with wildcard mode");
                load("Cat cat scatter");
                query->SetValue("c?t");
                Toggle(matchCase, true);
                next();
                Selection(editor, 4, 7, "wildcard matcher honors case sensitivity");
                Toggle(matchCase, false);
                Toggle(wholeWord, true);
                next();
                Selection(editor, 9, 12, "original builtin regex retains its whole-word flag behavior");
                Require(wholeWord->IsEnabled(), "whole-word option remains enabled in original wildcard mode");
                Toggle(regex, true);
                Require(!wildcard->GetValue(), "checking regular expression clears wildcard mode");
                Toggle(wildcard, true);
                Require(!regex->GetValue(), "checking wildcard clears regular expression mode");
                Toggle(wholeWord, false);
                std::puts("PASS: original case/word behavior and native regex/wildcard exclusion");
                load("a1c a2c\naxc");
                query->SetValue("a?c");
                auto* findAll = wxDynamicCast(wxWindow::FindWindowByName("Find All", frame), wxButton);
                auto* bookmark = wxDynamicCast(wxWindow::FindWindowByName("Bookmark matched line", frame), wxCheckBox);
                auto* count = wxDynamicCast(wxWindow::FindWindowByName("Find All result", frame), wxStaticText);
                Require(findAll && bookmark && count, "Find All exposes original result and bookmark options");
                Toggle(bookmark, true);
                Click(query->GetParent(), findAll->GetId());
                Require(count->GetLabel() == "3 matches" && editor->Send(Message::IndicatorValueAt, 8, 0) &&
                            editor->Send(Message::IndicatorValueAt, 8, 4) &&
                            editor->Send(Message::IndicatorValueAt, 8, 8) &&
                            (editor->Send(Message::MarkerGet, 0) & 1) && (editor->Send(Message::MarkerGet, 1) & 1),
                        "Find All shares wildcard matches, occurrence indicators and bookmarks");
                dialog->Present(true, 0);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                Require(replacement, "Replace exposes replacement text");
                replacement->SetValue("$0:\\0");
                editor->Send(Message::SetSel, 0, 0);
                next();
                Click(query->GetParent(), wxID_REPLACE);
                Require(Text(editor) == "a1c:a1c a2c\naxc",
                        "wildcard Replace uses original regex replacement formatter");
                Selection(editor, 8, 11, "wildcard Replace advances to the following prepared match");
                editor->Send(Message::Undo);
                Require(Text(editor) == "a1c a2c\naxc", "wildcard Replace undoes exactly");
                editor->Send(Message::Redo);
                Require(Text(editor) == "a1c:a1c a2c\naxc", "wildcard Replace redoes exactly");
                load("a1c a2c\naxc");
                replacement->SetValue("X");
                Click(query->GetParent(), wxID_REPLACE_ALL);
                Require(Text(editor) == "X X\nX", "wildcard Replace All applies to every line");
                editor->Send(Message::Undo);
                Require(Text(editor) == "a1c a2c\naxc" && !editor->Send(Message::CanUndo) &&
                            !editor->Send(Message::GetModify),
                        "wildcard Replace All has one undo group and restores save point");
                replacement->SetValue("$0");
                Click(query->GetParent(), wxID_REPLACE_ALL);
                Require(Text(editor) == "a1c a2c\naxc" && editor->Send(Message::CanRedo),
                        "unchanged wildcard replacement preserves redo");
                editor->Send(Message::Redo);
                Require(Text(editor) == "X X\nX", "no-op wildcard replacement retains the redo edit");
                load("a1c a2c\naxc");
                replacement->SetValue("X");
                editor->Send(Message::SetSel, 0, 7);
                auto* inSelection = wxDynamicCast(wxWindow::FindWindowByName("Replace in selection", frame), wxButton);
                Require(inSelection, "native selection replacement exists");
                Click(query->GetParent(), inSelection->GetId());
                Require(Text(editor) == "X X\naxc", "selection replacement uses the shared wildcard preparation");
                editor->Send(Message::Undo);
                Require(Text(editor) == "a1c a2c\naxc", "wildcard selection replacement undoes together");
                load("");
                query->SetValue("*");
                replacement->SetValue("X");
                Click(query->GetParent(), wxID_REPLACE_ALL);
                Require(Text(editor).empty() && !editor->Send(Message::CanUndo),
                        "original builtin wildcard matcher leaves an empty document and history unchanged");
                std::puts("PASS: shared Find All, Replace, Replace All, selection replacement and Undo/Redo");
                auto* transform = wxDynamicCast(wxWindow::FindWindowByName("Transform backslashes", frame), wxCheckBox);
                Require(transform, "wildcard search exposes the shared backslash transform option");
                Toggle(transform, true);
                Require(wildcard->GetValue() && !regex->GetValue(),
                        "original wildcard and backslash transform options can be combined");
                load(".A AA .B");
                query->SetValue("\\x2e?");
                replacement->SetValue("\\x24\\x30\\t");
                Click(query->GetParent(), findAll->GetId());
                Require(count->GetLabel() == "2 matches", "transformation precedes wildcard metacharacter escaping");
                Click(query->GetParent(), wxID_REPLACE_ALL);
                Require(Text(editor) == ".A\t AA .B\t",
                        "combined wildcard replacement transforms first and then uses original regex formatting");
                editor->Send(Message::Undo);
                Require(Text(editor) == ".A AA .B", "combined transform replacement undoes as one edit");
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(WildcardApp);
