#include "editor/editor_control.h"
#include "app/editor_frame.h"
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
    std::string SavedText() const {
        wxFFile file(directory_ + "/received.txt", "rb");
        Require(file.IsOpened(), "open independently saved native clipboard bytes");
        std::string bytes(static_cast<size_t>(file.Length()), '\0');
        Require(file.Read(bytes.data(), bytes.size()) == bytes.size(),
                "read independently saved native clipboard bytes");
        return bytes;
    }
    void PreserveSavedRectangle() const {
        Require(wxCopyFile(directory_ + "/received.txt", "clipboard-shape-peer.txt", true),
                "retain peer's independent rectangle bytes");
    }
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
        if (!frame_->IsActive() || !editor_->HasFocus())
            throw std::runtime_error("editor must receive actual native focus after peer command: " +
                                     command.ToStdString());
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
void Shapes(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("A\t中文Z\nB\t日本Q"));
    editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 18);
    Gesture(editor);
    editor->Send(Message::Copy);
    SettleClipboard();
    Require(peer.Request("shape-read") == "rectangle", "separate native peer receives original rectangle marker");
#ifdef __WXMSW__
    Require(peer.SavedText() == "\t中文\n\t日本", "peer independently saves exact rectangle Unicode/tab bytes");
#else
    Require(peer.SavedText() == "\t中文\n\t日本\n", "peer independently saves exact rectangle Unicode/tab bytes");
#endif
    peer.PreserveSavedRectangle();
    std::puts("PASS: native rectangle marker and independent saved Unicode bytes");
    const auto load = [editor](const char* text, intptr_t start, intptr_t end) {
        editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
        editor->Send(Message::SetSel, start, end);
        editor->Send(Message::EmptyUndoBuffer);
        editor->Send(Message::SetSavePoint);
    };
    const auto paste = [editor, &peer](const char* offer) {
        const auto response = peer.Request(offer);
        if (response != "ready") throw std::runtime_error(std::string(offer) + ": " + response.ToStdString());
        editor->Send(Message::Paste);
    };
    const auto undo = [editor](const char* before) {
        editor->Send(Message::Undo);
        Require(Text(editor) == before && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                "one Undo restores shape paste and save point");
        editor->Send(Message::Redo);
    };
    load("A\t中文Z\nB\t日本Q", 0, 0);
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 18);
    Gesture(editor);
    editor->Send(Message::Cut);
    Require(Text(editor) == "AZ\nBQ", "rectangle Cut removes the selected Unicode/tab columns");
    SettleClipboard();
    Require(peer.Request("shape-read") == "rectangle", "rectangle Cut publishes native rectangle meaning");
    undo("A\t中文Z\nB\t日本Q");
    load("ab12\ncd34", 2, 2);
    paste("shape-rectangle");
    Require(Text(editor) == "ab中文\tX12\ncd日本\tY34", "rectangle inserts each Unicode/tab row at the same column");
    undo("ab12\ncd34");
    const auto saved = wxFileName::GetCwd() + "/clipboard-shape-editor.txt";
    Require(frame->SaveAs(saved).status == SaveStatus::Saved, "save actual rectangle edit result");
    {
        wxFFile file(saved, "rb");
        std::string bytes(static_cast<size_t>(file.Length()), '\0');
        Require(file.Read(bytes.data(), bytes.size()) == bytes.size() && bytes == "ab中文\tX12\ncd日本\tY34",
                "independent disk bytes retain rectangle Unicode and tabs");
    }
    load("ab12\ncd34", 2, 2);
    paste("shape-both");
    Require(Text(editor) == "ab中文\tX12\ncd日本\tY34", "rectangle wins over line metadata");
    for (const char* offer : {"shape-line", "shape-vsline"}) {
        load("ab12\ncd34", 2, 2);
        paste(offer);
        Require(Text(editor) == "中文\tX\n日本\tY\nab12\ncd34",
                "line paste inserts at current line start and adds missing EOL");
        Require(editor->Send(Message::GetCurrentPos) == 20, "line paste retains original caret content offset");
        undo("ab12\ncd34");
        load("ab12\ncd34", 1, 3);
        paste(offer);
        Require(Text(editor) == "a中文\tX\n日本\tY2\ncd34", "nonempty selection consumes line offer as stream");
        undo("ab12\ncd34");
    }
    load("ab12\ncd34", 2, 2);
    editor->Send(Message::LineCopy, 1);
    SettleClipboard();
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "ab12\n",
            "LineCopy publishes both original line tags with Unicode text");
    load("ab12\ncd34", 2, 2);
    editor->Send(Message::LineCut, 1);
    SettleClipboard();
    Require(Text(editor) == "cd34" && peer.Request("shape-read") == "line" && peer.SavedText() == "ab12\n",
            "LineCut preserves line tags and removes copied line");
    undo("ab12\ncd34");
    load("ab12\ncd34", 1, 3);
    editor->Send(Message::SetReadOnly, 1);
    paste("shape-line");
    editor->Send(Message::Cut);
    Require(Text(editor) == "ab12\ncd34" && !editor->Send(Message::CanUndo),
            "read-only shape paste and cut retain bytes/history");
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::CrLf));
    load("ab12\r\ncd34", 2, 2);
    paste("shape-line");
    Require(Text(editor) == "中文\tX\r\n日本\tY\r\nab12\r\ncd34", "line paste converts peer LF to local CRLF");
    editor->Send(Message::SetPasteConvertEndings, 0);
    load("ab12\r\ncd34", 2, 2);
    paste("shape-line");
    Require(Text(editor) == "中文\tX\n日本\tY\r\nab12\r\ncd34",
            "disabled paste conversion retains peer LF and uses local appended EOL");
    editor->Send(Message::SetPasteConvertEndings, 1);
    editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
    for (const auto mode : {Scintilla::MultiPaste::Once, Scintilla::MultiPaste::Each}) {
        load("ab12\ncd34", 1, 2);
        editor->Send(Message::SetMultipleSelection, 1);
        editor->Send(Message::SetAdditionalSelectionTyping, 0);
        editor->Send(Message::AddSelection, 7, 6);
        editor->Send(Message::SetMainSelection, 0);
        editor->Send(Message::SetMultiPaste, static_cast<uintptr_t>(mode));
        paste("shape-line");
        Require(Text(editor) == (mode == Scintilla::MultiPaste::Each ? "a中文\tX\n日本\tY12\nc中文\tX\n日本\tY34"
                                                                     : "a中文\tX\n日本\tY12\ncd34"),
                "stream paste of selected line offer follows original multiple-selection policy");
        undo("ab12\ncd34");
    }
#ifdef __WXMSW__
    load("ab12\ncd34", 2, 2);
    paste("shape-borland");
    Require(Text(editor) == "ab中文\tX12\ncd日本\tY34", "Borland byte 0x02 selects rectangle paste");
    load("ab12\ncd34", 2, 2);
    paste("shape-malformed");
    Require(Text(editor) == "ab中文\tX\n日本\tY12\ncd34", "other Borland byte is ordinary stream");
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    Require(peer.Request("hold") == "held", "peer holds clipboard before LineCut");
    editor->Send(Message::LineCut, 1);
    Require(Text(editor) == "ab中文\tX\n日本\tY12\ncd34" && !editor->Send(Message::CanUndo),
            "failed LineCut publication preserves bytes and history");
    Require(peer.Request("release") == "released", "peer releases clipboard");
#endif
    Require(peer.Request("copy") == "ready" && peer.Request("shape-read") == "stream",
            "stock native text widget offers ordinary Unicode without line/rectangle metadata");
    std::puts("PASS: line/rectangle precedence, insertion/EOL/multi-paste rules, history, read-only and saved bytes");
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
                wxString shapes;
                if (wxGetEnv("WXNOTEPAD_CLIPBOARD_SHAPES", &shapes)) {
                    Shapes(frame, editor, peer);
                    result_ = 0;
                } else {
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
                            std::fprintf(
                                stderr,
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
                        Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行" &&
                                    editor->Send(Message::GetModify),
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
                        Require(frame->GetMenuBar()->IsEnabled(wxID_COPY) &&
                                    !frame->GetMenuBar()->IsEnabled(wxID_CUT) &&
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
                        Require(Text(editor) == "peer English 中文 😀\r\n第二行\r\nlast",
                                "Ctrl+V pastes copied selection");
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
