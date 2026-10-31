#include "native_search_helpers.h"
#include <wx/menu.h>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Paint(EditorControl* editor) {
    editor->Refresh(false);
    editor->Update();
    wxYield();
}
void AllVisible(EditorControl* editor) {
    for (intptr_t line = 0; line < editor->Send(Message::GetLineCount); ++line)
        Require(editor->Send(Message::GetLineVisible, line), "Expand All must reveal every document line");
}
class FoldAllApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->ShowWithoutActivating();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                const std::string source = "void outer() {\n  if (true) {\n    call();\n  }\n  tail();\n}\nafter();\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(source.c_str()));
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "C/C++"));
                Paint(editor);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 0);
                const int collapse = frame->GetMenuBar()->FindMenuItem("View", "Collapse All");
                const int expand = frame->GetMenuBar()->FindMenuItem("View", "Expand All");
                Require(collapse != wxNOT_FOUND && expand != wxNOT_FOUND, "native fold menu commands must exist");
                NativeSearch::Command(frame, collapse);
                Require(!editor->Send(Message::GetFoldExpanded, 0) && !editor->Send(Message::GetFoldExpanded, 1) &&
                            !editor->Send(Message::GetLineVisible, 1) && !editor->Send(Message::GetLineVisible, 2) &&
                            editor->Send(Message::GetLineVisible, 6),
                        "Collapse All must contract both outer and nested lexer headers");
                NativeSearch::Command(frame, collapse);
                NativeSearch::Command(frame, expand);
                AllVisible(editor);
                Require(editor->Send(Message::GetFoldExpanded, 0) && editor->Send(Message::GetFoldExpanded, 1),
                        "Expand All must reset every nested expansion flag");
                editor->Send(Message::FoldLine, 1, static_cast<intptr_t>(Scintilla::FoldAction::Contract));
                NativeSearch::Command(frame, expand);
                NativeSearch::Command(frame, expand);
                AllVisible(editor);
                Require(editor->Send(Message::GetFoldExpanded, 1), "Expand All must open individually collapsed folds");
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo),
                        "repeated fold commands must preserve bytes, save point and empty history");
                std::puts("PASS: actual menu commands contract nested headers and expand every line repeatedly");
                const auto hidden = editor->Send(Message::PositionFromLine, 2) + 4;
                editor->Send(Message::SetSel, hidden + 2, hidden);
                NativeSearch::Command(frame, collapse);
                Require(editor->Send(Message::GetCurrentPos) == 0 && editor->Send(Message::GetAnchor) == 0 &&
                            !editor->Send(Message::GetFoldExpanded, 0),
                        "Collapse All must move hidden caret and anchor to a visible header without reopening folds");
                const auto caretY = editor->Send(Message::PointYFromPosition, 0, editor->Send(Message::GetCurrentPos));
                Require(caretY >= 0 && caretY < editor->GetClientSize().y,
                        "repaired caret must remain inside the visible viewport");
                NativeSearch::Command(frame, expand);
                std::puts("PASS: Collapse All repairs hidden selection endpoints and keeps the caret on screen");
                const auto append = [editor](const char* text) {
                    editor->Send(Message::BeginUndoAction);
                    editor->Send(Message::AppendText, std::char_traits<char>::length(text),
                                 reinterpret_cast<intptr_t>(text));
                    editor->Send(Message::EndUndoAction);
                };
                append("first");
                append("second");
                NativeSearch::Command(frame, wxID_UNDO);
                Require(editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                            editor->Send(Message::GetModify),
                        "history fixture must have Undo, Redo and modified text");
                const auto revision = editor->DocumentRevision();
                NativeSearch::Command(frame, collapse);
                NativeSearch::Command(frame, expand);
                Require(NativeSearch::Text(editor) == source + "first" && editor->DocumentRevision() == revision &&
                            editor->Send(Message::GetModify) && editor->Send(Message::CanUndo) &&
                            editor->Send(Message::CanRedo),
                        "fold commands must preserve dirty bytes, document revision and existing Undo/Redo");
                NativeSearch::Command(frame, wxID_UNDO);
                Require(NativeSearch::Text(editor) == source && !editor->Send(Message::GetModify),
                        "Undo after folding must still reach the original save point");
                NativeSearch::Command(frame, wxID_REDO);
                NativeSearch::Command(frame, wxID_REDO);
                Require(NativeSearch::Text(editor) == source + "firstsecond",
                        "Redo after folding must retain both original edits in order");
                std::puts("PASS: dirty bytes, document revision, save point and existing Undo/Redo survive folding");
                std::string scrolled;
                for (int block = 0; block < 30; ++block)
                    scrolled += source;
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(scrolled.c_str()));
                Paint(editor);
                frame->SetClientSize(frame->FromDIP(wxSize(540, 300)));
                editor->Send(Message::GotoPos, editor->Send(Message::PositionFromLine, 198));
                editor->Send(Message::SetFirstVisibleLine, 190);
                Paint(editor);
                NativeSearch::Command(frame, collapse);
                const auto current = editor->Send(Message::GetCurrentPos);
                const auto currentLine = editor->Send(Message::LineFromPosition, current);
                const auto y = editor->Send(Message::PointYFromPosition, 0, current);
                Require(editor->Send(Message::GetLineVisible, currentLine) && y >= 0 && y < editor->GetClientSize().y &&
                            editor->Send(Message::VisibleFromDocLine, 6) == 1 &&
                            editor->Send(Message::DocLineFromVisible, 1) == 6 &&
                            editor->Send(Message::GetFirstVisibleLine) < editor->Send(Message::GetLineCount),
                        "scrolled Collapse All must map display lines and keep the repaired caret on screen");
                NativeSearch::Command(frame, expand);
                AllVisible(editor);
                Require(editor->Send(Message::VisibleFromDocLine, 198) == 198,
                        "Expand All must restore the full document display mapping");
                std::puts("PASS: resized and scrolled commands retain visible caret and coherent display-line mapping");
                struct LanguageFixture {
                    const char* name;
                    const char* source;
                };
                const LanguageFixture languages[] = {
                    {"C/C++", "void outer() {\n  if (true) {\n    call();\n  }\n}\n"},
                    {"Python", "class Outer:\n    def inner(self):\n        call()\n    tail()\nafter()\n"},
                    {"JSON", "{\n  \"items\": [\n    1,\n    2\n  ]\n}\n"},
                };
                for (const auto& language : languages) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(language.source));
                    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", language.name));
                    Paint(editor);
                    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                    const bool capable = editor->Send(Message::GetMarginWidthN, 1) > 0;
                    Require(frame->GetMenuBar()->IsEnabled(collapse) == capable &&
                                frame->GetMenuBar()->IsEnabled(expand) == capable,
                            "fold command enablement must follow the selected language's folding capability");
                    if (!capable) continue;
                    editor->Send(Message::GotoPos, 0);
                    NativeSearch::Command(frame, collapse);
                    Require(!editor->Send(Message::GetFoldExpanded, 0) && !editor->Send(Message::GetFoldExpanded, 1) &&
                                !editor->Send(Message::GetLineVisible, 2),
                            "every enabled language must support nested Collapse All");
                    NativeSearch::Command(frame, expand);
                    AllVisible(editor);
                    Require(editor->Send(Message::GetFoldExpanded, 0) && editor->Send(Message::GetFoldExpanded, 1),
                            "every enabled language must reset nested flags with Expand All");
                    for (const char* noHeaders : {"plain_value\n", ""}) {
                        editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(noHeaders));
                        editor->Send(Message::EmptyUndoBuffer);
                        editor->Send(Message::SetSavePoint);
                        editor->Send(Message::GotoPos, 0);
                        Paint(editor);
                        NativeSearch::Command(frame, collapse);
                        NativeSearch::Command(frame, collapse);
                        NativeSearch::Command(frame, expand);
                        AllVisible(editor);
                        Require(NativeSearch::Text(editor) == noHeaders && !editor->Send(Message::GetModify) &&
                                    !editor->Send(Message::CanUndo) && editor->Send(Message::GetCurrentPos) == 0,
                                "empty and header-free fold commands must be safe view-only no-ops");
                    }
                    std::printf("PASS: %s nested, empty and header-free documents use native fold commands\n",
                                language.name);
                }
                NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Language", "Plain Text"));
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetMenuBar()->IsEnabled(collapse) && !frame->GetMenuBar()->IsEnabled(expand),
                        "Plain Text must disable both native fold commands");
                std::puts("PASS: native command enablement follows effective language and disables Plain Text");
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
wxIMPLEMENT_APP(FoldAllApp);
