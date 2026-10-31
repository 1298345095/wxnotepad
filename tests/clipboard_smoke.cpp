#include "editor_control.h"
#include "editor_frame.h"
#include <wx/wx.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/stopwatch.h>
#include <cstdio>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void SettleClipboard() {
    // Desktop clipboard synchronizers can briefly open the native clipboard
    // after our owned fixture changes. Never inspect or log previous contents.
    wxStopWatch watch;
    while (watch.Time() < 250) { wxYield(); wxMilliSleep(10); }
}
std::string Text(EditorControl* editor) {
    using Scintilla::Message;
    std::string text(static_cast<size_t>(editor->Send(Message::GetLength)) + 1, '\0');
    editor->Send(Message::GetText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    text.pop_back();
    return text;
}
class Peer {
public:
    Peer() {
        directory_ = wxFileName::CreateTempFileName("wxnotepad-clipboard-");
        wxRemoveFile(directory_);
        Require(wxMkdir(directory_), "create owned peer IPC directory");
        wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
        executable.SetName("clipboard_peer");
        Require(wxExecute("\"" + executable.GetFullPath() + "\" \"" + directory_ + "\"", wxEXEC_ASYNC) > 0,
                "launch independent native text clipboard peer");
    }
    ~Peer() {
        try { Request("quit"); } catch (...) {}
        wxFileName::Rmdir(directory_, wxPATH_RMDIR_RECURSIVE);
    }
    wxString Request(const wxString& command) {
        if (wxFileExists(directory_ + "/response")) wxRemoveFile(directory_ + "/response");
        { wxFFile file(directory_ + "/request.tmp", "wb"); Require(file.Write(command, wxConvUTF8), "write peer request"); }
        Require(wxRenameFile(directory_ + "/request.tmp", directory_ + "/request", true), "publish peer request");
        wxStopWatch watch;
        while (!wxFileExists(directory_ + "/response") && watch.Time() < 5000) { wxYield(); wxMilliSleep(10); }
        if (!wxFileExists(directory_ + "/response"))
            throw std::runtime_error("independent peer response timeout: " + command.ToStdString());
        wxString response;
        wxFFile file(directory_ + "/response", "rb");
        Require(file.ReadAll(&response, wxConvUTF8), "read owned peer response");
        if (command == "copy") SettleClipboard();
        if (command == "paste") response.Replace("\r\n", "\n");
        return response;
    }
private:
    wxString directory_;
};
void Key(EditorControl* editor, int key) {
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = key;
    event.m_controlDown = true;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Menu(EditorFrame* frame, int id) {
    wxCommandEvent event(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(event);
}
class ClipboardApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        wxLog::SetActiveTarget(new wxLogStderr);
        auto* frame = new EditorFrame;
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                {
                    wxClipboardLocker lock;
                    Require(!(!lock), "open system clipboard for owned fixture");
                    Require(wxTheClipboard->SetData(new wxTextDataObject("owned seed")), "seed clipboard");
                }
                SettleClipboard();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("A中文\r\nline B"));
                editor->Send(Message::SetSel, 1, 15);
                editor->Send(Message::Copy);
                SettleClipboard();
                wxTextDataObject copied;
                {
                    wxClipboardLocker lock;
                    Require(!(!lock) && wxTheClipboard->GetData(copied), "read copied text");
                }
                auto copiedText = copied.GetText();
                copiedText.Replace("\r\n", "\n");
                Require(copiedText == wxString::FromUTF8("中文\nline B"), "Copy publishes selected Unicode multiline text");
                std::puts("PASS: Copy selected Unicode multiline text to system clipboard");
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                {
                    wxClipboardLocker lock;
                    Require(!(!lock) && wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8("paste中文\nsecond line\n第三行"))), "publish paste fixture");
                }
                SettleClipboard();
                editor->Send(Message::Paste);
                Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行", "Paste replaces selection with owned UTF-8 and core EOL conversion");
                Require(editor->Send(Message::GetModify), "Paste marks document modified");
                editor->Send(Message::Undo);
                Require(Text(editor) == "A中文\r\nline B" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo), "one Undo restores the replaced selection at save point");
                editor->Send(Message::Redo);
                Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行" && editor->Send(Message::GetModify), "Redo restores paste and modified state");
                std::puts("PASS: Paste selection replacement, one-step Undo/Redo and save point");
                Peer peer;
                editor->Send(Message::SetSel, 1, editor->Send(Message::GetLength));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
#ifdef __WXMSW__
                Require(peer.Request("hold") == "held", "external peer holds system clipboard");
                editor->Send(Message::Cut);
                Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                    "failed system copy must not delete Cut selection or change history");
                Require(peer.Request("release") == "released", "external peer releases clipboard");
#endif
                editor->Send(Message::Cut);
                Require(Text(editor) == "A" && editor->Send(Message::GetModify), "Cut deletes selection after copy succeeds");
                SettleClipboard();
                Require(peer.Request("paste") == wxString::FromUTF8("paste中文\nsecond line\n第三行"), "independent stock text app receives Cut text");
                editor->Send(Message::Undo);
                Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo), "one Undo restores Cut at save point");
                editor->Send(Message::Redo);
                Require(Text(editor) == "A" && editor->Send(Message::GetModify), "Redo restores Cut and modified state");
                std::puts("PASS: Cut success/failure, external receiver, one-step Undo/Redo and save point");
                Require(peer.Request("nontext") == "ready", "independent app publishes non-text fixture");
                SettleClipboard();
                editor->Send(Message::SetSel, 0, 0);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(!frame->GetMenuBar()->IsEnabled(wxID_COPY) && !frame->GetMenuBar()->IsEnabled(wxID_CUT) && !frame->GetMenuBar()->IsEnabled(wxID_PASTE),
                    "empty selection and non-text clipboard disable Copy/Cut/Paste");
                Require(!editor->Send(Message::CanPaste), "CanPaste rejects non-text clipboard");
                Require(peer.Request("copy") == "ready", "independent stock text app copies Unicode multiline fixture");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsEnabled(wxID_PASTE), "text from external app enables Paste");
                editor->Send(Message::SetSel, 0, 1);
                editor->Send(Message::SetReadOnly, 1);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsEnabled(wxID_COPY) && !frame->GetMenuBar()->IsEnabled(wxID_CUT) && !frame->GetMenuBar()->IsEnabled(wxID_PASTE),
                    "read-only selection permits Copy but disables Cut/Paste");
                editor->Send(Message::Cut);
                editor->Send(Message::Paste);
                Require(Text(editor) == "A", "read-only commands preserve document");
                editor->Send(Message::SetReadOnly, 0);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                Menu(frame, wxID_PASTE);
                Require(Text(editor) == "peer English 中文 😀\r\n第二行\r\nlast", "Paste menu receives external stock-widget text");
                editor->Send(Message::SelectAll);
                Key(editor, 'C');
                SettleClipboard();
                Require(peer.Request("paste") == wxString::FromUTF8("peer English 中文 😀\n第二行\nlast"), "Ctrl+C publishes complete external roundtrip");
                Menu(frame, wxID_CUT);
                Require(Text(editor).empty(), "Cut menu deletes selection");
                SettleClipboard();
                Key(editor, 'V');
                Require(Text(editor) == "peer English 中文 😀\r\n第二行\r\nlast", "Ctrl+V pastes copied selection");
                editor->Send(Message::SelectAll);
                Key(editor, 'X');
                Require(Text(editor).empty(), "Ctrl+X cuts selection");
                SettleClipboard();
                Menu(frame, wxID_PASTE);
                Menu(frame, wxID_UNDO);
                Require(Text(editor).empty(), "one Undo reverses only the latest Paste");
                Menu(frame, wxID_UNDO);
                Require(Text(editor) == "peer English 中文 😀\r\n第二行\r\nlast", "next Undo independently reverses the preceding Cut");
                Menu(frame, wxID_COPY);
                SettleClipboard();
                Require(peer.Request("paste") == wxString::FromUTF8("peer English 中文 😀\n第二行\nlast"), "Copy menu sends external text");
                std::puts("PASS: menus, Ctrl+C/X/V events, availability, independent process Unicode multiline roundtrips");
                result_ = 0;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }
    int OnRun() override { wxApp::OnRun(); return result_; }
    int OnExit() override { return result_; }
private:
    int result_ = 1;
};
}
wxIMPLEMENT_APP(ClipboardApp);
