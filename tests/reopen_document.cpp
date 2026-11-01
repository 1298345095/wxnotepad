#include "document/document_session.h"
#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include <wx/wx.h>
#include <wx/filename.h>
#include <wx/file.h>
#include <cstdio>
#include <stdexcept>

namespace {
using Scintilla::Message;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Write(const wxString& path, const std::string& bytes) {
    wxFile file(path, wxFile::write);
    Require(file.IsOpened() && file.Write(bytes.data(), bytes.size()) == bytes.size() && file.Close(),
            "fixture write must succeed");
}
std::string Text(EditorControl& editor) {
    std::string bytes(static_cast<size_t>(editor.Send(Message::GetLength)) + 1, '\0');
    editor.Send(Message::GetText, bytes.size(), reinterpret_cast<intptr_t>(bytes.data()));
    bytes.pop_back();
    return bytes;
}
class ReopenApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new EditorFrame;
        auto* editor = frame->Editor();
        frame->ShowWithoutActivating();
        CallAfter([this, frame, editor] {
            const wxString path = wxFileName::CreateTempFileName("wxnotepad-reopen-") + ".cpp";
            try {
                std::string bytes;
                for (int line = 0; line < 100; ++line)
                    bytes += std::string(200, 'x') + "\n";
                Write(path, bytes);
                DocumentSession document(*editor);
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened,
                        "initial Open must succeed");
                document.SelectLanguage(SyntaxLanguage::Python);
                editor->Send(Message::SetSel, 6020, 6040);
                editor->Send(Message::SetFirstVisibleLine, 25);
                editor->Send(Message::SetXOffset, 120);
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened &&
                            document.Language() == SyntaxLanguage::Python &&
                            document.LanguageOverride() == SyntaxLanguage::Python &&
                            editor->Send(Message::GetAnchor) == 6020 && editor->Send(Message::GetCurrentPos) == 6040 &&
                            editor->Send(Message::GetFirstVisibleLine) == 25 &&
                            editor->Send(Message::GetXOffset) == 120,
                        "reopening the current path must preserve the effective scheme, selection and view");
                std::puts("PASS: same-path reopen preserves the manual scheme, selection and viewport");
                std::string wider;
                for (int line = 0; line < 100; ++line)
                    wider += std::string(2000, 'y') + "\n";
                Write(path, wider);
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened &&
                            editor->Send(Message::GetCurrentPos) == 60040 && editor->Send(Message::GetAnchor) == 60040,
                        "large line displacement must use the original saved line/column jump, clearing selection");
                const auto caretY = editor->Send(Message::PointYFromPosition, 0, 60040);
                Require(caretY >= 0 && caretY < editor->GetClientSize().y,
                        "the fallback jump must keep the restored caret visible");
                Write(path, "short");
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened &&
                            editor->Send(Message::GetCurrentPos) == 5 && editor->Send(Message::GetAnchor) == 5 &&
                            !editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo) &&
                            !editor->Send(Message::GetModify),
                        "a deleted saved line jumps to EOF and successful load clears history at its save point");
                std::puts("PASS: changed disk content uses original fallback and EOF clipping");
                Write(path, bytes);
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened,
                        "the displacement boundary fixture must load");
                editor->Send(Message::SetSel, 6020, 6040);
                std::string fewerLines = bytes;
                for (size_t index = 0; index < 4; ++index)
                    fewerLines[index * 201 + 200] = 'x';
                Write(path, fewerLines);
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened &&
                            editor->Send(Message::GetAnchor) == 6020 && editor->Send(Message::GetCurrentPos) == 6040,
                        "a displacement of exactly five in the original mixed line bases retains byte positions");
                Write(path, bytes);
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened,
                        "the five-line fixture must reload");
                editor->Send(Message::SetSel, 6020, 6040);
                fewerLines[4 * 201 + 200] = 'x';
                Write(path, fewerLines);
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened &&
                            editor->Send(Message::GetAnchor) == 7045 && editor->Send(Message::GetCurrentPos) == 7045,
                        "removing five preceding newlines crosses the original fallback threshold");
                Write(path, "abcdefghij");
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened,
                        "the short single-line fixture must load");
                editor->Send(Message::SetSel, 2, 9);
                Write(path, "tiny");
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened &&
                            editor->Send(Message::GetAnchor) == 2 && editor->Send(Message::GetCurrentPos) == 4,
                        "small line displacement lets the core clip each selection endpoint independently");
                std::puts("PASS: exact displacement boundary and raw endpoint clipping");

                editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("dirty "));
                editor->Send(Message::SetSel, 2, 7);
                Write(path, "external\n");
                const auto revision = editor->DocumentRevision();
                Require(document.Open(path, [] { return false; }).status == OpenStatus::Cancelled &&
                            Text(*editor) == "dirty tiny" && editor->DocumentRevision() == revision &&
                            editor->Send(Message::GetModify) && editor->Send(Message::CanUndo) &&
                            editor->Send(Message::GetAnchor) == 2 && editor->Send(Message::GetCurrentPos) == 7 &&
                            document.Language() == SyntaxLanguage::Python,
                        "Cancel retains edited bytes, view, scheme and undo history");
                Require(document.Open(std::nullopt, [] { return true; }).status == OpenStatus::Cancelled &&
                            document.Open(path + ".missing", [] { return true; }).status == OpenStatus::Failed &&
                            editor->DocumentRevision() == revision && editor->Send(Message::GetModify),
                        "a cancelled chooser or failed load retains the existing document");
                Write(path, std::string(1, '\xff'));
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Failed &&
                            Text(*editor) == "dirty tiny" && editor->Send(Message::CanUndo),
                        "failed decoding retains edited bytes and history");
                Write(path, "external\n");
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened &&
                            Text(*editor) == "external\n" && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "Discard reloads disk bytes and clears the previous edit history");
                editor->Send(Message::InsertText, 0, reinterpret_cast<intptr_t>("saved "));
                editor->Send(Message::SetSel, 2, 7);
                Write(path, "external replacement\n");
                Require(document.Open(path, [&] { return document.Save().status == SaveStatus::Saved; }).status ==
                                OpenStatus::Opened &&
                            Text(*editor) == "saved external\n" && editor->Send(Message::GetAnchor) == 2 &&
                            editor->Send(Message::GetCurrentPos) == 7 && !editor->Send(Message::GetModify) &&
                            !editor->Send(Message::CanUndo),
                        "Save reloads the newly saved bytes rather than the stale pre-prompt read");
                wxFile saved(path, wxFile::read);
                std::string disk(static_cast<size_t>(saved.Length()), '\0');
                Require(saved.Read(disk.data(), disk.size()) == disk.size() && saved.Close() &&
                            disk == "saved external\n",
                        "the independently read disk bytes must contain the Save decision's edited content");
                std::puts("PASS: cancelled/failed reopen, Discard and Save retain original transaction semantics");

#ifdef __WXMSW__
                Require(document.Open(path.Upper(), [] { return true; }).status == OpenStatus::Opened &&
                            document.Language() == SyntaxLanguage::Python && editor->Send(Message::GetAnchor) == 2 &&
                            editor->Send(Message::GetCurrentPos) == 7,
                        "Windows ordinal case-equivalent path spelling must retain the current view and scheme");
#endif
                document.SelectLanguage(std::nullopt);
                Require(document.Open(path, [] { return true; }).status == OpenStatus::Opened &&
                            document.Language() == SyntaxLanguage::Cpp && !document.LanguageOverride() &&
                            editor->Send(Message::GetAnchor) == 2 && editor->Send(Message::GetCurrentPos) == 7,
                        "automatic language choice remains automatic on same-path reopen");
                wxFileName alternate(path);
                const wxString dotted =
                    alternate.GetPath() + wxFILE_SEP_PATH + "." + wxFILE_SEP_PATH + alternate.GetFullName();
                document.SelectLanguage(SyntaxLanguage::Python);
                Require(document.Open(dotted, [] { return true; }).status == OpenStatus::Opened &&
                            document.Language() == SyntaxLanguage::Cpp && !document.LanguageOverride() &&
                            document.Path() == path && editor->Send(Message::GetCurrentPos) == 0 &&
                            editor->Send(Message::GetAnchor) == 0,
                        "a distinct supplied path spelling uses ordinary Open, without a new alias identity rule");
                const wxString other = path.BeforeLast('.');
                Write(other, "different file\n");
                document.SelectLanguage(SyntaxLanguage::Python);
                Require(document.Open(other, [] { return true; }).status == OpenStatus::Opened &&
                            document.Language() == SyntaxLanguage::PlainText && !document.LanguageOverride() &&
                            Text(*editor) == "different file\n" && editor->Send(Message::GetCurrentPos) == 0 &&
                            editor->Send(Message::GetXOffset) == 0,
                        "opening a different file resets selection, scroll and automatic language classification");
                std::puts("PASS: native path recognition, automatic choice and different-file Open control");
                Require(frame->Open(path).status == OpenStatus::Opened, "the application Open must succeed");
                const int python = frame->GetMenuBar()->FindMenuItem("Language", "Python");
                wxCommandEvent choose(wxEVT_MENU, python);
                frame->GetEventHandler()->ProcessEvent(choose);
                Require(frame->GetMenuBar()->IsChecked(python), "the manual language command must be checked");
                Require(frame->Open(path).status == OpenStatus::Opened && frame->GetMenuBar()->IsChecked(python),
                        "the application reopen must retain the manual language menu choice");
                std::puts("PASS: application reopen keeps its language menu synchronized");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            wxRemoveFile(path);
            wxRemoveFile(path.BeforeLast('.'));
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
wxIMPLEMENT_APP(ReopenApp);
