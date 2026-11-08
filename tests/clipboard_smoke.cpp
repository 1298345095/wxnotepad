#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "native_search_helpers.h"
#include "native_key_helpers.h"
#include "encoded_file_helpers.h"
#include "settings/settings_store.h"
#include <wx/wx.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include "steady_stopwatch.h"
#include <cstdio>
#include <chrono>
#include <stdexcept>
#include <vector>
#ifdef __WXMSW__
#include <windows.h>
#endif
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (std::chrono::steady_clock::now() < deadline) {
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
    void OfferText(const std::string& bytes, const char* command = "utility-offer") {
        {
            wxFFile file(directory_ + "/offer.txt", "wb");
            Require(file.IsOpened() && file.Write(bytes.data(), bytes.size()) == bytes.size(),
                    "write owned native text offer");
        }
        Require(Request(command) == "ready", "independent process publishes literal Unicode offer");
    }
    std::string SavedText() const {
        wxFFile file(directory_ + "/received.txt", "rb");
        Require(file.IsOpened(), "open independently saved native clipboard bytes");
        std::string bytes(static_cast<size_t>(file.Length()), '\0');
        Require(file.Read(bytes.data(), bytes.size()) == bytes.size(),
                "read independently saved native clipboard bytes");
        return bytes;
    }
    void PreserveSavedText(const wxString& filename) const {
        Require(wxCopyFile(directory_ + "/received.txt", filename, true), "retain peer's independent clipboard bytes");
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
    Peer(const Peer&) = delete;
    Peer& operator=(const Peer&) = delete;
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
        const auto responseDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!wxFileExists(directory_ + "/response") && std::chrono::steady_clock::now() < responseDeadline) {
            wxYield();
            wxMilliSleep(10);
        }
        if (!wxFileExists(directory_ + "/response"))
            throw std::runtime_error("independent peer response timeout: " + command.ToStdString());
        wxString response;
        wxFFile file(directory_ + "/response", "rb");
        Require(file.ReadAll(&response, wxConvUTF8), "read owned peer response");
        if (response == "failed" || response == "native focus unavailable" || response == "native gesture unavailable")
            throw std::runtime_error("independent peer " + command.ToStdString() + ": " + response.ToStdString());
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
        const auto activationDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool acknowledged = privateFocusFile.empty();
        while (std::chrono::steady_clock::now() < activationDeadline) {
            if (!privateFocusFile.empty()) {
                wxString pending;
                wxFFile file(privateFocusFile, "rb");
                file.ReadAll(&pending);
                acknowledged = pending.empty();
            }
            // The peer's first map can take native focus before this process
            // dispatches its deactivate event. Keep the request until the driver
            // claims it, and process native events before trusting cached focus.
            wxYield();
            if (acknowledged && frame_->IsActive() && editor_->HasFocus()) break;
            wxMilliSleep(10);
        }
        if (!acknowledged || !frame_->IsActive() || !editor_->HasFocus())
            throw std::runtime_error("editor must receive actual native focus after peer command: " +
                                     command.ToStdString());
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!received && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(received, "private native modifier gesture must be received before clipboard ownership changes");
}
void Key(EditorControl* editor, int key, bool shift = false, bool control = true) {
    Gesture(editor);
    wxKeyEvent event(wxEVT_KEY_DOWN);
    event.m_keyCode = key;
    event.m_controlDown = control;
    event.m_shiftDown = shift;
    event.SetEventObject(editor);
    editor->GetEventHandler()->ProcessEvent(event);
}
void Menu(EditorFrame* frame, int id) {
    if (id == wxID_COPY || id == wxID_CUT || id == wxID_PASTE) Gesture(frame->Editor());
    wxCommandEvent event(wxEVT_MENU, id);
    frame->GetEventHandler()->ProcessEvent(event);
}
#ifdef __WXGTK__
std::string NativeClipboardFormats(GtkClipboard* clipboard) {
    std::string formats;
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
    return formats;
}
#endif
void NonTextOffer(EditorControl* editor) {
#ifdef __WXGTK__
    auto* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    std::string formats;
    TestClock::StopWatch watch;
    do {
        formats = NativeClipboardFormats(clipboard);
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
    TestClock::StopWatch watch;
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
void Shapes(EditorFrame* frame, EditorControl* editor, Peer& peer, bool readinessOnly = false) {
    using Scintilla::Message;
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("first\nmiddle\nlast"));
    editor->Send(Message::GotoPos, 2);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(wxID_COPY) && frame->GetMenuBar()->IsEnabled(wxID_CUT),
            "built-in Visual Studio mode enables current-line Copy/Cut with an empty selection");
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    Menu(frame, wxID_COPY);
    SettleClipboard();
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "first\n",
            "default empty-selection Copy publishes the current line and original line tags");
    Require(Text(editor) == "first\nmiddle\nlast" && editor->Send(Message::GetCurrentPos) == 2 &&
                !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
            "current-line Copy preserves bytes, caret and history");
    editor->Send(Message::GotoPos, 8);
    Menu(frame, wxID_CUT);
    Require(Text(editor) == "first\nlast" && editor->Send(Message::GetCurrentPos) == 8,
            "default empty-selection Cut removes the middle line and retains the next line's column");
    if (readinessOnly) return;
    editor->Send(Message::Undo);
    Require(Text(editor) == "first\nmiddle\nlast" && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo),
            "one Undo restores current-line Cut and save point");
    editor->Send(Message::Redo);
    Require(Text(editor) == "first\nlast", "Redo restores current-line Cut");
    editor->Send(Message::GotoPos, 8);
    Key(editor, 'C');
    SettleClipboard();
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "last",
            "Ctrl+C follows the same current-line option as Copy menu");
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
    peer.PreserveSavedText("clipboard-shape-peer.txt");
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
void NativeLineShortcut(EditorControl* editor, int key, bool shift = true, bool alt = false) {
#ifdef __WXMSW__
    BYTE previous[256];
    Require(GetKeyboardState(previous), "read native test thread modifiers");
    BYTE modified[256];
    std::copy(std::begin(previous), std::end(previous), std::begin(modified));
    modified[VK_CONTROL] = 0x80;
    modified[VK_SHIFT] = shift ? 0x80 : 0;
    modified[VK_MENU] = alt ? 0x80 : 0;
    Require(SetKeyboardState(modified), "set native test thread shortcut modifiers");
    int downs = 0;
    int ups = 0;
    bool modifiersCorrect = true;
    wxEvtHandler observed;
    const auto observe = [&](wxKeyEvent& event, int& count) {
        if (event.GetKeyCode() == key) {
            ++count;
            modifiersCorrect =
                modifiersCorrect && event.ControlDown() && event.ShiftDown() == shift && event.AltDown() == alt;
        }
        event.Skip();
    };
    observed.Bind(wxEVT_KEY_DOWN, [&](wxKeyEvent& event) { observe(event, downs); });
    observed.Bind(wxEVT_KEY_UP, [&](wxKeyEvent& event) { observe(event, ups); });
    editor->PushEventHandler(&observed);
    MSG message{};
    message.hwnd = static_cast<HWND>(editor->GetHandle());
    message.message = alt ? WM_SYSKEYDOWN : WM_KEYDOWN;
    message.wParam = key;
    // wxMSW reads Alt from the native message context, not keyboard state alone.
    message.lParam = alt ? (LPARAM{KF_ALTDOWN} << 16) : 0;
    const bool handled = wxGetTopLevelParent(editor)->MSWTranslateMessage(&message);
    if (!handled) SendMessageW(message.hwnd, message.message, key, message.lParam);
    SendMessageW(message.hwnd, alt ? WM_SYSKEYUP : WM_KEYUP, key, message.lParam);
    const bool restored = SetKeyboardState(previous);
    editor->PopEventHandler(false);
    Require(restored, "restore native test thread keyboard state");
    Require(ups == 1 && modifiersCorrect, "native shortcut release preserves actual Ctrl/Shift/Alt modifiers");
    Require((handled && !alt) || downs == 1, "unhandled and extra-Alt chords reach the actual editor key handler");
#else
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native keys require the private display");
    bool released = false;
    wxEvtHandler nativeEvents;
    nativeEvents.Bind(wxEVT_KEY_UP, [key, &released](wxKeyEvent& event) {
        if (event.GetKeyCode() == key) released = true;
        event.Skip();
    });
    editor->PushEventHandler(&nativeEvents);
    {
        wxFFile file(focusFile, "wb");
        Require(file.Write(wxString("key:ctrl+") + (shift ? "shift+" : "") + (alt ? "alt+" : "") +
                           wxString(wxUniChar(static_cast<unsigned int>(key - 'A' + 'a')))),
                "request an actual key on the isolated native display");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!released && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    editor->PopEventHandler(false);
    Require(released, "actual native line-shortcut key release reaches the editor");
#endif
    wxYield();
}
void LineCommandModes(EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    for (const auto mode : {LineSelectionMode::None, LineSelectionMode::VisualStudio, LineSelectionMode::Normal,
                            LineSelectionMode::OldVisualStudio}) {
        editor->SetLineSelectionMode(mode);
        const bool line = mode == LineSelectionMode::VisualStudio || mode == LineSelectionMode::OldVisualStudio;
        TestEditor::ResetStream(editor, "aa\nbb\ncc", 4, 4, Scintilla::EndOfLine::Lf);
        Gesture(editor);
        editor->CopyLine();
        Require(peer.Request("shape-read") == (line ? "line" : "stream") && peer.SavedText() == "bb\n",
                "explicit Copy Line retains each original mode's metadata bit");
        TestEditor::RequireSelection(editor, 4, 4, "Copy Line retains the caret in every mode");
        Require(!editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                "Copy Line preserves history and save point");
        TestEditor::ResetStream(editor, "AB\nCD", 1, 1);
        editor->Send(Message::Paste);
        Require(Text(editor) == (line ? "bb\nAB\nCD" : "Abb\nB\nCD"),
                "caret-only paste uses the original line or stream insertion position");
        TestEditor::RequireSelection(editor, 4, 4, "line or stream paste retains original caret offset");
        editor->Send(Message::Undo);
        Require(Text(editor) == "AB\nCD" && !editor->Send(Message::GetModify),
                "one Undo restores each mode's line paste and save point");
        TestEditor::ResetStream(editor, "aa\nbb\ncc", 4, 4);
        Gesture(editor);
        editor->CutLine();
        Require(Text(editor) == "aa\ncc" && peer.Request("shape-read") == (line ? "line" : "stream") &&
                    peer.SavedText() == "bb\n",
                "explicit Cut Line uses each mode's original metadata without ordinary Cut's column jump");
        TestEditor::RequireSelection(editor, 3, 3, "explicit Cut Line collapses at the deleted line start");
        editor->Send(Message::Undo);
        Require(Text(editor) == "aa\nbb\ncc" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                "one Undo restores original Cut Line text and save point");
        editor->Send(Message::Redo);
        Require(Text(editor) == "aa\ncc" && !editor->Send(Message::CanRedo), "Redo restores explicit Cut Line");
    }
    editor->SetLineSelectionMode(LineSelectionMode::VisualStudio);
    std::puts("PASS: all four explicit line-command modes, metadata, paste placement and Undo/Redo");
}
void LineCommandSelections(EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    struct Fixture {
        const char* before;
        intptr_t anchor;
        intptr_t caret;
        const char* copied;
        const char* cut;
        intptr_t position;
        Scintilla::EndOfLine eol = Scintilla::EndOfLine::Lf;
    };
    // Editor::WndProc includes the physical line containing either endpoint.
    const Fixture fixtures[] = {
        {"aa\nbb\ncc\ndd", 1, 4, "aa\nbb\n", "cc\ndd", 0},
        {"aa\nbb\ncc\ndd", 4, 1, "aa\nbb\n", "cc\ndd", 0},
        {"aa\nbb\ncc\ndd", 1, 3, "aa\nbb\n", "cc\ndd", 0},
        {"aa\nbb\ncc\ndd", 1, 7, "aa\nbb\ncc\n", "dd", 0},
        {"aa\nbb\ncc\ndd", 10, 10, "dd", "aa\nbb\ncc\n", 9},
        {"aa\r\nbb\r\ncc", 1, 5, "aa\r\nbb\r\n", "cc", 0, Scintilla::EndOfLine::CrLf},
        {"aa\rbb\rcc", 1, 4, "aa\rbb\r", "cc", 0, Scintilla::EndOfLine::Cr},
        {"tail", 2, 2, "tail", "", 0},
    };
    for (const auto& fixture : fixtures) {
        TestEditor::ResetStream(editor, fixture.before, fixture.anchor, fixture.caret, fixture.eol);
        Gesture(editor);
        editor->CopyLine();
        Require(peer.Request("shape-read") == "line" && peer.SavedText() == fixture.copied,
                "independent native peer reads exact original selected-line boundaries and final EOL");
        TestEditor::RequireSelection(editor, fixture.anchor, fixture.caret, "Copy Line retains selection direction");
        Require(Text(editor) == fixture.before && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                "Copy Line preserves selected text, history and save point");
        Gesture(editor);
        editor->CutLine();
        Require(Text(editor) == fixture.cut, "Cut Line removes the same original inclusive physical-line range");
        TestEditor::RequireSelection(editor, fixture.position, fixture.position, "Cut Line's original position");
        Require(peer.Request("shape-read") == "line" && peer.SavedText() == fixture.copied,
                "Cut Line publishes exact original selected-line bytes");
        editor->Send(Message::Undo);
        Require(Text(editor) == fixture.before && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                "one Undo restores inclusive selected-line Cut");
        editor->Send(Message::Redo);
        Require(Text(editor) == fixture.cut && !editor->Send(Message::CanRedo), "Redo restores selected-line Cut");
    }
    struct DeleteFixture {
        intptr_t anchor;
        intptr_t caret;
        const char* after;
        intptr_t afterAnchor;
        intptr_t afterCaret;
    };
    for (const auto& fixture : {DeleteFixture{1, 4, "aa\ncc\ndd", 1, 3}, DeleteFixture{4, 1, "bb\ncc\ndd", 1, 0},
                                DeleteFixture{1, 3, "aa\ncc\ndd", 1, 3}, DeleteFixture{1, 7, "aa\nbb\ndd", 1, 6},
                                DeleteFixture{10, 10, "aa\nbb\ncc\n", 9, 9}}) {
        TestEditor::ResetStream(editor, "aa\nbb\ncc\ndd", fixture.anchor, fixture.caret);
        editor->DeleteLine();
        Require(Text(editor) == fixture.after, "Delete Line removes only the main caret's physical line");
        TestEditor::RequireSelection(editor, fixture.afterAnchor, fixture.afterCaret,
                                     "Delete Line retains original endpoint adjustment");
        editor->Send(Message::Undo);
        Require(Text(editor) == "aa\nbb\ncc\ndd" && !editor->Send(Message::GetModify),
                "one Undo restores caret-line Delete");
        editor->Send(Message::Redo);
        Require(Text(editor) == fixture.after, "Redo restores caret-line Delete");
    }
    std::puts("PASS: partial/reversed/multiline/next-line-start selections, caret-line Delete and exact EOLs");
}
void LineCommandShapes(EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const auto rectangle = [editor] {
        TestEditor::ResetStream(editor, "ab12\ncd34\nef56", 0, 0, Scintilla::EndOfLine::Lf);
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, 1);
        editor->Send(Message::SetRectangularSelectionCaret, 12);
    };
    for (const auto mode : {LineSelectionMode::VisualStudio, LineSelectionMode::Normal}) {
        editor->SetLineSelectionMode(mode);
        const bool line = mode == LineSelectionMode::VisualStudio;
        rectangle();
        Gesture(editor);
        editor->CopyLine();
        Require(peer.Request("shape-read") == (line ? "line" : "stream") && peer.SavedText() == "ef56",
                "rectangle Copy Line copies the main range's physical line with the original mode tag");
        Require(editor->Send(Message::GetSelections) == 3 &&
                    editor->Send(Message::GetRectangularSelectionAnchor) == 1 &&
                    editor->Send(Message::GetRectangularSelectionCaret) == 12,
                "rectangle Copy Line preserves its ranges and rectangle endpoints");
        Gesture(editor);
        editor->CutLine();
        Require(Text(editor) == "a12\nc34\ne56", "rectangle Cut Line retains the original rectangle deletion");
        Require(peer.Request("shape-read") == (line ? "line" : "rectangle"),
                "rectangle Cut Line's mode argument retains original line-versus-rectangle publication");
        const auto rectangleBytes =
#ifdef __WXGTK__
            "b\nd\nf\n";
#else
            "b\nd\nf";
#endif
        Require(peer.SavedText() == (line ? "f" : rectangleBytes),
                "rectangle Cut Line publishes the original main-range or complete rectangle text");
        editor->Send(Message::Undo);
        Require(Text(editor) == "ab12\ncd34\nef56" && !editor->Send(Message::GetModify),
                "one Undo restores rectangle Cut Line");
        rectangle();
        editor->DeleteLine();
        Require(Text(editor) == "ab12\ncd34\n", "rectangle Delete Line deletes only the main caret line");
        editor->Send(Message::Undo);
        Require(Text(editor) == "ab12\ncd34\nef56", "one Undo restores rectangle Delete Line");
    }
    const auto multiple = [editor](bool additional) {
        TestEditor::ResetStream(editor, "aa\nbb\ncc\ndd", 1, 2, Scintilla::EndOfLine::Lf);
        editor->Send(Message::SetMultipleSelection, 1);
        editor->Send(Message::SetAdditionalSelectionTyping, additional);
        editor->Send(Message::AddSelection, 10, 9);
        editor->Send(Message::SetMainSelection, 0);
    };
    for (const auto mode : {LineSelectionMode::VisualStudio, LineSelectionMode::Normal}) {
        editor->SetLineSelectionMode(mode);
        const bool line = mode == LineSelectionMode::VisualStudio;
        multiple(true);
        Gesture(editor);
        editor->CopyLine();
        Require(peer.Request("shape-read") == (line ? "line" : "stream") && peer.SavedText() == "aa\n" &&
                    editor->Send(Message::GetSelections) == 2,
                "multiple-selection Copy Line uses only the main physical-line range and retains other ranges");
        Gesture(editor);
        editor->CutLine();
        Require(Text(editor) == "bb\ncc\nd" && editor->Send(Message::GetSelections) == 2,
                "Cut Line retains original additional-selection typing deletion");
        Require(peer.Request("shape-read") == (line ? "line" : "stream") &&
                    peer.SavedText() == (line ? "aa\n" : "aa\n\nd"),
                "multiple-selection Cut Line's mode argument controls main-line versus combined-stream publication");
        editor->Send(Message::Undo);
        Require(Text(editor) == "aa\nbb\ncc\ndd" && !editor->Send(Message::GetModify),
                "one Undo restores multiple-selection Cut Line");
    }
    editor->SetLineSelectionMode(LineSelectionMode::VisualStudio);
    multiple(false);
    Gesture(editor);
    editor->CutLine();
    Require(Text(editor) == "bb\ncc\ndd" && editor->Send(Message::GetSelections) == 1,
            "Cut Line drops additional selections when original additional typing is disabled");
    multiple(true);
    editor->DeleteLine();
    Require(Text(editor) == "bb\ncc\ndd" && editor->Send(Message::GetSelections) == 2,
            "multiple-selection Delete Line retains extra ranges and deletes only the main caret line");
    editor->Send(Message::SetAdditionalSelectionTyping, 0);
    std::puts("PASS: original rectangle and multiple-selection Cut/Copy/Delete distinctions");
}
void LineCommandUnchanged(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const int copy = frame->GetMenuBar()->FindMenuItem("Edit", "Copy Line");
    const int cut = frame->GetMenuBar()->FindMenuItem("Edit", "Cut Line");
    const int erase = frame->GetMenuBar()->FindMenuItem("Edit", "Delete Line");
    for (const char* text : {"", "aa\nbb\ncc"}) {
        TestEditor::ResetStream(editor, text);
        editor->Send(Message::SetReadOnly, 1);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(copy) && frame->GetMenuBar()->IsEnabled(cut) &&
                    frame->GetMenuBar()->IsEnabled(erase),
                "original explicit line menus stay available for empty and read-only documents");
    }
    TestEditor::ResetStream(editor, "aa\nbb\ncc", 1, 4);
    editor->Send(Message::SetReadOnly, 1);
    Gesture(editor);
    NativeSearch::Command(frame, copy);
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "aa\nbb\n",
            "read-only Copy Line remains available and publishes full lines");
    NativeSearch::Command(frame, cut);
    Require(Text(editor) == "aa\nbb\ncc" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
            "read-only Cut Line preserves bytes and history");
    TestEditor::RequireSelection(editor, 6, 0, "read-only Cut Line retains the original expanded selection");
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "aa\nbb\n",
            "read-only Cut Line retains previous clipboard data");
    editor->Send(Message::SetSel, 1, 4);
    NativeSearch::Command(frame, erase);
    TestEditor::RequireSelection(editor, 1, 4, "read-only Delete Line preserves the selection");
    Require(Text(editor) == "aa\nbb\ncc" && !editor->Send(Message::CanUndo), "read-only Delete Line preserves history");
    for (const auto mode : {LineSelectionMode::None, LineSelectionMode::VisualStudio, LineSelectionMode::Normal,
                            LineSelectionMode::OldVisualStudio}) {
        editor->SetLineSelectionMode(mode);
        const bool line = mode == LineSelectionMode::VisualStudio || mode == LineSelectionMode::OldVisualStudio;
        TestEditor::ResetStream(editor, "tail\n", 5, 5);
        Require(peer.Request("shape-line") == "ready", "seed an independently owned nonempty tagged payload");
        Gesture(editor);
        editor->CutLine();
        Require(peer.Request("shape-read") == (line ? "line" : "stream") &&
                    peer.SavedText() == (line ? "" : "中文\tX\n日本\tY"),
                "empty Cut Line preserves each mode's original publication versus unchanged-clipboard behavior");
        Gesture(editor);
        editor->CopyLine();
        Require(peer.Request("shape-read") == (line ? "line" : "stream") && peer.SavedText().empty(),
                "empty Copy Line publishes an empty payload with each mode's original metadata");
        editor->DeleteLine();
        Require(Text(editor) == "tail\n" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                "empty final-line commands preserve bytes/history/save point");
        TestEditor::RequireSelection(editor, 5, 5, "empty final-line commands retain original endpoints");
    }
    editor->SetLineSelectionMode(LineSelectionMode::VisualStudio);
    TestEditor::ResetStream(editor, "aa", 1, 1);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    Gesture(editor);
    editor->CopyLine();
    Require(Text(editor) == "aa" && editor->Send(Message::CanRedo) && !editor->Send(Message::GetModify),
            "Copy Line preserves a pre-existing redo branch and save point");
    editor->Send(Message::Redo);
    Require(Text(editor) == "aa!", "Copy Line retains the actual original redo edit");
    std::puts("PASS: original menu availability, read-only selection effects, empty payloads and retained redo");
}
void LineCommandSavedBytes(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    TestEditor::ResetStream(editor, "tail", 2, 2, Scintilla::EndOfLine::Lf);
    Gesture(editor);
    editor->CopyLine();
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "tail",
            "Copy Line preserves an unterminated last line's native bytes");
    TestEditor::ResetStream(editor, "AB\nCD", 1, 1);
    editor->Send(Message::Paste);
    Require(Text(editor) == "tail\nAB\nCD", "caret-only line paste supplies the original missing terminal EOL");
    TestEditor::RequireSelection(editor, 6, 6, "unterminated line paste retains original caret offset");

    const char* original = "head\r\n中文\tline\r\ntail";
    TestEditor::ResetStream(editor, original, 9, 9, Scintilla::EndOfLine::CrLf);
    Gesture(editor);
    editor->CopyLine();
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "中文\tline\r\n",
            "independent native process saves exact UTF-8/tab/CRLF Copy Line bytes");
    peer.PreserveSavedText("clipboard-line-command-peer.txt");
    Require(peer.Request("paste") == wxString::FromUTF8("中文\tline\n"),
            "stock native text control pastes Copy Line as plain Unicode text");
    const auto path = TestFiles::TemporaryDirectory("line-commands.txt");
    Require(frame->SaveAs(TestFiles::Filename(path)).status == SaveStatus::Saved && TestFiles::Bytes(path) == original,
            "Copy Line retains the actual independently saved document bytes");
    Gesture(editor);
    editor->CutLine();
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == "head\r\ntail",
            "Cut Line independently saves the original UTF-8/CRLF deletion result");
    editor->Send(Message::Undo);
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == original,
            "one Undo and Save restores original Cut Line disk bytes");
    editor->Send(Message::Redo);
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == "head\r\ntail",
            "one Redo and Save restores Cut Line disk bytes");
    TestEditor::ResetStream(editor, original, 9, 9, Scintilla::EndOfLine::CrLf);
    editor->DeleteLine();
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == "head\r\ntail",
            "Delete Line independently saves the original UTF-8/CRLF deletion result");
    editor->Send(Message::Undo);
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == original,
            "one Undo and Save restores original Delete Line disk bytes");
    editor->Send(Message::Redo);
    Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == "head\r\ntail",
            "one Redo and Save restores Delete Line disk bytes");
    std::puts(
        "PASS: native stock-control interoperability, missing-EOL paste and independently saved UTF-8/CRLF bytes");
}
void LineCommands(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const int copy = frame->GetMenuBar()->FindMenuItem("Edit", "Copy Line");
    const int cut = frame->GetMenuBar()->FindMenuItem("Edit", "Cut Line");
    const int erase = frame->GetMenuBar()->FindMenuItem("Edit", "Delete Line");
    Require(copy != wxNOT_FOUND && cut != wxNOT_FOUND && erase != wxNOT_FOUND,
            "native Edit menu exposes Cut Line, Copy Line and Delete Line");
    editor->SetLineSelectionMode(LineSelectionMode::None);
    TestEditor::ResetStream(editor, "tail\n", 1, 1);
    editor->Send(Message::SetMultipleSelection, 1);
    editor->Send(Message::SetAdditionalSelectionTyping, 0);
    editor->Send(Message::AddSelection, 5, 5);
    editor->DeleteLine();
    Require(editor->Send(Message::GetSelections) == 2, "empty Delete Line retains additional caret ranges");
    editor->CutLine();
    Require(Text(editor) == "tail\n" && editor->Send(Message::GetSelections) == 1 && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "empty untagged Cut Line still applies the original additional-selection filtering");
    editor->SetLineSelectionMode(LineSelectionMode::VisualStudio);
    TestEditor::ResetStream(editor, "first\nmiddle\nlast", 8, 8, Scintilla::EndOfLine::Lf);
    Gesture(editor);
    NativeSearch::Command(frame, copy);
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "middle\n",
            "Copy Line publishes native whole-line text and metadata");
    TestEditor::ResetStream(editor, "tail\n", 5, 5);
    Gesture(editor);
    editor->CutLine();
    Require(peer.Request("shape-read") == "line" && peer.SavedText().empty(),
            "Cut Line on an empty final line publishes the original empty tagged payload");
    Require(Text(editor) == "tail\n" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
            "empty final-line Cut preserves bytes, history and save point");
    TestEditor::ResetStream(editor, "first\nmiddle\nlast", 8, 8);
    NativeLineShortcut(editor, 'C');
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "middle\n",
            "actual Ctrl+Shift+C publishes the line through the native menu accelerator");
    NativeLineShortcut(editor, 'X');
    Require(Text(editor) == "first\nlast", "actual Ctrl+Shift+X cuts the current line");
    TestEditor::RequireSelection(editor, 6, 6, "explicit native Cut Line retains the original core position");
    TestEditor::ResetStream(editor, "first\nmiddle\nlast", 8, 8);
    NativeLineShortcut(editor, 'D');
    Require(Text(editor) == "first\nlast", "actual Ctrl+Shift+D deletes the caret line");
    TestEditor::ResetStream(editor, "first\nmiddle\nlast", 8, 8);
    NativeLineShortcut(editor, 'D', false);
    if (Text(editor) != "first\nmiddle\nmiddle\nlast")
        std::fprintf(stderr, "Native Ctrl+D actual=<%s>\n", Text(editor).c_str());
    Require(Text(editor) == "first\nmiddle\nmiddle\nlast", "ordinary Ctrl+D retains Line Duplicate");
    TestEditor::ResetStream(editor, "first\nmiddle\nlast", 1, 4);
    NativeLineShortcut(editor, 'C', false);
    Require(peer.Request("shape-read") == "stream" && peer.SavedText() == "irs",
            "ordinary native Ctrl+C retains selected-text Copy");
    NativeLineShortcut(editor, 'X', false);
    Require(Text(editor) == "ft\nmiddle\nlast", "ordinary native Ctrl+X retains selected-text Cut");
    for (const int key : {'C', 'X', 'D'}) {
        TestEditor::ResetStream(editor, "first\nmiddle\nlast", 8, 8);
        NativeLineShortcut(editor, key, true, true);
        Require(Text(editor) == "first\nmiddle\nlast" && !editor->Send(Message::CanUndo),
                "extra native Alt modifier does not invoke a line command");
        if (key == 'C')
            Require(peer.Request("shape-read") == "stream" && peer.SavedText() == "irs",
                    "extra native Alt modifier leaves the clipboard unchanged for Copy Line");
    }
    LineCommandModes(editor, peer);
    LineCommandSelections(editor, peer);
    LineCommandShapes(editor, peer);
    LineCommandUnchanged(frame, editor, peer);
    LineCommandSavedBytes(frame, editor, peer);
}
void CurrentLineEnabledMode(EditorControl* editor, EditorControl* other, Peer& peer, LineSelectionMode mode,
                            const wxString& shape) {
    using Scintilla::Message;
    const bool line = mode != LineSelectionMode::Normal;
    Require(shape == (line ? "line" : "stream") && peer.SavedText() == "middle\n",
            "current-line Copy follows the original mode's clipboard meaning");
    Require(!editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify) &&
                editor->Send(Message::GetCurrentPos) == 8,
            "Copy preserves caret, save point and undo history in every enabled mode");
    other->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("ab\ncd"));
    other->Send(Message::SetEOLMode, static_cast<uintptr_t>(Scintilla::EndOfLine::Lf));
    other->Send(Message::GotoPos, 1);
    other->Send(Message::Paste);
    Require(Text(other) == (line ? "middle\nab\ncd" : "amiddle\nb\ncd") && other->Send(Message::GetCurrentPos) == 8,
            "a separate editor document pastes line/stream text at the original position");
    Key(editor, 'V');
    Require(Text(editor) == (line ? "first\nmiddle\nmiddle\nlast" : "first\nmimiddle\nddle\nlast") &&
                editor->Send(Message::GetCurrentPos) == 15,
            "same-editor paste follows the original line/stream meaning and caret offset");
    editor->Send(Message::Undo);
    Require(Text(editor) == "first\nmiddle\nlast" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "one Undo restores current-line paste and save point");
    editor->Send(Message::GotoPos, 8);
    Key(editor, 'X');
    Require(Text(editor) == "first\nlast" &&
                editor->Send(Message::GetCurrentPos) == (mode == LineSelectionMode::OldVisualStudio ? 2 : 8),
            "Cut follows the original next-line or old Visual Studio previous-line column");
    editor->Send(Message::Undo);
    Require(Text(editor) == "first\nmiddle\nlast" && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "one Undo restores Cut and save point in every enabled mode");
    editor->Send(Message::Redo);
    Require(Text(editor) == "first\nlast", "Redo restores each mode's Cut bytes");
}
void LoadCurrentLine(EditorControl* editor, const char* text, intptr_t position,
                     Scintilla::EndOfLine eol = Scintilla::EndOfLine::Lf) {
    using Scintilla::Message;
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
    editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(eol));
    editor->Send(Message::GotoPos, position);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}
void CurrentLineModes(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    EditorControl otherWindow(frame);
    auto* other = &otherWindow;
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(wxID_COPY) && !frame->GetMenuBar()->IsEnabled(wxID_CUT),
            "empty document disables Copy/Cut menus in the original default mode");
    for (const auto mode : {LineSelectionMode::None, LineSelectionMode::VisualStudio, LineSelectionMode::Normal,
                            LineSelectionMode::OldVisualStudio}) {
        editor->SetLineSelectionMode(mode);
        LoadCurrentLine(editor, "first\nmiddle\nlast", 8);
        Require(peer.Request("shape-line") == "ready", "seed a separately owned line clipboard");
        Key(editor, 'C');
        SettleClipboard();
        const auto shape = peer.Request("shape-read");
        if (mode == LineSelectionMode::None) {
            Require(shape == "stream" && peer.SavedText() == "中文\tX\n日本\tY",
                    "None leaves the clipboard untouched for empty-selection Copy");
            Key(editor, 'X');
            Require(Text(editor) == "first\nmiddle\nlast" && editor->Send(Message::GetCurrentPos) == 8 &&
                        !editor->Send(Message::CanUndo),
                    "None leaves bytes, caret and history untouched for empty-selection Cut");
        } else {
            CurrentLineEnabledMode(editor, other, peer, mode, shape);
        }
        // Explicit line commands remain available in None and use the same tag bit.
        LoadCurrentLine(editor, "first\nmiddle\nlast", 8);
        Key(editor, 'C', true);
        SettleClipboard();
        Require(peer.Request("shape-read") == ((static_cast<int>(mode) & 1) ? "line" : "stream") &&
                    peer.SavedText() == "middle\n",
                "Ctrl+Shift+C follows the actual line-selection option");
        Key(editor, 'X', true);
        Require(Text(editor) == "first\nlast" && editor->Send(Message::GetCurrentPos) == 6,
                "Ctrl+Shift+X retains the original explicit line command position");
    }
}
void CurrentLines(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    CurrentLineModes(frame, editor, peer);
    editor->SetLineSelectionMode(LineSelectionMode::VisualStudio);
    struct Case {
        const char* before;
        intptr_t caret;
        const char* after;
        intptr_t cutCaret;
        const char* copied;
        Scintilla::EndOfLine eol = Scintilla::EndOfLine::Lf;
    };
    for (const auto& item : {
             Case{"first\nmiddle\nlast", 2, "middle\nlast", 2, "first\n"},
             Case{"first\nmiddle\nlast", 8, "first\nlast", 8, "middle\n"},
             Case{"first\nmiddle\nlast", 15, "first\nmiddle\n", 13, "last"},
             Case{"first\nmiddle\nlast\n", 15, "first\nmiddle\n", 13, "last\n"},
             Case{"first\r\nmiddle\r\nlast", 2, "middle\r\nlast", 2, "first\r\n", Scintilla::EndOfLine::CrLf},
             Case{"first\r\nmiddle\r\nlast", 9, "first\r\nlast", 9, "middle\r\n", Scintilla::EndOfLine::CrLf},
             Case{"first\r\nmiddle\r\nlast", 17, "first\r\nmiddle\r\n", 15, "last", Scintilla::EndOfLine::CrLf},
             Case{"first\r\nmiddle\r\nlast\r\n", 17, "first\r\nmiddle\r\n", 15, "last\r\n", Scintilla::EndOfLine::CrLf},
             Case{"first\rmiddle\rlast", 8, "first\rlast", 8, "middle\r", Scintilla::EndOfLine::Cr},
         }) {
        LoadCurrentLine(editor, item.before, item.caret, item.eol);
        Menu(frame, wxID_CUT);
        Require(Text(editor) == item.after && editor->Send(Message::GetCurrentPos) == item.cutCaret,
                "first/middle/final-line Cut preserves original EOL and next-line column");
        SettleClipboard();
        Require(peer.Request("shape-read") == "line" && peer.SavedText() == item.copied,
                "independent peer saves exact current-line bytes including the actual final newline");
        editor->Send(Message::Undo);
        Require(Text(editor) == item.before && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                "one Undo restores each boundary Cut and save point");
        editor->Send(Message::Redo);
        Require(Text(editor) == item.after, "Redo restores each boundary Cut");
    }
    LoadCurrentLine(editor, "first\n\t中文long\nab\tXYZ", 13);
    Key(editor, 'X');
    Require(Text(editor) == "first\nab\tXYZ" && editor->Send(Message::GetCurrentPos) == 11,
            "Cut restores the original expanded column across tabs and Unicode");
    LoadCurrentLine(editor, "first\nlast\n", 11);
    Key(editor, 'X');
    Require(Text(editor) == "first\nlast\n" && editor->Send(Message::GetCurrentPos) == 11 &&
                !editor->Send(Message::CanUndo),
            "Cut on the empty final line preserves bytes and history");
    LoadCurrentLine(editor, "first\nmiddle\nlast", 8);
    Key(editor, WXK_INSERT);
    SettleClipboard();
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "middle\n",
            "Ctrl+Insert uses current-line Copy");
    editor->Send(Message::SetReadOnly, 1);
    Key(editor, WXK_DELETE, true, false);
    Require(Text(editor) == "first\nmiddle\nlast" && !editor->Send(Message::CanUndo),
            "read-only current-line Cut preserves bytes/history");
    Key(editor, 'C');
    SettleClipboard();
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "middle\n",
            "read-only current-line Copy remains available");
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::GotoPos, 8);
    Key(editor, WXK_DELETE, true, false);
    Require(Text(editor) == "first\nlast" && editor->Send(Message::GetCurrentPos) == 8,
            "Shift+Delete uses current-line Cut");
    LoadCurrentLine(editor, "first\n中文\tline\nlast", 8);
    Menu(frame, wxID_COPY);
    SettleClipboard();
    Require(peer.Request("shape-read") == "line" && peer.SavedText() == "中文\tline\n",
            "independent peer saves current-line Unicode and tab bytes");
    peer.PreserveSavedText("clipboard-line-peer.txt");
    Key(editor, 'V');
    Require(frame->SaveAs(wxFileName::GetCwd() + "/clipboard-line-editor.txt").status == SaveStatus::Saved,
            "save actual same-editor line paste");
    {
        wxFFile file("clipboard-line-editor.txt", "rb");
        std::string bytes(static_cast<size_t>(file.Length()), '\0');
        Require(file.Read(bytes.data(), bytes.size()) == bytes.size() && bytes == "first\n中文\tline\n中文\tline\nlast",
                "independent disk bytes preserve current-line paste Unicode, tabs and line position");
    }
    std::puts("PASS: all original current-line modes, boundary EOLs, columns, same/peer paste, shortcuts and history");
}
void UtilityShortcut(EditorFrame* frame, int key, bool control, bool alt = false) {
    // Queued state events update the document title; drain them before naming
    // the temporary native input target.
    wxYield();
    const auto title = frame->GetTitle();
    const auto focusName = wxString::Format("wxnotepad clipboard utilities target %lu", wxGetProcessId());
    frame->SetTitle(focusName);
    NativeKeys::Shortcut(frame, key, control, alt, false, nullptr, focusName);
    frame->SetTitle(title);
}

void UtilityBytes(EditorControl* editor, const std::string& text, intptr_t anchor, intptr_t caret) {
    using Scintilla::Message;
    TestEditor::ResetStream(editor, "", 0, 0, Scintilla::EndOfLine::Lf);
    editor->Send(Message::AddText, text.size(), reinterpret_cast<intptr_t>(text.data()));
    editor->Send(Message::SetSel, anchor, caret);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
}

void UtilityTextBoundaries(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    struct AppendCase {
        std::string old, added, expected;
        bool whole = false;
    };
    const AppendCase cases[] = {
        {"", "alpha", "\r\n\r\nalpha"},
        {"X\nY\rZ", "中文😀\r\nB\nC\r", "X\nY\rZ\r\n\r\n中文😀\r\nB\nC\r"},
        {std::string("old\0suffix", 10), "new", "old\r\n\r\nnew"},
        {"old", std::string("A\0B", 3), "old\r\n\r\nA B"},
        {"old", std::string("\0B", 2), "old\r\n\r\n B"},
        {"old", std::string("A\0B", 3), "old\r\n\r\nA", true},
        {"old", std::string("\0B", 2), "old", true},
        {"old", "", "old"},
    };
    for (const auto& fixture : cases) {
        UtilityBytes(editor, fixture.added, 0, fixture.whole ? 0 : static_cast<intptr_t>(fixture.added.size()));
        peer.OfferText(fixture.old);
        Gesture(editor);
        Require(editor->CopyAdd(), "plain append accepts literal Unicode and NUL-boundary fixture");
        Require(peer.Request("utility-read") == "stream" && peer.SavedText() == fixture.expected,
                "independent native text consumer receives original empty/mixed-EOL/NUL append boundary");
        Require(Text(editor) == fixture.added && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                "append boundary fixture retains full document bytes and save point");
    }
    UtilityBytes(editor, "whole\r\nbuffer", 3, 3);
    peer.OfferText("old");
    const int copyAdd = frame->GetMenuBar()->FindMenuItem("Edit", "Copy Add");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(copyAdd), "caret-only native menu remains selection-gated");
    UtilityShortcut(frame, 'E', true);
    Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "old",
            "disabled native Ctrl+E retains clipboard despite direct handler's whole-buffer branch");
    Gesture(editor);
    Require(editor->CopyAdd(), "direct caret-only append accepts a whole document");
    Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "old\r\n\r\nwhole\r\nbuffer",
            "direct Copy Add whole-buffer branch is distinct from native availability");
    UtilityBytes(editor, std::string("A\0B", 3), 2, 0);
    Gesture(editor);
    editor->Send(Message::CopyRange, 0, editor->Send(Message::GetLength));
    Require(peer.Request("utility-raw") == "stream" && peer.SavedText() == "A B",
            "Copy All replaces embedded NUL with a space in native storage");
    Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "A B",
            "ordinary native Unicode consumer receives Copy All's original NUL replacement");
    peer.PreserveSavedText("clipboard-utilities-nul.txt");
    UtilityBytes(editor, "", 0, 0);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(copyAdd) &&
                !frame->GetMenuBar()->IsEnabled(frame->GetMenuBar()->FindMenuItem("Edit", "Copy All")),
            "empty document disables original native bulk-copy commands");
    peer.OfferText("sentinel");
    UtilityShortcut(frame, 'A', false, true);
    Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "sentinel",
            "disabled native Alt+A retains clipboard for an empty document");
    Gesture(editor);
    editor->Send(Message::CopyRange, 0, 0);
    Require(peer.Request("utility-read") == "stream" && peer.SavedText().empty(),
            "direct empty-range Copy All publishes empty Unicode text");
    std::puts("PASS: original clipboard empty, mixed-EOL and NUL boundaries");
}

void UtilityRectangleText(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    TestEditor::ResetStream(editor, "a1b\nc2d", 0, 0, Scintilla::EndOfLine::Lf);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 6);
    Gesture(editor);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Copy All"));
    Require(peer.Request("shape-read") == "stream" && peer.SavedText() == "a1b\nc2d" &&
                editor->Send(Message::SelectionIsRectangle) &&
                editor->Send(Message::GetRectangularSelectionAnchor) == 1 &&
                editor->Send(Message::GetRectangularSelectionCaret) == 6,
            "Copy All publishes the whole buffer while retaining rectangular selection");
    Gesture(editor);
    editor->Send(Message::Copy);
    SettleClipboard();
    TestEditor::ResetStream(editor, "tail", 0, 4);
    Gesture(editor);
    Require(editor->CopyAdd(), "stream Copy Add accepts an earlier rectangular clipboard");
    Require(peer.Request("shape-read") == "stream" && peer.SavedText() == "1\n2\r\n\r\ntail",
            "Copy Add uses rectangle text without a synthetic native transport separator");
    TestEditor::ResetStream(editor, "a1b\n\n", 0, 0, Scintilla::EndOfLine::Lf);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 4);
    editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, 0);
    Gesture(editor);
    editor->Send(Message::Copy);
    SettleClipboard();
    TestEditor::ResetStream(editor, "tail", 0, 4);
    Gesture(editor);
    Require(editor->CopyAdd(), "append accepts a previously copied rectangle with an empty final row");
    Require(peer.Request("shape-read") == "stream" && peer.SavedText() == "a\n\r\n\r\ntail",
            "plain rectangle transfer preserves a genuine final row separator");
    peer.PreserveSavedText("clipboard-utilities-rectangle.txt");
}

class UtilityFeedback final : public wxModalDialogHook {
public:
    UtilityFeedback() { Register(); }
    int Enter(wxDialog*) override {
        ++count;
        return wxID_OK;
    }
    int count = 0;
};

void UtilityFallbacks(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const int copyAdd = frame->GetMenuBar()->FindMenuItem("Edit", "Copy Add");
    for (const char* offer : {"utility-empty", "nontext"}) {
        for (const auto mode : {LineSelectionMode::None, LineSelectionMode::VisualStudio, LineSelectionMode::Normal,
                                LineSelectionMode::OldVisualStudio}) {
            TestEditor::ResetStream(editor, "first\nlast", 2, 2);
            editor->SetLineSelectionMode(mode);
            Require(peer.Request(offer) == "ready", "independent process seeds a missing-Unicode clipboard");
            const auto before = peer.Request("utility-formats");
            Gesture(editor);
            Require(editor->CopyAdd(), "caret-only missing-Unicode fallback invokes underlying Copy");
            Require(peer.Request("utility-formats") == before && !editor->Send(Message::CanPaste),
                    "underlying Copy leaves missing-Unicode clipboard unchanged for every current-line preference");
            TestEditor::RequireSelection(editor, 2, 2, "caret-only fallback preserves its endpoints");
        }
        TestEditor::ResetStream(editor, "first\nlast", 1, 4);
        Require(peer.Request(offer) == "ready", "seed missing Unicode for selected fallback");
        Gesture(editor);
        NativeSearch::Command(frame, copyAdd);
        Require(peer.Request("shape-read") == "stream" && peer.SavedText() == "irs",
                "selected missing-Unicode fallback transfers ordinary core selection text");
        TestEditor::ResetStream(editor, "a1b\nc2d", 0, 0, Scintilla::EndOfLine::Lf);
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, 1);
        editor->Send(Message::SetRectangularSelectionCaret, 6);
        Require(peer.Request(offer) == "ready", "seed missing Unicode for rectangular fallback");
        Gesture(editor);
        UtilityFeedback feedback;
        NativeSearch::Command(frame, copyAdd);
        Require(feedback.count == 0 && peer.Request("shape-read") == "rectangle",
                "missing-Unicode branch copies a rectangle without the Unicode-append warning");
#ifdef __WXMSW__
        Require(peer.SavedText() == "1\n2", "Windows fallback retains original rectangle text");
#else
        Require(peer.SavedText() == "1\n2\n", "GTK fallback retains native rectangle transport meaning");
#endif
    }
    editor->SetLineSelectionMode(LineSelectionMode::VisualStudio);
    TestEditor::ResetStream(editor, "a1b\nc2d", 0, 0, Scintilla::EndOfLine::Lf);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 6);
    peer.OfferText("unchanged");
    Gesture(editor);
    {
        UtilityFeedback feedback;
        NativeSearch::Command(frame, copyAdd);
        Require(feedback.count == 1 && peer.Request("utility-read") == "stream" && peer.SavedText() == "unchanged",
                "Unicode append reports a rectangular selection and preserves the independently owned offer");
    }
    Require(Text(editor) == "a1b\nc2d" && editor->Send(Message::GetRectangularSelectionAnchor) == 1 &&
                editor->Send(Message::GetRectangularSelectionCaret) == 6 && !editor->Send(Message::CanUndo),
            "rectangle rejection retains editor bytes, rectangle and history");
    editor->Send(Message::SetRectangularSelectionCaret, 1);
    Gesture(editor);
    Require(editor->CopyAdd(), "empty rectangle uses the original whole-buffer Unicode branch");
    Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "unchanged\r\n\r\na1b\nc2d",
            "empty rectangle is separate from rejected nonempty rectangular selection");
    std::puts("PASS: original clipboard fallback branches and rectangular notification");
}

void UtilitySavedState(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const auto directory = TestFiles::TemporaryDirectory("clipboard-utilities-中文");
    std::filesystem::create_directory(directory);
    const TestFiles::EncodedFixture fixtures[] = {
        {"utf8", FileEncoding::Utf8,
         "\xef\xbb\xbf"
         "A B\r\n",
         "\xef\xbb\xbf"
         "A B\r\n!"},
        {"gbk", FileEncoding::Gbk, "\xd6\xd0\r\n", "\xd6\xd0\r\n!"},
        {"utf16", FileEncoding::Utf16Le,
         std::string("\xff\xfe"
                     "A\0 \0B\0\r\0\n\0",
                     12),
         std::string("\xff\xfe"
                     "A\0 \0B\0\r\0\n\0!\0",
                     14)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "open independently encoded clipboard utility fixture");
        const auto text = Text(editor);
        const auto bom = frame->DocumentHasBom();
        const auto eol = editor->Send(Message::GetEOLMode);
        editor->Send(Message::EmptyUndoBuffer);
        editor->Send(Message::SetSavePoint);
        editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
        editor->Send(Message::Undo);
        Require(!editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                "seed an original pending Redo before clipboard commands");
        editor->Send(Message::SetSel, 3, 0);
        editor->Send(Message::SetReadOnly, 1);
        {
            UtilityFeedback feedback;
            for (const char* label : {"Copy All", "Copy Add", "Clear Clipboard"}) {
                const int command = frame->GetMenuBar()->FindMenuItem("Edit", label);
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(frame->GetMenuBar()->IsEnabled(command),
                        "selected read-only document retains copy availability");
                Gesture(editor);
                NativeSearch::Command(frame, command);
            }
            Require(feedback.count == 0, "successful clipboard utilities produce no modal notification");
        }
        Require(peer.Request("utility-formats") == "empty", "encoded utility commands leave no clipboard formats");
        Require(Text(editor) == text, "clipboard utilities retain full decoded editor bytes");
        Require(frame->DocumentEncoding() == fixture.encoding && frame->DocumentHasBom() == bom &&
                    editor->Send(Message::GetEOLMode) == eol,
                "clipboard utilities retain encoding, BOM and EOL metadata");
        Require(!editor->Send(Message::GetModify), "clipboard utilities retain document save point");
        Require(!editor->Send(Message::CanUndo) && !editor->Send(Message::CanRedo),
                "read-only clipboard commands retain disabled Undo/Redo availability");
        TestEditor::RequireSelection(editor, 3, 0, "clipboard utilities retain reverse selected endpoints");
        editor->Send(Message::SetReadOnly, 0);
        Require(!editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                "clipboard commands preserve pending Redo while read-only commands are disabled");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == fixture.before,
                "clipboard utilities independently save the unchanged original encoded bytes");
        const auto artifact = std::string("clipboard-utilities-") + fixture.name;
        std::filesystem::copy_file(path, artifact + "-before.txt", std::filesystem::copy_options::overwrite_existing);
        editor->Send(Message::Redo);
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"clipboard commands preserve previous Redo encoded result",
                                         "clipboard commands preserve previous Undo encoded result",
                                         "clipboard commands preserve repeated Redo encoded result"});
        std::filesystem::copy_file(path, artifact + "-after.txt", std::filesystem::copy_options::overwrite_existing);
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: clipboard utilities retain encoded disk bytes and original Undo/Redo");
}

void Utilities(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const int copyAll = frame->GetMenuBar()->FindMenuItem("Edit", "Copy All");
    Require(copyAll != wxNOT_FOUND, "native Copy All menu exists");
    const std::string text = "first\r\n中文😀\nlast\rtail";
    TestEditor::ResetStream(editor, text.c_str());
    editor->Send(Message::SetSel, 12, 2);
    const auto anchor = editor->Send(Message::GetAnchor);
    const auto caret = editor->Send(Message::GetCurrentPos);
    Gesture(editor);
    NativeSearch::Command(frame, copyAll);
    Require(peer.Request("shape-read") == "stream" && peer.SavedText() == text,
            "Copy All transfers whole Unicode mixed-EOL buffer as native stream text");
    peer.PreserveSavedText("clipboard-utilities-peer.txt");
    Require(Text(editor) == text && editor->Send(Message::GetAnchor) == anchor &&
                editor->Send(Message::GetCurrentPos) == caret && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo),
            "Copy All retains selection, caret, bytes and save point");
    UtilityShortcut(frame, 'A', false, true);
    Require(peer.Request("shape-read") == "stream" && peer.SavedText() == text, "native Alt+A invokes Copy All");
    std::puts("PASS: native Copy All");
    const int copyAdd = frame->GetMenuBar()->FindMenuItem("Edit", "Copy Add");
    Require(copyAdd != wxNOT_FOUND, "native Copy Add menu exists");
    TestEditor::ResetStream(editor, "alpha\nbeta", 0, 5);
    Require(peer.Request("shape-line") == "ready", "independent process owns tagged Unicode clipboard");
    Gesture(editor);
    NativeSearch::Command(frame, copyAdd);
    Require(peer.Request("shape-read") == "stream" && peer.SavedText() == "中文\tX\n日本\tY\r\n\r\nalpha",
            "Copy Add appends two CRLF separators and selected text while dropping line tags");
    Require(Text(editor) == "alpha\nbeta" && editor->Send(Message::GetAnchor) == 0 &&
                editor->Send(Message::GetCurrentPos) == 5 && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo),
            "Copy Add retains document, selection, save point and history");
    Require(peer.Request("shape-line") == "ready", "prepare independently owned clipboard for native Ctrl+E");
    UtilityShortcut(frame, 'E', true);
    Require(peer.Request("shape-read") == "stream" && peer.SavedText() == "中文\tX\n日本\tY\r\n\r\nalpha",
            "native Ctrl+E invokes Copy Add");
    std::puts("PASS: native Copy Add");
    const int clearClipboard = frame->GetMenuBar()->FindMenuItem("Edit", "Clear Clipboard");
    Require(clearClipboard != wxNOT_FOUND, "native Clear Clipboard menu exists");
    Require(peer.Request("nontext") == "ready" && peer.Request("utility-formats") == "data",
            "independent owner publishes a non-text clipboard format");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(clearClipboard), "any native format enables Clear Clipboard");
    Gesture(editor);
    NativeSearch::Command(frame, clearClipboard);
    Require(peer.Request("utility-formats") == "empty", "independent native process observes a cleared clipboard");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(clearClipboard), "empty native clipboard disables Clear Clipboard");
    Require(Text(editor) == "alpha\nbeta" && editor->Send(Message::GetAnchor) == 0 &&
                editor->Send(Message::GetCurrentPos) == 5 && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo),
            "Clear Clipboard retains document, selection, save point and history");
    std::puts("PASS: native Clear Clipboard");
    UtilityRectangleText(frame, editor, peer);
    UtilityTextBoundaries(frame, editor, peer);
    UtilityFallbacks(frame, editor, peer);
    UtilitySavedState(frame, editor, peer);
}

void PathTextCopies(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    auto* edit = frame->GetMenuBar()->GetMenu(frame->GetMenuBar()->FindMenu("Edit"));
    const int submenu = edit->FindItem("Copy to Clipboard");
    Require(submenu != wxNOT_FOUND, "original Copy to Clipboard submenu exists");
    auto* copy = edit->FindItem(submenu)->GetSubMenu();
    const auto state = [editor] {
        std::vector<intptr_t> result;
        for (const auto message : {Message::GetAnchor, Message::GetCurrentPos, Message::GetModify, Message::CanUndo,
                                   Message::CanRedo, Message::GetSelections, Message::GetMainSelection,
                                   Message::GetSelectionMode, Message::GetEOLMode, Message::GetReadOnly})
            result.push_back(editor->Send(message));
        for (intptr_t i = 0; i < editor->Send(Message::GetSelections); ++i) {
            result.push_back(editor->Send(Message::GetSelectionNAnchor, i));
            result.push_back(editor->Send(Message::GetSelectionNCaret, i));
        }
        return result;
    };
    const auto check = [&](const char* label, const std::string& expected, int shortcut = 0) {
        const int id = copy->FindItem(label);
        Require(id != wxNOT_FOUND, "original filename copy command exists");
        if (shortcut)
            Require(peer.Request("copy") == "ready", "independent peer seeds distinct owned text before shortcut");
        const auto before = state();
        const auto text = Text(editor);
        const auto encoding = frame->DocumentEncoding();
        const bool bom = frame->DocumentHasBom();
        if (shortcut) {
            std::printf("Checking native %sShift+F9 publication\n", shortcut == 2 ? "Alt+" : "");
            const auto title = frame->GetTitle();
            const auto focusName = wxString::Format("wxnotepad path copy target %lu", wxGetProcessId());
            frame->SetTitle(focusName);
            NativeKeys::Shortcut(frame, WXK_F9, false, shortcut == 2, true, nullptr, focusName);
            frame->SetTitle(title);
        } else {
            Gesture(editor);
            NativeSearch::Command(frame, id);
        }
        Require(peer.Request("shape-read") == "stream" && peer.SavedText() == expected,
                "independent native clipboard peer receives original literal filename text without shape tags");
        Require(Text(editor) == text && state() == before && frame->DocumentEncoding() == encoding &&
                    frame->DocumentHasBom() == bom,
                "copying preserves text, directed/all selection ranges, histories, save point and metadata");
    };
    TestEditor::ResetStream(editor, "AoldZ", 4, 1);
    for (const char* label : {"File Name", "File Name and Ext.", "Full Path Name"})
        check(label, "Untitled");
    const auto directory = TestFiles::TemporaryDirectory("path-text-copy") / std::filesystem::u8path("目录😀");
    std::filesystem::create_directories(directory);
    const std::pair<const char*, const char*> names[] = {{"report.txt", "report"},
                                                         {"archive.part.txt", "archive.part"},
                                                         {"README", "README"},
                                                         {".env", ""},
                                                         {".config.json", ".config"},
                                                         {"dot.before space", "dot.before space"},
                                                         {"no.ext space.txt", "no.ext space"},
                                                         {"é中文😀.txt", "é中文😀"}};
    for (const auto& [name, base] : names) {
        const auto path = directory / std::filesystem::u8path(name);
        TestFiles::WriteBytes(path, "AoldZ");
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Utf8).status == OpenStatus::Opened,
                "copy filename fixture opens through native document identity");
        TestEditor::ResetStream(editor, "AoldZ", 4, 1);
        check("File Name", base);
        check("File Name and Ext.", name);
        check("Full Path Name", TestFiles::Filename(path).utf8_string());
    }
    const auto legacyPath = directory / std::filesystem::u8path("é中文😀.txt");
    Require(frame->Open(TestFiles::Filename(legacyPath), FileEncoding::Gbk).status == OpenStatus::Opened,
            "open explicit GBK identity before Unicode clipboard publication");
    check("File Name and Ext.", "é中文😀.txt");
    for (const int shape : {0, 1, 2, 3, 4}) {
        TestEditor::ResetStream(editor, "abcd\nefgh", 1, 2, Scintilla::EndOfLine::Lf);
        if (shape == 0)
            editor->Send(Message::SetSel, 1, 1);
        else if (shape == 1)
            editor->Send(Message::AddSelection, 7, 6);
        else if (shape == 2) {
            editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
            editor->Send(Message::SetRectangularSelectionAnchor, 1);
            editor->Send(Message::SetRectangularSelectionCaret, 7);
        } else if (shape == 3)
            editor->Send(Message::SetReadOnly, 1);
        else {
            editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
            check("File Name and Ext.", "é中文😀.txt");
            editor->Send(Message::Undo);
        }
        check("File Name and Ext.", "é中文😀.txt");
    }
    editor->Send(Message::SetReadOnly, 0);
    check("File Name and Ext.", "é中文😀.txt", 1);
    check("Full Path Name", frame->DocumentPath().utf8_string(), 2);
    const auto previous = frame->DocumentPath();
    Require(frame->SaveAs(std::nullopt).status == SaveStatus::Cancelled && frame->DocumentPath() == previous,
            "cancelled Save As retains the copied identity");
    check("File Name and Ext.", "é中文😀.txt");
    Require(frame->SaveAs(TestFiles::Filename(directory / "missing" / "failed.txt")).status == SaveStatus::Failed &&
                frame->DocumentPath() == previous,
            "failed Save As retains the copied identity");
    check("Full Path Name", previous.utf8_string());
    const auto renamed = directory / "renamed.txt";
    Require(frame->SaveAs(TestFiles::Filename(renamed)).status == SaveStatus::Saved,
            "successful Save As commits the copied identity");
    check("File Name and Ext.", "renamed.txt");
    check("Full Path Name", TestFiles::Filename(renamed).utf8_string());
    std::filesystem::remove(renamed);
    check("File Name and Ext.", "renamed.txt");
    std::puts("PASS: all three name/path copy commands, literal Unicode/native paths, identity transitions,");
    std::puts("      real menu/shortcuts, plain native peer bytes and unchanged editor/history/metadata");
}

void ReadOnlyCommands(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const int mode = frame->GetMenuBar()->FindMenuItem("File", "Read Only Mode");
    Require(mode != wxNOT_FOUND, "original read-only mode exists in the native File Mode menu");
    for (bool keyboard : {false, true}) {
        TestEditor::ResetStream(editor, "alpha\r\nbeta\r\n", 1, 4);
        Require(peer.Request("copy") == "ready", "independent stock app publishes the clipboard seed");
        const auto seed = peer.Request("paste");
        Gesture(editor);
        NativeSearch::Command(frame, mode);
        Require(frame->GetMenuBar()->IsEnabled(wxID_CUT), "original nonempty Cut stays enabled while protected");
        if (keyboard)
            NativeLineShortcut(editor, 'X', false);
        else
            NativeSearch::Command(frame, wxID_CUT);
        Require(Text(editor) == "alpha\r\nbeta\r\n" && !editor->Send(Message::GetModify) &&
                    editor->Send(Message::GetUndoActions) == 0 && peer.Request("paste") == seed,
                "protected native Cut and Ctrl+X retain bytes, save point, history and independent clipboard bytes");
        TestEditor::RequireSelection(editor, 1, 4, "original protected stream Cut retains selection");
        Gesture(editor);
        if (keyboard)
            NativeLineShortcut(editor, 'X', true);
        else
            NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Cut Line"));
        Require(Text(editor) == "alpha\r\nbeta\r\n" && peer.Request("paste") == seed,
                "protected native Cut Line and Ctrl+Shift+X preserve document and independently received clipboard");
        TestEditor::RequireSelection(editor, 7, 0,
                                     "original protected Cut Line expands selection before rejecting writes");
        editor->Send(Message::SetSel, 1, 4);
        Gesture(editor);
        if (keyboard)
            NativeLineShortcut(editor, 'C', false);
        else
            NativeSearch::Command(frame, wxID_COPY);
        Require(peer.Request("paste") == "lph", "native Copy and Ctrl+C remain usable on protected selections");

        TestEditor::ResetStream(editor, "alpha\r\nbeta\r\n", 4, 1);
        Require(peer.Request("copy") == "ready", "republish independent native text before protected Paste");
        Gesture(editor);
        NativeSearch::Command(frame, mode);
        Require(!frame->GetMenuBar()->IsEnabled(wxID_PASTE),
                "original native Paste menu is disabled by write protection");
        if (keyboard)
            NativeLineShortcut(editor, 'V', false);
        else
            editor->Send(Message::Paste);
        Require(Text(editor) == "alpha\r\nbeta\r\n" && !editor->Send(Message::GetModify) &&
                    editor->Send(Message::GetUndoActions) == 0 && peer.Request("paste") == seed,
                "protected Ctrl+V and independent editor-message Paste preserve text and history");
        // Original IDR_MAINWND consumes disabled Ctrl+V without dispatching SCI_PASTE.
        TestEditor::RequireSelection(editor, keyboard ? 4 : 1, 1,
                                     keyboard ? "original disabled Ctrl+V retains the selection"
                                              : "original direct protected Paste retains selection collapse");
    }
    TestEditor::ResetStream(editor, "history");
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    NativeSearch::Command(frame, mode);
    const auto history = editor->Send(Message::GetUndoCurrent);
    NativeLineShortcut(editor, 'Z', false);
    NativeLineShortcut(editor, 'Y', false);
    Require(Text(editor) == "history" && editor->Send(Message::GetUndoCurrent) == history,
            "protected native Ctrl+Z/Ctrl+Y retain the underlying history cursor");
    NativeSearch::Command(frame, mode);
    editor->Send(Message::Redo);
    Require(Text(editor) == "history!", "original Redo survives protected keyboard Undo/Redo attempts");
    std::puts("PASS: F10 mode, native/keyboard Cut/Copy/Paste/Undo/Redo with independently received clipboard bytes");
}
void SwapNow(EditorControl* editor) {
    Gesture(editor);
    editor->SwapClipboard();
}

void SwapBoundaries(EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    // Literal original Windows results at Notepad4 89b8b36; see original-clipboard-transfer.md.
    struct EolCase {
        Scintilla::EndOfLine mode;
        const char* selected;
        intptr_t selectedEnd;
        const char* pasted;
        intptr_t pastedEnd;
    };
    const EolCase endings[] = {
        {Scintilla::EndOfLine::CrLf, "AX\r\nY\r\nZ\r\nQZ", 11, "AoX\r\nY\r\nZ\r\nQldZ", 12},
        {Scintilla::EndOfLine::Cr, "AX\rY\rZ\rQZ", 8, "AoX\rY\rZ\rQldZ", 9},
        {Scintilla::EndOfLine::Lf, "AX\nY\nZ\nQZ", 8, "AoX\nY\nZ\nQldZ", 9},
    };
    for (const auto& fixture : endings) {
        for (const bool convert : {false, true}) {
            TestEditor::ResetStream(editor, "AoldZ", 1, 4, fixture.mode);
            editor->Send(Message::SetPasteConvertEndings, convert);
            peer.OfferText("X\r\nY\nZ\rQ");
            SwapNow(editor);
            Require(Text(editor) == fixture.selected,
                    "selected Swap always applies original clipboard EOL normalization");
            TestEditor::RequireSelection(editor, 1, fixture.selectedEnd,
                                         "normalized selected Swap uses byte endpoints");
            Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "old",
                    "normalized Swap independently transfers the original cut bytes");
            TestEditor::ResetStream(editor, "AoldZ", 2, 2, fixture.mode);
            peer.OfferText("X\r\nY\nZ\rQ");
            SwapNow(editor);
            Require(Text(editor) == (convert ? fixture.pasted : "AoX\r\nY\nZ\rQldZ"),
                    "empty-selection Swap preserves the ordinary paste-convert setting");
            TestEditor::RequireSelection(editor, 2, convert ? fixture.pastedEnd : 10,
                                         "ordinary Paste determines the exact selected span");
            Require(peer.Request("utility-formats") == "empty",
                    "mixed-EOL caret Swap independently clears native data");
        }
    }
    editor->Send(Message::SetPasteConvertEndings, 1);
    for (const auto& offer : {std::string(), std::string("\0suffix", 7), std::string("X\0suffix", 8)}) {
        const bool inserts = !offer.empty() && offer.front() == 'X';
        TestEditor::ResetStream(editor, "AoldZ", 1, 4);
        peer.OfferText(offer);
        SwapNow(editor);
        Require(Text(editor) == (inserts ? "AXZ" : "AoldZ") && editor->Send(Message::GetModify) == inserts &&
                    editor->Send(Message::CanUndo) == inserts,
                "selected Swap uses the native C-string boundary and leaves empty text unchanged");
        TestEditor::RequireSelection(editor, 1, inserts ? 2 : 4,
                                     "selected C-string boundary retains original endpoints");
        Require(peer.Request("utility-read") == "stream" && peer.SavedText() == (inserts ? "old" : ""),
                "independent consumer sees original C-string clipboard results");
        TestEditor::ResetStream(editor, "AoldZ", 2, 2);
        peer.OfferText(offer);
        SwapNow(editor);
        Require(Text(editor) == (inserts ? "AoXldZ" : "AoldZ") && peer.Request("utility-formats") == "empty",
                "caret C-string Swap clears Unicode even when Paste inserts nothing");
    }
    UtilityBytes(editor, std::string("A\0BCZ", 5), 0, 4);
    peer.OfferText("Q");
    SwapNow(editor);
    Require(Text(editor) == "QZ" && peer.Request("utility-read") == "stream" && peer.SavedText() == "A BC",
            "Swap Cut preserves original selected NUL-to-space clipboard conversion");
    editor->Send(Message::Undo);
    Require(Text(editor) == std::string("A\0BCZ", 5), "single Undo independently restores original embedded NUL bytes");
    TestEditor::ResetStream(editor, "A中文😀Z", 1, 11);
    peer.OfferText("X");
    SwapNow(editor);
    Require(Text(editor) == "AXZ" && peer.Request("utility-read") == "stream" && peer.SavedText() == "中文😀",
            "selected Swap independently transfers the original non-ASCII cut text as native Unicode");
    std::puts("PASS: original Swap EOL and empty/embedded-NUL text boundaries");
}

void SwapAvailability(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const int swap = frame->GetMenuBar()->FindMenuItem("Edit", "Swap");
    for (const char* offer : {"utility-empty", "nontext"}) {
        TestEditor::ResetStream(editor, "AoldZ", 1, 4);
        Require(peer.Request(offer) == "ready", "independent owner publishes absent/non-text Swap fixture");
        const auto formats = peer.Request("utility-formats");
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(swap), "selection enables Swap without native paste capability");
        SwapNow(editor);
        Require(Text(editor) == "AoldZ" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify) &&
                    peer.Request("utility-formats") == formats,
                "unreadable selected Swap retains document history and independent native formats");
        editor->Send(Message::SetSel, 2, 2);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(!frame->GetMenuBar()->IsEnabled(swap), "absent/non-text caret disables native Swap");
        UtilityShortcut(frame, 'K', true);
        Require(peer.Request("utility-formats") == formats, "disabled native Ctrl+K retains the independent offer");
        SwapNow(editor);
        Require(Text(editor) == "AoldZ" && !editor->Send(Message::CanUndo) &&
                    peer.Request("utility-formats") == "empty",
                "forced caret command clears non-text formats separately from native availability");
    }
    const int readOnly = frame->GetMenuBar()->FindMenuItem("File", "Read Only Mode");
    for (const bool empty : {false, true}) {
        TestEditor::ResetStream(editor, "AoldZ", empty ? 2 : 1, empty ? 2 : 4);
        peer.OfferText("replacement");
        NativeSearch::Command(frame, readOnly);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(frame->GetMenuBar()->IsEnabled(swap) == !empty, "original selection enables Swap in read-only mode");
        SwapNow(editor);
        Require(Text(editor) == "AoldZ" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
                "read-only Swap retains document bytes and history");
        TestEditor::RequireSelection(editor, empty ? 2 : 1, empty ? 2 : 5,
                                     "read-only Swap retains original selected endpoint side effect");
        if (empty)
            Require(peer.Request("utility-formats") == "empty", "forced read-only caret Swap clears the native offer");
        else
            Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "replacement",
                    "selected read-only Swap independently retains the offered text");
        NativeSearch::Command(frame, readOnly);
    }
    TestEditor::ResetStream(editor, "AoldZ", 1, 4);
    editor->Send(Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Message::Undo);
    editor->Send(Message::SetSel, 1, 4);
    Require(peer.Request("nontext") == "ready", "prepare a non-text no-op with pending Redo");
    SwapNow(editor);
    Require(!editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo), "no-op Swap preserves pending Redo");
    peer.OfferText("X");
    SwapNow(editor);
    Require(Text(editor) == "AXZ" && !editor->Send(Message::CanRedo), "actual Swap replaces the old Redo branch");
#ifdef __WXMSW__
    TestEditor::ResetStream(editor, "AoldZ", 1, 4);
    peer.OfferText("owned held offer");
    Require(peer.Request("hold") == "held", "independent process holds Windows clipboard");
    SwapNow(editor);
    Require(peer.Request("release") == "released", "independent process releases Windows clipboard");
    Require(Text(editor) == "AoldZ" && !editor->Send(Message::GetModify) && peer.Request("utility-read") == "stream" &&
                peer.SavedText() == "owned held offer",
            "unavailable Windows clipboard leaves selected Swap unchanged");
#endif
    std::puts("PASS: native Swap availability, read-only outcomes, unavailable text and pending Redo");
}

void RequireRectangleRanges(EditorControl* editor, bool reverse) {
    using Scintilla::Message;
    const intptr_t ranges[3][2] = {{1, 2}, {7, 8}, {13, 14}};
    for (int index = 0; index < 3; ++index) {
        const auto* range = ranges[reverse ? 2 - index : index];
        Require(editor->Send(Message::GetSelectionNAnchor, index) == range[reverse ? 1 : 0] &&
                    editor->Send(Message::GetSelectionNCaret, index) == range[reverse ? 0 : 1],
                "native rectangular ranges match independent original byte positions");
    }
}
void SwapSelectedRectangles(EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    for (const bool reverse : {false, true}) {
        // Identical rows keep the original post-Cut rectangle geometry independent of native fonts.
        TestEditor::ResetStream(editor, "abcd\r\nabcd\r\nabcd", 0, 0, Scintilla::EndOfLine::CrLf);
        wxYield();
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, reverse ? 14 : 1);
        editor->Send(Message::SetRectangularSelectionCaret, reverse ? 1 : 14);
        peer.OfferText("Q\nR");
        Require(editor->Send(Message::GetSelections) == 3 && editor->Send(Message::GetMainSelection) == 2,
                "uniform native rectangle matches independently captured original selection state");
        RequireRectangleRanges(editor, reverse);
        SwapNow(editor);
        Require(Text(editor) == (reverse ? "aQ\r\nRcd\r\nacd\r\nacd" : "acd\r\nacd\r\naQ\r\nRcd"),
                "selected rectangle Swap retains original main-range insertion site");
        TestEditor::RequireSelection(editor, reverse ? 5 : 1, reverse ? 1 : 5,
                                     "rectangle Swap uses original rectangle endpoint direction and becomes stream");
        Require(!editor->Send(Message::SelectionIsRectangle) && peer.Request("utility-read") == "rectangle",
                "selected rectangle Swap independently publishes original rectangular Cut metadata");
#ifdef __WXGTK__
        Require(peer.SavedText() == "b\r\nb\r\nb\n", "GTK native rectangle retains its required LF transport carrier");
#else
        Require(peer.SavedText() == "b\r\nb\r\nb", "Windows native rectangle retains original cut text bytes");
#endif
        peer.PreserveSavedText("clipboard-swap-rectangle-peer.txt");
    }
}
void SwapRectanglePlainText(EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    // Our earlier rectangle Copy offers exact plain text in addition to its native shape carrier.
    for (const bool emptyLastRow : {false, true}) {
        TestEditor::ResetStream(editor, emptyLastRow ? "a1b\n\n" : "a1b\nc2d", 0, 0, Scintilla::EndOfLine::Lf);
        editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
        editor->Send(Message::SetRectangularSelectionAnchor, 1);
        editor->Send(Message::SetRectangularSelectionCaret, emptyLastRow ? 4 : 6);
        if (emptyLastRow) editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, 2);
        Gesture(editor);
        editor->Send(Message::Copy);
        TestEditor::ResetStream(editor, "AoldZ", 1, 4, Scintilla::EndOfLine::Lf);
        SwapNow(editor);
        Require(Text(editor) == (emptyLastRow ? "A1\nZ" : "A1\n2Z"),
                "selected Swap retains exact rectangle plain text and genuine empty final rows");
    }
}
void SwapShapes(EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    for (const char* marker : {"utility-offer-line", "utility-offer-vsline"}) {
        TestEditor::ResetStream(editor, "AoldZ", 2, 2, Scintilla::EndOfLine::CrLf);
        peer.OfferText("X\r\nY", marker);
        SwapNow(editor);
        Require(Text(editor) == "X\r\nY\r\nAoldZ", "caret Swap uses original tagged line insertion at line start");
        TestEditor::RequireSelection(editor, 2, 8, "line Swap selects the actual old/new caret span");
        Require(peer.Request("utility-formats") == "empty",
                "tagged line Swap independently clears every native format");
    }
    for (const char* marker : {"utility-offer-rectangle", "utility-offer-both"}) {
        TestEditor::ResetStream(editor, "AA\r\nAA", 1, 1, Scintilla::EndOfLine::CrLf);
        peer.OfferText("X\r\nY\r\n", marker);
        SwapNow(editor);
        Require(Text(editor) == "AXA\r\nAYA",
                "caret Swap retains rectangle insertion and rectangle-over-line precedence");
        TestEditor::RequireSelection(editor, 1, 1,
                                     "rectangle Paste can retain an empty old/new caret span after editing");
        Require(peer.Request("utility-formats") == "empty", "rectangle Swap independently clears its native tags");
        TestEditor::ResetStream(editor, "AoldZ", 1, 4, Scintilla::EndOfLine::CrLf);
        peer.OfferText("b\r\n", marker);
        SwapNow(editor);
        Require(Text(editor) == "Ab\r\nZ", "selected Swap consumes tagged rectangle text as normalized stream bytes");
        Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "old",
                "selected stream Swap replaces incoming shape metadata with original Cut metadata");
    }
    SwapSelectedRectangles(editor, peer);
    SwapRectanglePlainText(editor, peer);
    std::puts("PASS: line/rectangle native offers, precedence, selection direction and clipboard metadata");
}

void SwapMultiple(EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    for (const bool additional : {false, true}) {
        TestEditor::ResetStream(editor, "abcd efgh ijkl", 1, 3, Scintilla::EndOfLine::CrLf);
        editor->Send(Message::SetMultipleSelection, 1);
        editor->Send(Message::SetAdditionalSelectionTyping, additional);
        editor->Send(Message::AddSelection, 8, 6);
        editor->Send(Message::SetMainSelection, 1);
        peer.OfferText("QQQ");
        SwapNow(editor);
        Require(Text(editor) == (additional ? "ad eQQQh ijkl" : "abcd eQQQh ijkl"),
                "Swap retains original selected-range filtering and main insertion");
        TestEditor::RequireSelection(editor, 6, 9, "multiple-selection Swap retains recorded original endpoints");
        Require(editor->Send(Message::GetSelections) == 1 && peer.Request("utility-read") == "stream" &&
                    peer.SavedText() == "bc\r\nfg",
                "Cut extracts every original range before selection filtering");
    }
    for (const auto mode : {Scintilla::MultiPaste::Once, Scintilla::MultiPaste::Each}) {
        TestEditor::ResetStream(editor, "abcd efgh", 1, 1);
        editor->Send(Message::SetMultipleSelection, 1);
        editor->Send(Message::SetAdditionalSelectionTyping, 1);
        editor->Send(Message::AddSelection, 6, 6);
        editor->Send(Message::SetMainSelection, 1);
        editor->Send(Message::SetMultiPaste, static_cast<uintptr_t>(mode));
        peer.OfferText("Q");
        SwapNow(editor);
        Require(Text(editor) == (mode == Scintilla::MultiPaste::Once ? "abcd eQfgh" : "aQbcd eQfgh"),
                "caret Swap retains original Once/Each multi-paste behavior");
        TestEditor::RequireSelection(editor, 6, mode == Scintilla::MultiPaste::Once ? 7 : 8,
                                     "multi-caret Swap selects the recorded old/new main caret span");
        Require(peer.Request("utility-formats") == "empty", "multi-caret Swap clears independent native ownership");
    }
    editor->Send(Message::SetMultipleSelection, 0);
    editor->Send(Message::SetAdditionalSelectionTyping, 0);
    editor->Send(Message::SetMultiPaste, static_cast<uintptr_t>(Scintilla::MultiPaste::Once));
    TestEditor::ResetStream(editor, "a\r\nb", 1, 1, Scintilla::EndOfLine::CrLf);
    editor->Send(Message::SetVirtualSpaceOptions, 3);
    editor->Send(Message::SetSelectionNAnchorVirtualSpace, 0, 2);
    editor->Send(Message::SetSelectionNCaretVirtualSpace, 0, 2);
    peer.OfferText("Q");
    SwapNow(editor);
    Require(Text(editor) == "a  Q\r\nb", "caret Swap realizes original virtual space through Paste");
    TestEditor::RequireSelection(editor, 1, 4, "virtual caret Swap selects padding and inserted text together");
    editor->Send(Message::Undo);
    Require(editor->Send(Message::GetSelectionNCaretVirtualSpace, 0) == 2,
            "Undo restores the original virtual caret state");
    TestEditor::ResetStream(editor, "a\r\nb", 0, 0, Scintilla::EndOfLine::CrLf);
    editor->Send(Message::SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(Message::SetRectangularSelectionAnchor, 1);
    editor->Send(Message::SetRectangularSelectionCaret, 4);
    editor->Send(Message::SetRectangularSelectionAnchorVirtualSpace, 1);
    editor->Send(Message::SetRectangularSelectionCaretVirtualSpace, 3);
    peer.OfferText("Q");
    SwapNow(editor);
    Require(Text(editor) == "a\r\nbQ" && peer.Request("utility-read") == "rectangle" && peer.SavedText() == "\r\n",
            "selected virtual rectangle Swap retains original empty-row Cut payload and insertion");
    TestEditor::RequireSelection(editor, 1, 2, "selected virtual rectangle Swap becomes the original stream span");
    std::puts("PASS: multiple selections, Once/Each carets and original virtual space");
}

void SwapSavedState(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const auto directory = TestFiles::TemporaryDirectory("clipboard-swap-中文");
    std::filesystem::create_directory(directory);
    const TestFiles::EncodedFixture fixtures[] = {
        {"utf8", FileEncoding::Utf8, "AoldZ", "AXZ"},
        {"utf8-bom", FileEncoding::Utf8,
         "\xef\xbb\xbf"
         "AoldZ",
         "\xef\xbb\xbf"
         "AXZ"},
        {"gbk", FileEncoding::Gbk,
         "\xc7\xb0"
         "old"
         "\xe1\xe1",
         "\xc7\xb0"
         "X"
         "\xe1\xe1"},
        {"utf16", FileEncoding::Utf16Le,
         std::string("\xff\xfe"
                     "A\0o\0l\0d\0Z\0",
                     12),
         std::string("\xff\xfe"
                     "A\0X\0Z\0",
                     8)},
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / (std::string(fixture.name) + ".txt");
        TestFiles::WriteBytes(path, fixture.before);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), fixture.encoding).status == OpenStatus::Opened,
                "open independently encoded Swap fixture with Unicode path");
        const auto bom = frame->DocumentHasBom();
        const auto eol = editor->Send(Message::GetEOLMode);
        const bool gbk = fixture.encoding == FileEncoding::Gbk;
        editor->Send(Message::SetSel, gbk ? 3 : 1, gbk ? 6 : 4);
        editor->Send(Message::EmptyUndoBuffer);
        peer.OfferText("X");
        SwapNow(editor);
        Require(Text(editor) == (gbk ? "前X後" : "AXZ") && frame->DocumentEncoding() == fixture.encoding &&
                    frame->DocumentHasBom() == bom && editor->Send(Message::GetEOLMode) == eol,
                "Swap retains decoded Unicode, file encoding, BOM and EOL metadata");
        Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "old",
                "independent peer receives cut text from every encoded document");
        TestFiles::RequireSavedUndoRedo(frame, path, fixture,
                                        {"Swap saves independent expected encoded bytes",
                                         "one Undo saves the independent original encoded bytes",
                                         "one Redo saves the independent exchanged encoded bytes"});
        Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "old",
                "independent encoded Save/Undo/Redo leaves clipboard ownership unchanged");
        const auto artifact = std::string("clipboard-swap-") + fixture.name + ".txt";
        std::filesystem::copy_file(path, artifact, std::filesystem::copy_options::overwrite_existing);
    }
    peer.PreserveSavedText("clipboard-swap-peer.txt");
    std::filesystem::remove_all(directory);
    std::puts("PASS: independent UTF-8/BOM/UTF-16LE/GBK disk bytes, metadata, save points and history");
}

void SwapDefaultCaretEncoding(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
#ifdef __WXMSW__
    if (GetACP() != 936) {
        std::puts("SKIP: captured default-ANSI caret Swap requires system ACP 936");
        return;
    }
    const std::string expected = "Aoé中文??\r\nXldZ";
    const std::string saved = "Ao\xa8\xa6\xd6\xd0\xce\xc4??\r\nXldZ";
    constexpr intptr_t end = 15;
#else
    const std::string expected = "Aoé中文😀\r\nXldZ";
    const std::string saved = "Aoé中文😀\r\nXldZ";
    constexpr intptr_t end = 17;
#endif
    const auto directory = TestFiles::TemporaryDirectory("clipboard-swap-default-caret");
    std::filesystem::create_directory(directory);
    const auto path = directory / "default.txt";
    TestFiles::WriteBytes(path, "AoldZ");
    Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
            "configured system-default caret document opens through native detection");
    editor->Send(Message::SetSel, 2, 2);
    peer.OfferText("é中文😀\rX");
    Gesture(editor);
    NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Swap"));
    Require(Text(editor) == expected, "caret Swap applies captured system-default conversion in native Paste");
    TestEditor::RequireSelection(editor, 2, end, "default-ANSI caret Swap selects the converted UTF-8 byte span");
    Require(peer.Request("utility-formats") == "empty", "default-ANSI caret Swap clears all native formats");
    int lossPrompts = 0;
    Require(frame->Save([&lossPrompts] {
                     ++lossPrompts;
                     return GbkLossChoice::Cancel;
                 }).status == SaveStatus::Saved &&
                lossPrompts == 0 && TestFiles::Bytes(path) == saved,
            "default-ANSI caret Swap saves captured native bytes without a second loss decision");
    editor->Send(Message::Undo);
    Require(Text(editor) == "AoldZ" && !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo) &&
                frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == "AoldZ",
            "one Undo restores independent original default-ANSI caret bytes");
    editor->Send(Message::Redo);
    Require(Text(editor) == expected && frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == saved &&
                peer.Request("utility-formats") == "empty",
            "Redo restores converted caret bytes and preserves independent clipboard emptiness");
    std::filesystem::remove_all(directory);
    std::puts("PASS: captured system-default caret Swap conversion, span, disk and history");
}

void SwapExplicitGbk(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const auto directory = TestFiles::TemporaryDirectory("clipboard-swap-explicit-gbk");
    std::filesystem::create_directory(directory);
    const auto path = directory / "gbk.txt";
    const std::string original = "\xc7\xb0old\xe1\xe1";
    for (const bool caret : {false, true}) {
        TestFiles::WriteBytes(path, original);
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path), FileEncoding::Gbk).status == OpenStatus::Opened,
                "explicit GBK document opens into the UTF-8 core");
        editor->Send(Message::SetSel, 3, caret ? 3 : 6);
        peer.OfferText("中文😀\nX");
        Gesture(editor);
        NativeSearch::Command(frame, frame->GetMenuBar()->FindMenuItem("Edit", "Swap"));
        // Independent original actual-GBK captures in 141/original-swap-corrected.json.
        const std::string expected = caret ? "前中文😀\r\nXold後" : "前中文😀\r\nX後";
        Require(Text(editor) == expected && frame->DocumentEncoding() == FileEncoding::Gbk,
                "explicit GBK Swap retains original Unicode until the encoded-save boundary");
        TestEditor::RequireSelection(editor, 3, 16, "explicit GBK Swap retains captured UTF-8 byte endpoints");
        if (caret)
            Require(peer.Request("utility-formats") == "empty", "explicit GBK caret Swap clears native formats");
        else
            Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "old",
                    "explicit GBK selected Swap independently publishes the original selection");
        Require(frame->Save([] { return GbkLossChoice::Cancel; }).status == SaveStatus::Cancelled &&
                    TestFiles::Bytes(path) == original && Text(editor) == expected && editor->Send(Message::GetModify),
                "explicit GBK save cancellation preserves Unicode, modification and original disk bytes");
        const std::string saved =
            caret ? "\xc7\xb0\xd6\xd0\xce\xc4??\r\nXold\xe1\xe1" : "\xc7\xb0\xd6\xd0\xce\xc4??\r\nX\xe1\xe1";
        Require(frame->Save([] { return GbkLossChoice::Replace; }).status == SaveStatus::Saved &&
                    TestFiles::Bytes(path) == saved && Text(editor) == expected,
                "approved GBK save writes native placeholders while keeping Unicode in the editor");
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: explicit GBK selected/caret Swap preserves Unicode and existing loss-save decisions");
}

void SwapDefaultReadOnly(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
#ifdef __WXMSW__
    if (GetACP() != 936) {
        std::puts("SKIP: captured default-ANSI read-only Swap requires system ACP 936");
        return;
    }
#endif
    const std::string ascii = "0123456789012345678901234567890123456789";
    const auto directory = TestFiles::TemporaryDirectory("clipboard-swap-default-readonly");
    std::filesystem::create_directory(directory);
    const auto path = directory / "default.txt";
    const int swap = frame->GetMenuBar()->FindMenuItem("Edit", "Swap");
    const int readOnly = frame->GetMenuBar()->FindMenuItem("File", "Read Only Mode");
    for (const bool dbcs : {false, true}) {
        const std::string original = dbcs ? "Aold中文XYZ" : ascii;
#ifdef __WXMSW__
        const std::string disk = dbcs ? "Aold\xd6\xd0\xce\xc4XYZ" : ascii;
        constexpr intptr_t end = 7;
#else
        const std::string disk = original;
        constexpr intptr_t end = 11;
#endif
        for (const bool reversed : {false, true}) {
            TestFiles::WriteBytes(path, disk);
            Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                    "configured system-default protected document opens through native detection");
            NativeSearch::Command(frame, readOnly);
            Require(editor->Send(Message::GetReadOnly), "native Read Only mode protects the actual document");
            editor->Send(Message::SetSel, reversed ? 4 : 1, reversed ? 1 : 4);
            peer.OfferText("中文😀");
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            Require(frame->GetMenuBar()->IsEnabled(swap), "original selected Swap remains enabled in Read Only mode");
            Gesture(editor);
            NativeSearch::Command(frame, swap);
            // Independently observed original ACP936 endpoints in
            // review-fixes/original-ansi-readonly.json; protected writes insert nothing.
            TestEditor::RequireSelection(editor, reversed ? end : 1, reversed ? 1 : end,
                                         "protected default-ANSI Swap uses original converted native byte endpoints");
            Require(Text(editor) == original && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                        !editor->Send(Message::CanRedo) && TestFiles::Bytes(path) == disk &&
                        peer.Request("utility-read") == "stream" && peer.SavedText() == "中文😀",
                    "protected default-ANSI Swap preserves independent document, disk, clipboard and history");
            NativeSearch::Command(frame, readOnly);
        }
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: captured default-ANSI read-only native byte endpoints and independent unchanged state");
}

void SwapDefaultEncoding(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
#ifdef __WXMSW__
    if (GetACP() != 936) {
        std::puts("SKIP: captured default-ANSI Swap requires system ACP 936");
        return;
    }
    const std::string replacement = "é中文??\r\nX";
    const std::string saved = "A\xa8\xa6\xd6\xd0\xce\xc4??\r\nXZ";
#else
    const std::string replacement = "é中文😀\r\nX";
    const std::string saved = "Aé中文😀\r\nXZ";
#endif
    // Independent original ACP936 capture: 141/original-swap.json and
    // capture-conclusions.md, not the migrated encoder's output.
    const auto directory = TestFiles::TemporaryDirectory("clipboard-swap-default");
    std::filesystem::create_directory(directory);
    const auto path = directory / "default.txt";
    const int swap = frame->GetMenuBar()->FindMenuItem("Edit", "Swap");
    for (const bool reversed : {false, true}) {
        TestFiles::WriteBytes(path, "AoldZ");
        editor->Send(Message::SetSavePoint);
        Require(frame->Open(TestFiles::Filename(path)).status == OpenStatus::Opened,
                "configured system-default document opens through native detection");
        const auto encoding = frame->DocumentEncoding();
        const auto bom = frame->DocumentHasBom();
        editor->Send(Message::SetSel, reversed ? 4 : 1, reversed ? 1 : 4);
        peer.OfferText("é中文😀\rX");
        if (reversed) {
            UtilityShortcut(frame, 'K', true);
        } else {
            Gesture(editor);
            NativeSearch::Command(frame, swap);
        }
        Require(Text(editor) == "A" + replacement + "Z",
                "selected Swap applies captured system-default conversion before replacement");
        const auto end = 1 + static_cast<intptr_t>(replacement.size());
        TestEditor::RequireSelection(editor, reversed ? end : 1, reversed ? 1 : end,
                                     "default-ANSI Swap retains direction with converted UTF-8 endpoints");
        Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "old",
                "default-ANSI Swap independently publishes the original selected text");
        int lossPrompts = 0;
        Require(frame->Save([&lossPrompts] {
                         ++lossPrompts;
                         return GbkLossChoice::Cancel;
                     }).status == SaveStatus::Saved &&
                    lossPrompts == 0 && TestFiles::Bytes(path) == saved && frame->DocumentEncoding() == encoding &&
                    frame->DocumentHasBom() == bom,
                "converted default-ANSI Swap saves captured bytes without a second loss decision");
        editor->Send(Message::Undo);
        Require(Text(editor) == "AoldZ" && !editor->Send(Message::CanUndo) && editor->Send(Message::CanRedo),
                "one Undo restores the default-ANSI source edit");
        Require(frame->Save().status == SaveStatus::Saved && TestFiles::Bytes(path) == "AoldZ",
                "Undo saves independent original default-ANSI bytes");
        editor->Send(Message::Redo);
        Require(Text(editor) == "A" + replacement + "Z" && frame->Save().status == SaveStatus::Saved &&
                    TestFiles::Bytes(path) == saved && peer.Request("utility-read") == "stream" &&
                    peer.SavedText() == "old",
                "Redo restores converted bytes while independent clipboard retains cut text");
    }
    std::filesystem::remove_all(directory);
    std::puts("PASS: captured system-default selected Swap conversion, endpoints, disk and history");
}

void Swap(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const int swap = frame->GetMenuBar()->FindMenuItem("Edit", "Swap");
    Require(swap != wxNOT_FOUND, "native Swap menu exists");
    TestEditor::ResetStream(editor, "AoldZ", 1, 4);
    peer.OfferText("X");
    Gesture(editor);
    NativeSearch::Command(frame, swap);
    Require(Text(editor) == "AXZ", "original selected Swap replaces the cut span with the earlier offer");
    TestEditor::RequireSelection(editor, 1, 2, "selected Swap retains forward replacement endpoints");
    Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "old",
            "independent native receiver gets the original cut selection");
    editor->Send(Message::Undo);
    Require(Text(editor) == "AoldZ" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                editor->Send(Message::CanRedo),
            "one Undo restores the original saved document");
    editor->Send(Message::Redo);
    Require(Text(editor) == "AXZ" && peer.Request("utility-read") == "stream" && peer.SavedText() == "old",
            "Redo restores the document exchange while the native clipboard retains cut text");
    std::puts("PASS: original selected Swap and independent clipboard history");
    TestEditor::ResetStream(editor, "AoldZ", 4, 1);
    peer.OfferText("中文😀");
    UtilityShortcut(frame, 'K', true);
    Require(Text(editor) == "A中文😀Z", "native Ctrl+K exchanges a reversed Unicode selection");
    TestEditor::RequireSelection(editor, 11, 1, "native Ctrl+K retains reversed UTF-8 byte endpoints");
    Require(peer.Request("utility-read") == "stream" && peer.SavedText() == "old",
            "independent receiver observes the cut selection from native Ctrl+K");
    std::puts("PASS: real native Ctrl+K and reversed Unicode endpoints");
    TestEditor::ResetStream(editor, "AoldZ", 2, 2);
    peer.OfferText("X");
    Gesture(editor);
    editor->SwapClipboard();
    Require(Text(editor) == "AoXldZ", "empty-selection Swap uses the original native paste path");
    TestEditor::RequireSelection(editor, 2, 3, "empty-selection Swap selects the original-to-resulting caret span");
    Require(peer.Request("utility-formats") == "empty", "empty-selection Swap clears every native clipboard format");
    editor->Send(Message::Undo);
    Require(Text(editor) == "AoldZ" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo) &&
                editor->Send(Message::CanRedo) && peer.Request("utility-formats") == "empty",
            "empty-selection Undo restores the saved document while its clipboard remains cleared");
    editor->Send(Message::Redo);
    Require(Text(editor) == "AoXldZ" && peer.Request("utility-formats") == "empty",
            "empty-selection Redo retains independently observed clipboard emptiness");
    std::puts("PASS: empty-selection Paste, selected span and independent Clear Clipboard history");
    SwapBoundaries(editor, peer);
    SwapAvailability(frame, editor, peer);
    SwapShapes(editor, peer);
    SwapMultiple(editor, peer);
    SwapSavedState(frame, editor, peer);
}

void ClipboardRoundtrip(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
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
        std::fprintf(stderr, "Copy diagnostic: seed remains=%d, returned characters=%zu, selection=%ld..%ld\n",
                     copiedText == "owned seed", copiedText.length(),
                     static_cast<long>(editor->Send(Message::GetSelectionStart)),
                     static_cast<long>(editor->Send(Message::GetSelectionEnd)));
    Require(copiedText == wxString::FromUTF8("中文\nline B"), "Copy publishes selected Unicode multiline text");
    std::puts("PASS: Copy selected Unicode multiline text to system clipboard");
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    {
        wxClipboardLocker lock;
        Require(!(!lock) &&
                    wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8("paste中文\nsecond line\n第三行"))),
                "publish paste fixture");
    }
    SettleClipboard();
    editor->Send(Message::Paste);
    Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行",
            "Paste replaces selection with owned UTF-8 and core EOL conversion");
    Require(editor->Send(Message::GetModify), "Paste marks document modified");
    editor->Send(Message::Undo);
    Require(Text(editor) == "A中文\r\nline B" && !editor->Send(Message::GetModify) && !editor->Send(Message::CanUndo),
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
    Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行" && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo),
            "failed system copy must not delete Cut selection or change history");
    Require(peer.Request("release") == "released", "external peer releases clipboard");
#endif
    Gesture(editor);
    editor->Send(Message::Cut);
    Require(Text(editor) == "A" && editor->Send(Message::GetModify), "Cut deletes selection after copy succeeds");
    SettleClipboard();
    Require(peer.Request("paste") == wxString::FromUTF8("paste中文\nsecond line\n第三行"),
            "independent stock text app receives Cut text");
    editor->Send(Message::Undo);
    Require(Text(editor) == "Apaste中文\r\nsecond line\r\n第三行" && !editor->Send(Message::GetModify) &&
                !editor->Send(Message::CanUndo),
            "one Undo restores Cut at save point");
    editor->Send(Message::Redo);
    Require(Text(editor) == "A" && editor->Send(Message::GetModify), "Redo restores Cut and modified state");
    std::puts("PASS: Cut success/failure, external receiver, one-step Undo/Redo and save point");
    Require(peer.Request("nontext") == "ready", "independent app publishes non-text fixture");
    SettleClipboard();
    editor->Send(Message::SetSel, 0, 0);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(wxID_COPY), "nonempty document enables Copy");
    Require(frame->GetMenuBar()->IsEnabled(wxID_CUT), "nonempty writable document enables Cut");
    NonTextOffer(editor);
    Require(!frame->GetMenuBar()->IsEnabled(wxID_PASTE), "non-text clipboard disables Paste menu");
    Require(peer.Request("copy") == "ready", "independent stock text app copies Unicode multiline fixture");
    TextOffer(editor);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(wxID_PASTE), "text from external app enables Paste");
    editor->Send(Message::SetSel, 0, 1);
    editor->Send(Message::SetReadOnly, 1);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->IsEnabled(wxID_COPY), "original read-only nonempty document permits Copy");
    Require(frame->GetMenuBar()->IsEnabled(wxID_CUT), "original read-only nonempty document keeps Cut available");
    Require(!frame->GetMenuBar()->IsEnabled(wxID_PASTE), "original read-only document disables the Paste menu");
    editor->Send(Message::Cut);
    editor->Send(Message::Paste);
    Require(Text(editor) == "A", "read-only commands preserve document");
    TestEditor::RequireSelection(editor, 0, 0, "original direct protected Paste collapses the selection");
    editor->Send(Message::SetReadOnly, 0);
    editor->Send(Message::SetSel, 0, 1);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    Menu(frame, wxID_PASTE);
    Require(Text(editor) == "peer English 中文 😀\r\n第二行\r\nlast", "Paste menu receives external stock-widget text");
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
}
void OfferArrival(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    Require(peer.Request("nontext") == "ready", "external app publishes non-text fixture");
    NonTextOffer(editor);
    Require(peer.Request("copy") == "ready", "external stock app publishes text fixture");
    TextOffer(editor);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(editor->Send(Message::CanPaste) && frame->GetMenuBar()->IsEnabled(wxID_PASTE),
            "new native text offer enables Paste");
    std::puts("PASS: focused native offer arrival and Paste availability");
}
void RunClipboardScenario(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    wxString shapes;
    if (wxGetEnv("WXNOTEPAD_CLIPBOARD_SHAPES", &shapes)) {
        if (shapes == "swap-ansi-readonly")
            SwapDefaultReadOnly(frame, editor, peer);
        else if (shapes == "swap-ansi-caret")
            SwapDefaultCaretEncoding(frame, editor, peer);
        else if (shapes == "swap-encoding") {
            SwapDefaultEncoding(frame, editor, peer);
            SwapDefaultCaretEncoding(frame, editor, peer);
            SwapExplicitGbk(frame, editor, peer);
            SwapDefaultReadOnly(frame, editor, peer);
        } else if (shapes == "swap")
            Swap(frame, editor, peer);
        else if (shapes == "utilities")
            Utilities(frame, editor, peer);
        else if (shapes == "path-text")
            PathTextCopies(frame, editor, peer);
        else if (shapes == "read-only")
            ReadOnlyCommands(frame, editor, peer);
        else if (shapes == "commands")
            LineCommands(frame, editor, peer);
        else if (shapes == "lines")
            CurrentLines(frame, editor, peer);
        else
            Shapes(frame, editor, peer, shapes == "readiness");
    } else {
        wxString offerOnly;
        if (wxGetEnv("WXNOTEPAD_CLIPBOARD_OFFER_ONLY", &offerOnly)) {
            OfferArrival(frame, editor, peer);
        } else {
            ClipboardRoundtrip(frame, editor, peer);
        }
    }
}

class ClipboardApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        wxLog::SetActiveTarget(new wxLogStderr);
        wxString mode;
        const bool defaultSwap = wxGetEnv("WXNOTEPAD_CLIPBOARD_SHAPES", &mode) &&
                                 (mode == "swap-encoding" || mode == "swap-ansi-caret" || mode == "swap-ansi-readonly");
        encodingSettings_.values.encoding.defaultEncoding = 0;
        encodingSettings_.values.encoding.loadASCIIasUTF8 = false;
        encodingSettings_.values.encoding.loadANSIasUTF8 = false;
        auto* frame = new EditorFrame(defaultSwap ? &encodingSettings_ : nullptr);
        wxString focusProbe;
        if (wxGetEnv("WXNOTEPAD_CLIPBOARD_FOCUS_PROBE", &focusProbe)) {
            InitFocusProbe(frame);
            return true;
        }
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool presented) {
            try {
                Require(presented, "private compositor must present the owned editor before native scenarios");
                using Scintilla::Message;
                auto* editor = frame->Editor();
                Peer peer(frame, editor);
                Require(peer.Request("ready") == "ready", "external clipboard application is running");
                RunClipboardScenario(frame, editor, peer);
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
    int OnExit() override { return result_; }

private:
    void InitFocusProbe(EditorFrame* frame) {
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
    }

    int result_ = 1;
    wxTimer probeTimer_;
    SettingsStore encodingSettings_;
};
} // namespace
wxIMPLEMENT_APP(ClipboardApp);
