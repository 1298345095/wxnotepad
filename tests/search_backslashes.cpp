#include "editor/editor_control.h"
#include "search/search_session.h"
#include "document/document_session.h"
#include "app/find_replace_dialog.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/textctrl.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
using Scintilla::Message;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Text(EditorControl& editor) {
    std::string text(static_cast<size_t>(editor.Send(Message::GetLength)) + 1, '\0');
    editor.Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
void Load(EditorControl& editor, const std::string& text) {
    editor.Send(Message::ClearAll);
    editor.Send(Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    editor.Send(Message::EmptyUndoBuffer);
    editor.Send(Message::SetSavePoint);
    editor.Send(Message::SetSel, 0, 0);
}
class BackslashApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad backslash transforms");
        auto* editor = new EditorControl(frame);
        frame->Show();
        CallAfter([this, frame, editor] {
            try {
                SearchSession search(*editor);
                Require(!search.Request().options.transformBackslashes, "transform default is off");
                Load(*editor, "中\r\n\t文 中\r\n\t文");
                search.Configure({"\\u4E2D\\r\\n\\t\\u6587", "", {false, false, false, true}, false});
                const auto first = search.Find(SearchDirection::Forward);
                Require(first.status == FindStatus::Found && first.start == 0 && first.end == 9,
                        "transformed multiline query selects UTF-8 bytes");
                const auto second = search.Find(SearchDirection::Forward);
                Require(second.status == FindStatus::Found && second.start == 10 && second.end == 19,
                        "Find Next shares transformed query");
                editor->Send(Message::SetSel, 19, 19);
                const auto previous = search.Find(SearchDirection::Backward);
                Require(previous.status == FindStatus::Found && previous.start == 10 && previous.end == 19,
                        "literal Previous supports transformed multiline ranges");
                auto markedRequest = search.Request();
                markedRequest.bookmarkMatchedLines = true;
                search.Configure(markedRequest);
                Require(search.FindAll().count == 2 && editor->Send(Message::IndicatorValueAt, 8, 0) &&
                            editor->Send(Message::IndicatorValueAt, 8, 18) &&
                            (editor->Send(Message::MarkerGet, 0) & 1) && (editor->Send(Message::MarkerGet, 1) & 1) &&
                            (editor->Send(Message::MarkerGet, 2) & 1),
                        "Find All shares transformed bytes and bookmarks multiline matches");
                std::puts("PASS: transformed multiline Find Next/Previous and UTF-8 positions");
                Load(*editor, "中\r\n\t文 中\r\n\t文");
                search.Configure(
                    {"\\u4E2D\\r\\n\\t\\u6587", "\\u6587\\n$1\\1\\\\", {false, false, false, true}, false});
                const auto all = search.ReplaceAll();
                Require(all.count == 2 && all.changed && Text(*editor) == "文\n$1\\1\\ 文\n$1\\1\\",
                        "literal transforms preserve capture spellings as literal bytes");
                editor->Send(Message::Undo);
                Require(Text(*editor) == "中\r\n\t文 中\r\n\t文" && !editor->Send(Message::GetModify),
                        "transformed Replace All is one undo action");
                search.Configure(
                    {"\\u4E2D\\r\\n\\t\\u6587", "\\u4E2D\\r\\n\\t\\u6587", {false, false, false, true}, false});
                Require(search.ReplaceAll().count == 2 && editor->Send(Message::CanRedo) &&
                            !editor->Send(Message::GetModify),
                        "unchanged transformed replacement retains redo and save point");
                search.Configure({"missing\\n", "changed", {false, false, false, true}, false});
                Require(search.ReplaceAll().count == 0 && editor->Send(Message::CanRedo),
                        "transformed no-match replacement preserves history");
                std::puts("PASS: transformed replacement bytes, capture distinction, Undo and no-op history");
                const std::pair<const char*, std::string> forms[] = {
                    {"\\a\\b\\e\\f\\n\\r\\t\\v\\\\", "\a\b\x1b\f\n\r\t\v\\"},
                    {"\\x41\\xFF\\x00Z", std::string("A\xff\0Z", 4)},
                    {"\\u4E2D\\uD83D\\uDE00", "中😀"},
                    {"\\uD800\\u0041\\uDC00", "�A�"},
                    {"\\uD800\\uD800\\uDC00", "�𐀀"},
                    {"\\x1 \\xGG \\u123 \\uGGGG \\q \\0 \\U00000041 \\",
                     "\\x1 \\xGG \\u123 \\uGGGG \\q \\0 \\U00000041 \\"},
                };
                for (const auto& [escaped, expected] : forms) {
                    Load(*editor, "[" + expected + "]");
                    search.Configure({escaped, "", {false, false, false, true}, false});
                    const auto found = search.Find(SearchDirection::Forward);
                    Require(found.status == FindStatus::Found && found.start == 1 && found.end == 1 + expected.size(),
                            "original escape grammar selects exact document bytes");
                    search.Configure({"[", escaped, {false, false, false, true}, false});
                    editor->Send(Message::SetSel, 0, 1);
                    Require(search.ReplaceOne().changed && Text(*editor) == expected + expected + "]",
                            "Replace One uses the same original transform grammar");
                    editor->Send(Message::Undo);
                    Require(Text(*editor) == "[" + expected + "]", "transformed Replace One undoes exactly");
                }
                std::puts("PASS: controls, raw hex/NUL, Unicode/surrogates and invalid/incomplete escapes");
                Load(*editor, "a\nb a\nb a\nb");
                editor->Send(Message::SetSel, 4, 7);
                search.Configure({"a\\nb", "X\\tY", {false, false, false, true}, false});
                Require(search.ReplaceInSelection().count == 1 && Text(*editor) == "a\nb X\tY a\nb",
                        "selection replacement shares transformed multiline bytes");
                editor->Send(Message::Undo);
                Require(Text(*editor) == "a\nb a\nb a\nb", "selection replacement undoes in one action");
                Load(*editor, "\\n\n");
                search.Configure({"\\n", "", {}, false});
                Require(search.Find(SearchDirection::Forward).end == 2, "default literal input is unchanged");
                std::puts("PASS: transformed selection replacement and unchanged default literal mode");
                const wxString path = wxFileName::CreateTempFileName("wxnotepad-backslashes-");
#ifdef __WXMSW__
                const std::filesystem::path nativePath(path.ToStdWstring());
#else
                const std::filesystem::path nativePath(path.ToStdString(wxConvUTF8));
#endif
                const std::string original("\xff\xfe\x2d\x4e\x0d\0\x0a\0\x87\x65", 10);
                {
                    std::ofstream file(nativePath, std::ios::binary);
                    file.write(original.data(), original.size());
                }
                DocumentSession document(*editor);
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened,
                        "open independent UTF-16LE fixture");
                search.Configure({"\\u4E2D\\r\\n\\u6587", "\\u6587\\t\\u4E2D", {false, false, false, true}, false});
                const auto encodedMatch = search.Find(SearchDirection::Forward);
                Require(encodedMatch.start == 0 && encodedMatch.end == 8 && search.ReplaceOne().changed,
                        "encoded file search still uses UTF-8 editor positions");
                Require(document.Save().status == SaveStatus::Saved, "save transformed UTF-16LE replacement");
                std::ifstream saved(nativePath, std::ios::binary);
                const std::string bytes{std::istreambuf_iterator<char>(saved), std::istreambuf_iterator<char>()};
                saved.close();
                wxRemoveFile(path);
                Require(bytes == std::string("\xff\xfe\x87\x65\x09\0\x2d\x4e", 8),
                        "independent disk bytes preserve file encoding after transformed replacement");
                std::puts("PASS: independent UTF-16LE disk bytes and UTF-8 search positions");
                SearchSession nativeSearch(*editor);
                FindResult nativeResult{FindStatus::NotFound};
                auto* dialog = new FindReplaceDialog(
                    frame, *editor, nativeSearch, [&](const FindResult& result) { nativeResult = result; },
                    [](wxKeyEvent& event) { event.Skip(); }, 0);
                dialog->Present(false);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", dialog), wxTextCtrl);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", dialog), wxTextCtrl);
                auto* transform =
                    wxDynamicCast(wxWindow::FindWindowByName("Transform backslashes", dialog), wxCheckBox);
                auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", dialog), wxCheckBox);
                Require(query && replacement && transform && regex && transform->IsEnabled() && !transform->GetValue(),
                        "native Find exposes enabled unchecked transform option");
                NativeSearch::Toggle(regex, true);
                NativeSearch::Toggle(transform, true);
                Require(!regex->GetValue() && transform->GetValue(), "checking transform unchecks regular expression");
                Load(*editor, "\\n\n");
                query->SetValue("\\n");
                NativeSearch::Click(dialog, wxID_FORWARD);
                Require(nativeResult.status == FindStatus::Found && nativeResult.start == 2 && nativeResult.end == 3,
                        "native Find applies transformed newline");
                NativeSearch::Toggle(regex, true);
                Require(regex->GetValue() && !transform->GetValue() && transform->IsEnabled(),
                        "checking regex unchecks transform while keeping it enabled");
                dialog->Present(true);
                wxYield();
                NativeSearch::Toggle(transform, true);
                Require(transform->GetValue() && !regex->GetValue(), "replacement view enables transform exclusively");
                query->SetValue("\\n");
                replacement->SetValue("\\t$1");
                NativeSearch::Click(dialog, wxID_REPLACE_ALL);
                if (Text(*editor) != "\\n\t$1")
                    std::fprintf(stderr, "Native bytes [%s]; transform=%d regex=%d replacement=[%s]\n",
                                 Text(*editor).c_str(), nativeSearch.Request().options.transformBackslashes,
                                 nativeSearch.Request().options.regularExpression,
                                 nativeSearch.Request().replacement.utf8_str().data());
                Require(Text(*editor) == "\\n\t$1",
                        "native Replace shares literal transform without capture formatting");
                editor->Send(Message::Undo);
                Require(Text(*editor) == "\\n\n", "native transformed Replace All undoes once");
                NativeSearch::Toggle(transform, false);
                editor->Send(Message::SetSel, 0, 0);
                NativeSearch::Click(dialog, wxID_FORWARD);
                Require(nativeResult.start == 0 && nativeResult.end == 2,
                        "unchecking transform searches input backslashes literally");
                std::puts("PASS: native Find/Replace transform control, regex exclusion and enabled/off behavior");
                frame->Destroy();
                ExitMainLoop();
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
                frame->Destroy();
                ExitMainLoop();
            }
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(BackslashApp);
