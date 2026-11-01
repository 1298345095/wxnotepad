#include "editor/editor_control.h"
#include "ScintillaTypes.h"
#include <wx/wx.h>
#include <cstdio>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Key(EditorControl* editor, int key, bool control = false, bool shift = false) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = key;
    event.m_controlDown = control;
    event.m_shiftDown = shift;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Layout(EditorControl* editor) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        wxYield();
        editor->Refresh();
        editor->Update();
        wxMilliSleep(10);
    }
}
void Selection(EditorControl* editor, intptr_t anchor, intptr_t caret, const char* message) {
    Require(editor->Send(Message::GetAnchor) == anchor && editor->Send(Message::GetCurrentPos) == caret, message);
}
void SeedHistory(EditorControl* editor) {
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
}
void Unchanged(EditorControl* editor, const std::string& expected) {
    std::string text(static_cast<size_t>(editor->Send(Message::GetLength)) + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    Require(text == expected && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                editor->Send(Message::CanRedo),
            "navigation preserves text, save point and undo/redo availability");
    editor->Send(Message::Redo);
    Require(editor->Send(Message::GetLength) == static_cast<intptr_t>(expected.size() + 1),
            "pending redo still performs the original edit");
    editor->Send(Message::Undo);
}
void Visible(EditorControl* editor) {
    const auto position = editor->Send(Message::GetCurrentPos);
    const auto x = editor->Send(Message::PointXFromPosition, 0, position);
    const auto y = editor->Send(Message::PointYFromPosition, 0, position);
    Require(x >= 0 && x < editor->GetClientSize().x && y >= 0 && y < editor->GetClientSize().y,
            "navigation reveals the caret on both axes");
}
class NavigationApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame =
            new wxFrame(nullptr, wxID_ANY, "Original navigation bindings", wxDefaultPosition, wxSize(360, 240));
        auto* editor = new EditorControl(frame);
        frame->Show();
        CallAfter([this, frame, editor] {
            try {
                std::string text = "\t  ";
                for (int repeat = 0; repeat < 35; ++repeat)
                    text += "word 中文😀 ";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
                SeedHistory(editor);
                editor->Send(Message::SetWrapMode, static_cast<uintptr_t>(Scintilla::Wrap::Word));
                editor->Send(Message::GotoPos, 0);
                Layout(editor);
                Require(editor->Send(Message::WrapCount, 0) > 3, "fixture has continuation rows");
                const auto height = editor->Send(Message::TextHeight, 0);
                const auto textLeft = editor->Send(Message::PointXFromPosition, 0, 0);
                const auto rowStart = editor->Send(Message::PositionFromPointClose, textLeft, height + 2);
                const auto insideRow = editor->Send(Message::PositionFromPointClose, textLeft + 120, height + 2);
                Require(rowStart > 3 && insideRow > rowStart, "hit testing locates second visual row");
                editor->Send(Message::GotoPos, insideRow);
                Key(editor, WXK_HOME);
                Selection(editor, rowStart, rowStart, "Home first reaches the continuation row start");
                Key(editor, WXK_HOME);
                Selection(editor, 3, 3, "Home at continuation start reaches logical indentation");
                Key(editor, WXK_HOME);
                Selection(editor, 0, 0, "Home at indentation reaches physical start");
                Key(editor, WXK_HOME);
                Selection(editor, 3, 3, "Home on first row toggles back to indentation");
                std::puts("PASS: wrapped Home continuation rows and first-row indentation toggle");
                editor->Send(Message::GotoPos, insideRow);
                Key(editor, WXK_HOME, false, true);
                Selection(editor, insideRow, rowStart, "Shift+Home extends to continuation start");
                Key(editor, WXK_HOME, false, true);
                Selection(editor, insideRow, 3, "repeated Shift+Home retains anchor at logical indentation");
                // The pinned public display command supplies the font-dependent visual end;
                // literal logical endpoints below independently check the repeated-key rule.
                editor->Send(Message::GotoPos, insideRow);
                editor->Send(Message::LineEndDisplay);
                const auto rowEnd = editor->Send(Message::GetCurrentPos);
                Require(rowEnd > insideRow && rowEnd < static_cast<intptr_t>(text.size()),
                        "second row ends before logical EOL");
                editor->Send(Message::GotoPos, insideRow);
                Key(editor, WXK_END);
                Selection(editor, rowEnd, rowEnd, "End first reaches the visual row end");
                Key(editor, WXK_END);
                Selection(editor, static_cast<intptr_t>(text.size()), static_cast<intptr_t>(text.size()),
                          "repeated End reaches logical EOL");
                editor->Send(Message::GotoPos, insideRow);
                Key(editor, WXK_END, false, true);
                Selection(editor, insideRow, rowEnd, "Shift+End extends to visual row end");
                Key(editor, WXK_END, false, true);
                Selection(editor, insideRow, static_cast<intptr_t>(text.size()),
                          "repeated Shift+End retains anchor at logical EOL");
                Visible(editor);
                editor->Send(Message::GotoPos, 4);
                Key(editor, WXK_HOME);
                Selection(editor, 3, 3, "Home inside the first visual row reaches indentation");
                editor->Send(Message::GotoPos, 4);
                Key(editor, WXK_END);
                Require(editor->Send(Message::GetCurrentPos) > 4 && editor->Send(Message::GetCurrentPos) < insideRow,
                        "End inside the first visual row stops before the continuation row");
                Unchanged(editor, text);
                editor->Send(Message::SetWrapMode, 0);
                Layout(editor);
                editor->Send(Message::GotoPos, text.size());
                Key(editor, WXK_HOME, false, true);
                Selection(editor, static_cast<intptr_t>(text.size()), 3,
                          "unwrapped Shift+Home reaches tab/space indentation");
                Key(editor, WXK_HOME, false, true);
                Selection(editor, static_cast<intptr_t>(text.size()), 0,
                          "unwrapped repeated Shift+Home reaches logical start");
                Key(editor, WXK_END, false, true);
                Selection(editor, static_cast<intptr_t>(text.size()), static_cast<intptr_t>(text.size()),
                          "unwrapped Shift+End reaches logical end");
                Visible(editor);
                Unchanged(editor, text);
                std::puts("PASS: wrapped End and Shift Home/End selection endpoints");
                Paragraphs(editor);
                editor->Send(Message::SetWrapMode, static_cast<uintptr_t>(Scintilla::Wrap::Word));
                Paragraphs(editor);
                FoldedParagraphs(editor);
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
    void Paragraphs(EditorControl* editor) {
        const std::string text = "\t  alpha中文😀\r\nbeta\r\n \t\r\n\r\nsecond\r\nline\r\n\t\r\nthird";
        const auto second = static_cast<intptr_t>(text.find("second"));
        const auto third = static_cast<intptr_t>(text.find("third"));
        editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
        SeedHistory(editor);
        Layout(editor);
        editor->Send(Message::GotoPos, 4);
        Key(editor, WXK_PAGEDOWN, true);
        Selection(editor, second, second, "Ctrl+PageDown skips nonempty lines and whitespace-only separators");
        Key(editor, WXK_PAGEDOWN, true, true);
        Selection(editor, second, third, "Ctrl+Shift+PageDown extends to next paragraph");
        Key(editor, WXK_PAGEUP, true, true);
        Selection(editor, second, second, "Ctrl+Shift+PageUp contracts selection to paragraph start");
        Key(editor, WXK_PAGEUP, true, true);
        Selection(editor, second, 0, "Ctrl+Shift+PageUp reverses selection through first paragraph");
        editor->Send(Message::GotoPos, second + 2);
        Key(editor, WXK_PAGEUP, true);
        Selection(editor, second, second, "Ctrl+PageUp inside a paragraph visits its own start");
        Key(editor, WXK_PAGEUP, true);
        Selection(editor, 0, 0, "Ctrl+PageUp at paragraph start visits previous paragraph");
        Key(editor, WXK_PAGEUP, true);
        Selection(editor, 0, 0, "paragraph up clamps at document start");
        editor->Send(Message::GotoPos, third);
        Key(editor, WXK_PAGEDOWN, true);
        Selection(editor, static_cast<intptr_t>(text.size()), static_cast<intptr_t>(text.size()),
                  "paragraph down clamps at document end");
        Visible(editor);
        Unchanged(editor, text);
        std::puts("PASS: paragraph navigation, whitespace separators, selection reversal and document boundaries");
    }
    void FoldedParagraphs(EditorControl* editor) {
        std::string text = "top\n\nheader\n\nhidden\n\nvisible\n\n";
        for (int line = 0; line < 60; ++line)
            text += "\t中文😀 long line\n";
        text += "\nlast";
        editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
        SeedHistory(editor);
        editor->Send(Message::SetFoldLevel, 2, 0x2400);
        for (int line = 3; line <= 5; ++line)
            editor->Send(Message::SetFoldLevel, line, 0x401);
        editor->Send(Message::SetFoldLevel, 6, 0x400);
        editor->Send(Message::SetFoldExpanded, 2, 0);
        editor->Send(Message::HideLines, 3, 5);
        editor->Send(Message::GotoPos, 5);
        Layout(editor);
        Key(editor, WXK_PAGEDOWN, true);
        Selection(editor, 21, 21, "paragraph down skips folded paragraph without expanding it");
        Key(editor, WXK_PAGEUP, true, true);
        Selection(editor, 21, 5, "extended paragraph up skips hidden paragraph and retains anchor");
        Key(editor, WXK_END);
        Selection(editor, 11, 11, "End on collapsed header retains its logical endpoint");
        Key(editor, WXK_HOME, false, true);
        Selection(editor, 11, 5, "Shift+Home on collapsed header retains its anchor");
        Require(!editor->Send(Message::GetLineVisible, 4) && !editor->Send(Message::GetFoldExpanded, 2),
                "navigation retains fold contraction");
        editor->Send(Message::GotoPos, 21);
        Key(editor, WXK_PAGEDOWN, true);
        const auto body = static_cast<intptr_t>(text.find("\t中文"));
        Selection(editor, body, body, "paragraph down reaches the long paragraph");
        Key(editor, WXK_PAGEDOWN, true, true);
        Selection(editor, body, static_cast<intptr_t>(text.size()) - 4,
                  "extended paragraph down crosses offscreen lines");
        Visible(editor);
        Key(editor, WXK_PAGEUP, true);
        Selection(editor, body, body, "paragraph up returns from an offscreen paragraph");
        Visible(editor);
        Unchanged(editor, text);
        std::puts("PASS: folded paragraphs, wrapped caret visibility and preserved text/history");
    }
};
} // namespace
wxIMPLEMENT_APP(NavigationApp);
