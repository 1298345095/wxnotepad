#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "editor_test_helpers.h"
#include "settings/settings_store.h"
#include "app/command_ids.h"
#include "app/toolbar_art.h"
#include "app/localization.h"
#include "file_helpers.h"
#include <wx/toolbar.h>
#include <wx/log.h>
#include <wx/dialog.h>
#include <wx/modalhook.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef __WXGTK__
#include <X11/Xatom.h>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Predicate> void Await(Predicate ready, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        wxYield();
        if (ready()) return;
        wxMilliSleep(10);
    } while (std::chrono::steady_clock::now() < deadline);
    Require(false, message);
}

bool NativeTopmost(wxTopLevelWindow* frame) {
#ifdef __WXMSW__
    return (GetWindowLongPtrW(static_cast<HWND>(frame->GetHandle()), GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
#elif defined(__WXGTK__)
    auto* window = gtk_widget_get_window(GTK_WIDGET(frame->GetHandle()));
    if (!GDK_IS_X11_DISPLAY(gdk_window_get_display(window))) return false;
    auto* display = GDK_WINDOW_XDISPLAY(window);
    Atom type = 0;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* bytes = nullptr;
    const auto state = XInternAtom(display, "_NET_WM_STATE", False);
    const auto above = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    constexpr long MaximumStateAtoms = 64;
    const int status = XGetWindowProperty(display, GDK_WINDOW_XID(window), state, 0, MaximumStateAtoms, False, XA_ATOM,
                                          &type, &format, &count, &remaining, &bytes);
    const auto* atoms = reinterpret_cast<const Atom*>(bytes);
    const bool active = status == Success && type == XA_ATOM && format == 32 && bytes &&
                        std::find(atoms, atoms + count, above) != atoms + count;
    if (bytes) XFree(bytes);
    return active;
#endif
}

bool AbovePeer(wxTopLevelWindow* frame, wxTopLevelWindow* peer) {
#ifdef __WXMSW__
    const auto first = static_cast<HWND>(frame->GetHandle());
    const auto second = static_cast<HWND>(peer->GetHandle());
    for (auto window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT)) {
        if (window == first) return true;
        if (window == second) return false;
    }
    throw std::runtime_error("owned native windows remain in Windows stacking order");
#elif defined(__WXGTK__)
    auto* window = gtk_widget_get_window(GTK_WIDGET(frame->GetHandle()));
    auto* display = GDK_WINDOW_XDISPLAY(window);
    Atom type = 0;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* bytes = nullptr;
    const auto atom = XInternAtom(display, "_NET_CLIENT_LIST_STACKING", False);
    const int status = XGetWindowProperty(display, DefaultRootWindow(display), atom, 0, 4096, False, XA_WINDOW, &type,
                                          &format, &count, &remaining, &bytes);
    Require(status == Success && type == XA_WINDOW && format == 32 && remaining == 0,
            "owned X11 WM publishes its actual native client stacking order");
    const auto* order = reinterpret_cast<unsigned long*>(bytes);
    const auto first = GDK_WINDOW_XID(window);
    const auto second = GDK_WINDOW_XID(gtk_widget_get_window(GTK_WIDGET(peer->GetHandle())));
    const auto* firstPosition = std::find(order, order + count, first);
    const auto* secondPosition = std::find(order, order + count, second);
    const bool found = firstPosition != order + count && secondPosition != order + count;
    const bool above = firstPosition > secondPosition;
    XFree(bytes);
    Require(found, "native WM stacking contains both owned clients");
    return above;
#endif
}

void CheckOwner(EditorFrame* frame, wxTopLevelWindow* dialog) {
    Require(dialog->GetParent() == frame, "native dialog retains the original editor owner");
#ifdef __WXMSW__
    Require(GetWindow(static_cast<HWND>(dialog->GetHandle()), GW_OWNER) == static_cast<HWND>(frame->GetHandle()),
            "native Windows popup remains owned by the existing editor frame");
#elif defined(__WXGTK__)
    Require(gtk_window_get_transient_for(GTK_WINDOW(dialog->GetHandle())) == GTK_WINDOW(frame->GetHandle()),
            "native GTK dialog retains its existing transient-for owner");
#endif
    Await([&] { return AbovePeer(dialog, frame); }, "native owned dialog stays above its editor owner");
}

void CheckFind(EditorFrame* frame, int topmost) {
    NativeSearch::Command(frame, wxID_FIND);
    auto* query = wxWindow::FindWindowByName("Find query", frame);
    Require(query, "real Find command opens its native query control");
    auto* dialog = wxDynamicCast(query->GetParent(), wxDialog);
    Require(dialog && dialog->IsShown(), "real Find window is natively shown");
    Await([&] { return wxWindow::FindFocus() == query; }, "real Find command focuses its query");
    CheckOwner(frame, dialog);
    for (const bool active : {false, true}) {
        NativeSearch::Command(frame, topmost);
        Await([&] { return NativeTopmost(frame) == active; }, "topmost changes while the owned Find window is open");
        Require(wxWindow::FindFocus() == query, "changing topmost retains native Find input focus");
        CheckOwner(frame, dialog);
    }
    dialog->Close();
    Await([&] { return !dialog->IsShown(); }, "real Find Close hides its existing modeless window");
}

class OwnedModal final : public wxModalDialogHook {
public:
    explicit OwnedModal(EditorFrame* frame) : frame_(frame) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() != _("Go To Line")) return wxID_NONE;
        opened = true;
        wxTheApp->CallAfter([this, dialog] {
            NativeKeys::AfterPresentation(dialog, [this, dialog](bool ready) {
                try {
                    Require(ready, "real Go To Line modal is natively presented");
                    CheckOwner(frame_, dialog);
                    Require(NativeTopmost(frame_), "real modal preserves its owner's topmost request");
                    NativeSearch::Click(dialog, wxID_CANCEL);
                } catch (...) {
                    failure = std::current_exception();
                    dialog->EndModal(wxID_CANCEL);
                }
            });
        });
        return wxID_NONE;
    }
    bool opened = false;
    std::exception_ptr failure;

private:
    EditorFrame* frame_;
};

void ToolbarClick(EditorFrame* frame, int command) {
    auto* toolbar = frame->GetToolBar();
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(frame->GetHandle()), WM_COMMAND, MAKEWPARAM(command, 0),
                 reinterpret_cast<LPARAM>(toolbar->GetHandle()));
#elif defined(__WXGTK__)
    const int position = toolbar->GetToolPos(command);
    auto* tool = gtk_toolbar_get_nth_item(toolbar->GTKGetToolbar(), position);
    Require(GTK_IS_TOOL_BUTTON(tool), "identity 26 has a real native GTK toolbar button");
    Require(GTK_IS_TOGGLE_TOOL_BUTTON(tool), "topmost uses a native toggle toolbar button");
    gtk_toggle_tool_button_set_active(GTK_TOGGLE_TOOL_BUTTON(tool),
                                      !gtk_toggle_tool_button_get_active(GTK_TOGGLE_TOOL_BUTTON(tool)));
#endif
}

void Check(EditorFrame* frame) {
    const int command = frame->GetMenuBar()->FindMenuItem(_("Sett&ings"), _("Always On &Top\tAlt+Shift+T"));
    Require(command == std::to_underlying(AppCommand::AlwaysOnTop),
            "localized menu uses stable Always On Top command identity");
    auto* menu = frame->GetMenuBar()->FindItem(command);
    auto* toolbar = frame->GetToolBar();
    Require(toolbar->GetToolsCount() == 3 && toolbar->GetToolByPos(0)->GetId() == command &&
                toolbar->GetToolByPos(1)->IsSeparator() && toolbar->GetToolByPos(2)->GetId() == command,
            "original ToolbarButtons=26 0 26 retains identity, duplicate order and separator");
    const auto actualArt =
        toolbar->FindById(command)->GetNormalBitmapBundle().GetBitmap(wxSize(16, 16)).ConvertToImage();
    const auto originalArt = ToolbarArt::Icon(25).GetBitmap(wxSize(16, 16)).ConvertToImage();
    const auto rgbBytes = static_cast<size_t>(originalArt.GetWidth() * originalArt.GetHeight()) * 3;
    Require(actualArt.GetSize() == originalArt.GetSize() &&
                std::memcmp(actualArt.GetData(), originalArt.GetData(), rgbBytes) == 0,
            "identity 26 uses original topmost artwork");
    Require(toolbar->FindById(command)->GetShortHelp() == wxStripMenuCodes(menu->GetItemLabel(), wxStrip_Mnemonics),
            "toolbar tooltip retains localized menu caption and original shortcut");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
#ifdef __WXGTK__
    const bool unsupported = GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(GTK_WIDGET(frame->GetHandle())));
#else
    const bool unsupported = false;
#endif
    if (unsupported) {
        Require(!menu->IsEnabled() && !menu->IsChecked() && !toolbar->GetToolEnabled(command) &&
                    !toolbar->GetToolState(command) && !menu->GetHelp().empty(),
                "unsupported backend disables menu and toolbar with localized explanation and no effective check");
        NativeSearch::Command(frame, command);
        Require(!NativeTopmost(frame), "unsupported native command cannot claim or apply topmost");
        return;
    }
    Require(menu->IsEnabled(), "native stacking backend enables Always On Top");
    auto* peer = new wxFrame(nullptr, wxID_ANY, "Owned topmost stacking peer", wxPoint(30, 30), wxSize(400, 300));
    peer->Show();
    try {
        Await([&] { return peer->IsShownOnScreen(); }, "owned stacking peer is shown");
        auto* editor = frame->Editor();
        using enum Scintilla::Message;
        TestEditor::ResetStream(editor, "alpha beta\nsecond\n");
        editor->Send(InsertText, 3, reinterpret_cast<intptr_t>("!"));
        editor->Send(SetSel, 9, 2);
        const auto text = TestEditor::Text(editor);
        const auto bounds = frame->GetRect();
        const auto verifyDocument = [&] {
            Require(TestEditor::Text(editor) == text && editor->Send(GetAnchor) == 9 &&
                        editor->Send(GetCurrentPos) == 2 && editor->Send(CanUndo) && !editor->Send(CanRedo),
                    "topmost preserves native editor contents, reversed selection and Undo/Redo");
            Require(frame->GetRect() == bounds, "topmost preserves native frame position and size");
        };
        const auto verify = [&](bool active) {
            std::printf("verify effective topmost=%d\n", active);
            std::fflush(stdout);
            Await([&] { return NativeTopmost(frame) == active; }, "native topmost state reaches requested value");
#ifdef __WXMSW__
            Require(SetWindowPos(static_cast<HWND>(peer->GetHandle()), HWND_TOP, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE),
                    "owned normal peer accepts native raise without activation");
            std::printf("stack expected=%d frame=%p peer=%p frameTopmost=%d peerTopmost=%d peerOwner=%p\n", active,
                        frame->GetHandle(), peer->GetHandle(), NativeTopmost(frame), NativeTopmost(peer),
                        GetWindow(static_cast<HWND>(peer->GetHandle()), GW_OWNER));
#else
            peer->Raise();
#endif
            Await([&] { return AbovePeer(frame, peer) == active; },
                  "owned normal peer proves effective native topmost and return to ordinary stacking");
            frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
            Require(menu->IsChecked() == active && toolbar->GetToolState(command) == active &&
                        toolbar->GetToolByPos(2)->IsToggled() == active,
                    "menu and duplicate native toolbar buttons report actual effective state");
            verifyDocument();
        };
        verify(false);
        NativeSearch::Command(frame, command);
        verify(true);
        ToolbarClick(frame, command);
        verify(false);
        NativeKeys::Shortcut(frame, 'T', false, true, true);
        verify(true);
        CheckFind(frame, command);
        OwnedModal modal(frame);
        NativeSearch::Command(frame, std::to_underlying(AppCommand::GoToLine));
        if (modal.failure) std::rethrow_exception(modal.failure);
        Require(modal.opened, "real Go To Line command opens the existing native modal");
        verify(true);
        NativeKeys::Shortcut(frame, WXK_F11, false);
        NativeKeys::Shortcut(frame, WXK_F11, false);
        Await([&] { return frame->GetRect() == bounds; },
              "native fullscreen restoration reaches the original placement before topmost invariants");
        verify(true);
        frame->Iconize();
        Await([&] { return frame->IsIconized(); }, "owned topmost frame minimizes");
        frame->Iconize(false);
        Await([&] { return !frame->IsIconized(); }, "owned topmost frame restores");
        Await([&] { return frame->GetRect() == bounds; },
              "native minimize restoration reaches the original placement before topmost invariants");
        verify(true);
        NativeSearch::Command(frame, command);
        verify(false);
        editor->Send(Undo);
        Require(TestEditor::Text(editor) == "alpha beta\nsecond\n", "topmost leaves native Undo usable");
        editor->Send(Redo);
        Require(TestEditor::Text(editor) == text, "topmost leaves native Redo usable");
    } catch (...) {
        peer->Destroy();
        throw;
    }
    peer->Destroy();
}

class TopmostApp final : public wxApp {
public:
    bool OnInit() override {
        delete wxLog::SetActiveTarget(new wxLogStderr);
        if (argc > 1) {
            long language = 0;
            Require(wxString(argv[1]).ToLong(&language), "topmost language is an original LANGID");
            AppLocalization::Initialize(static_cast<uint16_t>(language));
        }
        settings_ =
            std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory() / "topmost.ini"));
        settings_->values.toolbarButtons = "26 0 26";
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "owned editor is natively presented");
                Check(frame);
                result_ = 0;
                std::puts(
                    "PASS: native Always On Top menu/shortcut/toolbar, real owned stacking and document invariants");
            } catch (const std::exception& error) {
                DiagnosticPrint::Print(stderr, "FAIL: {}\n", error.what());
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
    std::unique_ptr<SettingsStore> settings_;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(TopmostApp);
