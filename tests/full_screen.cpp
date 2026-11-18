#include "native_key_helpers.h"
#include "native_completion_helpers.h"
#include "native_paint_helpers.h"
#include "editor_test_helpers.h"
#include "settings/settings_store.h"
#include <wx/display.h>
#include <wx/filename.h>
#include <wx/ffile.h>
#include <cstdio>
#ifdef __WXGTK__
#include <gdk/gdkx.h>
#undef None
#endif

namespace {
using enum Scintilla::Message;

void Require(bool condition, const char* message) {
    if (!condition) throw TestEditor::Failure(message);
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

#ifdef __WXGTK__
GdkWindow* NativeWindow(EditorFrame* frame) {
    return gtk_widget_get_window(GTK_WIDGET(frame->GetHandle()));
}

void ReportGtkGeometry(EditorFrame* frame, const char* phase) {
    auto* native = NativeWindow(frame);
    gint left = 0, top = 0;
    gdk_window_get_origin(native, &left, &top);
    const auto rect = frame->GetScreenRect();
    std::fprintf(stderr, "%s: wx=%d,%d %dx%d native=%d,%d %dx%d state=%u\n", phase, rect.x, rect.y, rect.width,
                 rect.height, left, top, gdk_window_get_width(native), gdk_window_get_height(native),
                 static_cast<unsigned>(gdk_window_get_state(native)));
}

bool IsWayland(EditorFrame* frame) {
    return GDK_IS_WAYLAND_DISPLAY(gdk_window_get_display(NativeWindow(frame)));
}

bool IsManagedX11(EditorFrame* frame) {
    return !IsWayland(frame) &&
           gdk_x11_screen_supports_net_wm_hint(gdk_window_get_screen(NativeWindow(frame)),
                                               gdk_atom_intern_static_string("_NET_WM_STATE_FULLSCREEN"));
}
#endif

bool NativeFullScreen(EditorFrame* frame) {
#ifdef __WXMSW__
    const auto window = static_cast<HWND>(frame->GetHandle());
    RECT rect{};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetWindowRect(window, &rect) ||
        !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        return false;
    const auto& screen = monitor.rcMonitor;
    return rect.left <= screen.left && rect.top <= screen.top && rect.right >= screen.right &&
           rect.bottom >= screen.bottom && !(GetWindowLongW(window, GWL_STYLE) & WS_THICKFRAME);
#elif defined(__WXGTK__)
    auto* window = NativeWindow(frame);
    const bool confirmed = (gdk_window_get_state(window) & GDK_WINDOW_STATE_FULLSCREEN) != 0;
    if (IsWayland(frame)) return confirmed;
    const auto bounds = wxDisplay(frame).GetGeometry();
    gint left = 0, top = 0;
    gdk_window_get_origin(window, &left, &top);
    return (confirmed || !IsManagedX11(frame)) && left <= bounds.x && top <= bounds.y &&
           left + gdk_window_get_width(window) >= bounds.x + bounds.width &&
           top + gdk_window_get_height(window) >= bounds.y + bounds.height;
#else
    return frame->IsFullScreen();
#endif
}

bool NativeMenuVisible(EditorFrame* frame) {
#ifdef __WXMSW__
    return GetMenu(static_cast<HWND>(frame->GetHandle())) != nullptr;
#else
    return frame->GetMenuBar()->IsShown();
#endif
}

const wxSize NormalSize(720, 510);

void AwaitOwnedWindow(EditorFrame* frame) {
#ifdef __WXGTK__
    if (IsManagedX11(frame))
        Await(
            [&] {
                return gtk_widget_get_mapped(GTK_WIDGET(frame->GetHandle())) && frame->IsActive() &&
                       frame->GetSize() == NormalSize;
            },
            "managed X11 presents the owned editor at its requested size before shortcuts");
#endif
}

int ViewCommand(EditorFrame* frame) {
#ifdef __WXMSW__
    const auto id = frame->GetMenuBar()->FindMenuItem("View", "Toggle Full Screen");
#else
    const auto id = frame->GetMenuBar()->FindMenuItem("View", "Toggle Full Screen [Esc]");
#endif
    auto* item = frame->GetMenuBar()->FindItem(id);
    Require(item && !item->IsCheckable() && item->IsEnabled(), "View offers the enabled original toggle command");
    return id;
}

void CancelPopup(EditorFrame* frame, Scintilla::Message active, const char* message) {
    auto* editor = frame->Editor();
    Await([&] { return editor->Send(active) != 0; }, "editor activates the requested popup");
#ifdef __WXMSW__
    if (!NativeSearch::WindowsShortcut(editor, VK_ESCAPE, {{VK_CONTROL, 0}, {VK_MENU, 0}, {VK_SHIFT, 0}}))
        NativeCompletion::Key(editor, VK_ESCAPE);
#else
    NativeKeys::Shortcut(frame, WXK_ESCAPE, false);
#endif
    Require(!editor->Send(active) && NativeFullScreen(frame), message);
}

bool SupportsMaximize(EditorFrame* frame) {
#ifdef __WXGTK__
    return IsWayland(frame) || IsManagedX11(frame);
#else
    return true;
#endif
}

bool NativeMaximized(EditorFrame* frame) {
#ifdef __WXMSW__
    return IsZoomed(static_cast<HWND>(frame->GetHandle())) != FALSE;
#elif defined(__WXGTK__)
    return (gdk_window_get_state(NativeWindow(frame)) & GDK_WINDOW_STATE_MAXIMIZED) != 0;
#else
    return frame->IsMaximized();
#endif
}

void CheckMaximized(EditorFrame* frame) {
    if (!SupportsMaximize(frame)) return;
#ifdef __WXGTK__
    const bool compareBounds = !IsWayland(frame);
#else
    const bool compareBounds = true;
#endif
    const auto normalBounds = frame->GetScreenRect();
    frame->Maximize();
    Await([&] { return NativeMaximized(frame) && (!compareBounds || frame->GetScreenRect() != normalBounds); },
          "the native window manager maximizes this frame");
    const auto maximizedBounds = frame->GetScreenRect();
    for (int cycle = 0; cycle != 2; ++cycle) {
        NativeSearch::Command(frame, ViewCommand(frame));
        Await([&] { return NativeFullScreen(frame); }, "View command enters fullscreen from maximized state");
        NativeKeys::Shortcut(frame, WXK_F11, false);
        Await(
            [&] {
                return !NativeFullScreen(frame) && NativeMaximized(frame) &&
                       (!compareBounds || frame->GetScreenRect() == maximizedBounds);
            },
            "F11 restores the maximized window after using the native View command");
    }
    std::puts("PASS: View command and F11 repeatedly restore native maximized state");
}

void CheckCycle(EditorFrame* frame) {
    AwaitOwnedWindow(frame);
#ifdef __WXMSW__
    std::printf("Windows DPI: %u; available displays: %u\n", GetDpiForWindow(static_cast<HWND>(frame->GetHandle())),
                wxDisplay::GetCount());
#elif defined(__WXGTK__)
    ReportGtkGeometry(frame, "normal before F11");
#endif
    const auto initial = frame->GetScreenRect();
    const auto size = frame->GetSize();
    auto* editor = frame->Editor();
    const std::string row = "alpha beta gamma ";
    std::string document;
    for (int line = 0; line < 200; ++line) {
        for (int word = 0; word < 10; ++word)
            document += row;
        document += '\n';
    }
    editor->Send(SetWrapMode, std::to_underlying(Scintilla::Wrap::None));
    TestEditor::ResetStream(editor, document.c_str());
    editor->Send(InsertText, 200, reinterpret_cast<intptr_t>("!"));
    editor->Send(SetSel, 8402, 8406);
    editor->Send(SetFirstVisibleLine, 43);
    NativePaint::Complete(editor);
    const auto contents = TestEditor::Text(editor);
    const auto anchor = editor->Send(GetAnchor);
    const auto caret = editor->Send(GetCurrentPos);
    const auto encoding = frame->DocumentEncoding();
    const auto expectDocument = [&] {
        Require(TestEditor::Text(editor) == contents && editor->Send(GetAnchor) == anchor &&
                    editor->Send(GetCurrentPos) == caret && editor->Send(CanUndo) && editor->Send(GetModify) &&
                    frame->DocumentEncoding() == encoding,
                "fullscreen preserves document bytes, selection, modified state, history and encoding");
        Require(editor->Send(GetFirstVisibleLine) == 43,
                "fullscreen preserves the original top visible line when the viewport can display it");
    };
    Require(NativeMenuVisible(frame) && frame->GetStatusBar()->IsShown(), "normal frame has both existing bars");
    NativeKeys::Shortcut(frame, WXK_F11, false);
    Await([&] { return NativeFullScreen(frame); }, "unmodified F11 enters native fullscreen");
    Require(NativeMenuVisible(frame) && frame->GetStatusBar()->IsShown(), "F11 retains both existing bars");
    expectDocument();
    NativeKeys::Shortcut(frame, WXK_F11, true, false, true);
    Require(NativeFullScreen(frame) && NativeMenuVisible(frame) && frame->GetStatusBar()->IsShown(),
            "extra modifiers do not fall through to fullscreen or visibility commands");
    editor->Send(AutoCSetSeparator, ' ');
    editor->Send(AutoCShow, 0, reinterpret_cast<intptr_t>("alpha alpine"));
    CancelPopup(frame, AutoCActive, "Escape cancels completion before leaving fullscreen");
    editor->Send(CallTipShow, caret, reinterpret_cast<intptr_t>("sample CallTip"));
    CancelPopup(frame, CallTipActive, "Escape cancels a CallTip before leaving fullscreen");
    NativeKeys::Shortcut(frame, WXK_ESCAPE, false);
    Await([&] { return !NativeFullScreen(frame) && frame->GetSize() == size; }, "Escape restores the normal size");
#ifdef __WXGTK__
    if (!IsWayland(frame))
#endif
        Require(frame->GetScreenRect() == initial, "normal bounds are restored on positioning-capable backends");
    expectDocument();
    std::puts("PASS: F11 and Escape enter/exit native fullscreen and preserve bars and document state");
    CheckMaximized(frame);
    expectDocument();
}

wxMenuItem* Setting(EditorFrame* frame, const char* label) {
    auto* menu = frame->GetMenuBar();
    auto* item = menu->FindItem(menu->FindMenuItem("Settings", label));
    Require(item, "the fullscreen workflow exposes its settings through the native menu");
    return item;
}

wxString SavedSettings(const wxString& path) {
    wxFFile file(path, "rb");
    wxString text;
    Require(file.ReadAll(&text, wxMBConvUTF16LE()), "read the independently saved INI file");
    return text;
}

void CheckSettings(EditorFrame* frame, const wxString& path) {
    AwaitOwnedWindow(frame);
    Require(!NativeFullScreen(frame) && !NativeMenuVisible(frame) && !frame->GetStatusBar()->IsShown(),
            "original default starts windowed, retaining independent hidden bars");
    auto* startup = Setting(frame, "On Startup");
    auto* title = Setting(frame, "Hide Title");
    const auto saveNow = Setting(frame, "Save Settings Now")->GetId();
    const auto saveOnExit = Setting(frame, "Save Settings On Exit")->GetId();
    NativeSearch::Command(frame, saveOnExit);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(startup->IsCheckable() && !startup->IsChecked() && title->IsCheckable() && title->IsChecked(),
            "On Startup defaults off and Hide Title defaults on");
    NativeSearch::Command(frame, startup->GetId());
    Require(!NativeFullScreen(frame), "changing On Startup does not enter fullscreen on the current frame");
    NativeSearch::Command(frame, startup->GetId());
    NativeKeys::Shortcut(frame, WXK_F11, false);
    Await([&] { return NativeFullScreen(frame); }, "F11 works while the existing menu is hidden");
    NativeSearch::Command(frame, saveNow);
    const auto transient = SavedSettings(path);
    Require(transient.Contains("SaveSettings=0\r\n") && !transient.Contains("FullScreenMode="),
            "explicit save works with exit saving disabled and leaves transient fullscreen out of the INI");
#ifdef __WXMSW__
    const int hiddenTop = frame->GetScreenRect().GetTop();
#endif
    NativeSearch::Command(frame, title->GetId());
    Require(NativeFullScreen(frame) && !NativeMenuVisible(frame) && !frame->GetStatusBar()->IsShown(),
            "changing Hide Title in fullscreen keeps the native mode and existing bars");
#ifdef __WXMSW__
    Require(frame->GetScreenRect().GetTop() > hiddenTop, "Windows immediately brings the native title into view");
#endif
    NativeSearch::Command(frame, saveNow);
    Require(SavedSettings(path).Contains("FullScreenMode=0\r\n"), "Save Settings Now writes the live policy bits");
    NativeSearch::Command(frame, startup->GetId());
    Require(NativeFullScreen(frame) && SavedSettings(path).Contains("FullScreenMode=0\r\n"),
            "changing On Startup does not transition or write the current window");
    NativeSearch::Command(frame, saveOnExit);
    Require(frame->Close(), "close the owned settings fixture through its usual save-on-exit path");
    const auto saved = SavedSettings(path);
    Require(saved.Contains("FullScreenMode=1\r\n") && saved.Contains("ShowMenu=0\r\n") &&
                saved.Contains("ShowStatusbar=0\r\n"),
            "exit saves only the original policy bit and both independent visibility choices");
}
} // namespace

class FullScreenApp final : public wxApp {
public:
    bool OnInit() override {
        wxString settingsMode;
        if (wxGetEnv("WXNOTEPAD_FULL_SCREEN_SETTINGS", &settingsMode) && settingsMode == "1") {
            iniPath_ = wxFileName::CreateTempFileName("wxnotepad-fullscreen-");
            wxFFile fixture(iniPath_, "wb");
            if (!fixture.Write("[Settings]\nShowMenu=0\nShowStatusbar=0\n") || !fixture.Close()) return false;
            settings_ = std::make_unique<SettingsStore>(iniPath_);
        }
        auto* frame = new EditorFrame(settings_.get());
        frame->SetSize(wxRect(wxPoint(100, 120), NormalSize));
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "native window received initial presentation");
                if (settings_) {
                    CheckSettings(frame, iniPath_);
                    CheckRelaunch();
                    return;
                }
                CheckCycle(frame);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
#ifdef __WXGTK__
                ReportGtkGeometry(frame, "at failure");
#endif
                status_ = 1;
            }
            frame->Destroy();
        });
        return true;
    }
    int OnRun() override {
        wxApp::OnRun();
        return status_;
    }
    int OnExit() override {
        if (!iniPath_.empty()) wxRemoveFile(iniPath_);
        return wxApp::OnExit();
    }

private:
    void CheckRelaunch() {
        reloadedSettings_ = std::make_unique<SettingsStore>(iniPath_);
        auto* frame = new EditorFrame(reloadedSettings_.get());
        frame->SetSize(wxRect(wxPoint(100, 120), NormalSize));
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "reopened native window received initial presentation");
                Await([&] { return NativeFullScreen(frame); }, "reopening applies original On Startup policy");
                Require(!NativeMenuVisible(frame) && !frame->GetStatusBar()->IsShown(),
                        "startup fullscreen retains saved hidden bars");
                NativeKeys::Shortcut(frame, WXK_ESCAPE, false);
                Await([&] { return !NativeFullScreen(frame) && frame->GetSize() == NormalSize; },
                      "Escape exits startup fullscreen to the restored normal size");
                std::puts("PASS: native Full Screen Mode checks, save now, close, reload and Escape");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
                status_ = 1;
            }
            frame->Destroy();
        });
    }

    int status_ = 0;
    wxString iniPath_;
    std::unique_ptr<SettingsStore> settings_;
    std::unique_ptr<SettingsStore> reloadedSettings_;
};
wxIMPLEMENT_APP(FullScreenApp);
