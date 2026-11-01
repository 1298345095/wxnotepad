#include "document/document_session.h"
#include "editor/editor_control.h"
#include "search/search_session.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::filesystem::path NativePath(const wxString& path) {
#ifdef __WXMSW__
    return path.ToStdWstring();
#else
    return path.ToStdString(wxConvUTF8);
#endif
}
struct Files {
    wxString base = wxFileName::CreateTempFileName("wxnotepad-sessions-");
    wxString cpp = base + ".cpp";
    wxString json = base + ".json";
    ~Files() {
        for (const auto* path : {&base, &cpp, &json}) {
            if (wxFileExists(*path)) wxRemoveFile(*path);
        }
    }
};
std::string Text(EditorControl& editor) {
    std::string text(static_cast<size_t>(editor.Send(Message::GetLength)) + 1, '\0');
    editor.Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
void Load(EditorControl& editor, const char* text) {
    editor.Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor.Send(Message::EmptyUndoBuffer);
    editor.Send(Message::SetSavePoint);
    editor.Send(Message::SetSel, 0, 0);
}
class SessionApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad feature sessions");
        auto* editor = new EditorControl(frame);
        frame->Show();
        CallAfter([this, frame, editor] {
            try {
                SearchSession search(*editor);
                Require(search.Find(SearchDirection::Forward).status == FindStatus::EmptyQuery,
                        "search must operate without any dialog");
                Load(*editor, "one one");
                editor->Send(Message::InsertText, 7, reinterpret_cast<intptr_t>("!"));
                editor->Send(Message::Undo);
                editor->Send(Message::SetSel, 0, 0);
                search.Configure({"one", "one", {}, false});
                const auto match = search.Find(SearchDirection::Forward);
                Require(match.status == FindStatus::Found && match.start == 0 && match.end == 3 && !match.wrapped,
                        "Find returns the selected byte range");
                const auto one = search.ReplaceOne();
                Require(!one.changed && one.next && one.next->start == 4 && one.next->status == FindStatus::Found &&
                            editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
                        "no-op replacement reports no edit, advances and preserves redo/save point");
                const auto all = search.ReplaceAll();
                Require(all.status == ReplaceAllStatus::Completed && all.count == 2 && !all.changed &&
                            editor->Send(Message::CanRedo) && Text(*editor) == "one one",
                        "Replace All returns the count without introducing no-op history");

                Load(*editor, "中文");
                search.Configure({wxString::FromUTF8("(?=文)"), "X", {false, false, true}, false});
                Require(search.Find(SearchDirection::Forward).start == 3, "regex positions are UTF-8 byte offsets");
                const auto repeated = search.Find(SearchDirection::Forward);
                Require(repeated.status == FindStatus::Found && repeated.start == 3 && repeated.end == 3,
                        "pinned Find resumes at the selection end, including the same zero-width match");
                auto request = search.Request();
                request.query = "other";
                search.Configure(request);
                request.query = wxString::FromUTF8("(?=文)");
                search.Configure(request);
                const auto reset = search.Find(SearchDirection::Forward);
                Require(reset.status == FindStatus::Found && reset.start == 3,
                        "restoring the query resumes at the current empty selection");
                Load(*editor, "aab");
                search.Configure({"aa|(?<=aa)b", "", {false, false, true}, false});
                Require(search.Find(SearchDirection::Forward).end == 2, "original alternation first selects aa");
                Require(search.Find(SearchDirection::Forward).status == FindStatus::NotFound,
                        "original forward resumption does not retain lookbehind before target start");
                Load(*editor, "a\nb a\nb");
                editor->Send(Message::GotoPos, 7);
                search.Configure({"a\\nb", "", {false, false, true}, false});
                Require(search.Find(SearchDirection::Backward).status == FindStatus::NotFound,
                        "original backward regex searches separate lines without cross-line matches");
                Load(*editor, "");
                search.Configure({"^", "X", {false, false, true}, false});
                Require(search.ReplaceOne().changed && Text(*editor) == "X",
                        "original Replace accepts matching empty selection without separate confirmation");
                Load(*editor, "a a");
                editor->Send(Message::SetSel, 0, 3);
                search.Configure({"(a)", "X", {false, false, true}, false});
                const auto selectedMatch = search.ReplaceOne();
                Require(!selectedMatch.changed && selectedMatch.next &&
                            selectedMatch.next->status == FindStatus::Found && selectedMatch.next->start == 0 &&
                            selectedMatch.next->end == 1 && Text(*editor) == "a a",
                        "original Replace selects the first revalidation match inside a larger selection");
                Load(*editor, "a");
                search.Configure({"a*", "_", {false, false, true}, false});
                const auto consumingAll = search.ReplaceAll();
                Require(consumingAll.count == 1 && Text(*editor) == "_",
                        "original Replace All stops at document end after a consuming replacement");
                Load(*editor, "a\nb\n");
                search.Configure({"^", "", {false, false, true}, false});
                Require(
                    search.ReplaceAll().count == 2 && Text(*editor) == "a\nb\n" && !editor->Send(Message::CanUndo),
                    "original pattern-leading caret skips the final empty line when replacement ends at line start");
                Load(*editor, "中文");
                request.query = "(";
                search.Configure(request);
                Require(search.Find(SearchDirection::Forward).status == FindStatus::InvalidPattern &&
                            search.ReplaceAll().status == ReplaceAllStatus::InvalidPattern && Text(*editor) == "中文",
                        "invalid patterns return structured errors without changing text");
                std::puts("PASS: independent SearchSession outcomes, progress and history");

                Files files;
                const std::string bytes = "int value;\n";
                {
                    std::ofstream output(NativePath(files.cpp), std::ios::binary);
                    output << bytes;
                    Require(output.good(), "fixture write must succeed");
                }
                DocumentSession document(*editor);
                Require(document.Open(files.cpp, [] { return true; }).status == OpenStatus::Opened &&
                            document.Language() == SyntaxLanguage::Cpp && !document.LanguageOverride(),
                        "document Open owns automatic classification without a frame");
                document.SelectLanguage(SyntaxLanguage::Python);
                Require(document.SaveAs(files.json, {}, SaveType::AllFiles).status == SaveStatus::Saved &&
                            document.Language() == SyntaxLanguage::Json && !document.LanguageOverride(),
                        "Save As clears the explicit language choice and reidentifies from the filename");
                document.SelectLanguage(std::nullopt);
                Require(document.Language() == SyntaxLanguage::Json, "Automatic follows the current file path");
                Require(document.SaveAs(std::nullopt).status == SaveStatus::Cancelled &&
                            document.Language() == SyntaxLanguage::Json && document.Path() == files.json,
                        "cancelled Save As retains document state");
                std::ifstream saved(NativePath(files.json), std::ios::binary);
                const std::string disk{std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()};
                Require(disk == bytes, "language changes preserve independently read saved bytes");
                saved.close();
                document.New();
                Require(document.Language() == SyntaxLanguage::PlainText && !document.LanguageOverride() &&
                            !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            Text(*editor).empty() && search.Request().query == "(",
                        "New resets document identity/language while retaining search preferences");
                std::puts("PASS: independent DocumentSession language transitions and saved bytes");
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

wxIMPLEMENT_APP(SessionApp);
