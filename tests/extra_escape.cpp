#include "native_key_helpers.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include "editor_test_helpers.h"
#include "file_command_driver.h"
#include <wx/weakref.h>
#include <memory>
#ifdef __WXGTK__
#include <gdk/gdkkeysyms.h>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
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
        const auto title = wxString::Format("wxnotepad Escape target %p", static_cast<void*>(frame));
        frame->SetTitle(title);
        NativeKeys::FocusEditor(frame, title, focus);
    }
    {
        wxFFile request(focus, "wb");
        Require(request.Write(shift ? "key:shift+Escape" : "key:Escape"), "publish native Escape gesture");
    }
    Await(
        [&] {
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
        Await([&] { return observation.received; }, "native Escape reaches the composing editor dispatch path");
#endif
#ifdef __WXMSW__
        SendMessageW(static_cast<HWND>(editor->GetHandle()), WM_IME_ENDCOMPOSITION, 0, 0);
#elif defined(__WXGTK__)
        g_signal_emit_by_name(editor->m_imContext, "preedit-end");
#endif
    }
    driver.Stop();
    std::fprintf(stderr, "composition result prompts=%d shown=%d text='%s' anchor=%lld caret=%lld undo=%lld\n",
                 driver.prompts, frame->IsShown(), TestEditor::Text(editor).c_str(),
                 static_cast<long long>(editor->Send(GetAnchor)), static_cast<long long>(editor->Send(GetCurrentPos)),
                 static_cast<long long>(editor->Send(CanUndo)));
    Require(driver.prompts == 0 && frame->IsShown() && TestEditor::Text(editor) == "IME cancellation!" &&
                editor->Send(GetAnchor) == 8 && editor->Send(GetCurrentPos) == 2 && editor->Send(CanUndo),
            "active native composition lifecycle retains Escape/Shift+Escape cancellation ownership");
    std::puts("PASS: composition-lifecycle replay guard (real human IME evidence remains separately required)");
}
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
        Await([&] { return GetForegroundWindow() == window && GetFocus() == focused; },
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
    std::fprintf(stderr, "WIDGET outcome dialogShown=%d mainShown=%d mainDeleting=%d\n", dialog->IsShown(),
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
    Await([&] { return !dialog->IsShown(); }, "Escape dismisses the focused modeless dialog");
    Require(driver.prompts == 0 && frame->IsShown(), "dialog Escape does not invoke the main-frame exit preference");
    wxDialog modal(frame, wxID_ANY, "wxnotepad Escape modal", wxDefaultPosition, wxSize(360, 150));
    auto* field = new wxTextCtrl(&modal, wxID_ANY, "native field", wxPoint(15, 15), wxSize(300, 25));
    new wxButton(&modal, wxID_CANCEL, "Cancel", wxPoint(15, 55));
    bool delivered = false;
    modal.Bind(wxEVT_SHOW, [&](wxShowEvent& event) {
        if (event.IsShown())
            NativeKeys::AfterPresentation(&modal, [&](bool ready) {
                Require(ready, "owned native modal dialog is presented");
                WidgetEscape(field);
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
    const auto expectDocument = [&] {
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
    Await([&] { return frame->GetSize() != normal; }, "F11 enters the native fullscreen window");
    NativeKeys::Shortcut(frame, WXK_ESCAPE, false);
    Await([&] { return frame->GetSize() == normal; }, "Escape restores fullscreen before the configured action");
    Require(!frame->IsIconized(), "leaving fullscreen does not also minimize the window");
    for (const int modifiers : {int{wxMOD_CONTROL}, int{wxMOD_ALT}, wxMOD_CONTROL | wxMOD_SHIFT}) {
        NativeKeys::Shortcut(frame, WXK_ESCAPE, (modifiers & wxMOD_CONTROL) != 0, (modifiers & wxMOD_ALT) != 0,
                             (modifiers & wxMOD_SHIFT) != 0);
        Require(!frame->IsIconized(), "arbitrary modified Escape is not plain Escape");
    }
    WindowActionKey(frame);
    Await([&] { return !NativeWindowVisible(frame); }, "configured Escape actually minimizes the native window");
    frame->Iconize(false);
    frame->Raise();
#ifdef __WXGTK__
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(GTK_WIDGET(frame->GetHandle()))))
        Require(wxExecute("xdotool key --clearmodifiers super+Tab", wxEXEC_SYNC | wxEXEC_NODISABLE) == 0,
                "restore the owned minimized window through the private compositor switcher");
#endif
    Await([&] { return NativeWindowVisible(frame); }, "native window restores from minimized state");
    editor->SetFocus();
    expectDocument();
    NativeKeys::Shortcut(frame, 'Z', true);
    Require(TestEditor::Text(editor) == "original document\nsecond line", "restored editor retains working Undo");
    editor->Send(SetSavePoint);
    std::puts("PASS: native Escape precedence, exact modifiers, minimize, restore and working history");
}
class EscapeApp final : public wxApp {
public:
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
                CheckMenu(frame);
                CheckComposition(frame);
                CheckWidgetFocus(frame);
                CheckMinimize(frame);
                CloseCase(frame, 0);
                return;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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
                using enum Scintilla::Message;
                auto* editor = candidate->Editor();
                const auto path =
                    TestFiles::TemporaryDirectory() / ("escape-save-" + std::to_string(scenario) + ".txt");
                if (scenario == 2)
                    Require(candidate->SaveFirst(TestFiles::Filename(path)).status == SaveStatus::Saved,
                            "failed-Save case begins with a named saved document");
                editor->Send(AddText, 15, reinterpret_cast<intptr_t>("unsaved content"));
                wxYield();
                const auto before = TestEditor::Text(editor);
                const auto originalPath = candidate->DocumentPath();
                if (scenario == 1) editor->Send(SetReadOnly, 1);
                if (scenario == 2) {
                    std::filesystem::remove(path);
                    std::filesystem::create_directory(path);
                }
                const bool shift = scenario == 1 || scenario == 4;
                if (shift) {
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
                    Await([&] { return candidate->GetSize() != normal && editor->GetClientSize().x > 100; },
                          "candidate native fullscreen presentation finishes before Shift+Escape");
                    editor->Send(CallTipShow, 1,
                                 reinterpret_cast<intptr_t>("Shift+Escape bypasses CallTip/fullscreen"));
                }
                const int choice = scenario < 2 ? wxID_CANCEL : scenario == 4 ? wxID_NO : wxID_YES;
                NativeDialogs::FileCommandDriver driver(NativeDialogs::FileDialogProfile::Save, choice,
                                                        scenario == 3 ? TestFiles::Filename(path) : wxString{});
                candidate->SetTitle(wxString::Format("wxnotepad Escape candidate %d", scenario));
                WindowActionKey(candidate, shift);
                Await([&] { return driver.prompts == 1; },
                      "actual Escape exit opens the existing unsaved confirmation");
                if (scenario < 3) {
                    Await([&] { return retained && retained->IsEnabled(); },
                          "Cancel/failed Save returns to the editor");
                    if (scenario == 2)
                        Await([&] { return driver.errors == 1; }, "failed Save shows the existing native error");
                    Require(retained && !retained->IsBeingDeleted() && TestEditor::Text(editor) == before &&
                                editor->Send(GetModify) && retained->DocumentPath() == originalPath,
                            "Cancel and failed Save preserve the same owned document and history");
                    Require((editor->Send(GetReadOnly) != 0) == (scenario == 1),
                            "exit cancellation preserves read-only mode");
                    // SCI_CANUNDO is false in read-only mode even with retained history.
                    if (scenario == 1) editor->Send(SetReadOnly, 0);
                    Require(editor->Send(CanUndo), "cancelled close retains the document undo history");
                    if (scenario == 1) editor->Send(SetReadOnly, 1);
                    retained->Destroy();
                } else {
                    Await([&] { return !retained; }, "successful Save/Discard closes the owned frame");
                    if (scenario == 3)
                        Require(driver.choosers == 1 && TestFiles::Bytes(path) == "unsaved content",
                                "Save from Escape writes the actual unnamed document before closing");
                }
                driver.Stop();
                Require(sentinel->IsShown() && !sentinel->IsBeingDeleted(),
                        "exit closes only its owned application window");
                wxYield();
                CloseCase(sentinel, scenario + 1);
                return;
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL close case %d: %s\n", scenario, error.what());
                if (retained)
                    std::fprintf(stderr, "candidate size=%dx%d client=%dx%d active=%d editorFocus=%d\n",
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
