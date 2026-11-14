#include "editor/editor_control.h"
#include "app/editor_frame.h"
#include "file_helpers.h"
#include "file_command_driver.h"
#include "native_paint_helpers.h"
#include "native_focus_helpers.h"
#include <wx/wx.h>
#include <wx/dnd.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/process.h>
#include "steady_stopwatch.h"
#include <cstdio>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>
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
    TestClock::StopWatch watch;
    while (watch.Time() < milliseconds) {
        wxYield();
        wxMilliSleep(10);
    }
}
void WaitForDriver(const wxString& control) {
    TestClock::StopWatch watch;
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
#ifdef __WXGTK__
wxString ReadDragState(const wxString& path) {
    if (!wxFileExists(path)) return {};
    wxFFile file(path, "rb");
    wxString phase;
    if (file.IsOpened()) file.ReadAll(&phase);
    return phase;
}
void PublishDragState(const wxString& path, const wxString& state) {
    wxFFile file(path + ".tmp", "wb");
    Require(file.IsOpened() && file.Write(state) && file.Close() && wxRenameFile(path + ".tmp", path, true),
            "publish owned native drag readiness");
}
using DragContextOwner = std::unique_ptr<GdkDragContext, decltype(&g_object_unref)>;
struct SourceStatus {
    wxString control;
    wxString identity;
    DragContextOwner context;
};

gboolean ObserveSourceAction(gpointer value) {
    const auto& source = *static_cast<SourceStatus*>(value);
    const auto phase = ReadDragState(source.control + ".drag-phase");
    if (phase != source.identity + ":start" && phase != source.identity + ":target") return G_SOURCE_REMOVE;
    const auto target = ReadDragState(source.control + ".drag-motion");
    if (target.StartsWith(source.identity + ":target:")) {
        const auto action = static_cast<unsigned int>(gdk_drag_context_get_selected_action(source.context.get()));
        const auto status = source.identity + wxString::Format(":target:%u", action);
        if (action != 0 && target == status) {
            PublishDragState(source.control + ".drag-action", status);
            std::fprintf(stderr, "Native drag gesture=%s observed=source-selected-action\n", status.utf8_str().data());
            return G_SOURCE_REMOVE;
        }
    }
    return G_SOURCE_CONTINUE;
}
void ObserveNativeDrags(wxString* control) {
    // A handled drag-motion stops signal emission before connect_after handlers.
    // The idle runs after the native handler has returned and sent its status,
    // including action zero for a rejected read-only target.
    struct Observation {
        wxString path;
        wxString phase;
        GdkDragContext* target;
        const char* event;
    };
    static const auto queue = +[](const Observation& observation) {
        auto pending = std::make_unique<Observation>(
            observation.path, observation.phase,
            observation.target ? static_cast<GdkDragContext*>(g_object_ref(observation.target)) : nullptr,
            observation.event);
        g_idle_add_full(
            G_PRIORITY_DEFAULT_IDLE,
            +[](gpointer value) -> gboolean {
                const auto& observation = *static_cast<Observation*>(value);
                // Commit the native start/status/grab cleanup before the
                // independent input process can advance on this observation.
                gdk_display_sync(gdk_display_get_default());
                auto state = observation.phase;
                if (observation.target)
                    state += wxString::Format(":%u", gdk_drag_context_get_selected_action(observation.target));
                PublishDragState(observation.path, state);
                std::fprintf(stderr, "Native drag gesture=%s observed=%s\n", state.utf8_str().data(),
                             observation.event);
                return G_SOURCE_REMOVE;
            },
            pending.release(),
            +[](gpointer value) {
                const std::unique_ptr<Observation> completed(static_cast<Observation*>(value));
                if (completed->target) g_object_unref(completed->target);
            });
    };
    // GtkWidget registers these signals during class initialization.
    auto* widgetClass = g_type_class_ref(GTK_TYPE_WIDGET);
    g_signal_add_emission_hook(
        g_signal_lookup("button-press-event", GTK_TYPE_WIDGET), 0,
        +[](GSignalInvocationHint*, guint, const GValue* arguments, gpointer value) -> gboolean {
            const auto& control = *static_cast<wxString*>(value);
            const auto phase = ReadDragState(control + ".drag-phase");
            const auto* event = static_cast<GdkEventButton*>(g_value_get_boxed(arguments + 1));
            if (phase.EndsWith(":start") && event->button == 1) {
                std::fprintf(stderr, "Native drag gesture=%s press-type=%d at=%.0f,%.0f\n", phase.utf8_str().data(),
                             static_cast<int>(event->type), event->x, event->y);
                queue({control + ".drag-pressed", phase, nullptr, "button-press-processed"});
            }
            return TRUE;
        },
        control, nullptr);
    g_signal_add_emission_hook(
        g_signal_lookup("drag-begin", GTK_TYPE_WIDGET), 0,
        +[](GSignalInvocationHint*, guint, const GValue* arguments, gpointer value) -> gboolean {
            const auto& control = *static_cast<wxString*>(value);
            const auto phase = ReadDragState(control + ".drag-phase");
            if (!phase.EndsWith(":start")) return TRUE;
            queue({control + ".drag-started", phase, nullptr, "source-started"});
            auto* context = static_cast<GdkDragContext*>(g_value_get_object(arguments + 1));
            auto source = std::make_unique<SourceStatus>(
                control, phase.BeforeLast(':'),
                DragContextOwner(static_cast<GdkDragContext*>(g_object_ref(context)), &g_object_unref));
            g_timeout_add_full(
                G_PRIORITY_DEFAULT, 10, ObserveSourceAction, source.release(), +[](gpointer value) {
                    const std::unique_ptr<SourceStatus> completed(static_cast<SourceStatus*>(value));
                });
            return TRUE;
        },
        control, nullptr);
    g_signal_add_emission_hook(
        g_signal_lookup("drag-motion", GTK_TYPE_WIDGET), 0,
        +[](GSignalInvocationHint*, guint, const GValue* arguments, gpointer value) -> gboolean {
            const auto& control = *static_cast<wxString*>(value);
            const auto phase = ReadDragState(control + ".drag-phase");
            if (phase.EndsWith(":target"))
                queue({control + ".drag-motion", phase, static_cast<GdkDragContext*>(g_value_get_object(arguments + 1)),
                       "target-motion-processed"});
            return TRUE;
        },
        control, nullptr);
    g_signal_add_emission_hook(
        g_signal_lookup("drag-failed", GTK_TYPE_WIDGET), 0,
        +[](GSignalInvocationHint*, guint, const GValue* arguments, gpointer value) -> gboolean {
            const auto phase = ReadDragState(*static_cast<wxString*>(value) + ".drag-phase");
            std::fprintf(stderr, "Native drag gesture=%s result=%d\n", phase.utf8_str().data(),
                         g_value_get_enum(arguments + 2));
            return TRUE;
        },
        control, nullptr);
    g_signal_add_emission_hook(
        g_signal_lookup("drag-end", GTK_TYPE_WIDGET), 0,
        +[](GSignalInvocationHint*, guint, const GValue*, gpointer value) -> gboolean {
            const auto& control = *static_cast<wxString*>(value);
            const auto phase = ReadDragState(control + ".drag-phase");
            queue({control + ".drag-ended", phase.BeforeLast(':') + ":end", nullptr, "source-ended"});
            return TRUE;
        },
        control, nullptr);
    g_type_class_unref(widgetClass);
}
#endif
wxPoint At(EditorControl* editor, intptr_t position) {
    using Scintilla::Message;
    const wxPoint point(static_cast<int>(editor->Send(Message::PointXFromPosition, 0, position)) + 1,
                        static_cast<int>(editor->Send(Message::PointYFromPosition, 0, position)) + 5);
    // Include the editor's native client border in the frame-relative gesture.
    return editor->GetParent()->ScreenToClient(editor->ClientToScreen(point));
}
// GTK uses the toolkit's actual native text view as the independent peer.
// Windows uses wx's OLE text data object, without the editor implementation.
#ifdef __WXGTK__
using PeerWindow = wxTextCtrl;
using NativeUri = std::unique_ptr<gchar, decltype(&g_free)>;
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
                             peer.failedResult_ = GTK_DRAG_RESULT_SUCCESS;
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
                         G_CALLBACK(+[](GtkWidget*, GdkDragContext*, GtkDragResult result, gpointer value) -> gboolean {
                             auto& peer = *static_cast<NativePeer*>(value);
                             peer.failed_ = true;
                             peer.failedResult_ = result;
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
        g_signal_connect(GetConnectWidget(), "button-press-event",
                         G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, gpointer value) -> gboolean {
                             auto& peer = *static_cast<NativePeer*>(value);
                             if (peer.files_.empty()) return FALSE;
                             peer.start_ = wxPoint(static_cast<int>(event->x), static_cast<int>(event->y));
                             peer.armed_ = event->button == 1;
                             return TRUE;
                         }),
                         this);
        g_signal_connect(GetConnectWidget(), "motion-notify-event",
                         G_CALLBACK(+[](GtkWidget* widget, GdkEventMotion* event, gpointer value) -> gboolean {
                             auto& peer = *static_cast<NativePeer*>(value);
                             if (peer.files_.empty()) return FALSE;
                             if (peer.armed_ && (event->state & GDK_BUTTON1_MASK) &&
                                 gtk_drag_check_threshold(widget, peer.start_.x, peer.start_.y,
                                                          static_cast<int>(event->x), static_cast<int>(event->y))) {
                                 peer.armed_ = false;
                                 auto* targets = gtk_target_list_new(nullptr, 0);
                                 gtk_target_list_add_uri_targets(targets, 1);
                                 gtk_target_list_add_text_targets(targets, 0);
                                 gtk_drag_begin_with_coordinates(widget, targets, GDK_ACTION_COPY, 1,
                                                                 reinterpret_cast<GdkEvent*>(event), -1, -1);
                                 gtk_target_list_unref(targets);
                             }
                             return TRUE;
                         }),
                         this);
        g_signal_connect(
            GetConnectWidget(), "drag-data-get",
            G_CALLBACK(+[](GtkWidget* widget, GdkDragContext*, GtkSelectionData* data, guint, guint, gpointer value) {
                auto& peer = *static_cast<NativePeer*>(value);
                if (peer.files_.empty()) return;
                if (gtk_selection_data_get_target(data) == gdk_atom_intern_static_string("text/uri-list")) {
                    std::vector<NativeUri> ownedUris;
                    std::vector<gchar*> uris;
                    ownedUris.reserve(peer.files_.size());
                    uris.reserve(peer.files_.size() + 1);
                    for (const auto& file : peer.files_) {
                        NativeUri uri(g_filename_to_uri(file.fn_str(), nullptr, nullptr), g_free);
                        uris.push_back(uri.get());
                        ownedUris.push_back(std::move(uri));
                    }
                    uris.push_back(nullptr);
                    gtk_selection_data_set_uris(data, uris.data());
                    std::fprintf(stderr, "native-file-offer: text/uri-list items=%zu\n", peer.files_.size());
                } else {
                    gtk_selection_data_set_text(data, "file offer text fallback", -1);
                }
                g_signal_stop_emission_by_name(widget, "drag-data-get");
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
            wxDataObjectComposite data;
            if (!files_.empty()) {
                auto* files = new wxFileDataObject;
                for (const auto& file : files_)
                    files->AddFile(file);
                data.Add(files, true);
                std::fprintf(stderr, "native-file-offer: CF_HDROP items=%zu\n", files_.size());
            }
            data.Add(new wxTextDataObject(value));
            wxDropSource source(this);
            source.SetData(data);
            result = source.DoDragDrop(files_.empty() ? wxDrag_DefaultMove : wxDrag_CopyOnly);
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
        std::fprintf(output, " failed=%d failed-result=%d deleted=%d native-action-mask=%u selection=%ld,%ld", failed_,
                     static_cast<int>(failedResult_), deleted_, static_cast<unsigned int>(operation_), sourceStart_,
                     sourceEnd_);
#else
        std::fprintf(output, " native-result=%d", static_cast<int>(result));
#endif
        const auto bounds = GetScreenRect();
        std::fprintf(output, " screen-bounds=%d,%d,%d,%d\n", bounds.x, bounds.y, bounds.width, bounds.height);
    }
    void WaitForSource() const {
        // Failed native GTK drags can animate after pointer release. Observe
        // completion before starting another transfer from the same control.
        TestClock::StopWatch wait;
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
        files_.clear();
        value = text;
#ifdef __WXGTK__
        ChangeValue(text);
        SetSelection(-1, -1);
#endif
    }
    void OfferFiles(const wxArrayString& files) {
        Assign("file offer text fallback");
        files_ = files;
    }

private:
    wxArrayString files_;
    bool dragging_ = false;
    unsigned int sourceAttempts_ = 0;
    const char* sourceStage_ = "idle";
#ifdef __WXGTK__
    bool failed_ = false;
    GtkDragResult failedResult_ = GTK_DRAG_RESULT_SUCCESS;
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
          const wxString& switchTo = {}, bool reject = false) {
    wxString control;
    if (wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &control)) {
        static unsigned int gesture = 0;
        const auto identity = wxString::Format("%u", ++gesture);
#ifdef __WXGTK__
        PublishDragState(control + ".drag-phase", identity + ":start");
#endif
        std::fprintf(stderr, "Native drag gesture=%s from=%d,%d to=%d,%d modifier=%s cancel=%d\n",
                     identity.utf8_str().data(), from.x, from.y, to.x, to.y, modifier.utf8_str().data(), cancel);
        wxFFile request(control, "wb");
        const unsigned int action = reject ? 0 : modifier == "ctrl" ? 2 : 4;
        Require(request.Write(wxString::Format("drag:%d,%d,%d,%d,%s,%d,%d,%s,%s,%u", from.x, from.y, to.x, to.y,
                                               modifier, cancel, pause, switchTo, identity, action)),
                "publish private drag input");
        request.Close();
        WaitForDriver(control);
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
        TestClock::StopWatch wait;
        while (!wxFileExists(directory + "/ready") && wait.Time() < 5000)
            Wait(20);
        Require(wxFileExists(directory + "/ready"), "independent native peer must become ready");
    }
    PeerProcess(const PeerProcess&) = delete;
    PeerProcess& operator=(const PeerProcess&) = delete;
    ~PeerProcess() {
        wxFFile quit(directory + "/quit", "wb");
        quit.Close();
        TestClock::StopWatch wait;
        while (!wxFileExists(directory + "/done") && wait.Time() < 3000)
            Wait(20);
        if (!wxFileExists(directory + "/done")) wxKill(process, wxSIGTERM);
        wxFileName::Rmdir(directory, wxPATH_RMDIR_RECURSIVE);
    }
    wxString Value() const {
        {
            wxFFile request(directory + "/read", "wb");
        }
        TestClock::StopWatch wait;
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
        TestClock::StopWatch wait;
        while (wxFileExists(directory + "/seed") && wait.Time() < 3000)
            Wait(20);
        Require(!wxFileExists(directory + "/seed"), "peer must accept next Unicode source value");
    }
    void OfferFiles(const wxArrayString& paths) {
        wxFFile file(directory + "/files", "wb");
        Require(file.Write(wxJoin(paths, '\n'), wxConvUTF8) && file.Close(), "publish peer's native file offer");
        TestClock::StopWatch wait;
        while (wxFileExists(directory + "/files") && wait.Time() < 3000)
            Wait(20);
        Require(!wxFileExists(directory + "/files"), "peer must accept native file offer");
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
    TestClock::StopWatch wait;
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
void SelfDrags(wxFrame* frame, EditorControl* editor) {
    using Scintilla::Message;
    NativePaint::Complete(editor);
    editor->Send(Message::SetXOffset, 0);
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("one two three"));
    editor->Send(Message::SetSel, 0, 3);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
#ifdef __WXMSW__
    const auto owned = static_cast<HWND>(frame->GetHandle());
    const auto intended = static_cast<HWND>(editor->GetHandle());
    NativeFocus::Activate(owned, intended);
    Require(GetForegroundWindow() == owned && GetFocus() == intended,
            "native self drag requires the owned editor foreground and focus");
#endif
    Drag(frame, At(editor, 1), At(editor, 13));
    Require(Text(editor) == " two threeone", "native self drag must move stream text");
    editor->Send(Message::Undo);
    Require(Text(editor) == "one two three" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
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
                editor->Send(Message::GetSelectionStart) == 0 && editor->Send(Message::GetSelectionEnd) == 3 &&
                !editor->HasCapture(),
            "cancelled native drag must preserve source text, selection, save point and release capture");
    std::puts("PASS: Ctrl copy, self-drop and cancelled drag");
}
void IncomingPeerDrags(wxFrame* frame, EditorControl* editor, NativePeer* peer) {
    using Scintilla::Message;
    peer->Assign(wxString::FromUTF8("中😀\nline"));
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("left right"));
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    editor->Send(Message::SetReadOnly, 1);
    Drag(frame, wxPoint(810, 10), At(editor, 5), {}, false, 200, {}, true);
    peer->WaitForSource();
    peer->PrintSourceState();
    Require(Text(editor) == "left right" && peer->value == wxString::FromUTF8("中😀\nline") &&
                !editor->Send(Message::GetModify),
            "read-only drop must reject transfer and preserve native peer source");
    editor->Send(Message::SetReadOnly, 0);
    peer->Assign(wxString::FromUTF8("中😀\nline"));
#ifdef __WXGTK__
    // Process the threshold motion after the old driver's fixed
    // release window, before GTK initializes this native source.
    bool delayed = false;
    const auto delayHandler =
        g_signal_connect(peer->GetConnectWidget(), "motion-notify-event",
                         G_CALLBACK(+[](GtkWidget*, GdkEventMotion* event, gpointer value) -> gboolean {
                             auto& delayed = *static_cast<bool*>(value);
                             if (!delayed && (event->state & GDK_BUTTON1_MASK)) {
                                 delayed = true;
                                 g_usleep(600000);
                             }
                             return FALSE;
                         }),
                         &delayed);
#endif
    Drag(frame, wxPoint(810, 10), At(editor, 5));
#ifdef __WXGTK__
    g_signal_handler_disconnect(peer->GetConnectWidget(), delayHandler);
    Require(delayed, "incoming native move must exercise slow source initialization");
#endif
    peer->WaitForSource();
    peer->PrintSourceState();
    if (Text(editor) != "left 中😀\r\nlineright" || !peer->value.empty()) {
        peer->PrintSourceState();
        PrintBytes("Incoming peer target", Text(editor));
        PrintBytes("Incoming peer recorded source", peer->value.ToStdString(wxConvUTF8));
        PrintBytes("Incoming peer native source", peer->CurrentValue().ToStdString(wxConvUTF8));
    }
    Require(Text(editor) == "left 中😀\r\nlineright" && peer->value.empty(),
            "native peer move must transfer Unicode and normalize multiline text");
    editor->Send(Message::Undo);
    Require(Text(editor) == "left right" && !editor->Send(Message::CanUndo) && !editor->Send(Message::GetModify),
            "native peer insertion must undo once to the save point");
    std::puts("PASS: read-only rejection and incoming Unicode/multiline native peer move");
}
void OutgoingPeerDrags(wxFrame* frame, EditorControl* editor, NativePeer* peer) {
    using Scintilla::Message;
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
    Require(Text(editor) == "中😀\r\nline tail" && !peer->value.empty() && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify),
            "read-only source must retain its text and history after a completed native transfer");
    editor->Send(Message::SetReadOnly, 0);
    std::puts("PASS: original read-only source retention");
}
void DragScrollCancellation(wxFrame* frame, EditorControl* editor) {
    using Scintilla::Message;
    std::string lines;
    for (int index = 0; index < 120; ++index)
        lines += "line\r\n";
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>(lines.c_str()));
    editor->Send(Message::SetSel, 0, 4);
    Drag(frame, At(editor, 1), wxPoint(100, 690), {}, true, 1200);
    Require(editor->Send(Message::GetFirstVisibleLine) > 0 && Text(editor) == lines,
            "native insertion feedback must scroll the viewport while cancellation preserves text");
    std::puts("PASS: insertion feedback scrolling and cancellation");
}
void IndependentOutgoingDrags(wxFrame* frame, EditorControl* editor, PeerProcess& independent, wxPoint editorOrigin,
                              wxPoint peerOrigin, bool wayland) {
    using Scintilla::Message;
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("中😀\r\nline tail"));
    editor->Send(Message::GotoPos, 0);
    editor->Send(Message::SetSel, 0, 13);
    editor->Send(Message::EmptyUndoBuffer);
    editor->Send(Message::SetSavePoint);
    Drag(frame, At(editor, 3) + editorOrigin, peerOrigin + wxPoint(100, 100), {}, false, 300, wayland ? "" : "peer");
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
    Drag(frame, At(editor, 3) + editorOrigin, peerOrigin + wxPoint(100, 100), {}, true, 300, wayland ? "" : "peer");
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
    Require(Text(editor) == "中😀\r\nline tail" && cancelledPeer.empty() && !editor->Send(Message::CanUndo) &&
                !editor->Send(Message::GetModify) && editor->Send(Message::GetSelectionStart) == 0 &&
                editor->Send(Message::GetSelectionEnd) == 13,
            "cancelled inter-application drag must preserve source and leave the peer unchanged");
    std::puts("PASS: inter-application native copy and cancellation");
}
void IndependentIncomingDrag(wxFrame* frame, EditorControl* editor, PeerProcess& independent, wxPoint editorOrigin,
                             wxPoint peerOrigin, bool wayland) {
    using Scintilla::Message;
    editor->Send(Message::SetText, 0, reinterpret_cast<intptr_t>("left right"));
    editor->Send(Message::GotoPos, 0);
    independent.Seed();
    if (!wayland) Front(frame, "peer");
    Drag(frame, peerOrigin + wxPoint(10, 10), At(editor, 5) + editorOrigin, {}, false, 300, wayland ? "" : "main");
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
}
void IndependentProcessDrags(wxFrame* frame, EditorControl* editor) {
    using Scintilla::Message;
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
            request.Write(
                wxString::Format("move-peer:%d,%d,%d,%d", origins.second.x + 100, origins.second.y + 100, 950, 192));
        }
        WaitForDriver(control);
        origins = WaylandOrigins(editor, independent.directory);
        editorOrigin = origins.first;
        peerOrigin = origins.second;
        Require(peerOrigin.x >= editorOrigin.x + editor->GetSize().x,
                "native compositor must leave both owned transfer clients visible");
        std::fprintf(stderr, "Native client origins: editor=%d,%d peer=%d,%d\n", editorOrigin.x, editorOrigin.y,
                     peerOrigin.x, peerOrigin.y);
    } else
        Front(frame, "main");
    IndependentOutgoingDrags(frame, editor, independent, editorOrigin, peerOrigin, wayland);
    IndependentIncomingDrag(frame, editor, independent, editorOrigin, peerOrigin, wayland);
}
void NativeFileDrop(EditorFrame* frame) {
    auto* editor = frame->Editor();
    const auto directory = TestFiles::TemporaryDirectory("wxnotepad-native-file-drop");
    std::filesystem::create_directory(directory);
#ifdef __WXMSW__
    const auto first = directory / std::filesystem::path(u8"native 中 %20.txt");
#else
    const auto first = directory / std::filesystem::path(u8"native 中 %20|keep.txt");
#endif
    const auto second = directory / "ignored.txt";
    TestFiles::WriteBytes(first, "native first file\n");
    TestFiles::WriteBytes(second, "ignored file\n");
    PeerProcess peer;
    wxString backend;
    const bool wayland = wxGetEnv("GDK_BACKEND", &backend) && backend == "wayland";
    wxPoint editorOrigin;
    wxPoint peerOrigin;
    if (wayland) {
        auto origins = WaylandOrigins(editor, peer.directory);
        wxString control;
        wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &control);
        {
            wxFFile request(control, "wb");
            request.Write(
                wxString::Format("move-peer:%d,%d,%d,%d", origins.second.x + 100, origins.second.y + 100, 950, 192));
        }
        WaitForDriver(control);
        origins = WaylandOrigins(editor, peer.directory);
        editorOrigin = origins.first - editor->GetPosition();
        peerOrigin = origins.second;
    }
    const auto drop = [&](const wxArrayString& files) {
        peer.OfferFiles(files);
        if (!wayland) Front(frame, "peer");
        Drag(frame, peerOrigin + wxPoint(10, 10), At(editor, 0) + editorOrigin, "ctrl", false, 300,
             wayland ? "" : "main");
        peer.Value();
    };
    editor->Send(Scintilla::Message::SetReadOnly, 1);
    drop({TestFiles::Filename(first), TestFiles::Filename(second)});
    Require(frame->DocumentPath() == TestFiles::Filename(first) && Text(editor) == "native first file\n" &&
                !editor->Send(Scintilla::Message::CanUndo) && TestFiles::Bytes(first) == "native first file\n" &&
                TestFiles::Bytes(second) == "ignored file\n",
            "native file-list offer must open exactly the first document instead of inserting fallback text");
    std::puts("PASS: independent native file-list plus text offer opens only the first Unicode/percent path");
    editor->Send(Scintilla::Message::SetReadOnly, 0);
    editor->Send(Scintilla::Message::AppendText, 7, reinterpret_cast<intptr_t>("pending"));
    NativeDialogs::FileCommandDriver cancelDirectory(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL);
    drop({TestFiles::Filename(directory)});
    cancelDirectory.Stop();
    Require(cancelDirectory.choosers == 1 && cancelDirectory.prompts == 0 &&
                Text(editor) == "native first file\npending" && frame->DocumentPath() == TestFiles::Filename(first),
            "actual native directory offer must show its chooser before a dirty-document decision");
    NativeDialogs::FileCommandDriver cancelFile(NativeDialogs::FileDialogProfile::Open, wxID_CANCEL);
    drop({TestFiles::Filename(second)});
    cancelFile.Stop();
    Require(cancelFile.prompts == 1 && Text(editor) == "native first file\npending" &&
                TestFiles::Bytes(first) == "native first file\n",
            "actual native file offer Cancel must retain the live document and independent disk bytes");
    NativeDialogs::FileCommandDriver saveFile(NativeDialogs::FileDialogProfile::Open, wxID_YES);
    drop({TestFiles::Filename(second)});
    saveFile.Stop();
    Require(saveFile.prompts == 1 && Text(editor) == "ignored file\n" &&
                TestFiles::Bytes(first) == "native first file\npending" &&
                frame->DocumentPath() == TestFiles::Filename(second),
            "actual native file offer Save must write the original document before opening the next");
    std::puts("PASS: native directory chooser ordering and file Cancel/Save retain document and disk semantics");
    // Peer teardown pumps events; stop observing the fixture before deleting it.
    frame->SetFileWatchingOptions({FileWatchingMode::None});
    std::filesystem::remove_all(directory);
}
class DragApp final : public wxApp {
public:
    bool OnInit() override {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        {
            const std::unique_ptr<wxLog> previousLog(wxLog::SetActiveTarget(new wxLogStderr));
        }
#ifdef __WXGTK__
        if (wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &privateControl_)) ObserveNativeDrags(&privateControl_);
#endif
        if (argc == 2 && argv[1] == "--input-state-probe") return InitInputStateProbe();
        if (argc == 2 && argv[1] == "--file-drop") {
            auto* frame = new EditorFrame;
            frame->SetTitle("wxnotepad stream drag main");
            frame->Bind(wxEVT_IDLE, [frame](wxIdleEvent& event) {
                frame->SetTitle("wxnotepad stream drag main");
                event.Skip();
            });
            wxString backend;
            if (wxGetEnv("GDK_BACKEND", &backend) && backend == "wayland") {
                frame->SetClientSize(760, 700);
                frame->Show();
            } else
                frame->ShowFullScreen(true);
            CallAfter([this, frame] {
                try {
                    NativeFileDrop(frame);
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "FAIL: %s\n", error.what());
                    result_ = 1;
                }
                frame->Destroy();
                ExitMainLoop();
            });
            return true;
        }
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
            return InitPeer(argv[2], delayOffset);
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
                SelfDrags(frame, editor);
                IncomingPeerDrags(frame, editor, peer);
                OutgoingPeerDrags(frame, editor, peer);
                DragScrollCancellation(frame, editor);
                IndependentProcessDrags(frame, editor);
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
    bool InitPeer(const wxString& directory, bool delayOffset) {
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
        } else if (wxFileExists(directory + "/files") && !peer->IsDragging()) {
            wxFFile source(directory + "/files", "rb");
            wxString paths;
            Require(source.ReadAll(&paths, wxConvUTF8), "read peer's native file offer");
            source.Close();
            peer->OfferFiles(wxSplit(paths, '\n'));
            wxRemoveFile(directory + "/files");
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
    wxString privateControl_;
    TestClock::StopWatch offsetDelay_;
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(DragApp);
