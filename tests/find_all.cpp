#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include "document/document_session.h"
#include "native_search_helpers.h"
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/filename.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>

namespace {
using Scintilla::Message;
using namespace NativeSearch;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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
                Command(frame, wxID_FIND);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* action = wxDynamicCast(wxWindow::FindWindowByName("Find All", frame), wxButton);
                Require(query && action, "native Find exposes Find All");
                query->SetValue("one");
                Click(query->GetParent(), action->GetId());
                auto* result = wxDynamicCast(wxWindow::FindWindowByName("Find All result", frame), wxStaticText);
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
                const auto run = [&] { Click(query->GetParent(), action->GetId()); };
                const auto load = [&](const std::string& bytes) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(bytes.c_str()));
                    editor->Send(Message::MarkerDeleteAll, 0);
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    wxYield();
                };
                Toggle(bookmark, true);
                run();
                Require((editor->Send(Message::MarkerGet, 0) & 1) && (editor->Send(Message::MarkerGet, 1) & 1),
                        "Find All bookmarks each matched line");
                query->SetValue("absent");
                Require(!marked(0) && result->GetLabel().empty() && (editor->Send(Message::MarkerGet, 0) & 1),
                        "query change clears count and marks while retaining Find All bookmarks");
                Toggle(bookmark, false);
                query->SetValue("one");
                run();
                Require(editor->Send(Message::MarkerNext, 0, 1) == -1,
                        "next Find All without bookmarks clears the prior Find All bookmarks");
                editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("!"));
                wxYield();
                Require(result->GetLabel().empty() && !marked(1), "edits invalidate count and occurrence marks");
                editor->Send(Message::Undo);
                wxYield();
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
                Require(result->GetLabel() == "1 matches" && !marked(0), "original caret anchor line-count rule");
                query->SetValue("$");
                run();
                Require(result->GetLabel() == "2 matches" && !marked(3), "original dollar anchor line-count rule");
                query->SetValue("(");
                run();
                Require(result->GetLabel() == "Invalid regular expression" && !marked(0),
                        "invalid patterns clear old markings and report an error");
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

                load("void f() {\n  needle();\n}\n");
                const auto cpp = frame->GetMenuBar()->FindMenuItem("Language", "C/C++");
                Command(frame, cpp);
                editor->Send(Message::Colourise, 0, -1);
                editor->Send(Message::FoldAll, static_cast<uintptr_t>(Scintilla::FoldAction::Contract));
                Require(!editor->Send(Message::GetLineVisible, 1), "folded fixture hides matched line");
                query->SetValue("needle");
                run();
                Require(result->GetLabel() == "1 matches" && marked(13) && !editor->Send(Message::GetLineVisible, 1),
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
                wxYield();
                Require(result->GetLabel().empty() && editor->Send(Message::MarkerNext, 0, 1) == -1 && !marked(0),
                        "document replacement clears count, marks and bookmarks");
                Toggle(regex, true);
                query->SetValue("$");
                run();
                Require(result->GetLabel() == "1 matches", "original dollar anchor counts an empty document");
                document.New();
                wxYield();
                Require(result->GetLabel().empty(), "replacing an already empty document invalidates its count");
                query->SetValue("");
                Require(!action->IsEnabled(), "Find All disables on an empty query");
                std::puts("PASS: folded content, disk bytes, replacement invalidation and empty query");
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
wxIMPLEMENT_APP(FindAllApp);
