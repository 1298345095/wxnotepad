#include "editor/editor_control.h"
#include <wx/wx.h>
#include <wx/dnd.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/process.h>
#include <wx/stopwatch.h>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <functional>
#ifdef __WXMSW__
#include <windows.h>
#elif defined(__WXGTK__)
#include <gtk/gtk.h>
#endif

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
std::string Text(EditorControl* editor) {
    using Scintilla::Message;
    std::string value(static_cast<size_t>(editor->Send(Message::GetLength)) + 1, '\0');
    editor->Send(Message::GetText, value.size(), reinterpret_cast<intptr_t>(value.data()));
    value.pop_back();
    return value;
}
void Wait(int milliseconds) {
    wxStopWatch watch;
    while (watch.Time() < milliseconds) {
        wxYield();
        wxMilliSleep(10);
    }
}
wxPoint At(EditorControl* editor, intptr_t position) {
    using Scintilla::Message;
    return editor->GetPosition() +
           wxPoint(static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position)) + 1,
                   static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position)) + 5);
}
// GTK uses the toolkit's actual native text view as the independent peer.
// Windows uses wx's OLE text data object, without the editor implementation.
#ifdef __WXGTK__
using PeerWindow = wxTextCtrl;
#else
using PeerWindow = wxPanel;
#endif
class NativePeer final : public PeerWindow {
public:
#ifdef __WXGTK__
    explicit NativePeer(wxWindow* parent)
        : PeerWindow(parent, wxID_ANY, {}, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE) {
        Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
            value = GetValue();
            if (changed) changed();
        });
        // The peer's native text view permits both operations and defaults to
        // move. Keep its native hit testing, insertion and source deletion.
        g_signal_connect(
            GetConnectWidget(), "drag-motion",
            G_CALLBACK(
                +[](GtkWidget* widget, GdkDragContext* context, gint x, gint y, guint time, gpointer) -> gboolean {
                    const auto accepted = GTK_WIDGET_GET_CLASS(widget)->drag_motion(widget, context, x, y, time);
                    if (accepted)
                        gdk_drag_status(context, wxGetKeyState(WXK_CONTROL) ? GDK_ACTION_COPY : GDK_ACTION_MOVE, time);
                    return accepted;
                }),
            nullptr);
#else
    explicit NativePeer(wxWindow* parent) : PeerWindow(parent, wxID_ANY) {
        SetBackgroundColour(*wxLIGHT_GREY);
        SetDropTarget(new Target(*this));
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
            start_ = event.GetPosition();
            armed_ = true;
        });
        Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
            if (!armed_ || !event.LeftIsDown() ||
                (std::abs(event.GetX() - start_.x) <= 4 && std::abs(event.GetY() - start_.y) <= 4))
                return;
            armed_ = false;
            wxTextDataObject data(value);
            wxDropSource source(this);
            source.SetData(data);
            result = source.DoDragDrop(wxDrag_DefaultMove);
            if (result == wxDragMove) value.clear();
            if (changed) changed();
        });
#endif
    }
    wxString value;
    wxDragResult result = wxDragNone;
    std::function<void()> changed;
    void Assign(const wxString& text) {
        value = text;
#ifdef __WXGTK__
        ChangeValue(text);
        SetSelection(-1, -1);
#endif
    }

private:
    wxPoint start_;
    bool armed_ = false;
    class Target final : public wxTextDropTarget {
    public:
        explicit Target(NativePeer& peer) : peer_(peer) { SetDefaultAction(wxDragMove); }
        wxDragResult OnDragOver(wxCoord, wxCoord, wxDragResult action) override {
            return wxGetKeyState(WXK_CONTROL) ? wxDragCopy : action;
        }
        bool OnDropText(wxCoord, wxCoord, const wxString& text) override {
            peer_.value = text;
            if (peer_.changed) peer_.changed();
            return true;
        }

    private:
        NativePeer& peer_;
    };
};
// The Linux driver injects only into the private Xvfb/Weston output. Windows
// runs this native-input fixture on CI's desktop, never the shared local desktop.
void Drag(wxFrame* frame, wxPoint from, wxPoint to, const wxString& modifier = {}, bool cancel = false, int pause = 200,
          const wxString& switchTo = {}) {
    wxString control;
    if (wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &control)) {
        wxFFile request(control, "wb");
        Require(request.Write(wxString::Format("drag:%d,%d,%d,%d,%s,%d,%d,%s", from.x, from.y, to.x, to.y, modifier,
                                               cancel, pause, switchTo)),
                "publish private drag input");
        request.Close();
        wxStopWatch watch;
        while (watch.Time() < 8000) {
            wxYield();
            wxFFile response(control, "rb");
            wxString pending;
            response.ReadAll(&pending);
            if (pending.empty()) break;
            wxMilliSleep(10);
        }
        Wait(300);
    }
#ifdef __WXMSW__
    else {
        from = frame->ClientToScreen(from);
        to = frame->ClientToScreen(to);
        auto native = [&](wxPoint point) { SetCursorPos(point.x, point.y); };
        auto key = [](WORD code, bool up) {
            INPUT input{};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = code;
            input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
            Require(SendInput(1, &input, sizeof(input)) == 1, "send owned native drag modifier");
        };
        auto mouse = [](DWORD flags) {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = flags;
            Require(SendInput(1, &input, sizeof(input)) == 1, "send owned native drag button");
        };
        const WORD code = modifier == "ctrl" ? VK_CONTROL : modifier == "shift" ? VK_SHIFT : 0;
        native(from);
        mouse(MOUSEEVENTF_LEFTDOWN);
        // Native input continues while the UI thread runs the OLE modal loop.
        std::jthread driver([&] {
            Sleep(150);
            native(from + wxPoint(10, 0));
            Sleep(200);
            if (code) key(code, false);
            if (!switchTo.empty()) {
                const auto target = FindWindowW(nullptr, ("wxnotepad stream drag " + switchTo).wc_str());
                SetForegroundWindow(target);
                Sleep(300);
            }
            native(to);
            Sleep(pause);
            if (cancel) {
                key(VK_ESCAPE, false);
                key(VK_ESCAPE, true);
            }
            mouse(MOUSEEVENTF_LEFTUP);
            if (code) key(code, true);
        });
        Wait(pause + 1000);
    }
#else
    else {
        throw std::runtime_error("native drag fixture requires the private display driver");
    }
#endif
}
void Front(wxFrame* frame, const wxString& name, const wxString& directory = {}) {
    wxString control;
    wxString backend;
    const bool wayland = wxGetEnv("GDK_BACKEND", &backend) && backend == "wayland";
    wxYield();
    const auto active = [&] {
        if (name == "main") return frame->IsActive();
        wxFFile file(directory + "/active", "rb");
        wxString value;
        return file.IsOpened() && file.ReadAll(&value) && value == "1";
    };
    if (wayland && active()) return;
    if (wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &control)) {
        wxFFile request(control, "wb");
        request.Write("front:wxnotepad stream drag " + name);
        request.Close();
        Wait(700);
        if (wayland) {
            wxStopWatch wait;
            while (!active() && wait.Time() < 3000)
                Wait(20);
            Require(active(), "private compositor must activate the requested owned drag window");
        }
    }
#ifdef __WXMSW__
    else {
        SetForegroundWindow(FindWindowW(nullptr, ("wxnotepad stream drag " + name).wc_str()));
        Wait(300);
    }
#endif
}
class PeerProcess {
public:
    PeerProcess() {
        directory = wxFileName::CreateTempFileName("wxnotepad-stream-peer-");
        wxRemoveFile(directory);
        Require(wxMkdir(directory), "create owned native peer IPC directory");
        const auto executable = wxStandardPaths::Get().GetExecutablePath();
        process = wxExecute("\"" + executable + "\" --peer \"" + directory + "\"", wxEXEC_ASYNC);
        Require(process > 0, "start independent native drag peer process");
        wxStopWatch wait;
        while (!wxFileExists(directory + "/ready") && wait.Time() < 5000)
            Wait(20);
        Require(wxFileExists(directory + "/ready"), "independent native peer must become ready");
    }
    ~PeerProcess() {
        wxFFile quit(directory + "/quit", "wb");
        quit.Close();
        wxStopWatch wait;
        while (!wxFileExists(directory + "/done") && wait.Time() < 3000)
            Wait(20);
        if (!wxFileExists(directory + "/done")) wxKill(process, wxSIGTERM);
        wxFileName::Rmdir(directory, wxPATH_RMDIR_RECURSIVE);
    }
    wxString Value() const {
        wxFFile file(directory + "/value", "rb");
        wxString value;
        Require(file.IsOpened() && file.ReadAll(&value, wxConvUTF8), "read native peer's transferred bytes");
        return value;
    }
    void Seed() {
        wxFFile file(directory + "/seed", "wb");
        file.Close();
        wxStopWatch wait;
        while (wxFileExists(directory + "/seed") && wait.Time() < 3000)
            Wait(20);
        Require(!wxFileExists(directory + "/seed"), "peer must accept next Unicode source value");
    }
    wxString directory;
    long process = 0;
};
void Place(wxWindow* window, const wxString& directory, const wxString& name, wxPoint origin) {
    const wxString pointer = directory + "/" + name + "-pointer";
    wxEvtHandler monitor;
    monitor.Bind(wxEVT_MOTION, [pointer](wxMouseEvent& event) {
        wxFFile file(pointer, "wb");
        file.Write(wxString::Format("%d,%d", event.GetX(), event.GetY()));
        event.Skip();
    });
    window->PushEventHandler(&monitor);
    wxString control;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &control), "Wayland placement requires the private driver");
    {
        wxFFile request(control, "wb");
        request.Write(wxString::Format("place:%s,%d,%d", pointer, origin.x, origin.y));
    }
    Wait(1500);
    window->PopEventHandler(false);
}
class DragApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        if (argc == 3 && argv[1] == "--peer") {
            const wxString directory = argv[2];
            auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad stream drag peer");
            auto* peer = new NativePeer(frame);
            frame->Bind(wxEVT_ACTIVATE, [directory](wxActivateEvent& event) {
                wxFFile active(directory + "/active", "wb");
                active.Write(event.GetActive() ? "1" : "0");
                event.Skip();
            });
            peer->changed = [peer, directory] {
                wxFFile value(directory + "/value", "wb");
                value.Write(peer->value, wxConvUTF8);
            };
            wxString backend;
            if (wxGetEnv("GDK_BACKEND", &backend) && backend == "wayland") {
                frame->SetClientSize(600, 700);
                frame->Show();
                peer->Bind(wxEVT_MOTION, [directory](wxMouseEvent& event) {
                    wxFFile file(directory + "/peer-pointer", "wb");
                    file.Write(wxString::Format("%d,%d", event.GetX(), event.GetY()));
                    event.Skip();
                });
            } else
                frame->ShowFullScreen(true);
            timer_.SetOwner(this);
            Bind(wxEVT_TIMER, [this, frame, peer, directory](wxTimerEvent&) {
                if (wxFileExists(directory + "/quit")) {
                    timer_.Stop();
                    wxFFile done(directory + "/done", "wb");
                    frame->Destroy();
                    ExitMainLoop();
                } else if (wxFileExists(directory + "/seed")) {
                    peer->Assign(wxString::FromUTF8("中😀\nline"));
                    peer->changed();
                    wxRemoveFile(directory + "/seed");
                }
            });
            timer_.Start(30);
            CallAfter([frame, peer, directory] {
                frame->Raise();
                peer->SetFocus();
                peer->changed();
                wxFFile ready(directory + "/ready", "wb");
            });
            return true;
        }
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad stream drag main");
        auto* editor = new EditorControl(frame);
        auto* peer = new NativePeer(frame);
        editor->SetLineNumbersVisible(false);
        frame->ShowFullScreen(true);
        editor->SetSize(0, 0, 760, 700);
        peer->SetSize(800, 0, 600, 700);
        CallAfter([this, frame, editor, peer] {
            try {
                using Scintilla::Message;
                Wait(500);
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one two three"));
                editor->Send(Message::SetSel, 0, 3);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                Drag(frame, At(editor, 1), At(editor, 13));
                Require(Text(editor) == " two threeone", "native self drag must move stream text");
                editor->Send(Message::Undo);
                Require(Text(editor) == "one two three" && !editor->Send(Message::CanUndo) &&
                            !editor->Send(Message::GetModify),
                        "native move must be one undo action returning to save point");
                std::puts("PASS: native stream move and one-step undo");
                editor->Send(Message::SetSel, 0, 3);
                Drag(frame, At(editor, 1), At(editor, 13), "ctrl");
                std::fprintf(stderr, "Ctrl copy state: text='%s'\n", Text(editor).c_str());
                Require(Text(editor) == "one two threeone", "Ctrl native drag must copy stream text");
                editor->Send(Message::Undo);
                Require(Text(editor) == "one two three", "copy must undo in one action");
                editor->Send(Message::SetSel, 0, 3);
                Drag(frame, At(editor, 1), At(editor, 2));
                Require(Text(editor) == "one two three" && !editor->Send(Message::CanUndo),
                        "moving inside the source selection must be a no-op");
                editor->Send(Message::SetSel, 0, 3);
                Drag(frame, At(editor, 1), At(editor, 13), {}, true);
                std::fprintf(stderr, "Cancellation state: text='%s' selection=%lld..%lld modified=%lld capture=%d\n",
                             Text(editor).c_str(), static_cast<long long>(editor->Send(Message::GetSelectionStart)),
                             static_cast<long long>(editor->Send(Message::GetSelectionEnd)),
                             static_cast<long long>(editor->Send(Message::GetModify)), editor->HasCapture());
                Require(Text(editor) == "one two three" && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::GetSelectionStart) == 0 &&
                            editor->Send(Message::GetSelectionEnd) == 3 && !editor->HasCapture(),
                        "Escape must preserve source text, selection, save point and release capture");
                std::puts("PASS: Ctrl copy, self-drop and cancelled drag");

                peer->Assign(wxString::FromUTF8("中😀\nline"));
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("left right"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetReadOnly, 1);
                Drag(frame, wxPoint(810, 10), At(editor, 5));
                Require(Text(editor) == "left right" && peer->value == wxString::FromUTF8("中😀\nline") &&
                            !editor->Send(Message::GetModify),
                        "read-only drop must reject transfer and preserve native peer source");
                editor->Send(Message::SetReadOnly, 0);
                Drag(frame, wxPoint(810, 10), At(editor, 5));
                std::fprintf(stderr, "Incoming state: text='%s' peer='%s' result=%d\n", Text(editor).c_str(),
                             peer->value.utf8_str().data(), static_cast<int>(peer->result));
                Require(Text(editor) == "left 中😀\r\nlineright" && peer->value.empty(),
                        "native peer move must transfer Unicode and normalize multiline text");
                editor->Send(Message::Undo);
                Require(Text(editor) == "left right" && !editor->Send(Message::CanUndo) &&
                            !editor->Send(Message::GetModify),
                        "native peer insertion must undo once to the save point");
                std::puts("PASS: read-only rejection and incoming Unicode/multiline native peer move");

                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中😀\r\nline tail"));
                editor->Send(Message::SetSel, 0, 13);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                Drag(frame, At(editor, 3), wxPoint(850, 40));
                auto received = peer->value;
                received.Replace("\r\n", "\n");
                std::fprintf(stderr, "Outgoing state: text='%s' peer='%s'\n", Text(editor).c_str(),
                             received.utf8_str().data());
                Require(Text(editor) == " tail" && received == wxString::FromUTF8("中😀\nline"),
                        "completed native peer move must remove only the selected Unicode stream");
                editor->Send(Message::Undo);
                Require(Text(editor) == "中😀\r\nline tail" && !editor->Send(Message::GetModify),
                        "external move deletion must undo once to the save point");
                editor->Send(Message::SetSel, 0, 13);
                peer->Assign({});
                Drag(frame, At(editor, 3), wxPoint(850, 40), "ctrl");
                Require(Text(editor) == "中😀\r\nline tail" && !peer->value.empty() && !editor->Send(Message::CanUndo),
                        "completed native peer copy must preserve the source and its undo history");
                std::puts("PASS: outgoing native peer move/copy and source undo");

                std::string lines;
                for (int index = 0; index < 120; ++index)
                    lines += "line\r\n";
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(lines.c_str()));
                editor->Send(Message::SetSel, 0, 4);
                Drag(frame, At(editor, 1), wxPoint(100, 690), {}, true, 1200);
                Require(editor->Send(Message::GetFirstVisibleLine) > 0 && Text(editor) == lines,
                        "native insertion feedback must scroll the viewport while cancellation preserves text");
                std::puts("PASS: insertion feedback scrolling and cancellation");

                wxString backend;
                const bool wayland = wxGetEnv("GDK_BACKEND", &backend) && backend == "wayland";
                if (wayland) {
                    frame->ShowFullScreen(false);
                    frame->SetClientSize(760, 700);
                    editor->SetSize(0, 0, 760, 700);
                    Wait(300);
                }
                PeerProcess independent;
                if (wayland) {
                    Front(frame, "peer", independent.directory);
                    wxString control;
                    wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &control);
                    wxFFile request(control, "wb");
                    request.Write("place:" + independent.directory + "/peer-pointer,850,60");
                    request.Close();
                    Wait(1500);
                }
                Front(frame, "main");
                if (wayland) Place(editor, independent.directory, "main", wxPoint(50, 60));
                const wxPoint editorOrigin = wayland ? wxPoint(50, 60) : wxPoint();
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中😀\r\nline tail"));
                editor->Send(Message::GotoPos, 0);
                editor->Send(Message::SetSel, 0, 13);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                Drag(frame, At(editor, 3) + editorOrigin, wxPoint(900, 100), {}, false, 300, wayland ? "" : "peer");
                auto transferred = independent.Value();
                transferred.Replace("\r\n", "\n");
                Require(Text(editor) == " tail" && transferred == wxString::FromUTF8("中😀\nline"),
                        "separate native application must receive Unicode stream move before source deletion");
                if (!wayland) Front(frame, "main");
                editor->Send(Message::Undo);
                Require(Text(editor) == "中😀\r\nline tail" && !editor->Send(Message::GetModify),
                        "inter-application move must undo source deletion once");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("left right"));
                editor->Send(Message::GotoPos, 0);
                independent.Seed();
                Front(frame, "peer", independent.directory);
                Drag(frame, wayland ? wxPoint(860, 70) : wxPoint(10, 10), At(editor, 5) + editorOrigin, {}, false, 300,
                     wayland ? "" : "main");
                Require(Text(editor) == "left 中😀\r\nlineright" && independent.Value().empty(),
                        "separate native application's completed move must insert Unicode and remove its source");
                std::puts("PASS: inter-application Unicode/multiline native moves in both directions");
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
    wxTimer timer_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(DragApp);
