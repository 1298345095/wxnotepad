#include "editor/editor_control.h"
#include <wx/wx.h>
#include <wx/dnd.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/process.h>
#include <wx/stopwatch.h>
#include <cstdio>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <functional>
#include <string_view>
#ifdef __WXMSW__
#include <windows.h>
#elif defined(__WXGTK__)
#include <gtk/gtk.h>
#endif

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void PrintBytes(const char* name, std::string_view value) {
    std::fprintf(stderr, "%s (%zu bytes):", name, value.size());
    for (const unsigned char byte : value)
        std::fprintf(stderr, " %02x", static_cast<unsigned int>(byte));
    std::fputc('\n', stderr);
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
void WaitForDriver(const wxString& control) {
    wxStopWatch watch;
    while (watch.Time() < 8000) {
        wxYield();
        wxFFile response(control, "rb");
        wxString pending;
        Require(response.IsOpened() && response.ReadAll(&pending), "read owned native input request state");
        if (pending.empty()) return;
        wxMilliSleep(10);
    }
    throw std::runtime_error("private native input driver must complete the owned gesture");
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
        g_signal_connect(GetConnectWidget(), "drag-begin",
                         G_CALLBACK(+[](GtkWidget*, GdkDragContext* context, gpointer value) {
                             auto& peer = *static_cast<NativePeer*>(value);
                             ++peer.sourceAttempts_;
                             peer.sourceStage_ = "begin";
                             peer.dragging_ = true;
                             peer.failed_ = false;
                             peer.deleted_ = false;
                             peer.operation_ = static_cast<GdkDragAction>(0);
                             peer.GetSelection(&peer.sourceStart_, &peer.sourceEnd_);
                             g_signal_connect(context, "drop-performed",
                                              G_CALLBACK(+[](GdkDragContext* context, guint, gpointer value) {
                                                  auto& peer = *static_cast<NativePeer*>(value);
                                                  peer.operation_ = gdk_drag_context_get_selected_action(context);
                                                  peer.sourceStage_ = "drop-performed";
                                              }),
                                              value);
                         }),
                         this);
        g_signal_connect(GetConnectWidget(), "drag-failed",
                         G_CALLBACK(+[](GtkWidget*, GdkDragContext*, GtkDragResult, gpointer value) -> gboolean {
                             auto& peer = *static_cast<NativePeer*>(value);
                             peer.failed_ = true;
                             peer.sourceStage_ = "failed";
                             return FALSE;
                         }),
                         this);
        g_signal_connect(GetConnectWidget(), "drag-data-delete",
                         G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer value) {
                             auto& peer = *static_cast<NativePeer*>(value);
                             peer.deleted_ = true;
                             peer.sourceStage_ = "delete";
                         }),
                         this);
        g_signal_connect(GetConnectWidget(), "drag-end", G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer value) {
                             auto& peer = *static_cast<NativePeer*>(value);
                             // GTK's Wayland source can omit drag-data-delete across native
                             // processes. This peer host owns successful move deletion just
                             // as its Windows OLE host does, after native acknowledgement.
                             if (!peer.failed_ && !peer.deleted_ && peer.operation_ == GDK_ACTION_MOVE)
                                 peer.Remove(peer.sourceStart_, peer.sourceEnd_);
                             peer.dragging_ = false;
                             peer.sourceStage_ = "end";
                         }),
                         this);
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
            dragging_ = true;
            ++sourceAttempts_;
            sourceStage_ = "begin";
            wxTextDataObject data(value);
            wxDropSource source(this);
            source.SetData(data);
            result = source.DoDragDrop(wxDrag_DefaultMove);
            if (result == wxDragMove) value.clear();
            if (changed) changed();
            dragging_ = false;
            sourceStage_ = "end";
        });
#endif
    }
    wxString value;
    wxDragResult result = wxDragNone;
    std::function<void()> changed;
    bool IsDragging() const { return dragging_; }
    void PrintSourceState(FILE* output = stderr) const {
        std::fprintf(output, "Peer source stage=%s attempts=%u dragging=%d", sourceStage_, sourceAttempts_, dragging_);
#ifdef __WXGTK__
        std::fprintf(output, " failed=%d deleted=%d native-action-mask=%u selection=%ld,%ld", failed_, deleted_,
                     static_cast<unsigned int>(operation_), sourceStart_, sourceEnd_);
#else
        std::fprintf(output, " native-result=%d", static_cast<int>(result));
#endif
        const auto bounds = GetScreenRect();
        std::fprintf(output, " screen-bounds=%d,%d,%d,%d\n", bounds.x, bounds.y, bounds.width, bounds.height);
    }
    void WaitForSource() const {
        // Failed native GTK drags can animate after pointer release. Observe
        // completion before starting another transfer from the same control.
        wxStopWatch wait;
        while (dragging_ && wait.Time() < 3000)
            Wait(20);
        Require(!dragging_, "native peer source must finish the previous transfer");
    }
    wxString CurrentValue() const {
#ifdef __WXGTK__
        return GetValue();
#else
        return value;
#endif
    }
    void Assign(const wxString& text) {
        value = text;
#ifdef __WXGTK__
        ChangeValue(text);
        SetSelection(-1, -1);
#endif
    }

private:
    bool dragging_ = false;
    unsigned int sourceAttempts_ = 0;
    const char* sourceStage_ = "idle";
#ifdef __WXGTK__
    bool failed_ = false;
    bool deleted_ = false;
    GdkDragAction operation_ = static_cast<GdkDragAction>(0);
    long sourceStart_ = 0;
    long sourceEnd_ = 0;
#endif
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
        WaitForDriver(control);
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
        std::atomic<bool> released = false;
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
            if (cancel) key(VK_ESCAPE, false);
            mouse(MOUSEEVENTF_LEFTUP);
            released = true;
        });
        do {
            wxYield();
            if (!released) wxMilliSleep(10);
        } while (!released);
        driver.join();
        wxYield();
        // OLE chooses the action from the modifier state at Drop, after it
        // observes the button release. wxYield cannot return from the source's
        // synchronous mouse handler until DoDragDrop exits. Drain the released
        // input on this UI thread before releasing the modifier.
        if (code) {
            key(code, true);
            wxYield();
        }
        // Escape must also remain observable until QueryContinueDrag has
        // cancelled the native loop, including across the peer process.
        if (cancel) {
            key(VK_ESCAPE, true);
            wxYield();
        }
    }
#else
    else {
        throw std::runtime_error("native drag fixture requires the private display driver");
    }
#endif
}
void Front(wxFrame* frame, const wxString& name) {
    wxString control;
    if (wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &control)) {
        wxFFile request(control, "wb");
        request.Write("front:wxnotepad stream drag " + name);
        request.Close();
        WaitForDriver(control);
    }
#ifdef __WXMSW__
    else {
        SetForegroundWindow(FindWindowW(nullptr, ("wxnotepad stream drag " + name).wc_str()));
        Wait(300);
    }
#endif
}
#ifdef __WXGTK__
wxPoint ClientOffset(wxWindow* window) {
    auto* widget = window->GetConnectWidget();
    int x = 0;
    int y = 0;
    Require(gtk_widget_translate_coordinates(widget, gtk_widget_get_toplevel(widget), 0, 0, &x, &y),
            "native widget must expose its offset inside the owned Wayland surface");
    return wxPoint(x, y);
}
#endif
class PeerProcess {
public:
    explicit PeerProcess(bool delayOffset = false)
        : directory(PrepareDirectory()),
          process(wxExecute("\"" + wxStandardPaths::Get().GetExecutablePath() + "\" " +
                                (delayOffset ? "--peer-delayed-offset" : "--peer") + " \"" + directory + "\"",
                            wxEXEC_ASYNC)) {
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
        {
            wxFFile request(directory + "/read", "wb");
        }
        wxStopWatch wait;
        while (wxFileExists(directory + "/read") && wait.Time() < 3000)
            Wait(20);
        Require(!wxFileExists(directory + "/read"), "native peer must report its current control value");
        wxFFile file(directory + "/value", "rb");
        wxString value;
        Require(file.IsOpened() && file.ReadAll(&value, wxConvUTF8), "read native peer's transferred bytes");
        return value;
    }
    void PrintSourceState() const {
        wxFFile file(directory + "/source-state", "rb");
        wxString state;
        if (file.IsOpened() && file.ReadAll(&state)) std::fprintf(stderr, "%s", state.utf8_str().data());
    }
    void Seed(const wxString& text = wxString::FromUTF8("中😀\nline")) {
        wxFFile file(directory + "/seed", "wb");
        Require(file.Write(text, wxConvUTF8), "write native peer's next source text");
        file.Close();
        wxStopWatch wait;
        while (wxFileExists(directory + "/seed") && wait.Time() < 3000)
            Wait(20);
        Require(!wxFileExists(directory + "/seed"), "peer must accept next Unicode source value");
    }
    wxString directory;
    long process;

private:
    static wxString PrepareDirectory() {
        const auto path = wxFileName::CreateTempFileName("wxnotepad-stream-peer-");
        wxRemoveFile(path);
        Require(wxMkdir(path), "create owned native peer IPC directory");
        return path;
    }
};
std::pair<wxPoint, wxPoint> WaylandOrigins(EditorControl* editor, const wxString& directory) {
    wxString control;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &control), "Wayland geometry requires the private driver");
    const auto response = wxFileName(control).GetPathWithSep() + "geometry";
    if (wxFileExists(response)) wxRemoveFile(response);
    {
        wxFFile request(control, "wb");
        request.Write("geometry");
    }
    wxStopWatch wait;
    const auto offsetPath = directory + "/offset";
    while ((!wxFileExists(response) || !wxFileExists(offsetPath)) && wait.Time() < 5000)
        Wait(20);
    wxString positions;
    wxString offset;
    wxFFile geometry(response, "rb");
    wxFFile peerOffset(offsetPath, "rb");
    Require(geometry.IsOpened() && geometry.ReadAll(&positions) && peerOffset.IsOpened() && peerOffset.ReadAll(&offset),
            "read owned compositor positions and native peer widget offset");
    int mx, my, px, py, dx, dy;
    Require(std::sscanf(positions.utf8_str().data(), "%d,%d,%d,%d", &mx, &my, &px, &py) == 4 &&
                std::sscanf(offset.utf8_str().data(), "%d,%d", &dx, &dy) == 2,
            "owned native geometry must contain both window origins");
#ifdef __WXGTK__
    return {wxPoint(mx, my) + ClientOffset(editor), wxPoint(px + dx, py + dy)};
#else
    return {};
#endif
}
class DragApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        delete wxLog::SetActiveTarget(new wxLogStderr);
        if (argc == 2 && argv[1] == "--input-state-probe") return InitInputStateProbe();
        if (argc == 2 && argv[1] == "--geometry-probe") {
            wxString backend;
            if (!wxGetEnv("GDK_BACKEND", &backend) || backend != "wayland") {
                std::puts("SKIP: peer geometry readiness requires the private Wayland driver");
                result_ = 77;
                CallAfter([this] { ExitMainLoop(); });
                return true;
            }
            return InitGeometryProbe();
        }
        const bool delayOffset = argc == 3 && argv[1] == "--peer-delayed-offset";
        if (argc == 3 && (argv[1] == "--peer" || delayOffset)) {
            const wxString directory = argv[2];
            auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad stream drag peer");
            auto* peer = new NativePeer(frame);
            peer->changed = [peer, directory] {
                wxFFile value(directory + "/value", "wb");
                value.Write(peer->CurrentValue(), wxConvUTF8);
            };
            wxString backend;
            if (wxGetEnv("GDK_BACKEND", &backend) && backend == "wayland") {
                frame->SetClientSize(600, 700);
                frame->Show();
            } else
                frame->ShowFullScreen(true);
            timer_.SetOwner(this);
            offsetDelay_.Start();
            Bind(wxEVT_TIMER, [this, frame, peer, directory, delayOffset](wxTimerEvent&) {
                UpdatePeer(frame, peer, directory, delayOffset);
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
                if (Text(editor) != "one two threeone")
                    std::fprintf(stderr, "Observed Ctrl drag text: <%s>\n", Text(editor).c_str());
                Require(Text(editor) == "one two threeone", "Ctrl native drag must copy stream text");
                editor->Send(Message::Undo);
                Require(Text(editor) == "one two three", "copy must undo in one action");
                editor->Send(Message::SetSel, 0, 3);
                Drag(frame, At(editor, 1), At(editor, 2));
                Require(Text(editor) == "one two three" && !editor->Send(Message::CanUndo),
                        "moving inside the source selection must be a no-op");
                editor->Send(Message::SetSel, 0, 3);
                Drag(frame, At(editor, 1), At(editor, 13), {}, true);
                Require(Text(editor) == "one two three" && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::GetSelectionStart) == 0 &&
                            editor->Send(Message::GetSelectionEnd) == 3 && !editor->HasCapture(),
                        "cancelled native drag must preserve source text, selection, save point and release capture");
                std::puts("PASS: Ctrl copy, self-drop and cancelled drag");

                peer->Assign(wxString::FromUTF8("中😀\nline"));
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("left right"));
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                editor->Send(Message::SetReadOnly, 1);
                Drag(frame, wxPoint(810, 10), At(editor, 5));
                peer->WaitForSource();
                Require(Text(editor) == "left right" && peer->value == wxString::FromUTF8("中😀\nline") &&
                            !editor->Send(Message::GetModify),
                        "read-only drop must reject transfer and preserve native peer source");
                editor->Send(Message::SetReadOnly, 0);
                peer->Assign(wxString::FromUTF8("中😀\nline"));
                Drag(frame, wxPoint(810, 10), At(editor, 5));
                peer->WaitForSource();
                if (Text(editor) != "left 中😀\r\nlineright" || !peer->value.empty()) {
                    peer->PrintSourceState();
                    PrintBytes("Incoming peer target", Text(editor));
                    PrintBytes("Incoming peer recorded source", peer->value.ToStdString(wxConvUTF8));
                    PrintBytes("Incoming peer native source", peer->CurrentValue().ToStdString(wxConvUTF8));
                }
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
                editor->Send(Message::SetReadOnly, 1);
                editor->Send(Message::SetSel, 0, 13);
                peer->Assign({});
                Drag(frame, At(editor, 3), wxPoint(850, 40));
                Require(Text(editor) == "中😀\r\nline tail" && !peer->value.empty() &&
                            !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                        "read-only source must retain its text and history after a completed native transfer");
                editor->Send(Message::SetReadOnly, 0);
                std::puts("PASS: original read-only source retention");

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
                wxPoint editorOrigin;
                wxPoint peerOrigin;
                if (wayland) {
                    auto origins = WaylandOrigins(editor, independent.directory);
                    wxString control;
                    wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &control);
                    {
                        wxFFile request(control, "wb");
                        request.Write(wxString::Format("move-peer:%d,%d,%d,%d", origins.second.x + 100,
                                                       origins.second.y + 100, 950, 192));
                    }
                    WaitForDriver(control);
                    origins = WaylandOrigins(editor, independent.directory);
                    editorOrigin = origins.first;
                    peerOrigin = origins.second;
                    Require(peerOrigin.x >= editorOrigin.x + editor->GetSize().x,
                            "native compositor must leave both owned transfer clients visible");
                    std::fprintf(stderr, "Native client origins: editor=%d,%d peer=%d,%d\n", editorOrigin.x,
                                 editorOrigin.y, peerOrigin.x, peerOrigin.y);
                } else
                    Front(frame, "main");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中😀\r\nline tail"));
                editor->Send(Message::GotoPos, 0);
                editor->Send(Message::SetSel, 0, 13);
                editor->Send(Message::EmptyUndoBuffer);
                editor->Send(Message::SetSavePoint);
                Drag(frame, At(editor, 3) + editorOrigin, peerOrigin + wxPoint(100, 100), {}, false, 300,
                     wayland ? "" : "peer");
                auto transferred = independent.Value();
                transferred.Replace("\r\n", "\n");
                Require(Text(editor) == " tail" && transferred == wxString::FromUTF8("中😀\nline"),
                        "separate native application must receive Unicode stream move before source deletion");
                if (!wayland) Front(frame, "main");
                editor->Send(Message::Undo);
                Require(Text(editor) == "中😀\r\nline tail" && !editor->Send(Message::GetModify),
                        "inter-application move must undo source deletion once");
                independent.Seed({});
                editor->Send(Message::SetSel, 0, 13);
                Drag(frame, At(editor, 3) + editorOrigin, peerOrigin + wxPoint(100, 100), "ctrl", false, 300,
                     wayland ? "" : "peer");
                transferred = independent.Value();
                transferred.Replace("\r\n", "\n");
                Require(Text(editor) == "中😀\r\nline tail" && transferred == wxString::FromUTF8("中😀\nline") &&
                            !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
                        "separate native application's copy must preserve the editor source and history");
                if (!wayland) Front(frame, "main");
                independent.Seed({});
                editor->Send(Message::SetSel, 0, 13);
                Drag(frame, At(editor, 3) + editorOrigin, peerOrigin + wxPoint(100, 100), {}, true, 300,
                     wayland ? "" : "peer");
                const auto cancelledPeer = independent.Value();
                if (Text(editor) != "中😀\r\nline tail" || !cancelledPeer.empty() || editor->Send(Message::CanUndo) ||
                    editor->Send(Message::GetModify) || editor->Send(Message::GetSelectionStart) != 0 ||
                    editor->Send(Message::GetSelectionEnd) != 13) {
                    PrintBytes("Cancelled external drag source", Text(editor));
                    PrintBytes("Cancelled external drag target", cancelledPeer.ToStdString(wxConvUTF8));
                    std::fprintf(stderr, "Cancelled source undo=%lld modified=%lld selection=%lld,%lld\n",
                                 static_cast<long long>(editor->Send(Message::CanUndo)),
                                 static_cast<long long>(editor->Send(Message::GetModify)),
                                 static_cast<long long>(editor->Send(Message::GetSelectionStart)),
                                 static_cast<long long>(editor->Send(Message::GetSelectionEnd)));
                }
                Require(Text(editor) == "中😀\r\nline tail" && cancelledPeer.empty() &&
                            !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify) &&
                            editor->Send(Message::GetSelectionStart) == 0 &&
                            editor->Send(Message::GetSelectionEnd) == 13,
                        "cancelled inter-application drag must preserve source and leave the peer unchanged");
                std::puts("PASS: inter-application native copy and cancellation");
                editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("left right"));
                editor->Send(Message::GotoPos, 0);
                independent.Seed();
                if (!wayland) Front(frame, "peer");
                Drag(frame, peerOrigin + wxPoint(10, 10), At(editor, 5) + editorOrigin, {}, false, 300,
                     wayland ? "" : "main");
                // The injected button release does not establish the other
                // process's native drag completion. Observe its acknowledgement first.
                const auto remainingSource = independent.Value();
                if (Text(editor) != "left 中😀\r\nlineright" || !remainingSource.empty()) {
                    independent.PrintSourceState();
                    PrintBytes("External peer move target", Text(editor));
                    PrintBytes("External peer remaining source", remainingSource.ToStdString(wxConvUTF8));
                }
                Require(Text(editor) == "left 中😀\r\nlineright" && remainingSource.empty(),
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
    bool InitInputStateProbe() {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad stream drag main");
        frame->Show();
        CallAfter([this, frame] {
            try {
                wxString control;
                Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &control),
                        "native request snapshot requires the private driver");
                const wxString command = "front:wxnotepad stream drag main";
                const wxString prepared = control + ".snapshot.tmp";
                {
                    wxFFile request(prepared, "wb");
                    Require(request.Write(command) && request.Close(), "prepare native request snapshot probe");
                }
                wxFFile snapshot(prepared, "rb");
                Require(snapshot.IsOpened(), "open pending native request snapshot");
                Require(wxRenameFile(prepared, control, true), "publish native request snapshot probe");
                WaitForDriver(control);
                wxString observed;
                Require(snapshot.ReadAll(&observed) && observed == command,
                        "driver acknowledgement must preserve an already opened request snapshot");
                std::puts("PASS: native input acknowledgement preserves an open request snapshot");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }

    void UpdatePeer(wxFrame* frame, NativePeer* peer, const wxString& directory, bool delayOffset) {
#ifdef __WXGTK__
        if (!delayOffset || offsetDelay_.Time() >= 1500) {
            const auto origin = ClientOffset(peer);
            wxFFile offset(directory + "/offset.tmp", "wb");
            Require(offset.IsOpened() && offset.Write(wxString::Format("%d,%d", origin.x, origin.y)),
                    "write native peer widget coordinates");
            Require(offset.Close() && wxRenameFile(directory + "/offset.tmp", directory + "/offset", true),
                    "publish complete native peer widget coordinates");
        }
#endif
        if (wxFileExists(directory + "/quit")) {
            timer_.Stop();
            wxFFile done(directory + "/done", "wb");
            frame->Destroy();
            ExitMainLoop();
        } else if (wxFileExists(directory + "/seed") && !peer->IsDragging()) {
            wxFFile source(directory + "/seed", "rb");
            wxString text;
            Require(source.ReadAll(&text, wxConvUTF8), "read peer's requested source text");
            source.Close();
            peer->Assign(text);
            peer->changed();
            wxRemoveFile(directory + "/seed");
        } else if (wxFileExists(directory + "/read") && !peer->IsDragging()) {
            peer->changed();
            {
                wxFFile state(directory + "/source-state", "wb");
                Require(state.IsOpened(), "open native peer source diagnostics");
                peer->PrintSourceState(state.fp());
            }
            wxRemoveFile(directory + "/read");
        }
    }

    bool InitGeometryProbe() {
        auto* frame = new wxFrame(nullptr, wxID_ANY, "wxnotepad stream drag main");
        auto* editor = new EditorControl(frame);
        frame->SetClientSize(760, 700);
        frame->Show();
        CallAfter([this, frame, editor] {
            try {
                PeerProcess peer(true);
                WaylandOrigins(editor, peer.directory);
                std::puts("PASS: native peer geometry waits for delayed coordinate publication");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
            }
            frame->Destroy();
            ExitMainLoop();
        });
        return true;
    }

    wxTimer timer_;
    wxStopWatch offsetDelay_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(DragApp);
