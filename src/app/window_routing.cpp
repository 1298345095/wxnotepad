// Native routing follows Notepad4 89b8b36 ActivatePrevInst/ActivatePrevWindow.
#include "app/window_routing.h"
#include "app/editor_frame.h"
#include "editor/editor_control.h"
#include <wx/app.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>
#include <wx/base64.h>
#include <wx/modalhook.h>
#include <vector>
#include <filesystem>
#include <limits>
#ifdef __WXMSW__
#include <windows.h>
#else
#include <wx/sckipc.h>
#include <wx/dir.h>
#include <wx/stdpaths.h>
#include <sys/stat.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <gdk/gdkx.h>
#endif

namespace {
constexpr size_t MaximumPacketBytes = 1024 * 1024;

enum class ReceiverState { Unavailable = 0, Available = 1, Busy = 2 };
enum class DeliveryReply {
    Unacknowledged = 0,
    Activated = 1,
    Busy = 2,
    Closing = 3,
    Opened = 4,
    Cancelled = 5,
    Failed = 6
};

ReceiverState DecodeState(long long value) {
    if (value == std::to_underlying(ReceiverState::Unavailable)) return ReceiverState::Unavailable;
    if (value == std::to_underlying(ReceiverState::Busy)) return ReceiverState::Busy;
    return ReceiverState::Available;
}

DeliveryReply DecodeReply(long long value) {
    switch (value) {
    case std::to_underlying(DeliveryReply::Activated):
        return DeliveryReply::Activated;
    case std::to_underlying(DeliveryReply::Busy):
        return DeliveryReply::Busy;
    case std::to_underlying(DeliveryReply::Closing):
        return DeliveryReply::Closing;
    case std::to_underlying(DeliveryReply::Opened):
        return DeliveryReply::Opened;
    case std::to_underlying(DeliveryReply::Cancelled):
        return DeliveryReply::Cancelled;
    case std::to_underlying(DeliveryReply::Failed):
        return DeliveryReply::Failed;
    default:
        return DeliveryReply::Unacknowledged;
    }
}

wxString EncodeRequest(const WindowRequest& request) {
    const auto path = request.startup.utf8_string();
    const auto startup = request.startupId.utf8_string();
    wxString source;
    if (std::holds_alternative<DocumentLoad::NativeAnsiSource>(request.source))
        source = "ANSI";
    else if (const auto* encoding = std::get_if<EncodingChoice>(&request.source))
        source = wxString::Format("%d", EncodingSetting(*encoding));
    return "open\n" + wxBase64Encode(path.data(), path.size()) + "\n" +
           (request.encoding ? wxString::Format("%d", EncodingSetting(*request.encoding)) : wxString{}) + "\n" +
           (request.scheme ? wxString::Format("%d", std::to_underlying(*request.scheme)) : wxString{}) + "\n" + source +
           wxString::Format("\n%d\n%lld\n%lld\n%d\n", request.readOnly, static_cast<long long>(request.line),
                            static_cast<long long>(request.column), request.ordinaryFile) +
           (request.initialLocation
                ? wxString::Format("%lld\n%lld\n%u", static_cast<long long>(request.initialLocation->line),
                                   static_cast<long long>(request.initialLocation->column),
                                   request.initialLocation->columnType)
                : wxString("\n\n")) +
           "\n" + wxBase64Encode(startup.data(), startup.size());
}

std::optional<WindowRequest> DecodeRequest(const wxString& packet) {
    if (packet.length() > MaximumPacketBytes) return std::nullopt;
    const auto parts = wxSplit(packet, '\n', '\0');
    if (parts.size() != 13 || parts[0] != "open") return std::nullopt;
    const auto path = wxBase64Decode(parts[1]);
    WindowRequest request;
    request.startup = wxString::FromUTF8(static_cast<const char*>(path.GetData()), path.GetDataLen());
    if (request.startup.empty() || request.startup.find('\0') != wxString::npos ||
        !wxFileName(request.startup).IsAbsolute())
        return std::nullopt;
    const auto encoding = [](const wxString& text) -> std::optional<EncodingChoice> {
        long setting = 0;
        if (!text.ToLong(&setting) || setting < std::numeric_limits<int>::min() ||
            setting > std::numeric_limits<int>::max())
            return std::nullopt;
        return DefaultEncodingChoice(static_cast<int>(setting));
    };
    if (!parts[2].empty()) {
        request.encoding = encoding(parts[2]);
        if (!request.encoding) return std::nullopt;
    }
    if (!parts[3].empty()) {
        unsigned long scheme = 0;
        if (!parts[3].ToULong(&scheme) || scheme >= std::to_underlying(SyntaxLanguage::Count)) return std::nullopt;
        request.scheme = static_cast<SyntaxLanguage>(scheme);
    }
    if (parts[4] == "ANSI")
        request.source = DocumentLoad::NativeAnsiSource{};
    else if (!parts[4].empty()) {
        const auto source = encoding(parts[4]);
        if (!source) return std::nullopt;
        request.source = *source;
    }
    if ((parts[5] != "0" && parts[5] != "1") || (parts[8] != "0" && parts[8] != "1")) return std::nullopt;
    request.readOnly = parts[5] == "1";
    request.ordinaryFile = parts[8] == "1";
    wxLongLong_t line = 0, column = 0;
    if (!parts[6].ToLongLong(&line) || !parts[7].ToLongLong(&column)) return std::nullopt;
    request.line = static_cast<Scintilla::Position>(line);
    request.column = static_cast<Scintilla::Position>(column);
    if (!parts[9].empty()) {
        unsigned long type = 0;
        if (!parts[9].ToLongLong(&line) || !parts[10].ToLongLong(&column) || !parts[11].ToULong(&type) ||
            type > std::numeric_limits<unsigned>::max())
            return std::nullopt;
        request.initialLocation =
            InitialLocation{static_cast<intptr_t>(line), static_cast<intptr_t>(column), static_cast<unsigned>(type)};
    } else if (!parts[10].empty() || !parts[11].empty())
        return std::nullopt;
    const auto startup = wxBase64Decode(parts[12]);
    request.startupId = wxString::FromUTF8(static_cast<const char*>(startup.GetData()), startup.GetDataLen());
    return request;
}

bool SameFile(const wxString& first, const wxString& second) {
    if (first.empty() || second.empty()) return false;
    const auto nativePath = [](const wxString& path) {
#ifdef __WXMSW__
        return std::filesystem::path(path.ToStdWstring());
#else
        return std::filesystem::path(path.utf8_string());
#endif
    };
    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(nativePath(first), nativePath(second), error);
    if (!error) return equivalent;
    return wxFileName(first).SameAs(wxFileName(second));
}

bool OpenAnotherWindow() {
    // TRANSLATORS: Original IDS_ERR_PREVWINDISABLED; wxnotepad is the application name.
    return wxMessageBox(_("Existing wxnotepad window is busy or has an active dialog box. Would you like to open "
                          "another wxnotepad window?"),
                        "wxnotepad", wxYES_NO | wxICON_QUESTION) == wxYES;
}

WindowRouting::Result DeliveryResult(DeliveryReply reply) {
    if (reply == DeliveryReply::Busy)
        return OpenAnotherWindow() ? WindowRouting::Result::NewWindow : WindowRouting::Result::Handled;
    if (reply == DeliveryReply::Closing) return WindowRouting::Result::NewWindow; // Rejected before delivery.
    if (reply == DeliveryReply::Activated || reply == DeliveryReply::Opened || reply == DeliveryReply::Cancelled ||
        reply == DeliveryReply::Failed)
        return WindowRouting::Result::Handled;
    // No acknowledgement is an uncertain delivery: never execute it again.
    return WindowRouting::Result::Failed;
}

#ifdef __WXMSW__
constexpr wchar_t ReceiverClass[] = L"wxnotepad.WindowRouting.1";
constexpr UINT QueryMessage = WM_APP + 277;
constexpr ULONG_PTR PacketVersion = 1;
constexpr UINT QueryTimeoutMilliseconds = 3000;

bool SameUser(HWND window) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    HANDLE foreign = nullptr;
    HANDLE own = nullptr;
    bool same = false;
    if (OpenProcessToken(process, TOKEN_QUERY, &foreign) && OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own)) {
        DWORD foreignSize = 0;
        DWORD ownSize = 0;
        GetTokenInformation(foreign, TokenUser, nullptr, 0, &foreignSize);
        GetTokenInformation(own, TokenUser, nullptr, 0, &ownSize);
        std::vector<unsigned char> foreignData(foreignSize), ownData(ownSize);
        if (GetTokenInformation(foreign, TokenUser, foreignData.data(), foreignSize, &foreignSize) &&
            GetTokenInformation(own, TokenUser, ownData.data(), ownSize, &ownSize))
            same = EqualSid(reinterpret_cast<TOKEN_USER*>(foreignData.data())->User.Sid,
                            reinterpret_cast<TOKEN_USER*>(ownData.data())->User.Sid) != FALSE;
    }
    if (foreign) CloseHandle(foreign);
    if (own) CloseHandle(own);
    CloseHandle(process);
    return same;
}
#else
wxString SessionDirectory() {
    wxString runtime;
    if (!wxGetEnv("XDG_RUNTIME_DIR", &runtime) || runtime.empty()) runtime = wxFileName::GetTempDir();
    wxString display, wayland, session;
    wxGetEnv("DISPLAY", &display);
    wxGetEnv("WAYLAND_DISPLAY", &wayland);
    wxGetEnv("XDG_SESSION_ID", &session);
    // Include actual display identities: private sessions can share a login ID.
    constexpr uint64_t FnvOffsetBasis = 14695981039346656037ULL;
    constexpr uint64_t FnvPrime = 1099511628211ULL;
    uint64_t hash = FnvOffsetBasis;
    for (const unsigned char ch : (display + "\n" + wayland + "\n" + session).utf8_string()) {
        hash ^= ch;
        hash *= FnvPrime;
    }
    return wxFileName(runtime, wxString::Format("wxnotepad-%lu-%016llx", static_cast<unsigned long>(getuid()),
                                                static_cast<unsigned long long>(hash)))
        .GetFullPath();
}

bool PrivateDirectory(const wxString& path) {
    struct stat info{};
    return lstat(path.utf8_str(), &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == getuid() &&
           (info.st_mode & 077) == 0;
}

class ClientConnection final : public wxTCPConnection {
public:
    ClientConnection() = default;
    void SetRequestTimeout() { m_sock->SetTimeout(3); }
    bool OnDisconnect() override { return true; }
};
class Client final : public wxTCPClient {
public:
    wxConnectionBase* OnMakeConnection() override { return new ClientConnection; }
};

wxString Exchange(wxConnectionBase& connection, const wxString& command) {
    size_t size = 0;
    const auto* data = static_cast<const char*>(connection.Request(command, &size, wxIPC_UTF8TEXT));
    return data && size ? wxString::FromUTF8(data, size) : wxString{};
}
#endif
} // namespace

struct WindowRouting::Impl final : wxEvtHandler {
    EditorFrame* frame;
    bool receiving = false;
#ifdef __WXMSW__
    HWND window = nullptr;
    static LRESULT CALLBACK WindowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == QueryMessage) {
            SetWindowTextW(handle, self->frame ? self->frame->DocumentPath().wc_str() : L"");
            return std::to_underlying(self->State());
        }
        if (self && message == WM_COPYDATA) {
            const auto* packet = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            if (!packet || packet->dwData != PacketVersion || !packet->lpData || packet->cbData > MaximumPacketBytes)
                return std::to_underlying(DeliveryReply::Unacknowledged);
            return std::to_underlying(
                self->Handle(wxString::FromUTF8(static_cast<const char*>(packet->lpData), packet->cbData)));
        }
        return DefWindowProcW(handle, message, wParam, lParam);
    }
#else
    struct Connection final : wxTCPConnection {
        Impl& owner;
        std::string reply;
        explicit Connection(Impl& receiver) : owner(receiver) {}
        const void* OnRequest(const wxString&, const wxString& item, size_t* size, wxIPCFormat format) override {
            if (format != wxIPC_UTF8TEXT) return nullptr;
            const auto response = item == "query" ? wxString::Format("%d\n", std::to_underlying(owner.State())) +
                                                        (owner.frame ? owner.frame->DocumentPath() : wxString{})
                                                  : wxString::Format("%d", std::to_underlying(owner.Handle(item)));
            reply = response.utf8_string();
            *size = reply.size();
            return reply.data();
        }
    };
    struct Server final : wxTCPServer {
        Impl& owner;
        explicit Server(Impl& receiver) : owner(receiver) {}
        wxConnectionBase* OnAcceptConnection(const wxString& topic) override {
            return topic == "wxnotepad.1" ? new Connection(owner) : nullptr;
        }
    } server{*this};
    bool listening = false;
#endif

    explicit Impl(EditorFrame& receiver) : frame(&receiver) {
        frame->Bind(wxEVT_DESTROY, &Impl::OnDestroy, this);
#ifdef __WXMSW__
        WNDCLASSW type{};
        type.lpfnWndProc = WindowProc;
        type.hInstance = GetModuleHandleW(nullptr);
        type.lpszClassName = ReceiverClass;
        RegisterClassW(&type);
        window = CreateWindowExW(0, ReceiverClass, L"", 0, 0, 0, 0, 0, nullptr, nullptr, type.hInstance, this);
#else
        const auto directory = SessionDirectory();
        if (!wxDirExists(directory) && !wxFileName::Mkdir(directory, 0700)) return;
        if (!PrivateDirectory(directory)) return;
        const auto endpoint = wxFileName(directory, wxString::Format("%lu.sock", wxGetProcessId())).GetFullPath();
        // wxTCPServer removes only this process's unique Unix-domain socket.
        listening = server.Create(endpoint);
#endif
    }
    ~Impl() override {
        if (frame) frame->Unbind(wxEVT_DESTROY, &Impl::OnDestroy, this);
#ifdef __WXMSW__
        if (window) DestroyWindow(window);
#endif
    }
    void OnDestroy(wxWindowDestroyEvent& event) {
        if (event.GetEventObject() == frame) frame = nullptr;
        event.Skip();
    }
    ReceiverState State() const {
        if (!frame || frame->IsBeingDeleted() || wxTheApp->IsScheduledForDestruction(frame))
            return ReceiverState::Unavailable;
        // GTK modal windows use a native grab without changing IsEnabled().
        return receiving || !frame->IsEnabled() || wxModalDialogHook::GetOpenCount() != 0 ? ReceiverState::Busy
                                                                                          : ReceiverState::Available;
    }
    DeliveryReply Handle(const wxString& command) {
        const auto state = State();
        if (state != ReceiverState::Available)
            return state == ReceiverState::Busy ? DeliveryReply::Busy : DeliveryReply::Closing;
        const bool activate = command.StartsWith("activate\n");
        const auto request = activate ? std::optional<WindowRequest>{} : DecodeRequest(command);
        if (!activate && !request) return DeliveryReply::Unacknowledged;
        wxString startupId;
        if (activate) {
            const auto decoded = wxBase64Decode(command.Mid(9));
            startupId = wxString::FromUTF8(static_cast<const char*>(decoded.GetData()), decoded.GetDataLen());
        } else
            startupId = request->startupId;
        receiving = true;
#ifdef __WXGTK__
        if (!startupId.empty()) {
            auto* widget = GTK_WIDGET(frame->GetHandle());
            auto* display = gtk_widget_get_display(widget);
            if (GDK_IS_WAYLAND_DISPLAY(display)) {
                WindowRouting::SetStartupId(startupId);
            } else if (GDK_IS_X11_DISPLAY(display)) {
                // The synthetic _TIME form presents itself in GTK. Pass that
                // timestamp directly so Raise below remains the focus request.
                if (startupId.StartsWith("_TIME")) {
                    unsigned long time = 0;
                    if (startupId.Mid(5).ToULong(&time) && time <= std::numeric_limits<guint32>::max())
                        gdk_x11_window_set_user_time(gtk_widget_get_window(widget), static_cast<guint32>(time));
                } else
                    gtk_window_set_startup_id(GTK_WINDOW(widget), startupId.utf8_str());
            }
        }
#endif
        frame->Iconize(false);
        // wxMSW Raise calls SetForegroundWindow once; GTK presents through its
        // compositor. Do not add another activation call after this one.
        frame->Raise();
        frame->Editor()->SetFocus();
        auto reply = DeliveryReply::Activated;
        if (request) {
            const auto opened = frame->ReceiveWindowRequest(*request);
            reply = opened == OpenStatus::Opened      ? DeliveryReply::Opened
                    : opened == OpenStatus::Cancelled ? DeliveryReply::Cancelled
                                                      : DeliveryReply::Failed;
        }
        receiving = false;
        return reply;
    }
};

WindowRouting::WindowRouting(EditorFrame& frame) : impl_(std::make_unique<Impl>(frame)) {
}
WindowRouting::~WindowRouting() = default;
wxString WindowRouting::StartupId(const wxString& desktopId, const wxString& waylandToken) {
#ifdef __WXGTK__
    if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default()) && !waylandToken.empty()) return waylandToken;
    return desktopId;
#else
    (void)desktopId;
    (void)waylandToken;
    return {};
#endif
}
void WindowRouting::SetStartupId(const wxString& id) {
#ifdef __WXGTK__
    auto* display = gdk_display_get_default();
    if (!id.empty() && GDK_IS_WAYLAND_DISPLAY(display))
        gdk_wayland_display_set_startup_notification_id(display, id.utf8_str());
#else
    (void)id;
#endif
}
bool WindowRouting::IsOk() const {
#ifdef __WXMSW__
    return impl_->window != nullptr;
#else
    return impl_->listening;
#endif
}

WindowRouting::Result WindowRouting::Route(const WindowRequest& request, bool reuseWindow, bool singleFileInstance) {
    const bool sameFile = singleFileInstance && !request.startup.empty();
    if (!sameFile && !reuseWindow) return Result::NewWindow;
    const auto startup = request.startupId.utf8_string();
    const wxString command = sameFile || request.startup.empty()
                                 ? "activate\n" + wxBase64Encode(startup.data(), startup.size())
                                 : EncodeRequest(request);
    const wxLogNull noGuiLog;
#ifdef __WXMSW__
    std::vector<HWND> windows;
    EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
            wchar_t name[64]{};
            GetClassNameW(window, name, static_cast<int>(std::size(name)));
            if (wcscmp(name, ReceiverClass) == 0 && SameUser(window))
                reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    HWND chosen = nullptr;
    bool busy = false;
    for (const auto window : windows) {
        DWORD_PTR state = 0;
        if (!SendMessageTimeoutW(window, QueryMessage, 0, 0, SMTO_ABORTIFHUNG, QueryTimeoutMilliseconds, &state) ||
            DecodeState(static_cast<long long>(state)) == ReceiverState::Unavailable)
            continue;
        const int length = GetWindowTextLengthW(window);
        std::vector<wchar_t> title(static_cast<size_t>(length) + 1);
        GetWindowTextW(window, title.data(), length + 1);
        if (sameFile && !SameFile(request.startup, wxString(title.data()))) continue;
        chosen = window;
        busy = DecodeState(static_cast<long long>(state)) == ReceiverState::Busy;
        if (!busy) break;
    }
    if (!chosen) return Result::NewWindow;
    if (busy) return OpenAnotherWindow() ? Result::NewWindow : Result::Handled;
    DWORD pid = 0;
    GetWindowThreadProcessId(chosen, &pid);
    AllowSetForegroundWindow(pid);
    const auto bytes = command.utf8_string();
    COPYDATASTRUCT packet{PacketVersion, static_cast<DWORD>(bytes.size()), const_cast<char*>(bytes.data())};
    const auto reply = SendMessageW(chosen, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&packet));
    return DeliveryResult(DecodeReply(reply));
#else
    const auto directory = SessionDirectory();
    if (!PrivateDirectory(directory)) return Result::NewWindow;
    wxDir entries(directory);
    wxString entry;
    std::unique_ptr<wxConnectionBase> chosen;
    Client client;
    bool busy = false;
    for (bool found = entries.GetFirst(&entry, "*.sock", wxDIR_FILES); found; found = entries.GetNext(&entry)) {
        std::unique_ptr<wxConnectionBase> connection(
            client.MakeConnection("", wxFileName(directory, entry).GetFullPath(), "wxnotepad.1"));
        if (!connection) continue;
        static_cast<ClientConnection*>(connection.get())->SetRequestTimeout();
        const auto reply = Exchange(*connection, "query");
        if (reply.length() < 2) continue;
        const auto state = DecodeState(reply[0].GetValue() - '0');
        if (state == ReceiverState::Unavailable || (sameFile && !SameFile(request.startup, reply.Mid(2)))) continue;
        busy = state == ReceiverState::Busy;
        chosen = std::move(connection);
        if (!busy) break;
    }
    if (!chosen) return Result::NewWindow;
    if (busy) return OpenAnotherWindow() ? Result::NewWindow : Result::Handled;
    const auto reply = Exchange(*chosen, command);
    return DeliveryResult(reply.length() == 1 ? DecodeReply(reply[0].GetValue() - '0') : DeliveryReply::Unacknowledged);
#endif
}
