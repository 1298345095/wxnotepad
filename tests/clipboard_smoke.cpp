#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "editor_test_helpers.h"
#include "file_helpers.h"
#include "native_search_helpers.h"
#include <wx/wx.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/stopwatch.h>
#include <cstdio>
#include <chrono>
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
        while ((!frame_->IsActive() || !editor_->HasFocus()) && std::chrono::steady_clock::now() < activationDeadline) {
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
    if (shift) {
        const bool handled = NativeSearch::WindowsShortcut(
            editor, key, {{VK_CONTROL, 0x80}, {VK_SHIFT, 0x80}, {VK_MENU, static_cast<BYTE>(alt ? 0x80 : 0)}});
        Require(handled || alt, "native Windows accelerator translates the original line shortcut");
    } else {
        BYTE previous[256];
        Require(GetKeyboardState(previous), "read native test thread modifiers");
        BYTE modified[256];
        std::copy(std::begin(previous), std::end(previous), std::begin(modified));
        modified[VK_CONTROL] = 0x80;
        modified[VK_SHIFT] = 0;
        modified[VK_MENU] = 0;
        Require(SetKeyboardState(modified), "set native test thread Ctrl modifier");
        SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYDOWN, key, 0);
        SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_KEYUP, key, 0);
        SetKeyboardState(previous);
    }
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
void CurrentLines(EditorFrame* frame, EditorControl* editor, Peer& peer) {
    using Scintilla::Message;
    const auto load = [editor](const char* text, intptr_t position,
                               Scintilla::EndOfLine eol = Scintilla::EndOfLine::Lf) {
        editor->Send(Message::SetReadOnly, 0);
        editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(text));
        editor->Send(Message::SetEOLMode, static_cast<uintptr_t>(eol));
        editor->Send(Message::GotoPos, position);
        editor->Send(Message::EmptyUndoBuffer);
        editor->Send(Message::SetSavePoint);
    };
    auto* other = new EditorControl(frame);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->IsEnabled(wxID_COPY) && !frame->GetMenuBar()->IsEnabled(wxID_CUT),
            "empty document disables Copy/Cut menus in the original default mode");
    for (const auto mode : {LineSelectionMode::None, LineSelectionMode::VisualStudio, LineSelectionMode::Normal,
                            LineSelectionMode::OldVisualStudio}) {
        editor->SetLineSelectionMode(mode);
        load("first\nmiddle\nlast", 8);
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
            Require(Text(other) == (line ? "middle\nab\ncd" : "amiddle\nb\ncd") &&
                        other->Send(Message::GetCurrentPos) == 8,
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
        // Explicit line commands remain available in None and use the same tag bit.
        load("first\nmiddle\nlast", 8);
        Key(editor, 'C', true);
        SettleClipboard();
        Require(peer.Request("shape-read") == ((static_cast<int>(mode) & 1) ? "line" : "stream") &&
                    peer.SavedText() == "middle\n",
                "Ctrl+Shift+C follows the actual line-selection option");
        Key(editor, 'X', true);
        Require(Text(editor) == "first\nlast" && editor->Send(Message::GetCurrentPos) == 6,
                "Ctrl+Shift+X retains the original explicit line command position");
    }
    delete other;
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
        load(item.before, item.caret, item.eol);
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
    load("first\n\t中文long\nab\tXYZ", 13);
    Key(editor, 'X');
    Require(Text(editor) == "first\nab\tXYZ" && editor->Send(Message::GetCurrentPos) == 11,
            "Cut restores the original expanded column across tabs and Unicode");
    load("first\nlast\n", 11);
    Key(editor, 'X');
    Require(Text(editor) == "first\nlast\n" && editor->Send(Message::GetCurrentPos) == 11 &&
                !editor->Send(Message::CanUndo),
            "Cut on the empty final line preserves bytes and history");
    load("first\nmiddle\nlast", 8);
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
    load("first\n中文\tline\nlast", 8);
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
                    if (shapes == "commands")
                        LineCommands(frame, editor, peer);
                    else if (shapes == "lines")
                        CurrentLines(frame, editor, peer);
                    else
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
                        Require(frame->GetMenuBar()->IsEnabled(wxID_COPY), "nonempty document enables Copy");
                        Require(frame->GetMenuBar()->IsEnabled(wxID_CUT), "nonempty writable document enables Cut");
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
