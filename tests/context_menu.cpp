#include "app/editor_frame.h"
#include "app/localization.h"
#include "editor/editor_control.h"
#include "editor_test_helpers.h"
#include "native_key_helpers.h"
#include "file_helpers.h"
#include "native_paint_helpers.h"
#include <wx/wx.h>
#include <wx/menu.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/display.h>
#include <fstream>
#include <chrono>
#include <functional>
#include <set>

namespace {
using enum Scintilla::Message;
constexpr unsigned EnglishLanguage = 1033;
constexpr unsigned SimplifiedChineseLanguage = 2052;
constexpr unsigned TraditionalChineseLanguage = 1028;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void Focus(EditorFrame* frame) {
    wxYield();
    frame->SetTitle("wxnotepad context menu");
#ifdef __WXMSW__
    NativeFocus::Activate(static_cast<HWND>(frame->GetHandle()), static_cast<HWND>(frame->Editor()->GetHandle()));
#else
    wxString focusFile;
    NativeKeys::FocusEditor(frame, "wxnotepad context menu", focusFile);
#endif
}
#ifdef __WXGTK__
void NativeRequest(const wxString& command) {
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile), "native input uses private display driver");
    {
        wxFFile request(focusFile + ".tmp", "wb");
        Require(request.IsOpened() && request.Write(command), "write complete owned native-input request");
    }
    Require(wxRenameFile(focusFile + ".tmp", focusFile, true), "atomically publish native-input request");
}
#endif
void NativeClick(EditorFrame* frame, wxPoint screen, unsigned button) {
#ifdef __WXMSW__
    Require(GetForegroundWindow() == static_cast<HWND>(frame->GetHandle()),
            "native mouse targets owned foreground frame");
    const auto target = WindowFromPoint(POINT{screen.x, screen.y});
    DWORD process = 0;
    GetWindowThreadProcessId(target, &process);
    const auto editor = static_cast<HWND>(frame->Editor()->GetHandle());
    if (process != GetCurrentProcessId() || (target != editor && !IsChild(editor, target))) {
        wchar_t name[128]{};
        if (process == GetCurrentProcessId()) GetClassNameW(target, name, 128);
        std::fprintf(stderr, "Native mouse target=%p owned=%d class=%ls editor=%p; completion=%zd\n", target,
                     process == GetCurrentProcessId(), name, editor, frame->Editor()->Send(AutoCActive));
        throw std::runtime_error("native pointer must reach the owned editor client area");
    }
    Require(SetCursorPos(screen.x, screen.y), "native pointer moves to owned test window");
    INPUT input[2]{};
    input[0].type = input[1].type = INPUT_MOUSE;
    input[0].mi.dwFlags = button == 3 ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
    input[1].mi.dwFlags = button == 3 ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
    Require(SendInput(2, input, sizeof(INPUT)) == 2, "native mouse delivers one press and release");
#else
    NativeRequest(wxString::Format("click:%d,%d,%u", screen.x, screen.y, button));
#endif
}

class ClipboardPeer final {
public:
    explicit ClipboardPeer(EditorFrame* frame) : frame_(frame) {
        directory_ = TestFiles::Filename(TestFiles::TemporaryDirectory("context-clipboard"));
        Require(wxMkdir(directory_), "create owned clipboard peer IPC directory");
        wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
        executable.SetName("clipboard_peer");
        process_ = wxExecute("\"" + executable.GetFullPath() + "\" \"" + directory_ + "\"", wxEXEC_ASYNC);
        Require(process_ > 0, "launch independent stock-widget clipboard peer");
        Require(Request("ready") == "ready", "clipboard peer maps its native window");
    }
    ClipboardPeer(const ClipboardPeer&) = delete;
    ClipboardPeer& operator=(const ClipboardPeer&) = delete;
    ~ClipboardPeer() {
        try {
            Request("quit");
        } catch (...) {
        }
    }
    wxString Request(const wxString& command) {
        if (wxFileExists(directory_ + "/response")) wxRemoveFile(directory_ + "/response");
        {
            wxFFile file(directory_ + "/request.tmp", "wb");
            Require(file.IsOpened() && file.Write(command), "write clipboard peer request");
        }
        Require(wxRenameFile(directory_ + "/request.tmp", directory_ + "/request", true), "publish clipboard request");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!wxFileExists(directory_ + "/response") && std::chrono::steady_clock::now() < deadline) {
            wxYield();
            wxMilliSleep(10);
        }
        wxString response;
        wxFFile file(directory_ + "/response", "rb");
        Require(file.IsOpened() && file.ReadAll(&response), "independent clipboard peer responds before deadline");
        if (response == "failed" || response.Contains("unavailable")) {
            wxString stage;
            wxFFile stageFile(directory_ + "/peer-stage", "rb");
            if (stageFile.IsOpened()) stageFile.ReadAll(&stage, wxConvUTF8);
            std::fprintf(stderr, "Peer request=%s response=%s stage=%s requestPending=%d\n",
                         command.utf8_string().c_str(), response.utf8_string().c_str(), stage.utf8_string().c_str(),
                         wxFileExists(directory_ + "/request"));
#ifdef __WXMSW__
            DWORD foregroundProcess = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
            DWORD activeProcess = 0, focusProcess = 0;
            GetWindowThreadProcessId(GetActiveWindow(), &activeProcess);
            GetWindowThreadProcessId(::GetFocus(), &focusProcess);
            std::fprintf(stderr,
                         "Peer failure foreground=%p peerForeground=%d editorForeground=%d "
                         "editorActive=%d editorFocus=%d peerAlive=%d active=%p focus=%p "
                         "foregroundPid=%lu activePid=%lu focusPid=%lu editorPid=%lu peerPid=%ld\n",
                         GetForegroundWindow(), foregroundProcess == static_cast<DWORD>(process_),
                         foregroundProcess == GetCurrentProcessId(), frame_->IsActive(), frame_->Editor()->HasFocus(),
                         wxProcess::Exists(static_cast<int>(process_)), GetActiveWindow(), ::GetFocus(),
                         foregroundProcess, activeProcess, focusProcess, GetCurrentProcessId(), process_);
#endif
            throw std::runtime_error("independent peer " + command.ToStdString() + ": " + response.ToStdString());
        }
        Focus(frame_);
        response.Replace("\r\n", "\n");
        return response;
    }
    void Offer(const wxString& text) {
        {
            wxFFile file(directory_ + "/offer.txt", "wb");
            Require(file.Write(text), "write literal clipboard offer");
        }
        Require(Request("utility-offer") == "ready", "independent peer offers native Unicode clipboard text");
    }

private:
    EditorFrame* frame_;
    long process_ = 0;
    wxString directory_;
};

// Observe the actual native popup, using wx's public menu-open notification.
// The popup remains modal until the native platform dismisses it.
class Popup final : public wxEvtHandler {
public:
    explicit Popup(EditorFrame* frame, unsigned language = EnglishLanguage) : frame_(frame), language_(language) {
        frame_->Bind(wxEVT_MENU_OPEN, &Popup::Opened, this);
        frame_->Bind(wxEVT_MENU, &Popup::Command, this);
        frame_->Editor()->Bind(wxEVT_RIGHT_DCLICK, &Popup::RightDouble, this);
#ifdef __WXGTK__
        frame_->Editor()->Bind(wxEVT_KEY_UP, &Popup::EditorKeyReleased, this);
#endif
    }
    ~Popup() override {
#ifdef __WXGTK__
        try {
            CleanupInvocationKey();
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Owned popup key release failed: %s\n", error.what());
        }
        frame_->Editor()->Unbind(wxEVT_KEY_UP, &Popup::EditorKeyReleased, this);
#endif
        frame_->Unbind(wxEVT_MENU_OPEN, &Popup::Opened, this);
        frame_->Unbind(wxEVT_MENU, &Popup::Command, this);
        frame_->Editor()->Unbind(wxEVT_RIGHT_DCLICK, &Popup::RightDouble, this);
    }
    void Open(const wxPoint& screen, int key = 0) {
        if (screen == wxDefaultPosition && key == 0) key = WXK_MENU;
        std::printf("BEGIN: popup key=%d point=%d,%d action=%d\n", key, screen.x, screen.y, action);
        std::fflush(stdout);
        observed_ = 0;
        dispatches_ = 0;
        failure_ = {};
        driven_ = false;
        nativeReadyScheduled_ = false;
        nativeBounds_.reset();
#ifdef __WXGTK__
        openedState_.clear();
        drivenState_.clear();
        keyState_.clear();
        lifecycle_.clear();
        heldInvocationKey_.clear();
        invocationReleases_.clear();
        invocationReleaseRequested_ = false;
        nativeMenuOpen_ = false;
#endif
        if (key) {
            InvokeKeyboard(key);
        } else {
            NativeClick(frame_, screen, 3);
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
#ifdef __WXGTK__
        invocationDeadline_ = deadline;
#endif
        do {
            wxYield();
            wxMilliSleep(10);
        } while (!observed_ && std::chrono::steady_clock::now() < deadline);
        if (failure_) std::rethrow_exception(failure_);
        if (observed_ != 1)
            std::fprintf(stderr, "Observed native popup count: %d; right double-clicks: %d\n", observed_, rightDouble_);
        if (observed_ != 1 || (action != wxID_NONE && dispatches_ != 1)) {
#ifdef __WXGTK__
            std::fprintf(stderr, "Native popup observations: opened=[%s] drive=[%s] key=[%s]\n", openedState_.c_str(),
                         drivenState_.c_str(), keyState_.c_str());
            std::fprintf(stderr, "Native popup lifecycle: %s\n", lifecycle_.c_str());
#endif
        }
        Require(observed_ == 1, "one native editor popup opens for each invocation");
        if (action != wxID_NONE && dispatches_ != 1)
            std::fprintf(stderr, "Native command id=%d dispatch count=%d\n", action, dispatches_);
        Require(action == wxID_NONE || dispatches_ == 1, "native popup dispatches selected command exactly once");
#ifdef __WXGTK__
        CleanupInvocationKey();
#endif
    }
    std::function<void(wxMenu&)> inspect;
    int action = wxID_NONE;
    bool outside = false;
    std::optional<wxPoint> anchor;

private:
    void InvokeKeyboard(int key) {
        auto* editor = frame_->Editor();
        const auto focusDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while ((!editor->HasFocus() || !frame_->IsActive()) && std::chrono::steady_clock::now() < focusDeadline) {
            wxYield();
            wxMilliSleep(10);
        }
        Require(editor->HasFocus() && frame_->IsActive(),
                "native popup keyboard gesture retains existing editor focus");
#ifdef __WXMSW__
        Require(GetForegroundWindow() == static_cast<HWND>(frame_->GetHandle()),
                "native context-menu keys target the owned foreground editor");
        INPUT input[4]{};
        const UINT count = key == WXK_MENU ? 2 : 4;
        for (auto& item : input)
            item.type = INPUT_KEYBOARD;
        if (key == WXK_MENU) {
            input[0].ki.wVk = input[1].ki.wVk = VK_APPS;
            input[1].ki.dwFlags = KEYEVENTF_KEYUP;
        } else {
            input[0].ki.wVk = input[3].ki.wVk = VK_SHIFT;
            input[1].ki.wVk = input[2].ki.wVk = VK_F10;
            input[2].ki.dwFlags = input[3].ki.dwFlags = KEYEVENTF_KEYUP;
        }
        Require(SendInput(count, input, sizeof(INPUT)) == count, "native keyboard invokes context menu");
#else
        const wxString gesture = key == WXK_MENU ? "Menu" : "shift+F10";
        if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(static_cast<GtkWidget*>(editor->GetHandle())))) {
            // A release can invalidate the press serial while GTK creates
            // its native popup. Complete this one physical gesture only
            // after the popup is ready, then observe its real key-up(s).
            heldInvocationKey_ = gesture;
            invocationReleases_.insert(key);
            if (key == WXK_F10) invocationReleases_.insert(WXK_SHIFT);
            NativeRequest("keydown:" + gesture);
        } else {
            NativeRequest("key:" + gesture);
        }
#endif
    }

    EditorFrame* frame_;
    unsigned language_;
    int observed_ = 0;
    int dispatches_ = 0;
    void Command(wxCommandEvent& event) {
        if (event.GetId() == action) ++dispatches_;
        event.Skip();
    }
    int rightDouble_ = 0;
    bool driven_ = false;
    bool nativeReadyScheduled_ = false;
    std::optional<wxRect> nativeBounds_;
    wxMenu* activeMenu_ = nullptr;
    void RightDouble(wxMouseEvent& event) {
        ++rightDouble_;
        event.Skip();
    }
    std::exception_ptr failure_;
    void Opened(wxMenuEvent& event) {
        auto* menu = event.GetMenu();
        if (!menu || !event.IsPopup()) {
            event.Skip();
            return;
        }
        ++observed_;
#ifdef __WXGTK__
        openedState_ = NativeMenuState(menu->m_menu);
        nativeMenuOpen_ = true;
        ObserveLifecycle(menu->m_menu, "opened");
        for (const char* signal : {"hide", "unmap", "deactivate"}) {
            g_signal_connect(menu->m_menu, signal, G_CALLBACK(+[](GtkWidget* widget, gpointer data) {
                                 auto* popup = static_cast<Popup*>(data);
                                 popup->ObserveLifecycle(widget, "closed");
                                 popup->nativeMenuOpen_ = false;
                             }),
                             this);
        }
        g_signal_connect(menu->m_menu, "key-press-event",
                         G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* key, gpointer data) -> gboolean {
                             auto* popup = static_cast<Popup*>(data);
                             popup->keyState_ = popup->NativeMenuState(widget) +
                                                "; keyval=" + std::to_string(key->keyval) +
                                                " time=" + std::to_string(key->time);
                             return FALSE;
                         }),
                         this);
        g_signal_connect(
            menu->m_menu, "key-release-event",
            G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* key, gpointer data) -> gboolean {
                auto* popup = static_cast<Popup*>(data);
                if (!popup->invocationReleases_.empty() &&
                    (key->keyval == GDK_KEY_Menu || key->keyval == GDK_KEY_F10 || key->keyval == GDK_KEY_Shift_L)) {
                    popup->ObserveLifecycle(widget, "invocation-release");
                    popup->InvocationKeyReleased(key->keyval == GDK_KEY_Menu  ? WXK_MENU
                                                 : key->keyval == GDK_KEY_F10 ? WXK_F10
                                                                              : WXK_SHIFT);
                }
                return FALSE;
            }),
            this);
#endif
        try {
            const char* const english[] = {"&Undo", "&Redo",       "", "Cu&t",         "&Copy", "&Paste", "&Delete",
                                           "",      "Select &All", "", "Toggle &Fold", "",      "&Open"};
            const char* const simplified[] = {"撤销(&U)",     "重做(&R)", "",        "剪切(&T)", "复制(&C)",
                                              "粘贴(&P)",     "删除(&D)", "",        "全选(&A)", "",
                                              "切换折叠(&F)", "",         "打开(&O)"};
            const char* const traditional[] = {"復原(&U)",     "重做(&R)", "",        "剪下(&T)", "複製(&C)",
                                               "貼上(&P)",     "刪除(&D)", "",        "全選(&A)", "",
                                               "切換摺疊(&F)", "",         "開啟(&O)"};
            const auto* expected = language_ == SimplifiedChineseLanguage    ? simplified
                                   : language_ == TraditionalChineseLanguage ? traditional
                                                                             : english;
            Require(menu->GetMenuItemCount() == std::size(english), "original popup has all entries and separators");
            for (size_t index = 0; index < std::size(english); ++index) {
                const auto* item = menu->FindItemByPosition(index);
                Require(*expected[index] ? item->GetItemLabel() == wxString::FromUTF8(expected[index])
                                         : item->IsSeparator(),
                        "native popup follows original labels and grouping");
            }
            if (inspect) inspect(*menu);
        } catch (...) {
            failure_ = std::current_exception();
        }
        event.Skip();
#ifdef __WXGTK__
        if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(menu->m_menu))) {
            // GtkMenu::popped-up is the public notification that Wayland's
            // asynchronous native placement is final, including edge flipping.
            activeMenu_ = menu;
            g_signal_connect(menu->m_menu, "popped-up",
                             G_CALLBACK(+[](GtkMenu* nativeMenu, const GdkRectangle*, const GdkRectangle* finalRect,
                                            gboolean, gboolean, gpointer data) {
                                 auto* popup = static_cast<Popup*>(data);
                                 popup->ObserveLifecycle(GTK_WIDGET(nativeMenu), "popped-up");
                                 if (finalRect)
                                     popup->nativeBounds_ =
                                         wxRect(finalRect->x, finalRect->y, finalRect->width, finalRect->height);
                                 if (popup->nativeReadyScheduled_) return;
                                 popup->nativeReadyScheduled_ = true;
                                 // Initial GTK placement can precede Wayland's
                                 // configure/map. Wait for the first native frame.
                                 gtk_widget_add_tick_callback(
                                     GTK_WIDGET(nativeMenu),
                                     +[](GtkWidget*, GdkFrameClock*, gpointer data) -> gboolean {
                                         auto* popup = static_cast<Popup*>(data);
                                         popup->ObserveLifecycle(popup->activeMenu_->m_menu, "tick");
                                         wxTheApp->CallAfter([popup] {
                                             if (!popup->nativeMenuOpen_) return;
                                             popup->ObserveLifecycle(popup->activeMenu_->m_menu, "call-after");
                                             if (popup->heldInvocationKey_.empty()) {
                                                 popup->Drive(popup->activeMenu_);
                                             } else {
                                                 popup->ReleaseInvocationKey();
                                             }
                                         });
                                         return G_SOURCE_REMOVE;
                                     },
                                     popup, nullptr);
                             }),
                             this);
            return;
        }
#endif
        // Unwind the menu-open notification before driving the native menu.
        wxTheApp->CallAfter([this, menu] { Drive(menu); });
    }
#ifdef __WXGTK__
    std::string openedState_, drivenState_, keyState_, lifecycle_;
    wxString heldInvocationKey_;
    std::set<int> invocationReleases_;
    bool invocationReleaseRequested_ = false;
    bool nativeMenuOpen_ = false;
    std::chrono::steady_clock::time_point invocationDeadline_;
    void CleanupInvocationKey() {
        if (heldInvocationKey_.empty()) return;
        std::fprintf(stderr, "Held popup-key cleanup: keys=%s remaining=%zu opened=[%s] drive=[%s] key=[%s]\n",
                     heldInvocationKey_.utf8_string().c_str(), invocationReleases_.size(), openedState_.c_str(),
                     drivenState_.c_str(), keyState_.c_str());
        ReleaseInvocationKey();
        while (!invocationReleases_.empty() && std::chrono::steady_clock::now() < invocationDeadline_) {
            wxYield();
            wxMilliSleep(10);
        }
        Require(invocationReleases_.empty(), "owned popup key release arrives before existing invocation deadline");
    }
    void ReleaseInvocationKey() {
        if (!invocationReleaseRequested_) {
            NativeRequest("keyup:" + heldInvocationKey_);
            invocationReleaseRequested_ = true;
        }
    }
    void InvocationKeyReleased(int key) {
        // GTK can propagate the same native release through this menu twice.
        if (invocationReleases_.erase(key) && invocationReleases_.empty()) {
            heldInvocationKey_.clear();
            if (nativeMenuOpen_)
                wxTheApp->CallAfter([this] {
                    if (nativeMenuOpen_) Drive(activeMenu_);
                });
        }
    }
    void EditorKeyReleased(wxKeyEvent& event) {
        InvocationKeyReleased(event.GetKeyCode());
        event.Skip();
    }
    void ObserveLifecycle(GtkWidget* widget, const char* phase) {
        lifecycle_ += std::string(phase) + " [scheduled=" + std::to_string(nativeReadyScheduled_) +
                      " driven=" + std::to_string(driven_) + " " + NativeMenuState(widget) + "] ";
    }
    std::string NativeMenuState(GtkWidget* widget) const {
        auto* top = gtk_widget_get_toplevel(widget);
        auto* selected = gtk_menu_get_active(GTK_MENU(widget));
        const auto* label =
            selected && GTK_IS_MENU_ITEM(selected) ? gtk_menu_item_get_label(GTK_MENU_ITEM(selected)) : "none";
        char state[512]{};
        std::snprintf(state, sizeof(state),
                      "mapped=%d parentMapped=%d active=%d focus=%d sensitive=%d grab=%p selected=%s "
                      "ownerActive=%d ownerFocus=%d coreFocus=%zd readonly=%zd",
                      gtk_widget_get_mapped(widget), gtk_widget_get_mapped(top),
                      GTK_IS_WINDOW(top) && gtk_window_is_active(GTK_WINDOW(top)), gtk_widget_has_focus(widget),
                      gtk_widget_is_sensitive(widget), static_cast<void*>(gtk_grab_get_current()), label,
                      frame_->IsActive(), frame_->Editor()->HasFocus(), frame_->Editor()->Send(GetFocus),
                      frame_->Editor()->Send(GetReadOnly));
        return state;
    }
#endif
    void Drive(wxMenu* menu) {
        if (driven_) return;
        driven_ = true;
#ifdef __WXGTK__
        drivenState_ = NativeMenuState(menu->m_menu);
#endif
        try {
            if (anchor) {
#ifdef __WXMSW__
                RECT first{}, last{};
                const auto handle = static_cast<HMENU>(menu->GetHMenu());
                Require(GetMenuItemRect(nullptr, handle, 0, &first) &&
                            GetMenuItemRect(nullptr, handle, static_cast<UINT>(menu->GetMenuItemCount() - 1), &last),
                        "native popup exposes current menu bounds");
                const wxRect bounds(first.left, first.top, first.right - first.left, last.bottom - first.top);
#else
                GdkRectangle nativeBounds{};
                gdk_window_get_frame_extents(gtk_widget_get_window(menu->m_menu), &nativeBounds);
                const auto bounds = nativeBounds_.value_or(
                    wxRect(nativeBounds.x, nativeBounds.y, nativeBounds.width, nativeBounds.height));
#endif
                std::printf("Native popup bounds: %d,%d %dx%d; caret: %d,%d\n", bounds.x, bounds.y, bounds.width,
                            bounds.height, anchor->x, anchor->y);
                const auto display = wxDisplay::GetFromWindow(frame_);
                Require(display != wxNOT_FOUND &&
                            wxDisplay(static_cast<unsigned>(display)).GetClientArea().Contains(bounds),
                        "native popup fits the current monitor work area");
                Require(wxRect(bounds).Inflate(frame_->FromDIP(4)).Contains(*anchor),
                        "native keyboard popup stays beside current caret after scrolling or wrapping");
            }
            if (outside) {
                NativeClick(frame_, frame_->Editor()->ClientToScreen(wxPoint(5, 5)), 1);
                return;
            }
#ifdef __WXMSW__
            const auto owned = static_cast<HWND>(frame_->GetHandle());
            Require(GetForegroundWindow() == owned, "native popup keyboard input belongs to owned foreground frame");
            int key = VK_ESCAPE;
            if (action != wxID_NONE) {
                auto label = menu->FindItem(action)->GetItemLabel();
                const auto mnemonic = label.find('&');
                Require(mnemonic != wxString::npos, "native popup command has an original mnemonic");
                key = static_cast<int>(wxToupper(label[mnemonic + 1]));
            }
            INPUT input[2]{};
            input[0].type = input[1].type = INPUT_KEYBOARD;
            input[0].ki.wVk = input[1].ki.wVk = static_cast<WORD>(key);
            input[1].ki.dwFlags = KEYEVENTF_KEYUP;
            Require(SendInput(2, input, sizeof(INPUT)) == 2, "native menu receives Escape or command mnemonic");
#else
            if (action != wxID_NONE) {
                const auto label = menu->FindItem(action)->GetItemLabel();
                const auto mnemonic = label.find('&');
                Require(mnemonic != wxString::npos, "native popup command has an original mnemonic");
                // A real menu gesture supplies Wayland's clipboard serial
                // after keyboard focus enters the popup surface.
                NativeRequest("key:" + wxString(label[mnemonic + 1]).Lower());
            } else {
                NativeRequest("key:Escape");
            }
#endif
        } catch (...) {
            failure_ = std::current_exception();
#ifdef __WXMSW__
            EndMenu();
#else
            gtk_menu_popdown(GTK_MENU(menu->m_menu));
#endif
        }
    }
};

void CheckClipboardCommands(EditorFrame* frame, Popup& popup, const wxPoint& screen) {
    auto* editor = frame->Editor();
    ClipboardPeer peer(frame);
    peer.Offer("owned clipboard sentinel");
    TestEditor::ResetStream(editor, "first\nsecond\n", 2, 2);
    popup.action = wxID_COPY;
    popup.Open(wxDefaultPosition);
    Require(peer.Request("paste") == "first\n",
            "empty-selection popup Copy retains original current-line clipboard behavior");
    TestEditor::RequireSelection(editor, 2, 2, "current-line Copy keeps the empty caret selection");
    editor->Send(SetSel, 5, 0);
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Lines));
    popup.Open(screen);
    const auto lineClipboard = peer.Request("paste");
    Require(lineClipboard == "first" &&
                editor->Send(GetSelectionMode) == static_cast<int>(Scintilla::SelectionMode::Lines),
            "native popup Copy preserves line-selection bytes and mode");
    TestEditor::RequireSelection(editor, 5, 0, "original line selection excludes EOL and preserves direction");
    TestEditor::ResetStream(editor, "alpha beta\nnext line", 10, 6);
    popup.action = wxID_COPY;
    popup.Open(screen);
    Require(peer.Request("paste") == "beta", "independent stock text control pastes popup Copy bytes");
    TestEditor::RequireSelection(editor, 10, 6, "Copy preserves reversed source selection");
    popup.action = wxID_CUT;
    popup.Open(wxDefaultPosition);
    const auto cutClipboard = peer.Request("paste");
    const auto cutText = TestEditor::Text(frame);
    Require(cutClipboard == "beta" && cutText == "alpha \nnext line",
            "native Cut publishes selected bytes and removes them exactly once");
    peer.Offer("owned paste");
    popup.action = wxID_PASTE;
    popup.Open(wxDefaultPosition);
    Require(TestEditor::Text(frame) == "alpha owned paste\nnext line",
            "native popup Paste inserts independent offer once");
    popup.action = wxID_SELECTALL;
    popup.Open(wxDefaultPosition);
    TestEditor::RequireSelection(editor, 27, 0, "native Select All retains original reversed whole-document selection");
    TestEditor::ResetStream(editor, "ab12\ncd34\n", 2, 9);
    editor->Send(SetSelectionMode, static_cast<uintptr_t>(Scintilla::SelectionMode::Rectangle));
    editor->Send(SetRectangularSelectionAnchor, 2);
    editor->Send(SetRectangularSelectionCaret, 9);
    popup.action = wxID_COPY;
    popup.Open(screen);
    const auto rectangleClipboard = peer.Request("paste");
#ifdef __WXMSW__
    constexpr std::string_view expectedRectangle = "12\n34";
#else
    // The original GTK rectangle carrier includes a final LF for a stock
    // text widget; Windows publishes the original rows without that LF.
    constexpr std::string_view expectedRectangle = "12\n34\n";
#endif
    Require(rectangleClipboard == wxString::FromUTF8(expectedRectangle.data(), expectedRectangle.size()),
            "native popup Copy retains original rectangular clipboard rows");
    Require(editor->Send(GetSelectionMode) == static_cast<int>(Scintilla::SelectionMode::Rectangle) &&
                editor->Send(GetRectangularSelectionAnchor) == 2 && editor->Send(GetRectangularSelectionCaret) == 9,
            "pointer popup preserves rectangular selection endpoints and direction");
    TestEditor::ResetStream(editor, "alpha beta gamma", 0, 5);
    editor->Send(AddSelection, 10, 6);
    popup.Open(screen);
    Require(peer.Request("paste") == "alpha\nbeta", "native popup Copy preserves multiple-selection clipboard order");
    Require(editor->Send(GetSelections) == 2 && editor->Send(GetSelectionNAnchor, 0) == 0 &&
                editor->Send(GetSelectionNCaret, 0) == 5 && editor->Send(GetSelectionNAnchor, 1) == 6 &&
                editor->Send(GetSelectionNCaret, 1) == 10,
            "popup preserves every multiple-selection endpoint");
    peer.Offer("read-only availability offer");
    editor->Send(SetReadOnly, 1);
    popup.action = wxID_NONE;
    popup.inspect = [](wxMenu& menu) {
        Require(!menu.IsEnabled(wxID_PASTE) && menu.IsEnabled(wxID_CUT) && menu.IsEnabled(wxID_COPY) &&
                    menu.IsEnabled(wxID_DELETE),
                "original read-only popup availability follows live clipboard and core state");
    };
    popup.Open(wxDefaultPosition);
    TestEditor::ResetStream(editor, "");
    popup.inspect = [](wxMenu& menu) {
        Require(!menu.IsEnabled(wxID_CUT) && !menu.IsEnabled(wxID_COPY) && !menu.IsEnabled(wxID_DELETE) &&
                    !menu.IsEnabled(wxID_SELECTALL) && !menu.FindItemByPosition(12)->IsEnabled() &&
                    menu.IsEnabled(wxID_PASTE),
                "empty document popup refreshes document and clipboard command states");
    };
    popup.Open(wxDefaultPosition);
    popup.inspect = {};
}

void Check(EditorFrame* frame) {
    auto* editor = frame->Editor();
    Focus(frame);
    TestEditor::UseFixedWidthFont(editor);
    TestEditor::ResetStream(editor, "alpha beta\nnext line", 10, 6);
    editor->Send(BeginUndoAction);
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(EndUndoAction);
    editor->Send(Undo);
    editor->Send(SetSel, 10, 6);
    Popup popup(frame);
    // Target unobscured editor text, outside completion/CallTip near the caret.
    const auto screen = editor->ClientToScreen(wxPoint(editor->GetClientSize().x - 40, editor->GetClientSize().y - 40));
    popup.Open(screen);
    TestEditor::RequireSelection(editor, 10, 6, "pointer popup preserves reversed selection outside pointer");
    popup.Open(wxDefaultPosition);
    TestEditor::RequireSelection(editor, 10, 6, "keyboard popup preserves caret and selection direction");
    Require(TestEditor::Text(frame) == "alpha beta\nnext line" && !editor->Send(CanUndo) && editor->Send(CanRedo),
            "popup cancellation leaves document and undo untouched");
    popup.Open(wxDefaultPosition, WXK_F10);
    TestEditor::RequireSelection(editor, 10, 6, "Shift+F10 preserves reversed selection");
    popup.Open(wxDefaultPosition, WXK_MENU);
    TestEditor::RequireSelection(editor, 10, 6, "native menu key preserves directed selection");
    Require(editor->Send(CanRedo), "pointer and keyboard cancellation preserve the pending redo action");
    editor->Send(Redo);
    Require(TestEditor::Text(frame) == "alpha beta\nnext line!",
            "pending redo remains independently usable after cancellation");
    editor->Send(Undo);
    // Isolate focus/input delivery at an empty caret. Selection replacement's
    // existing undo grouping is independent of the context-menu contract.
    TestEditor::ResetStream(editor, "alpha beta\nnext line", 6, 6);
    popup.Open(wxDefaultPosition, WXK_MENU);
    const auto focusDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ((!editor->HasFocus() || !editor->Send(GetFocus)) && std::chrono::steady_clock::now() < focusDeadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(editor->HasFocus() && editor->Send(GetFocus), "Escape returns usable native focus to editor");
#ifdef __WXMSW__
    INPUT resumedInput[2]{};
    resumedInput[0].type = resumedInput[1].type = INPUT_KEYBOARD;
    // Commit the literal character through the native input queue, without
    // translating VK_X through the host's keyboard layout or starting preedit.
    resumedInput[0].ki.wScan = resumedInput[1].ki.wScan = L'x';
    resumedInput[0].ki.dwFlags = KEYEVENTF_UNICODE;
    resumedInput[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    Require(SendInput(2, resumedInput, sizeof(INPUT)) == 2, "native typing resumes after popup cancellation");
#else
    NativeRequest("key:x");
#endif
    const auto inputDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    constexpr std::string_view resumedText = "alpha xbeta\nnext line";
    while (editor->Send(GetLength) != static_cast<intptr_t>(resumedText.size()) &&
           std::chrono::steady_clock::now() < inputDeadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(TestEditor::Text(frame) == resumedText, "cancelled popup delivers resumed native input exactly once");
    editor->Send(Undo);
    Require(TestEditor::Text(frame) == "alpha beta\nnext line" && !editor->Send(CanUndo),
            "resumed native input remains one undo step");
    popup.outside = true;
    popup.Open(screen);
    popup.outside = false;
    Require(TestEditor::Text(frame) == "alpha beta\nnext line" && !editor->Send(CanUndo),
            "native outside-click cancellation edits no text and leaves no undo action");
    TestEditor::ResetStream(editor, "alpha beta\nnext line", 0, 0);
    popup.Open(screen);
    Require(editor->Send(GetCurrentPos) == 20 && editor->Send(GetAnchor) == 20,
            "empty pointer selection hit-tests the last document position");
    TestEditor::ResetStream(editor, "alpha beta\nnext line", 10, 6);
    popup.inspect = [](wxMenu& menu) {
        Require(!menu.IsEnabled(wxID_UNDO) && !menu.IsEnabled(wxID_REDO), "empty history disables Undo and Redo");
        Require(menu.IsEnabled(wxID_CUT) && menu.IsEnabled(wxID_COPY) && menu.IsEnabled(wxID_DELETE) &&
                    menu.IsEnabled(wxID_SELECTALL),
                "original nonempty document commands remain available");
    };
    popup.action = wxID_DELETE;
    popup.Open(wxDefaultPosition);
    Require(TestEditor::Text(frame) == "alpha \nnext line", "Delete removes selected bytes once through core Clear");
    popup.inspect = [](wxMenu& menu) { Require(menu.IsEnabled(wxID_UNDO), "popup refreshes Undo availability"); };
    popup.action = wxID_UNDO;
    popup.Open(wxDefaultPosition);
    Require(TestEditor::Text(frame) == "alpha beta\nnext line", "popup Undo restores exact selected bytes");
    popup.inspect = [](wxMenu& menu) { Require(menu.IsEnabled(wxID_REDO), "popup refreshes Redo availability"); };
    popup.action = wxID_REDO;
    popup.Open(wxDefaultPosition);
    Require(TestEditor::Text(frame) == "alpha \nnext line", "popup Redo applies exactly one delete");
    popup.inspect = {};
    TestEditor::ResetStream(editor, "alpha beta\nnext line", 10, 6);
    editor->Send(SetReadOnly, 1);
    popup.action = wxID_DELETE;
    popup.Open(wxDefaultPosition);
    Require(TestEditor::Text(frame) == "alpha beta\nnext line" && !editor->Send(CanUndo),
            "read-only core rejects native popup Delete without creating history");
    editor->Send(SetReadOnly, 0);
    popup.action = wxID_NONE;
    const auto transientPoint =
        editor->ClientToScreen(wxPoint(editor->GetClientSize().x - 20, editor->GetClientSize().y - 20));
    editor->Send(AutoCShow, 0, reinterpret_cast<intptr_t>("beta betamax"));
    Require(editor->Send(AutoCActive), "native completion is active before pointer popup");
    popup.Open(transientPoint);
    Require(!editor->Send(AutoCActive), "original right-button path cancels completion before popup");
    editor->Send(CallTipShow, 6, reinterpret_cast<intptr_t>("beta(value)"));
    Require(editor->Send(CallTipActive), "native CallTip is active before pointer popup");
    popup.Open(transientPoint);
    Require(!editor->Send(CallTipActive), "original right-button path cancels CallTip before popup");
    CheckClipboardCommands(frame, popup, screen);
    const auto folder = TestFiles::TemporaryDirectory("context-launch");
    std::filesystem::create_directory(folder);
    const auto cpp = folder / "fold.cpp";
    TestFiles::WriteBytes(cpp, "void f() {\n if (x) {\n  a();\n }\n}\n");
    editor->Send(SetSavePoint);
    Require(frame->Open(TestFiles::Filename(cpp), FileEncoding::Utf8).status == OpenStatus::Opened,
            "native folding fixture loads through ordinary document open");
    editor->Send(Colourise, 0, -1);
    editor->SetCodeFoldingVisible(true);
    editor->Send(SetSel, editor->Send(PositionFromLine, 2) + 4, editor->Send(PositionFromLine, 2) + 2);
    popup.action = frame->GetMenuBar()->FindMenuItem("View", "Current Block");
    popup.Open(screen);
    Require(!editor->Send(GetFoldExpanded, 1) && !editor->Send(GetLineVisible, 2),
            "native Toggle Fold invokes established Current Block on the surrounding nested block");
    TestEditor::RequireSelection(editor, 25, 23,
                                 "Current Block retains original reversed endpoints inside hidden text");
    popup.Open(wxDefaultPosition);
    Require(editor->Send(GetFoldExpanded, 1) && editor->Send(GetLineVisible, 2),
            "repeated popup Toggle Fold expands the current block once");
    const auto target = folder / "target.txt";
    TestFiles::WriteBytes(target, "first\nsecond\n");
    const auto input = (TestFiles::Filename(target) + ":2:3").utf8_string();
    TestEditor::ResetStream(editor, input.c_str(), input.size(), 0);
    Require(wxSetEnv("WXNOTEPAD_CONTEXT_ROOT", TestFiles::Filename(folder)),
            "configure owned child evidence directory");
    popup.action = wxID_NONE;
    popup.Open(screen);
    Require(!std::filesystem::exists(folder / "child-received"), "cancelling Open-capable popup launches no child");
    popup.action = frame->GetMenuBar()->FindMenuItem("Tools", "Open File, Folder, Link, etc.");
    popup.Open(wxDefaultPosition);
    const auto childDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!std::filesystem::exists(folder / "child-received") && std::chrono::steady_clock::now() < childDeadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(std::filesystem::exists(folder / "child-received") &&
                TestFiles::Bytes(folder / "child-received") == "first\nsecond\n\nposition=8\n",
            "popup Open launches a real child that independently loads disk bytes and one-based location");
    TestEditor::RequireSelection(editor, input.size(), 0, "popup Open preserves reversed source selection");
    Require(TestEditor::Text(frame) == input && !editor->Send(CanUndo),
            "popup Open preserves source document and history");
    size_t children = 0;
    for (const auto& file : std::filesystem::directory_iterator(folder))
        if (file.path().filename().string().starts_with("launched-")) ++children;
    Require(children == 1, "recreated popups retain exactly one Open binding and one child launch");
    popup.action = wxID_NONE;
    std::string scrolled;
    for (int line = 0; line < 80; ++line)
        scrolled += std::string(180, 'x') + '\n';
    scrolled += std::string(400, 'x') + "tail";
#ifdef __WXMSW__
    const auto display = wxDisplay::GetFromWindow(frame);
    Require(display != wxNOT_FOUND, "owned frame has a native monitor for edge-placement fixture");
    const auto work = wxDisplay(static_cast<unsigned>(display)).GetClientArea();
    frame->SetPosition(wxPoint(work.GetRight() - frame->GetSize().x + 1, work.GetBottom() - frame->GetSize().y + 1));
#endif
    TestEditor::ResetStream(editor, scrolled.c_str(), scrolled.size(), scrolled.size());
    for (const auto wrap : {Scintilla::Wrap::None, Scintilla::Wrap::Word}) {
        editor->SetWordWrapMode(wrap);
        editor->Send(GotoPos, scrolled.size());
        NativePaint::Complete(editor);
        Require(editor->Send(GetFirstVisibleLine) > 0, "placement fixture scrolls the document vertically");
        if (wrap == Scintilla::Wrap::None)
            Require(editor->Send(GetXOffset) > 0, "unwrapped placement fixture also scrolls horizontally");
        const auto caret = editor->Send(GetCurrentPos);
        popup.anchor = editor->ClientToScreen(wxPoint(static_cast<int>(editor->Send(PointXFromPosition, 0, caret)),
                                                      static_cast<int>(editor->Send(PointYFromPosition, 0, caret))));
        popup.Open(wxDefaultPosition, WXK_MENU);
        TestEditor::RequireSelection(editor, scrolled.size(), scrolled.size(), "keyboard placement preserves caret");
    }
    popup.anchor.reset();
    std::puts("PASS: original native popup groups, pointer and keyboard selection preservation, cancellation");
}
class ContextMenuApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        if (argc > 1 && wxString(argv[1]) == "--startup-file") {
            Require(argc == 5 && wxString(argv[3]) == "--startup-caret", "child receives ordinary startup arguments");
            const auto location = wxSplit(wxString(argv[4]), ',');
            long long line = 0, column = 0;
            Require(location.size() == 2 && location[0].ToLongLong(&line) && location[1].ToLongLong(&column),
                    "child receives native one-based line and column");
            wxString root;
            Require(wxGetEnv("WXNOTEPAD_CONTEXT_ROOT", &root), "child owns an explicit evidence directory");
            auto* child = new EditorFrame;
            Require(child->OpenRestartDocument(argv[2], {}, {}, false, line, column).status == OpenStatus::Opened,
                    "child opens the independently specified disk file");
            const auto directory = TestFiles::Utf8Path(root.utf8_string());
            TestFiles::WriteBytes(directory / ("launched-" + std::to_string(wxGetProcessId())), "opened\n");
            TestFiles::WriteBytes(directory / "child-received.tmp",
                                  TestEditor::Text(child) +
                                      "\nposition=" + std::to_string(child->Editor()->Send(GetCurrentPos)) + "\n");
            std::filesystem::rename(directory / "child-received.tmp", directory / "child-received");
            std::exit(0);
        }
        const unsigned language =
            argc == 3 && wxString(argv[1]) == "--localized"
                ? (wxString(argv[2]) == "zh_CN" ? SimplifiedChineseLanguage : TraditionalChineseLanguage)
                : EnglishLanguage;
        AppLocalization::Initialize(static_cast<uint16_t>(language));
        auto* frame = new EditorFrame;
        frame->SetTitle("wxnotepad context menu");
        frame->Show();
#ifdef __WXGTK__
        // Private pointer coordinates are output-local under Weston.
        frame->ShowFullScreen(true);
#endif
        NativeKeys::AfterPresentation(frame, [this, frame, language](bool presented) {
            try {
                Require(presented, "native compositor presents context-menu test editor");
                if (language == EnglishLanguage) {
                    Check(frame);
                } else {
                    Focus(frame);
                    TestEditor::ResetStream(frame->Editor(), "alpha beta", 10, 6);
                    Popup popup(frame, language);
                    popup.Open(wxDefaultPosition, WXK_MENU);
                    popup.Open(frame->Editor()->ClientToScreen(wxPoint(150, 80)));
                    TestEditor::RequireSelection(frame->Editor(), 10, 6,
                                                 "localized native popup preserves directed selection");
                }
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                result_ = 1;
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

private:
    int result_ = 0;
};
} // namespace
wxIMPLEMENT_APP(ContextMenuApp);
