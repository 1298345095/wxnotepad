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
#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void SettleClipboard() {
    // Desktop clipboard synchronizers can briefly open the native clipboard
    // after our owned fixture changes. Never inspect or log previous contents.
    wxStopWatch watch;
    while (watch.Time() < 250) {
        wxYield();
        wxMilliSleep(10);
    }
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
    Peer(EditorFrame* frame, EditorControl* editor) : frame_(frame), editor_(editor) {
        directory_ = wxFileName::CreateTempFileName("wxnotepad-clipboard-");
        wxRemoveFile(directory_);
        Require(wxMkdir(directory_), "create owned peer IPC directory");
        wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
        executable.SetName("clipboard_peer");
        Require(wxExecute("\"" + executable.GetFullPath() + "\" \"" + directory_ + "\"", wxEXEC_ASYNC) > 0,
                "launch independent native text clipboard peer");
    }
    ~Peer() {
        try {
            Request("quit");
        } catch (...) {
        }
        wxFileName::Rmdir(directory_, wxPATH_RMDIR_RECURSIVE);
    }
    wxString Request(const wxString& command) {
        if (wxFileExists(directory_ + "/response")) wxRemoveFile(directory_ + "/response");
        {
            wxFFile file(directory_ + "/request.tmp", "wb");
            Require(file.Write(command, wxConvUTF8), "write peer request");
        }
        Require(wxRenameFile(directory_ + "/request.tmp", directory_ + "/request", true), "publish peer request");
        wxStopWatch watch;
        while (!wxFileExists(directory_ + "/response") && watch.Time() < 5000) {
            wxYield();
            wxMilliSleep(10);
        }
        if (!wxFileExists(directory_ + "/response"))
            throw std::runtime_error("independent peer response timeout: " + command.ToStdString());
        wxString response;
        wxFFile file(directory_ + "/response", "rb");
        Require(file.ReadAll(&response, wxConvUTF8), "read owned peer response");
        if (command == "copy") SettleClipboard();
        if (command == "paste") response.Replace("\r\n", "\n");
        const auto title = frame_->GetTitle();
        const auto focusTitle = wxString::Format("wxnotepad clipboard focus: editor [%lu]", wxGetProcessId());
        frame_->SetTitle(focusTitle);
        wxString privateFocusFile;
        if (wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &privateFocusFile)) {
            wxFFile file(privateFocusFile, "wb");
            file.Write("editor");
        }
        frame_->Raise();
        editor_->SetFocus();
        wxStopWatch activation;
        while ((!frame_->IsActive() || !editor_->HasFocus()) && activation.Time() < 3000) {
            frame_->SetTitle(focusTitle);
            wxYield();
            wxMilliSleep(10);
        }
        Require(frame_->IsActive() && editor_->HasFocus(),
                "editor must receive actual native focus before clipboard commands");
        if (!privateFocusFile.empty()) {
            wxFFile file(privateFocusFile, "wb");
        }
        frame_->SetTitle(title);
        SettleClipboard();
        return response;
    }

private:
    wxString directory_;
    EditorFrame* frame_;
    EditorControl* editor_;
};
void Gesture(EditorControl* editor) {
    wxString focusFile;
    if (!wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile)) return;
    bool received = false;
    wxEvtHandler nativeEvents;
    nativeEvents.Bind(wxEVT_KEY_UP, [&received](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_SHIFT) received = true;
        event.Skip();
    });
    editor->PushEventHandler(&nativeEvents);
    {
        wxFFile file(focusFile, "wb");
        file.Write("gesture");
    }
    wxStopWatch wait;
    while (!received && wait.Time() < 3000) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "private native modifier gesture must be received before clipboard ownership changes");
}
void Key(EditorControl* editor, int key) {
    Gesture(editor);
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = key;
    event.m_controlDown = true;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Menu(EditorFrame* frame, int id) {
    if (id == wxID_COPY || id == wxID_CUT || id == wxID_PASTE) Gesture(frame->Editor());
    wxCommandEvent event(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(event);
}
void NonTextOffer(EditorControl* editor) {
#ifdef __WXGTK__
    auto* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    std::string formats;
    wxStopWatch watch;
    do {
        formats.clear();
        GdkAtom* targets = nullptr;
        gint count = 0;
        if (gtk_clipboard_wait_for_targets(clipboard, &targets, &count)) {
            for (int i = 0; i < count; ++i) {
                auto* name = gdk_atom_name(targets[i]);
                if (name) {
                    formats += name;
                    formats += ' ';
                    g_free(name);
                }
            }
        }
        g_free(targets);
        if (formats.find("application/x-wxnotepad-test") != std::string::npos) break;
        wxYield();
        wxMilliSleep(20);
    } while (watch.Time() < 2000);
    const bool nativeText = gtk_clipboard_wait_is_text_available(clipboard);
    const bool editorText = editor->Send(Scintilla::Message::CanPaste);
    std::fprintf(stderr, "Owned offer targets: %s; GTK text=%d; editor CanPaste=%d\n", formats.c_str(), nativeText,
                 editorText);
    Require(formats.find("application/x-wxnotepad-test") != std::string::npos,
            "owned non-text MIME offer has not arrived at the focused editor");
    Require(nativeText == editorText, "editor CanPaste must agree with independent GTK native text availability");
    Require(!nativeText, "non-text fixture is still accompanied by native text targets");
#endif
    Require(!editor->Send(Scintilla::Message::CanPaste), "CanPaste rejects non-text clipboard");
}
void TextOffer(EditorControl* editor) {
#ifdef __WXGTK__
    auto* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    wxStopWatch watch;
    bool nativeText = false;
    do {
        nativeText = gtk_clipboard_wait_is_text_available(clipboard);
        if (nativeText) break;
        wxYield();
        wxMilliSleep(20);
    } while (watch.Time() < 2000);
    const bool editorText = editor->Send(Scintilla::Message::CanPaste);
    std::fprintf(stderr, "New text offer: GTK text=%d; editor CanPaste=%d\n", nativeText, editorText);
    Require(nativeText, "new stock text MIME offer has not arrived at the focused editor");
    Require(editorText == nativeText, "editor CanPaste must agree with the new native text offer");
#endif
}
class ClipboardApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        wxLog::SetActiveTarget(new wxLogStderr);
        auto* frame = new EditorFrame;
        wxString focusProbe;
        if (wxGetEnv("WXNOTEPAD_CLIPBOARD_FOCUS_PROBE", &focusProbe)) {
            // Read-only native-window identity preflight: no clipboard query,
            // activation request, keyboard or mouse input.
            frame->SetMenuBar(nullptr);
            const auto title = wxString::Format("wxnotepad clipboard focus: editor [%lu]", wxGetProcessId());
            frame->SetTitle(title);
            frame->ShowWithoutActivating();
            probeTimer_.SetOwner(this);
            const auto started = wxGetUTCTimeMillis();
            Bind(wxEVT_TIMER, [this, frame, title, started](wxTimerEvent&) {
                if (wxGetUTCTimeMillis() - started < 15000)
                    frame->SetTitle(title);
                else {
                    probeTimer_.Stop();
                    result_ = 0;
                    frame->Destroy();
                    ExitMainLoop();
                }
            });
            probeTimer_.Start(100);
            return true;
        }
        frame->Show();
        CallAfter([this, frame] {
            try {
                using Scintilla::Message;
                auto* editor = frame->Editor();
                Peer peer(frame, editor);
                Require(peer.Request("ready") == "ready", "external clipboard application is running");
                wxString offerOnly;
                if (wxGetEnv("WXNOTEPAD_CLIPBOARD_OFFER_ONLY", &offerOnly)) {
                    Require(peer.Request("nontext") == "ready", "external app publishes non-text fixture");
                    NonTextOffer(editor);
                    Require(peer.Request("copy") == "ready", "external stock app publishes text fixture");
                    TextOffer(editor);
                    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                    Require(editor->Send(Message::CanPaste) && frame->GetMenuBar()->IsEnabled(wxID_PASTE),
                            "new native text offer enables Paste");
                    std::puts("PASS: focused native offer arrival and Paste availability");
                    result_ = 0;
                } else {
                    {
                        wxClipboardLocker lock;
                        Require(!(!lock), "open system clipboard for owned fixture");
                        Require(wxTheClipboard->SetData(new wxTextDataObject("owned seed")), "seed clipboard");
                    }
                    SettleClipboard();
                    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("A中文\r\nline B"));
                    editor->Send(Message::SetSel, 1, 15);
                    Gesture(editor);
                    editor->Send(Message::Copy);
                    SettleClipboard();
                    wxTextDataObject copied;
                    {
                        wxClipboardLocker lock;
                        Require(!(!lock) && wxTheClipboard->GetData(copied), "read copied text");
                    }
                    auto copiedText = copied.GetText();
                    copiedText.Replace("\r\n", "\n");
                    if (copiedText != wxString::FromUTF8("中文\nline B"))
                        std::fprintf(stderr,
                                     "Copy diagnostic: seed remains=%d, returned characters=%zu, selection=%ld..%ld\n",
                                     copiedText == "owned seed", copiedText.length(),
                                     static_cast<long>(editor->Send(Message::GetSelectionStart)),
                                     static_cast<long>(editor->Send(Message::GetSelectionEnd)));
                    Require(copiedText == wxString::FromUTF8("中文\nline B"),
                            "Copy publishes selected Unicode multiline text");
                    std::puts("PASS: Copy selected Unicode multiline text to system clipboard");
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    {
                        wxClipboardLocker lock;
                        Require(!(!lock) && wxTheClipboard->SetData(new wxTextDataObject(
                                                wxString::FromUTF8("paste中文\nsecond line\n第三行"))),
                                "publish paste fixture");
                    }
                    SettleClipboard();
                    editor->Send(Message::Paste);
                    Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行",
                            "Paste replaces selection with owned UTF-8 and core EOL conversion");
                    Require(editor->Send(Message::GetModify), "Paste marks document modified");
                    editor->Send(Message::Undo);
                    Require(Text(editor) == "A中文\r\nline B" && !editor->Send(Message::GetModify) &&
                                !editor->Send(Message::CanUndo),
                            "one Undo restores the replaced selection at save point");
                    editor->Send(Message::Redo);
                    Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行" && editor->Send(Message::GetModify),
                            "Redo restores paste and modified state");
                    std::puts("PASS: Paste selection replacement, one-step Undo/Redo and save point");
                    editor->Send(Message::SetSel, 1, editor->Send(Message::GetLength));
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
#ifdef __WXMSW__
                    Require(peer.Request("hold") == "held", "external peer holds system clipboard");
                    editor->Send(Message::Cut);
                    Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行" &&
                                !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                            "failed system copy must not delete Cut selection or change history");
                    Require(peer.Request("release") == "released", "external peer releases clipboard");
#endif
                    Gesture(editor);
                    editor->Send(Message::Cut);
                    Require(Text(editor) == "A" && editor->Send(Message::GetModify),
                            "Cut deletes selection after copy succeeds");
                    SettleClipboard();
                    Require(peer.Request("paste") == wxString::FromUTF8("paste中文\nsecond line\n第三行"),
                            "independent stock text app receives Cut text");
                    editor->Send(Message::Undo);
                    Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行" &&
                                !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                            "one Undo restores Cut at save point");
                    editor->Send(Message::Redo);
                    Require(Text(editor) == "A" && editor->Send(Message::GetModify),
                            "Redo restores Cut and modified state");
                    std::puts("PASS: Cut success/failure, external receiver, one-step Undo/Redo and save point");
                    Require(peer.Request("nontext") == "ready", "independent app publishes non-text fixture");
                    SettleClipboard();
                    editor->Send(Message::SetSel, 0, 0);
                    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                    Require(!frame->GetMenuBar()->IsEnabled(wxID_COPY), "empty selection disables Copy");
                    Require(!frame->GetMenuBar()->IsEnabled(wxID_CUT), "empty selection disables Cut");
                    NonTextOffer(editor);
                    Require(!frame->GetMenuBar()->IsEnabled(wxID_PASTE), "non-text clipboard disables Paste menu");
                    Require(peer.Request("copy") == "ready",
                            "independent stock text app copies Unicode multiline fixture");
                    TextOffer(editor);
                    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                    Require(frame->GetMenuBar()->IsEnabled(wxID_PASTE), "text from external app enables Paste");
                    editor->Send(Message::SetSel, 0, 1);
                    editor->Send(Message::SetReadOnly, 1);
                    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                    Require(frame->GetMenuBar()->IsEnabled(wxID_COPY) && !frame->GetMenuBar()->IsEnabled(wxID_CUT) &&
                                !frame->GetMenuBar()->IsEnabled(wxID_PASTE),
                            "read-only selection permits Copy but disables Cut/Paste");
                    editor->Send(Message::Cut);
                    editor->Send(Message::Paste);
                    Require(Text(editor) == "A", "read-only commands preserve document");
                    editor->Send(Message::SetReadOnly, 0);
                    editor->Send(Message::EmptyUndoBuffer);
                    editor->Send(Message::SetSavePoint);
                    Menu(frame, wxID_PASTE);
                    Require(Text(editor) == "peer English 中文 😀\r\n第二行\r\nlast",
                            "Paste menu receives external stock-widget text");
                    editor->Send(Message::SelectAll);
                    Key(editor, 'C');
                    SettleClipboard();
                    Require(peer.Request("paste") == wxString::FromUTF8("peer English 中文 😀\n第二行\nlast"),
                            "Ctrl+C publishes complete external roundtrip");
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
                    Require(Text(editor) == "peer English 中文 😀\r\n第二行\r\nlast",
                            "next Undo independently reverses the preceding Cut");
                    Menu(frame, wxID_COPY);
                    SettleClipboard();
                    Require(peer.Request("paste") == wxString::FromUTF8("peer English 中文 😀\n第二行\nlast"),
                            "Copy menu sends external text");
                    std::puts("PASS: menus, Ctrl+C/X/V events, availability, independent process Unicode multiline "
                              "roundtrips");
                    result_ = 0;
                }
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
    int OnExit() override { return result_; }

private:
    int result_ = 1;
    wxTimer probeTimer_;
};
} // namespace
wxIMPLEMENT_APP(ClipboardApp);
