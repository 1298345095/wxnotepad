#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include <print>
#include "settings/settings_store.h"
#include "file_helpers.h"
#include "editor_test_helpers.h"
#include "file_command_driver.h"
#include <wx/weakref.h>
#include <wx/apptrait.h>
#include <wx/log.h>
#include <memory>
#include <string_view>
#include <thread>
#ifdef __WXMSW__
#include <imm.h>
#endif
#ifdef __WXGTK__
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#undef None
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
#ifdef __WXGTK__
void SettleX11Frame(EditorFrame* frame, bool presented) {
    auto* widget = GTK_WIDGET(frame->GetHandle());
    gdk_display_sync(gtk_widget_get_display(widget));
    const auto deadline = std::chrono::steady_clock::now() + NativeKeys::FocusAcknowledgementTimeout;
    do {
        NativeSearch::DispatchEvents(frame);
        if ((gtk_widget_get_mapped(widget) != FALSE) == presented && frame->IsActive() == presented) return;
        wxMilliSleep(10);
    } while (std::chrono::steady_clock::now() < deadline);
}
void DelayedX11Activation(EditorFrame* frame) {
    auto* widget = GTK_WIDGET(frame->GetHandle());
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget))) return;
    wxString focus;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focus), "use the owned private X11 input mailbox");
    // X11 Show() can defer mapping until WM frame extents arrive. Hiding before
    // that first map completes can leave a late map notification after Hide().
    SettleX11Frame(frame, true);
    Require(gtk_widget_get_mapped(widget) && frame->IsActive(),
            "owned X11 activation starts from a fully presented active frame");
    frame->Hide();
    SettleX11Frame(frame, false);
    Require(!gtk_widget_get_mapped(widget) && !frame->IsActive(), "owned X11 activation begins genuinely unmapped");
    struct Observation {
        bool cancelled = false;
        bool unacknowledged = false;
        bool unmapped = false;
        bool shown = false;
    };
    auto observed = std::make_shared<Observation>();
    std::jthread mapping([frame, focus, observed](std::stop_token stop) {
        while (!stop.stop_requested()) {
            wxString pending;
            wxFFile file(focus, "rb");
            if (file.IsOpened() && file.ReadAll(&pending) && !pending.empty()) {
                constexpr auto MappingDelay = std::chrono::milliseconds(250);
                std::this_thread::sleep_for(MappingDelay);
                wxTheApp->CallAfter([frame, focus, observed] {
                    if (observed->cancelled) return;
                    wxString pending;
                    wxFFile file(focus, "rb");
                    observed->unacknowledged = file.IsOpened() && file.ReadAll(&pending) && !pending.empty();
                    observed->unmapped = !gtk_widget_get_mapped(GTK_WIDGET(frame->GetHandle()));
                    frame->Show();
                    observed->shown = true;
                });
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    try {
        NativeKeys::FocusEditor(frame, "wxnotepad", focus);
    } catch (...) {
        observed->cancelled = true;
        throw;
    }
    mapping.request_stop();
    mapping.join();
    Require(observed->shown && observed->unmapped && observed->unacknowledged && gtk_widget_get_mapped(widget) &&
                frame->IsActive() && frame->Editor()->HasFocus(),
            "owned X11 activation waits for mapping and acknowledges only actual native editor focus");
    std::puts("PASS: pending inactive X11 activation survives native mapping within the original focus budget");
}
void RejectedX11Activation(EditorFrame* frame) {
    auto* widget = GTK_WIDGET(frame->GetHandle());
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget))) return;
    wxString focus;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focus), "use the owned private X11 input mailbox");
    const auto read = [](const wxString& path) {
        wxString pending;
        wxFFile file(path, "rb");
        Require(file.IsOpened() && file.ReadAll(&pending), "read the actual private X11 focus protocol");
        return pending;
    };
    frame->Hide();
    wxYield();
    bool rejected = false;
    try {
        NativeKeys::FocusEditor(frame, wxString::Format("wxnotepad absent X11 target %lu", wxGetProcessId()), focus);
    } catch (const std::runtime_error& error) {
        rejected = std::string_view(error.what()) == "private native input must activate the owned editor";
    }
    Require(rejected && !read(focus).empty() && !gtk_widget_get_mapped(widget) && !frame->IsActive(),
            "a permanently absent X11 target fails the original focus budget without acknowledgement");
    frame->Show();
    NativeKeys::FocusEditor(frame, "wxnotepad", focus);
    const auto identity = wxString::Format("%lu", wxGetProcessId());
    const auto expired = "trace:" + identity + ":front-until:0:wxnotepad";
    {
        wxFFile file(focus, "wb");
        Require(file.IsOpened() && file.Write(expired) && file.Close(), "publish an expired real X11 activation");
    }
    const auto observed = [&] {
        return wxFileExists(focus + ".diagnostics") &&
               read(focus + ".diagnostics").Contains("\tevent=driver.expired\tgesture=" + identity + "\t");
    };
    const auto until = std::chrono::steady_clock::now() + NativeKeys::FocusAcknowledgementTimeout;
    while (!observed() && std::chrono::steady_clock::now() < until) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(observed() && read(focus) == expired && frame->IsActive() && frame->Editor()->HasFocus(),
            "an expired activation of a visible X11 target remains rejected and unacknowledged");
    std::puts("PASS: missing and expired X11 activation requests remain failures without false acknowledgements");
}
#endif
template <typename Predicate> void Await(Predicate ready, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    do {
        wxYield();
        if (ready()) return;
        wxMilliSleep(10);
    } while (std::chrono::steady_clock::now() < deadline);
    Require(false, message);
}
wxMenuItem* Setting(EditorFrame* frame, const char* label) {
    auto* menus = frame->GetMenuBar();
    auto* item = menus->FindItem(menus->FindMenuItem("Settings", label));
    Require(item, "Settings exposes the original Extra Esc Key Function entries");
    return item;
}
void WindowActionKey(EditorFrame* frame, bool shift = false, [[maybe_unused]] bool alreadyFocused = false) {
#ifdef __WXGTK__
    // A successful native minimize/close removes keyboard focus before key-up.
    // Keep the existing ordinary shortcut helper's release assertion intact.
    wxString focus;
    if (alreadyFocused)
        Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focus),
                "composition keys use the existing private driver");
    else {
        auto* widget = GTK_WIDGET(frame->GetHandle());
        wxString target;
        if (GDK_IS_X11_DISPLAY(gtk_widget_get_display(widget))) {
            // Document notifications can change the title during activation.
            // Keep targeting this owned native window throughout the handshake.
            target = wxString::Format("x11-window:%lu", gdk_x11_window_get_xid(gtk_widget_get_window(widget)));
        } else {
            target = wxString::Format("wxnotepad Escape target %p", static_cast<void*>(frame));
            frame->SetTitle(target);
        }
        NativeKeys::FocusEditor(frame, target, focus);
    }
    {
        wxFFile request(focus, "wb");
        Require(request.Write(shift ? "key:shift+Escape" : "key:Escape"), "publish native Escape gesture");
    }
    Await(
        [&focus] {
            wxFFile request(focus, "rb");
            wxString pending;
            return request.IsOpened() && request.ReadAll(&pending) && pending.empty();
        },
        "private native driver acknowledges the window-action key");
#else
    NativeKeys::Shortcut(frame, WXK_ESCAPE, false, false, shift);
#endif
}
bool NativeWindowVisible(EditorFrame* frame) {
#ifdef __WXGTK__
    auto* widget = GTK_WIDGET(frame->GetHandle());
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(widget))) {
        auto* surface = gdk_wayland_window_get_wl_surface(gtk_widget_get_window(widget));
        const auto id = wl_proxy_get_id(reinterpret_cast<wl_proxy*>(surface));
        const auto identity = wxString::Format("role xdg_toplevel, PID %lu, surface ID %u,", wxGetProcessId(), id);
        wxArrayString scene;
        Require(wxExecute("weston-debug scene-graph", scene, wxEXEC_SYNC | wxEXEC_NODISABLE) == 0,
                "observe the real private compositor scene without changing focus");
        for (const auto& line : scene)
            if (line.Contains(identity)) return true;
        return false;
    }
#endif
    return !frame->IsIconized();
}
void CheckComposition(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "IME cancellation");
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(SetSel, 8, 2);
    NativeSearch::Command(frame, Setting(frame, "Exit wxnotepad")->GetId());
    NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Save, wxID_CANCEL);
    for (const bool shift : {false, true}) {
#ifdef __WXGTK__
        wxString focus;
        NativeKeys::FocusEditor(frame, "wxnotepad", focus);
#endif
#ifdef __WXMSW__
        SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_IME_STARTCOMPOSITION, 0, 0);
#elif defined(__WXGTK__)
        g_signal_emit_by_name(editor->m_imContext, "preedit-start");
#endif
#ifdef __WXGTK__
        class CompositionKey final : public wxEventFilter {
        public:
            explicit CompositionKey(bool shift) : modifiers_(shift ? wxMOD_SHIFT : wxMOD_NONE) {
                wxEvtHandler::AddFilter(this);
            }
            ~CompositionKey() override { wxEvtHandler::RemoveFilter(this); }
            bool received = false;
            int FilterEvent(wxEvent& event) override {
                if (event.GetEventType() == wxEVT_CHAR_HOOK) {
                    const auto& key = static_cast<wxKeyEvent&>(event);
                    if (key.GetKeyCode() == WXK_ESCAPE && key.GetModifiers() == modifiers_) received = true;
                }
                return Event_Skip;
            }

        private:
            int modifiers_;
        } observation(shift);
#endif
        WindowActionKey(frame, shift, true);
#ifdef __WXGTK__
        Await([&observation] { return observation.received; },
              "native Escape reaches the composing editor dispatch path");
#endif
#ifdef __WXMSW__
        SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_IME_ENDCOMPOSITION, 0, 0);
#elif defined(__WXGTK__)
        g_signal_emit_by_name(editor->m_imContext, "preedit-end");
#endif
    }
    driver.Stop();
    std::println(stderr, "composition result prompts={:d} shown={:d} text='{}' anchor={:d} caret={:d} undo={:d}",
                 driver.prompts, frame->IsShown(), TestEditor::Text(editor).c_str(),
                 static_cast<long long>(editor->Send(GetAnchor)), static_cast<long long>(editor->Send(GetCurrentPos)),
                 static_cast<long long>(editor->Send(CanUndo)));
    Require(driver.prompts == 0 && frame->IsShown() && TestEditor::Text(editor) == "IME cancellation!" &&
                editor->Send(GetAnchor) == 8 && editor->Send(GetCurrentPos) == 2 && editor->Send(CanUndo),
            "active native composition lifecycle retains Escape/Shift+Escape cancellation ownership");
    std::puts("PASS: composition-lifecycle replay guard (real human IME evidence remains separately required)");
}
#ifdef __WXMSW__
// Replay the captured TSF boundary: cancellation synchronously ends preedit
// before wx's WH_KEYBOARD hook, while the retrieved MSG is VK_PROCESSKEY.
// Physical SendInput still drives wx's actual keyboard hook and message loop.
class CancelledImeKey final : public wxEventFilter {
public:
    explicit CancelledImeKey(EditorControl* editor) : editor_(static_cast<HWND>(editor->GetHandle())) {
        Require(!active_, "one owned IME cancellation replay is active");
        active_ = this;
        keyboard_ = SetWindowsHookExW(WH_KEYBOARD, Keyboard, nullptr, GetCurrentThreadId());
        messages_ = SetWindowsHookExW(WH_GETMESSAGE, Message, nullptr, GetCurrentThreadId());
        if (!keyboard_ || !messages_) {
            if (keyboard_) UnhookWindowsHookEx(keyboard_);
            if (messages_) UnhookWindowsHookEx(messages_);
            active_ = nullptr;
            Require(false, "observe the owned keyboard and retrieved-message boundaries");
        }
        wxEvtHandler::AddFilter(this);
        SendMessageW(editor_, WM_IME_STARTCOMPOSITION, 0, 0);
    }
    ~CancelledImeKey() override {
        wxEvtHandler::RemoveFilter(this);
        UnhookWindowsHookEx(messages_);
        UnhookWindowsHookEx(keyboard_);
        if (!ended_) SendMessageW(editor_, WM_IME_ENDCOMPOSITION, 0, 0);
        active_ = nullptr;
    }
    bool Complete() const noexcept { return processKey_ && released_; }
    bool InitialDownObserved() const noexcept { return initialDown_; }
    int EscapeHookCount() const noexcept { return escapeHooks_; }
    int FilterEvent(wxEvent& event) override {
        if (event.GetEventType() == wxEVT_CHAR_HOOK) {
            const auto& key = static_cast<wxKeyEvent&>(event);
            if (key.GetKeyCode() == WXK_ESCAPE) ++escapeHooks_;
        }
        return Event_Skip;
    }

private:
    void EndComposition() {
        if (ended_) return;
        ended_ = true;
        constexpr LPARAM cancelledPreedit = GCS_COMPSTR | GCS_COMPATTR | GCS_CURSORPOS | GCS_COMPCLAUSE |
                                            GCS_COMPREADSTR | GCS_COMPREADATTR | GCS_COMPREADCLAUSE | GCS_DELTASTART;
        SendMessageW(editor_, WM_IME_COMPOSITION, VK_ESCAPE, cancelledPreedit);
        SendMessageW(editor_, WM_IME_ENDCOMPOSITION, 0, 0);
    }
    static LRESULT CALLBACK Keyboard(int code, WPARAM key, LPARAM flags) {
        if (code == HC_ACTION && key == VK_ESCAPE && !(HIWORD(flags) & KF_UP) && GetFocus() == active_->editor_) {
            active_->initialDown_ = !(HIWORD(flags) & KF_REPEAT);
            active_->EndComposition();
        }
        return CallNextHookEx(nullptr, code, key, flags);
    }
    static LRESULT CALLBACK Message(int code, WPARAM removed, LPARAM data) {
        if (code == HC_ACTION && removed == PM_REMOVE) {
            auto* message = reinterpret_cast<MSG*>(data);
            if (message->hwnd == active_->editor_ && message->wParam == VK_ESCAPE) {
                if (message->message == WM_KEYDOWN) {
                    active_->EndComposition();
                    message->wParam = VK_PROCESSKEY;
                    active_->processKey_ = true;
                } else if (message->message == WM_KEYUP) {
                    message->wParam = VK_PROCESSKEY;
                    active_->released_ = true;
                }
            }
        }
        return CallNextHookEx(nullptr, code, removed, data);
    }
    inline static CancelledImeKey* active_ = nullptr;
    HWND editor_;
    HHOOK keyboard_ = nullptr;
    HHOOK messages_ = nullptr;
    bool ended_ = false;
    bool initialDown_ = false;
    bool processKey_ = false;
    bool released_ = false;
    int escapeHooks_ = 0;
};
void PhysicalEscape(EditorFrame* frame) {
    const auto window = static_cast<HWND>(frame->GetHandle());
    const auto editor = static_cast<HWND>(frame->Editor()->GetHandle());
    DWORD process = 0;
    GetWindowThreadProcessId(editor, &process);
    NativeFocus::Activate(window, editor);
    Await([window, editor] { return GetForegroundWindow() == window && GetFocus() == editor; },
          "physical Escape targets the owned focused editor");
    Require(process == GetCurrentProcessId(), "physical Escape stays inside the owned process");
    for (const int key : {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN, VK_ESCAPE})
        Require(!(GetAsyncKeyState(key) & 0x8000), "physical Escape starts with released shortcut keys");
    INPUT input[2]{};
    for (auto& event : input) {
        event.type = INPUT_KEYBOARD;
        event.ki.wVk = VK_ESCAPE;
        event.ki.wScan = static_cast<WORD>(MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC));
    }
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    Require(SendInput(2, input, sizeof(INPUT)) == 2, "deliver the owned physical Escape press and release");
}
void CheckCancelledCompositionKey(EditorFrame* frame) {
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "committed IME word");
    editor->Send(Scintilla::Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
    NativeSearch::Command(frame, Setting(frame, "Minimize wxnotepad")->GetId());
    {
        CancelledImeKey replay(editor);
        PhysicalEscape(frame);
        Await([frame, &replay] { return frame->IsIconized() || replay.Complete(); },
              "cancelled IME key finishes native dispatch");
        Require(!frame->IsIconized(), "IME cancellation before the keyboard hook must not also minimize");
        Require(replay.InitialDownObserved() && replay.Complete(),
                "initial physical Escape and retrieved VK_PROCESSKEY follow the captured native order");
        // wxMSW 3.3.4 handles Escape after GetMessage, where the IME has
        // already replaced this consumed key with VK_PROCESSKEY.
        Require(replay.EscapeHookCount() == 0, "IME-consumed Escape never reaches the application character hook");
        Require(TestEditor::Text(editor) == "committed IME word!" && editor->Send(Scintilla::Message::CanUndo),
                "cancelled IME key preserves document bytes and Undo");
    }
    PhysicalEscape(frame);
    Await([frame] { return frame->IsIconized(); }, "next independent Escape still performs native minimize");
    frame->Iconize(false);
    frame->Raise();
    Await([frame] { return !frame->IsIconized(); }, "restore the owned frame after ordinary Escape");
    editor->SetFocus();
    NativeKeys::Shortcut(frame, 'Z', true);
    Require(TestEditor::Text(editor) == "committed IME word", "working Undo survives cancelled and ordinary Escape");
    editor->Send(Scintilla::Message::SetSavePoint);
    std::puts("PASS: captured IME cancellation ordering through native keyboard hook, next Escape and Undo");
}
#endif
void WidgetEscape(wxWindow* control) {
    control->SetFocus();
    wxYield();
#ifdef __WXMSW__
    class Observation final : public wxEventFilter {
    public:
        explicit Observation(HWND window) : window_(window) {
            wxEvtHandler::AddFilter(this);
            Require(SetWindowSubclass(window_, WindowMessage, 1, 0), "observe the native field message boundary");
        }
        ~Observation() override {
            if (IsWindow(window_)) RemoveWindowSubclass(window_, WindowMessage, 1);
            wxEvtHandler::RemoveFilter(this);
        }
        int FilterEvent(wxEvent& event) override {
            if (event.GetEventType() == wxEVT_CHAR_HOOK || event.GetEventType() == wxEVT_KEY_DOWN ||
                event.GetEventType() == wxEVT_KEY_UP) {
                const auto& key = static_cast<wxKeyEvent&>(event);
                if (key.GetKeyCode() == WXK_ESCAPE)
                    std::fprintf(stderr, "WIDGET event=%s modifiers=%d target=%p focus=%p\n",
                                 event.GetEventType() == wxEVT_CHAR_HOOK  ? "char-hook"
                                 : event.GetEventType() == wxEVT_KEY_DOWN ? "key-down"
                                                                          : "key-up",
                                 key.GetModifiers(), static_cast<void*>(event.GetEventObject()),
                                 static_cast<void*>(wxWindow::FindFocus()));
            }
            return Event_Skip;
        }
        static LRESULT CALLBACK WindowMessage(HWND window, UINT message, WPARAM key, LPARAM flags, UINT_PTR,
                                              DWORD_PTR) {
            if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR || message == WM_IME_KEYDOWN ||
                message == WM_IME_KEYUP)
                std::fprintf(stderr, "WIDGET native hwnd=%p message=%u key=%llu flags=%lld\n",
                             static_cast<void*>(window), message, static_cast<unsigned long long>(key),
                             static_cast<long long>(flags));
            return DefSubclassProc(window, message, key, flags);
        }

    private:
        HWND window_;
    } observation(static_cast<HWND>(control->GetHandle()));
    auto* dialog = wxDynamicCast(wxGetTopLevelParent(control), wxDialog);
    Require(dialog, "native widget belongs to an owned dialog");
    if (dialog->IsModal())
        NativeKeys::DialogNavigation(dialog, VK_ESCAPE);
    else {
        const auto window = static_cast<HWND>(dialog->GetHandle());
        const auto focused = static_cast<HWND>(control->GetHandle());
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        Require(process == GetCurrentProcessId() && IsChild(window, focused),
                "modeless Escape belongs to this process and its native field");
        NativeFocus::Activate(window, focused);
        Await([window, focused] { return GetForegroundWindow() == window && GetFocus() == focused; },
              "the owned modeless field receives native foreground focus");
        wchar_t type[128]{};
        GetClassNameW(focused, type, 128);
        std::fprintf(stderr, "WIDGET focus hwnd=%p root=%p process=%lu class=%ls foreground=%p capture=%p layout=%p\n",
                     static_cast<void*>(GetFocus()), static_cast<void*>(window), process, type,
                     static_cast<void*>(GetForegroundWindow()), static_cast<void*>(GetCapture()),
                     static_cast<void*>(GetKeyboardLayout(0)));
        INPUT input[2]{};
        for (auto& key : input) {
            key.type = INPUT_KEYBOARD;
            key.ki.wVk = VK_ESCAPE;
        }
        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
        Require(SendInput(2, input, sizeof(INPUT)) == 2, "native Escape reaches the verified modeless field");
    }
#elif defined(__WXGTK__)
    auto* widget = control->GetConnectWidget();
    auto* event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = static_cast<GdkWindow*>(g_object_ref(gtk_widget_get_window(widget)));
    event->key.send_event = TRUE;
    event->key.keyval = GDK_KEY_Escape;
    gtk_widget_event(widget, event);
    gdk_event_free(event);
#endif
    wxYield();
}
// Observation-only baseline comparison; the registered fixture keeps its dismissal assertion.
void ProbeWidgetFocus(EditorFrame* frame, bool compositionReplay = false) {
#ifdef __WXMSW__
    std::fprintf(stderr, "WIDGET probe compositionReplay=%d interaction=%lld\n", compositionReplay,
                 static_cast<long long>(frame->Editor()->Send(Scintilla::Message::GetIMEInteraction)));
    if (compositionReplay) {
        const auto editor = static_cast<HWND>(frame->Editor()->GetHandle());
        SendMessageW(editor, WM_IME_STARTCOMPOSITION, 0, 0);
        SendMessageW(editor, WM_IME_ENDCOMPOSITION, 0, 0);
    }
#else
    wxUnusedVar(compositionReplay);
#endif
    NativeKeys::Shortcut(frame, 'F', true);
    auto* query = wxWindow::FindWindowByName("Find query", frame);
    Require(query, "baseline probe locates the actual Find field");
    auto* dialog = wxDynamicCast(wxGetTopLevelParent(query), wxDialog);
    Require(dialog && dialog->IsShown(), "baseline probe opens the modeless Find dialog");
    WidgetEscape(query);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (dialog->IsShown() && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    std::println(stderr, "WIDGET outcome dialogShown={:d} mainShown={:d} mainDeleting={:d}", dialog->IsShown(),
                 frame->IsShown(), frame->IsBeingDeleted());
    if (dialog->IsShown()) NativeSearch::Click(dialog, wxID_CANCEL);
}
void CheckWidgetFocus(EditorFrame* frame) {
    NativeSearch::Command(frame, Setting(frame, "Exit wxnotepad")->GetId());
    NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Save, wxID_CANCEL);
    NativeKeys::Shortcut(frame, 'F', true);
    auto* query = wxWindow::FindWindowByName("Find query", frame);
    Require(query, "Find exposes its actual native modeless text field");
    auto* dialog = wxDynamicCast(wxGetTopLevelParent(query), wxDialog);
    Require(dialog && dialog->IsShown(), "native modeless Find dialog is shown");
    WidgetEscape(query);
    Await([dialog] { return !dialog->IsShown(); }, "Escape dismisses the focused modeless dialog");
    Require(driver.prompts == 0 && frame->IsShown(), "dialog Escape does not invoke the main-frame exit preference");
    wxDialog modal(frame, wxID_ANY, "wxnotepad Escape modal", wxDefaultPosition, wxSize(360, 150));
    // These synchronous modal children leave scope before their parent dialog.
    wxTextCtrl field(&modal, wxID_ANY, "native field", wxPoint(15, 15), wxSize(300, 25));
    wxButton cancel(&modal, wxID_CANCEL, "Cancel", wxPoint(15, 55));
    bool delivered = false;
    modal.Bind(wxEVT_SHOW, [&modal, &field, &delivered](wxShowEvent& event) {
        if (event.IsShown())
            NativeKeys::AfterPresentation(&modal, [&field, &delivered](bool ready) {
                Require(ready, "owned native modal dialog is presented");
                WidgetEscape(&field);
                delivered = true;
            });
        event.Skip();
    });
    Require(modal.ShowModal() == wxID_CANCEL && delivered && driver.prompts == 0 && frame->IsShown(),
            "Escape dismisses the native modal widget without closing its main frame");
    driver.Stop();
    frame->Editor()->Send(Scintilla::Message::SetSavePoint);
    std::puts("PASS: focused native modal and modeless widgets retain Escape dismissal");
}
void CheckMenu(EditorFrame* frame) {
    auto* popup = Setting(frame, "Extra Esc Key Function");
    auto* submenu = popup->GetSubMenu();
    Require(submenu, "Extra Esc Key Function is a submenu");
    auto* none = submenu->FindItem(submenu->FindItem("None"));
    auto* minimize = submenu->FindItem(submenu->FindItem("Minimize wxnotepad"));
    auto* exit = submenu->FindItem(submenu->FindItem("Exit wxnotepad"));
    Require(none && minimize && exit, "all three original Extra Esc choices are present");
    Require(popup->GetSubMenu() == none->GetMenu() && none->GetMenu() == minimize->GetMenu() &&
                none->GetMenu() == exit->GetMenu(),
            "all three choices belong to the original Extra Esc Key Function submenu");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(none->IsChecked() && !minimize->IsChecked() && !exit->IsChecked(), "None is the original default");
    for (auto* chosen : {minimize, exit, none}) {
        NativeSearch::Command(frame, chosen->GetId());
        for (auto* item : {none, minimize, exit})
            Require(item->IsChecked() == (item == chosen), "menu selection immediately checks only its active choice");
    }
    NativeKeys::Shortcut(frame, WXK_ESCAPE, false);
    Require(frame->IsShown() && !frame->IsIconized(), "None leaves the ordinary editor open");
    std::puts("PASS: original Extra Esc submenu, default and immediate exclusive checks");
}
void CheckMinimize(EditorFrame* frame) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    TestEditor::ResetStream(editor, "original document\nsecond line");
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(SetSel, 10, 3);
    const auto contents = TestEditor::Text(editor);
    const auto expectDocument = [editor, &contents] {
        Require(TestEditor::Text(editor) == contents && editor->Send(GetAnchor) == 10 &&
                    editor->Send(GetCurrentPos) == 3 && editor->Send(CanUndo) && editor->Send(GetModify),
                "Escape and native minimize/restore preserve document bytes, selection, modified state and history");
    };
    NativeSearch::Command(frame, Setting(frame, "Minimize wxnotepad")->GetId());
    for (const auto active : {AutoCActive, CallTipActive}) {
        if (active == AutoCActive)
            editor->Send(AutoCShow, 0, reinterpret_cast<intptr_t>("alpha alpine"));
        else
            editor->Send(CallTipShow, 3, reinterpret_cast<intptr_t>("original CallTip"));
        Require(editor->Send(active), "requested transient editor UI is active");
        NativeKeys::Shortcut(frame, WXK_ESCAPE, false);
        Require(!editor->Send(active) && !frame->IsIconized(),
                "one Escape cancels only transient UI before minimizing");
        expectDocument();
    }
    const auto normal = frame->GetSize();
    NativeKeys::Shortcut(frame, WXK_F11, false);
    Await([frame, normal] { return frame->GetSize() != normal; }, "F11 enters the native fullscreen window");
    NativeKeys::Shortcut(frame, WXK_ESCAPE, false);
    Await([frame, normal] { return frame->GetSize() == normal; },
          "Escape restores fullscreen before the configured action");
    Require(!frame->IsIconized(), "leaving fullscreen does not also minimize the window");
    for (const int modifiers : {int{wxMOD_CONTROL}, int{wxMOD_ALT}, wxMOD_CONTROL | wxMOD_SHIFT}) {
        NativeKeys::Shortcut(frame, WXK_ESCAPE, (modifiers & wxMOD_CONTROL) != 0, (modifiers & wxMOD_ALT) != 0,
                             (modifiers & wxMOD_SHIFT) != 0);
        Require(!frame->IsIconized(), "arbitrary modified Escape is not plain Escape");
    }
    WindowActionKey(frame);
    Await([frame] { return !NativeWindowVisible(frame); }, "configured Escape actually minimizes the native window");
    frame->Iconize(false);
    frame->Raise();
#ifdef __WXGTK__
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(GTK_WIDGET(frame->GetHandle()))))
        Require(wxExecute("xdotool key --clearmodifiers super+Tab", wxEXEC_SYNC | wxEXEC_NODISABLE) == 0,
                "restore the owned minimized window through the private compositor switcher");
#endif
    Await([frame] { return NativeWindowVisible(frame); }, "native window restores from minimized state");
    editor->SetFocus();
    expectDocument();
    NativeKeys::Shortcut(frame, 'Z', true);
    Require(TestEditor::Text(editor) == "original document\nsecond line", "restored editor retains working Undo");
    editor->Send(SetSavePoint);
    std::puts("PASS: native Escape precedence, exact modifiers, minimize, restore and working history");
}
// Let wxWidgets create and own the stderr target, including during startup.
class EscapeAppTraits final : public wxGUIAppTraits {
public:
    wxLog* CreateLogTarget() override { return std::make_unique<wxLogStderr>().release(); }
};
class EscapeApp final : public wxApp {
public:
    wxAppTraits* CreateTraits() override { return std::make_unique<EscapeAppTraits>().release(); }
    bool OnInit() override {
        settings_ =
            argc == 4
                ? std::make_unique<SettingsStore>()
                : std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory() / "escape.ini"));
        if (argc != 4) settings_->values.selectOption &= ~SelectOption_CopyPasteBufferAsFindText;
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "owned Escape test frame is presented");
                if (argc == 2 && (argv[1] == "--widget-probe" || argv[1] == "--widget-ime-probe")) {
                    ProbeWidgetFocus(frame, argv[1] == "--widget-ime-probe");
                    status_ = 0;
                    frame->Destroy();
                    return;
                }
                if (argc == 4) {
                    const std::array<const char*, 3> labels = {"None", "Minimize wxnotepad", "Exit wxnotepad"};
                    const auto expected = wxAtoi(argv[1]);
                    auto* menu = Setting(frame, "Extra Esc Key Function")->GetSubMenu();
                    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                    for (size_t index = 0; index < labels.size(); ++index)
                        Require(menu->FindItem(menu->FindItem(labels[index]))->IsChecked() ==
                                    (static_cast<int>(index) == expected),
                                "restarted native menu reflects the original imported EscFunction value");
                    if (argv[2] != "keep") {
                        const auto chosen = wxAtoi(argv[2]);
                        NativeSearch::Command(frame, menu->FindItem(labels[static_cast<size_t>(chosen)]));
                    }
                    if (argv[3] == "explicit")
                        NativeSearch::Command(frame, Setting(frame, "Save Settings Now")->GetId());
                    Require(frame->Close(), "settings lifecycle uses ordinary application close");
                    status_ = 0;
                    return;
                }
#ifdef __WXGTK__
                DelayedX11Activation(frame);
                RejectedX11Activation(frame);
#endif
                CheckMenu(frame);
                CheckComposition(frame);
#ifdef __WXMSW__
                CheckCancelledCompositionKey(frame);
#endif
                CheckWidgetFocus(frame);
                CheckMinimize(frame);
                CloseCase(frame, 0);
                return;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
            }
            frame->Destroy();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return status_;
    }

private:
    struct CloseState {
        std::string text;
        wxString identity;
        bool shift;
    };
    static CloseState PrepareCloseCandidate(EditorFrame* candidate, int scenario, const std::filesystem::path& path) {
        using enum Scintilla::Message;
        auto* editor = candidate->Editor();
        if (scenario == 2)
            Require(candidate->SaveFirst(TestFiles::Filename(path)).status == SaveStatus::Saved,
                    "failed-Save case begins with a named saved document");
        editor->Send(AddText, 15, reinterpret_cast<intptr_t>("unsaved content"));
        wxYield();
        const CloseState state{TestEditor::Text(editor), candidate->DocumentPath(), scenario == 1 || scenario == 4};
        if (scenario == 1) editor->Send(SetReadOnly, 1);
        if (scenario == 2) {
            std::filesystem::remove(path);
            std::filesystem::create_directory(path);
        }
        if (state.shift) {
            auto* menu = Setting(candidate, "Extra Esc Key Function")->GetSubMenu();
            NativeSearch::Command(candidate, menu->FindItem("None"));
        } else
            NativeSearch::Command(candidate, Setting(candidate, "Exit wxnotepad")->GetId());
        if (scenario == 1) {
            const auto normal = candidate->GetSize();
#ifdef __WXMSW__
            constexpr auto fullScreenLabel = "Toggle Full Screen";
#else
            constexpr auto fullScreenLabel = "Toggle Full Screen [Esc]";
#endif
            NativeSearch::Command(candidate, candidate->GetMenuBar()->FindMenuItem("View", fullScreenLabel));
            Await([candidate, editor,
                   normal] { return candidate->GetSize() != normal && editor->GetClientSize().x > 100; },
                  "candidate native fullscreen presentation finishes before Shift+Escape");
            editor->Send(CallTipShow, 1, reinterpret_cast<intptr_t>("Shift+Escape bypasses CallTip/fullscreen"));
        }
        return state;
    }
    static void CheckCancelledClose(const wxWeakRef<EditorFrame>& retained, int scenario, const CloseState& before,
                                    NativeDialogs::FileCommandDriver& driver) {
        using enum Scintilla::Message;
        Await([&retained] { return retained && retained->IsEnabled(); }, "Cancel/failed Save returns to the editor");
        if (scenario == 2)
            Await([&driver] { return driver.errors == 1; }, "failed Save shows the existing native error");
        auto* editor = retained ? retained->Editor() : nullptr;
        Require(retained && !retained->IsBeingDeleted() && TestEditor::Text(editor) == before.text &&
                    editor->Send(GetModify) && retained->DocumentPath() == before.identity,
                "Cancel and failed Save preserve the same owned document and history");
        Require((editor->Send(GetReadOnly) != 0) == (scenario == 1), "exit cancellation preserves read-only mode");
        // SCI_CANUNDO is false in read-only mode even with retained history.
        if (scenario == 1) editor->Send(SetReadOnly, 0);
        Require(editor->Send(CanUndo), "cancelled close retains the document undo history");
        if (scenario == 1) editor->Send(SetReadOnly, 1);
        retained->Destroy();
    }
    static void CheckSuccessfulClose(const wxWeakRef<EditorFrame>& retained, int scenario,
                                     const std::filesystem::path& path,
                                     const NativeDialogs::FileCommandDriver& driver) {
        Await([&retained] { return !retained; }, "successful Save/Discard closes the owned frame");
        if (scenario == 3)
            Require(driver.choosers == 1 && TestFiles::Bytes(path) == "unsaved content",
                    "Save from Escape writes the actual unnamed document before closing");
    }
    void CloseCase(EditorFrame* sentinel, int scenario) {
        if (scenario == 5) {
            status_ = 0;
            std::puts("PASS: actual Escape/Shift+Escape dispatch, Cancel, failed Save, successful Save and Discard");
            sentinel->Destroy();
            return;
        }
        auto* candidate = new EditorFrame(settings_.get());
        candidate->SetFileWatchingOptions({.mode = FileWatchingMode::None});
        candidate->Show();
        NativeKeys::AfterPresentation(candidate, [this, sentinel, candidate, scenario](bool ready) {
            wxWeakRef<EditorFrame> retained(candidate);
            try {
                Require(ready, "owned close candidate is presented before actual native Escape");
                const auto path =
                    TestFiles::TemporaryDirectory() / ("escape-save-" + std::to_string(scenario) + ".txt");
                const auto before = PrepareCloseCandidate(candidate, scenario, path);
                const int choice = scenario < 2 ? wxID_CANCEL : scenario == 4 ? wxID_NO : wxID_YES;
                NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Save, choice,
                                                        scenario == 3 ? TestFiles::Filename(path) : wxString{});
#ifdef __WXGTK__
                if (scenario == 3 &&
                    !GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(GTK_WIDGET(candidate->GetHandle())))) {
                    // Closing immediately after an edit also has real document
                    // notifications queued when native focus is requested.
                    candidate->Editor()->Send(Scintilla::Message::BeginUndoAction);
                    candidate->Editor()->Send(Scintilla::Message::AppendText, 1, reinterpret_cast<intptr_t>("!"));
                    candidate->Editor()->Send(Scintilla::Message::EndUndoAction);
                    candidate->Editor()->Send(Scintilla::Message::Undo);
                    Require(TestEditor::Text(candidate->Editor()) == before.text,
                            "queued native document notifications retain the original close-case bytes");
                }
#endif
                candidate->SetTitle(wxString::Format("wxnotepad Escape candidate %d", scenario));
                WindowActionKey(candidate, before.shift);
                Await([&driver] { return driver.prompts == 1; },
                      "actual Escape exit opens the existing unsaved confirmation");
                if (scenario < 3)
                    CheckCancelledClose(retained, scenario, before, driver);
                else
                    CheckSuccessfulClose(retained, scenario, path, driver);
                driver.Stop();
                Require(sentinel->IsShown() && !sentinel->IsBeingDeleted(),
                        "exit closes only its owned application window");
                wxYield();
                CloseCase(sentinel, scenario + 1);
                return;
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL close case {:d}: {}\n", scenario, error.what());
                if (retained)
                    std::println(stderr, "candidate size={:d}x{:d} client={:d}x{:d} active={:d} editorFocus={:d}",
                                 retained->GetSize().x, retained->GetSize().y, retained->GetClientSize().x,
                                 retained->GetClientSize().y, retained->IsActive(), retained->Editor()->HasFocus());
            }
            if (retained) retained->Destroy();
            sentinel->Destroy();
        });
    }
    std::unique_ptr<SettingsStore> settings_;
    int status_ = 1;
};
} // namespace
wxIMPLEMENT_APP(EscapeApp);
