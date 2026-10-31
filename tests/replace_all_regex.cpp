#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>
#include <wx/textctrl.h>
#include "native_search_helpers.h"
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <filesystem>
#include <fstream>
#include <wx/statusbr.h>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
using NativeSearch::Click;
using NativeSearch::Command;
using NativeSearch::NativeF3;
using NativeSearch::Selection;
using NativeSearch::Text;
using NativeSearch::Toggle;
class RegexReplaceAllApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                Command(frame, wxID_REPLACE);
                auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
                auto* replacement = wxDynamicCast(wxWindow::FindWindowByName("Replacement text", frame), wxTextCtrl);
                auto* regex = wxDynamicCast(wxWindow::FindWindowByName("Regular expression", frame), wxCheckBox);
                Require(query && replacement && regex, "native Replace menu exposes regex controls");
                auto* dialog = query->GetParent();
                Toggle(regex, true);
                Require(dialog->FindWindow(wxID_REPLACE_ALL)->IsEnabled(), "regex Replace All must be enabled");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文12 中文34"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetSel, 17, 0);
                query->SetValue(wxString::FromUTF8("(中文)([0-9]+)"));
                replacement->SetValue("$2-$1");
                editor->Send(Message::GotoPos, 0);
                NativeF3(query);
                Selection(editor, 0, 8, "native F3 must confirm regex capture search");
                editor->Send(Message::SetSel, 17, 0);
                Click(dialog, wxID_REPLACE_ALL);
                Require(Text(editor) == "12-中文 34-中文", "full-document regex replacement expands each capture");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == "中文12 中文34" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "one Undo restores bytes and savepoint");
                Command(frame, wxID_REDO);
                Require(Text(editor) == "12-中文 34-中文", "one Redo restores all expansions");
                std::puts("PASS: native regex Replace All captures and grouped history");
                auto* result = wxDynamicCast(wxWindow::FindWindowByName("Replacement result", dialog), wxStaticText);
                Require(result, "Replace All must expose its result count");
                const auto example = [&](const char* before, const char* pattern, const char* with,
                                         const char* expected, const char* count) {
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(before));
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    editor->Send(Message::SetSel, editor->Send(Message::GetLength), 0);
                    query->SetValue(wxString::FromUTF8(pattern));
                    replacement->SetValue(wxString::FromUTF8(with));
                    Click(dialog, wxID_REPLACE_ALL);
                    if (Text(editor) != expected || result->GetLabel() != count)
                        std::fprintf(stderr, "Pattern %s: actual [%s] / %s; expected [%s] / %s\n", pattern,
                                     Text(editor).c_str(), result->GetLabel().utf8_str().data(), expected, count);
                    Require(Text(editor) == expected && result->GetLabel() == count,
                            "Replace All must produce the explicit regex bytes and count");
                    Command(frame, wxID_UNDO);
                    Require(Text(editor) == before && !editor->Send(Message::GetModify) &&
                                !editor->Send(Message::CanUndo),
                            "one Undo restores each complete replacement");
                    Command(frame, wxID_REDO);
                    Require(Text(editor) == expected && editor->Send(Message::GetModify),
                            "one Redo restores each complete replacement");
                };
                example("a", "a*", "_", "__", "2 replacements");
                std::puts("PASS: consuming match followed by eligible empty EOF match");
                example("a", "a|$", "_", "__", "2 replacements");
                example("ab", "a|(?=b)", "_", "__b", "2 replacements");
                example("中文ab", "a*", "_", "_中_文__b_", "5 replacements");
                example("中文中文", "(?=中)", "_", "_中文_中文", "2 replacements");
                example("😀中", "(?=.)", "_", "_😀_中", "2 replacements");
                example("中文\n测试\n", "^", ">", ">中文\n>测试\n>", "3 replacements");
                example("中文\r\n测试\r\n", "(?:^)", ">", ">中文\r\n>测试\r\n>", "3 replacements");
                example("中文\r\n测试\r\n", "$", "<", "中文<\r\n测试<\r\n<", "3 replacements");
                example("a\n\nb\n", "^$", "X", "a\nX\nb\nX", "2 replacements");
                example("a\r\nb\nc\r", "^", ">", ">a\r\n>b\n>c\r>", "4 replacements");
                example("", "^", "X", "X", "1 replacements");
                example("", "$", "X", "X", "1 replacements");
                example("", "\\A|\\z", "X", "X", "1 replacements");
                example("中\n", "\\A|\\z", "X", "X中\nX", "2 replacements");
                example("a\r\n", "^", "\\n>", "\n>a\r\n\n>", "2 replacements");
                example("中", "(?=中)", "中", "中中", "1 replacements");
                std::puts("PASS: empty/consuming alternatives, anchors, empty/final-newline documents, CRLF and UTF-8 "
                          "progress");
                example("aaaa", "(aa)", "<$1>", "<aa><aa>", "2 replacements");
                example("aaaa", "aa", "", "", "2 replacements");
                example("aa", "a", "aa", "aaaa", "2 replacements");
                example("中文12 中文34", "(中文)([0-9]+)", "\\2-\\1", "12-中文 34-中文", "2 replacements");
                example("中文12", "(中文)([0-9]+)", "$2:$1:$$:$&:$0:\\0", "12:中文:$:中文12:中文12:中文12",
                        "1 replacements");
                example("b b", "(a)?(b)", "$1|\\1|$2|\\2", "||b|b ||b|b", "2 replacements");
                example("a a", "(a)", "\\1\\n\\t", "a\n\t a\n\t", "2 replacements");
                example("a a", "(a)", "$$|\\\\|$10|${name}|\\u|$|\\",
                        "$|\\|a0|${name}|\\u|$|\\ $|\\|a0|${name}|\\u|$|\\", "2 replacements");
                example("中文后 中文后", "(?<=中)文(?=后)", "X", "中X后 中X后", "2 replacements");
                example("aab", "aa|(?<=aa)b", "$&!", "aa!b", "1 replacements");
                example("中\n文 中\n文", "(中)\\n(文)", "$2:$1", "文:中 文:中", "2 replacements");
                auto* matchCase = wxDynamicCast(wxWindow::FindWindowByName("Match case", frame), wxCheckBox);
                auto* wrap = wxDynamicCast(wxWindow::FindWindowByName("Wrap around", frame), wxCheckBox);
                Toggle(wrap, false);
                Toggle(matchCase, true);
                example("one ONE One", "(one)", "<$1>", "<one> ONE One", "1 replacements");
                Toggle(matchCase, false);
                example("one ONE One", "(one)", "<$1>", "<one> <ONE> <One>", "3 replacements");
                std::puts(
                    "PASS: captures, original replacement formats, growth/shrink/deletion and active case options");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中文 a"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::GotoPos, 8);
                editor->Send(Message::AddText, 1, reinterpret_cast<intptr_t>("!"));
                Command(frame, wxID_UNDO);
                editor->Send(Message::SetSel, 8, 0);
                const auto unchanged = [&](const char* pattern, const char* with, const char* label) {
                    query->SetValue(wxString::FromUTF8(pattern));
                    replacement->SetValue(wxString::FromUTF8(with));
                    Click(dialog, wxID_REPLACE_ALL);
                    Require(Text(editor) == "中文 a" && !editor->Send(Message::GetModify) &&
                                !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                            "no-op/invalid regex must preserve text, savepoint, Undo and Redo");
                    Require(editor->Send(Message::GetAnchor) == 8 && editor->Send(Message::GetCurrentPos) == 0,
                            "no-op/invalid regex must preserve selection direction");
                    Require(result->GetLabel() == label, "invalid regex and zero results must be distinct");
                };
                unchanged("missing", "X", "0 replacements");
                unchanged("(.)", "$&", "4 replacements");
                unchanged("(?=中)", "", "1 replacements");
                unchanged("[", "X", "Invalid regular expression");
                unchanged("", "X", "0 replacements");
                unchanged("a", "$0", "1 replacements");
                Require(frame->New(), "New must use the live document after no-op replacements");
                query->SetValue("[");
                Click(dialog, wxID_REPLACE_ALL);
                Require(Text(editor).empty() && !editor->Send(Message::CanUndo) &&
                            result->GetLabel() == "Invalid regular expression",
                        "invalid pattern validates on empty document");
                query->SetValue("^");
                replacement->SetValue("");
                Click(dialog, wxID_REPLACE_ALL);
                Require(Text(editor).empty() && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                            result->GetLabel() == "1 replacements",
                        "empty no-op EOF match must terminate and count once");
                std::puts("PASS: no-match, byte-identical, empty expansion/query and invalid input preserve history");
                const auto path = std::filesystem::temp_directory_path() /
                                  ("wxnotepad-regex-all-" + std::to_string(wxGetProcessId()) + ".txt");
                const std::string beforeBytes = "\xef\xbb\xbf"
                                                "中文12\r\n中文34\nend\r";
                {
                    std::ofstream file(path, std::ios::binary);
                    file.write(beforeBytes.data(), static_cast<std::streamsize>(beforeBytes.size()));
                }
#ifdef __WXMSW__
                const wxString filename(path.wstring());
#else
                const wxString filename = wxString::FromUTF8(path.string());
#endif
                Require(frame->Open(filename).status == OpenStatus::Opened, "mixed-EOL BOM fixture opens");
                const auto disk = [&] {
                    std::ifstream file(path, std::ios::binary);
                    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
                };
                query->SetValue(wxString::FromUTF8("(中文)([0-9]+)"));
                replacement->SetValue("$2:$1:$2");
                Click(dialog, wxID_REPLACE_ALL);
                Require(Text(editor) == "12:中文:12\r\n34:中文:34\nend\r" && result->GetLabel() == "2 replacements",
                        "growing regex replacement preserves untouched mixed-EOL bytes");
                Command(frame, wxID_SAVE);
                Require(disk() == "\xef\xbb\xbf"
                                  "12:中文:12\r\n34:中文:34\nend\r" &&
                            frame->DocumentHasBom() && frame->DocumentEncoding() == FileEncoding::Utf8 &&
                            !editor->Send(Message::GetModify),
                        "Save writes independently expected growing UTF-8/BOM/mixed-EOL bytes");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == "中文12\r\n中文34\nend\r" && editor->Send(Message::GetModify),
                        "Undo after Save restores original document away from its new savepoint");
                Command(frame, wxID_REDO);
                Require(Text(editor) == "12:中文:12\r\n34:中文:34\nend\r" && !editor->Send(Message::GetModify),
                        "Redo after Save restores the exact new savepoint");
                Require(frame->Open(filename).status == OpenStatus::Opened &&
                            Text(editor) == "12:中文:12\r\n34:中文:34\nend\r",
                        "saved growth reopens exactly");
                query->SetValue(wxString::FromUTF8("([0-9]+):中文:\\1"));
                replacement->SetValue("X");
                Click(dialog, wxID_REPLACE_ALL);
                Require(Text(editor) == "X\r\nX\nend\r" && result->GetLabel() == "2 replacements",
                        "shrinking backreference replacement visits the full document");
                Command(frame, wxID_UNDO);
                Require(Text(editor) == "12:中文:12\r\n34:中文:34\nend\r" && !editor->Send(Message::GetModify),
                        "one Undo restores the reopened savepoint before shrinking");
                Command(frame, wxID_REDO);
                Command(frame, wxID_SAVE);
                Require(disk() == "\xef\xbb\xbf"
                                  "X\r\nX\nend\r" &&
                            !editor->Send(Message::GetModify),
                        "Save writes independently expected shrinking bytes");
                Require(frame->Open(filename).status == OpenStatus::Opened && Text(editor) == "X\r\nX\nend\r" &&
                            !editor->Send(Message::GetModify),
                        "saved shrink reopens exactly");
                std::filesystem::remove(path);
                std::puts("PASS: growing/shrinking Save and reopen preserve exact BOM, mixed EOL and savepoints");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnExit() override { return result_; }
    int OnRun() override {
        wxApp::OnRun();
        return result_;
    }

private:
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(RegexReplaceAllApp);
