#include "diagnostics/print.h"
#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "document/document_session.h"
#include "native_search_helpers.h"
#include "native_paint_helpers.h"
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/filename.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <print>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace {
using Scintilla::Message;
using namespace NativeSearch;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Load(EditorControl* editor, const std::string& bytes) {
    using enum Scintilla::Message;
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(bytes.c_str()));
    editor->Send(MarkerDeleteAll, 0);
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    NativePaint::Complete(editor);
}
void SlicedFindAll(EditorControl* editor, wxTextCtrl* query, const wxButton* action, const wxStaticText* result) {
    // The first search slice covers at most its initial 1 MiB
    // window plus a line. Short lines keep the trailing matches
    // outside that window without relying on elapsed search time.
    std::string slicedText;
    constexpr size_t ShortLineBytes = 64;
    const std::string shortLine = std::string(ShortLineBytes - 1, 'x') + '\n';
    constexpr size_t PrefixLines = size_t{48} * 1024;
    for (size_t line = 0; line < PrefixLines; ++line)
        slicedText += shortLine;
    slicedText += "cat cat cat\n";
    Load(editor, slicedText);
    query->SetValue("cat");
#ifdef __WXMSW__
    const auto handle = static_cast<HWND>(action->GetHandle());
    SendMessageW(static_cast<HWND>(query->GetParent()->GetHandle()), WM_COMMAND,
                 MAKEWPARAM(GetDlgCtrlID(handle), BN_CLICKED), reinterpret_cast<LPARAM>(handle));
#elif defined(__WXGTK__)
    gtk_button_clicked(GTK_BUTTON(action->GetHandle()));
#endif
    Require(result->GetLabel() == "0 matches", "Find All initially reports the unfinished search count");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (result->GetLabel() != "3 matches" && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        if (result->GetLabel() != "3 matches") wxMilliSleep(1);
    }
    Require(editor->Send(Message::IndicatorValueAt, 8, static_cast<intptr_t>(slicedText.rfind("cat"))),
            "sliced Find All reaches the last match");
    Require(result->GetLabel() == "3 matches", "Find All refreshes its count after later search slices");
    Require(Text(editor) == slicedText && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
            "sliced Find All preserves bytes and history");
    std::puts("PASS: native Find All refreshes the initial partial count after asynchronous search");
}
class FindAllApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                auto* editor = frame->Editor();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one one\none"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 4, 7);
                NativePaint::Complete(editor);
                Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                const auto* action = wxDynamicCast(wxWindow::FindWindowByName("Find All", frame), wxButton);
                Require(query && action, "native Find exposes Find All");
                query->SetValue("one");
                Click(query->GetParent(), action->GetId());
                NativePaint::Complete(editor);
                const auto* result = wxDynamicCast(wxWindow::FindWindowByName("Find All result", frame), wxStaticText);
                Require(result && result->GetLabel() == "3 matches", "Find All reports whole-document count");
                Require(editor->Send(Message::IndicatorValueAt, 8, 0) &&
                            editor->Send(Message::IndicatorValueAt, 8, 4) &&
                            editor->Send(Message::IndicatorValueAt, 8, 8),
                        "Find All marks every occurrence");
                Selection(editor, 4, 7, "Find All preserves selection");
                Require(Text(editor) == "one one\none" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "Find All preserves bytes and history");
                std::puts("PASS: native Find All whole-document count, indicators, selection and history");
                auto* bookmark = wxDynamicCast(wxWindow::FindWindowByName("Bookmark matched line", frame), wxCheckBox);
                auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", frame), wxCheckBox);
                Require(bookmark && regex && !bookmark->GetValue(), "bookmark option defaults off");
                const auto marked = [&](intptr_t pos) { return editor->Send(Message::IndicatorValueAt, 8, pos); };
                const auto run = [&] {
                    NativePaint::Complete(editor);
                    Click(query->GetParent(), action->GetId());
                    NativePaint::Complete(editor);
                };
                const auto load = [&](const std::string& bytes) { Load(editor, bytes); };
                Toggle(bookmark, true);
                run();
                Require((editor->Send(Message::MarkerGet, 0) & 1) && (editor->Send(Message::MarkerGet, 1) & 1),
                        "Find All bookmarks each matched line");
                query->SetValue("one ");
                Require(marked(0) && result->GetLabel() == "3 matches",
                        "editing the query retains submitted Find All state until a dialog action");
                editor->Send(Message::GotoPos, 0);
                Click(query->GetParent(), wxID_FORWARD);
                NativePaint::Complete(editor);
                Require(
                    marked(0) && !marked(4) && result->GetLabel().empty() && (editor->Send(Message::MarkerGet, 0) & 1),
                    "changed-query FindNext replaces Find All with automatic selection marks and retains bookmarks");
                Toggle(bookmark, false);
                query->SetValue("one");
                run();
                Require(editor->Send(Message::MarkerNext, 0, 1) == 0,
                        "Find All without bookmarks retains markers after automatic selection takes channel ownership");
                editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("!"));
                NativePaint::Complete(editor);
                Require(result->GetLabel().empty() && !marked(1), "edits invalidate count and occurrence marks");
                editor->Send(Message::Undo);
                NativePaint::Complete(editor);
                run();
                Require(editor->Send(Message::CanRedo), "marking preserves pending redo");
                editor->Send(Message::Redo);
                Require(Text(editor) == "!one one\none", "marking retains original redo edit");
                std::puts("PASS: bookmark default, enable/disable, query/edit invalidation and redo");

                load("one one\none");
                Toggle(regex, true);
                Toggle(bookmark, true);
                query->SetValue("(?=one)");
                run();
                Require(result->GetLabel() == "3 matches" && !marked(0) &&
                            editor->Send(Message::MarkerNext, 0, 1) == -1,
                        "zero-width matches count without marks or bookmarks");
                query->SetValue("^");
                run();
                Require(result->GetLabel() == "1 match" && !marked(0), "original caret anchor line-count rule");
                query->SetValue("$");
                run();
                Require(result->GetLabel() == "2 matches" && !marked(3), "original dollar anchor line-count rule");
                query->SetValue("(");
                run();
                editor->Send(Message::ZoomIn);
                NativePaint::Complete(editor);
                Require(result->GetLabel() == "Invalid regular expression" && !marked(0),
                        "invalid patterns clear markings and retain error feedback across editor UI updates");
                editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("!"));
                NativePaint::Complete(editor);
                Require(result->GetLabel().empty(), "document edits clear stale invalid-pattern feedback");
                editor->Send(Message::Undo);
                NativePaint::Complete(editor);
                query->SetValue("absent");
                run();
                Require(result->GetLabel() == "0 matches", "no matches reports zero");
                std::puts("PASS: original empty-match and anchor counts, invalid regex and no matches");

                load("abc");
                query->SetValue("a|(?=b)|c");
                run();
                Require(result->GetLabel() == "3 matches" && marked(0) && marked(1) && !marked(2),
                        "original adjacent range aggregation retains mixed zero-width indicator positions");
                load("a\nb");
                query->SetValue("a\\n|b");
                run();
                Require(result->GetLabel() == "2 matches" && marked(2) && (editor->Send(Message::MarkerGet, 0) & 1) &&
                            !(editor->Send(Message::MarkerGet, 1) & 1),
                        "original adjacent multiline regex ranges share the first bookmarked line");

                load("a\nb\na\nb");
                query->SetValue("a\\nb");
                run();
                Require(result->GetLabel() == "2 matches" && marked(0) && marked(4) &&
                            (editor->Send(Message::MarkerGet, 0) & 1) && !(editor->Send(Message::MarkerGet, 1) & 1) &&
                            (editor->Send(Message::MarkerGet, 2) & 1),
                        "multiline regex marks whole matches and bookmarks original start-line scope");
                load("中文 中文");
                Toggle(regex, false);
                query->SetValue(wxString::FromUTF8("文"));
                run();
                Require(result->GetLabel() == "2 matches" && marked(3) && marked(10) && !marked(0),
                        "Unicode matches retain UTF-8 indicator boundaries");
                Require(Text(editor) == "中文 中文" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "matching preserves Unicode bytes and clean history");
                std::puts("PASS: multiline regex bookmark scope and Unicode byte boundaries");

                SlicedFindAll(editor, query, action, result);

                load("void f() {\n  needle();\n}\n");
                const auto cpp = frame->GetMenuBar()->FindMenuItem("Scheme", "C/C++");
                Command(frame, cpp);
                editor->Send(Message::Colourise, 0, -1);
                editor->Send(Message::FoldAll,
                             static_cast<uintptr_t>(std::to_underlying(Scintilla::FoldAction::Contract)));
                Require(!editor->Send(Message::GetLineVisible, 1), "folded fixture hides matched line");
                query->SetValue("needle");
                run();
                Require(result->GetLabel() == "1 match" && marked(13) && !editor->Send(Message::GetLineVisible, 1),
                        "Find All marks folded content without opening folds");
                const auto path = wxFileName::CreateTempFileName("wxnotepad-find-all-");
                DocumentSession document(*editor);
                Require(document.SaveAs(path).status == SaveStatus::Saved, "marked document saves");
#ifdef __WXMSW__
                std::ifstream saved(std::filesystem::path(path.ToStdWstring()), std::ios::binary);
#else
                std::ifstream saved(path.ToStdString(wxConvUTF8), std::ios::binary);
#endif
                const std::string disk{std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()};
                saved.close();
                wxRemoveFile(path);
                Require(disk == "void f() {\n  needle();\n}\n", "Find All preserves independently read disk bytes");
                document.New();
                NativePaint::Complete(editor);
                Require(result->GetLabel().empty() && editor->Send(Message::MarkerNext, 0, 1) == -1 && !marked(0),
                        "document replacement clears count, marks and bookmarks");
                Toggle(regex, true);
                query->SetValue("$");
                run();
                Require(result->GetLabel() == "1 match", "original dollar anchor counts an empty document");
                document.New();
                NativePaint::Complete(editor);
                Require(result->GetLabel().empty(), "replacing an already empty document invalidates its count");
                query->SetValue("");
                Require(!action->IsEnabled(), "Find All disables on an empty query");
                std::puts("PASS: folded content, disk bytes, replacement invalidation and empty query");
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
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(FindAllApp);
