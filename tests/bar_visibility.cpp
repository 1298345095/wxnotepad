#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include <memory>
#include <wx/statusbr.h>

namespace {
constexpr int LifecycleArgumentCount = 5;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <class Predicate> void WaitForLayout(Predicate ready, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!ready() && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(1);
    }
    Require(ready(), message);
}
bool MenuVisible(EditorFrame* frame) {
#ifdef __WXMSW__
    return GetMenu(static_cast<HWND>(frame->GetHandle())) != nullptr;
#else
    return frame->GetMenuBar()->IsShown();
#endif
}
void HiddenMenuEditing(EditorFrame* frame, int menu, int status) {
    using enum Scintilla::Message;
    auto* menus = frame->GetMenuBar();
    const auto appearance = menus->FindItem(menus->FindMenuItem("Settings", "Appearance"))->GetSubMenu();
    Require(menus->FindItem(menu)->GetMenu() == appearance && menus->FindItem(status)->GetMenu() == appearance,
            "both visibility controls belong to Settings Appearance");
    Require(menus->FindItem(menu)->GetItemLabel().AfterFirst('	') == "Alt+F11" &&
                menus->FindItem(status)->GetItemLabel().AfterFirst('	') == "Shift+F11",
            "menu labels expose the exact original gestures");
    auto* editor = frame->Editor();
    const std::string text = std::string(450, 'x') + "\nsecond";
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>(text.c_str()));
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(Undo);
    editor->Send(SetSel, 23, 5);
    const auto encoding = frame->DocumentEncoding();
    const auto marginWidth = editor->Send(GetMarginWidthN, 0);
    const auto checkDocument = [&] {
        Require(NativeSearch::Text(editor) == text && editor->Send(GetAnchor) == 23 &&
                    editor->Send(GetCurrentPos) == 5 && !frame->DocumentIsModified() && !editor->Send(CanUndo) &&
                    editor->Send(CanRedo) && frame->DocumentEncoding() == encoding,
                "bar changes preserve bytes, selection, encoding, modified state and history");
    };
    NativeKeys::Shortcut(frame, WXK_F11, false);
    NativeKeys::Shortcut(frame, WXK_F11, true);
    Require(MenuVisible(frame) && frame->GetStatusBar()->IsShown(), "plain and Ctrl F11 toggle neither bar");
    NativeKeys::Shortcut(frame, WXK_F11, false);
    NativeKeys::Shortcut(frame, WXK_F11, false, true);
    Require(!MenuVisible(frame) && frame->GetStatusBar()->IsShown(), "Alt F11 hides menu only");
    const auto withStatus = editor->GetSize();
    NativeKeys::Shortcut(frame, WXK_F11, false, false, true);
    NativePaint::Complete(editor);
    Require(!MenuVisible(frame) && !frame->GetStatusBar()->IsShown(), "Shift F11 hides status only");
    WaitForLayout([&] { return editor->GetSize().y > withStatus.y; },
                  "keyboard status toggle recalculates editor area");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!menus->IsChecked(menu) && !menus->IsChecked(status), "hidden bars have unchecked commands");
    checkDocument();
    frame->SetSize(frame->FromDIP(wxSize(640, 460)));
    wxYield();
    NativePaint::Complete(editor);
    Require(editor->GetClientSize().x > 0 && editor->GetClientSize().y > 0 && editor->Send(WrapCount, 0) > 1 &&
                editor->Send(GetMarginWidthN, 0) == marginWidth,
            "hidden bars retain valid wrapped layout and margins after native resize");
    checkDocument();
    NativeKeys::Shortcut(frame, 'Y', true);
    Require(NativeSearch::Text(editor) == text + "!" && frame->DocumentIsModified(),
            "Redo accelerator remains usable with hidden menu");
    NativeKeys::Shortcut(frame, 'Z', true);
    Require(NativeSearch::Text(editor) == text && !frame->DocumentIsModified(),
            "Undo accelerator retains original save point with hidden menu");
    NativeKeys::Shortcut(frame, 'F', true);
    auto* query = wxDynamicCast(wxWindow::FindWindowByName("Find query", frame), wxTextCtrl);
    Require(query && query->IsShownOnScreen(), "application Find accelerator works with hidden menu");
    query->SetFocus();
    wxYield();
    Require(query->HasFocus(), "native focus can leave editor while menu is hidden");
    query->GetParent()->Hide();
    editor->SetFocus();
    NativeKeys::Shortcut(frame, WXK_F11, false, true);
    Require(MenuVisible(frame) && !frame->GetStatusBar()->IsShown(), "Alt F11 restores menu after focus return");
    NativeSearch::Command(frame, menus->FindMenuItem("Scheme", "Python"));
    editor->Send(GotoPos, text.size());
    NativeKeys::Shortcut(frame, WXK_F11, false, false, true);
    Require(frame->GetStatusBar()->IsShown() && frame->GetStatusBar()->GetStatusText().Contains("Python") &&
                frame->GetStatusBar()->GetStatusText().Contains("UTF-8") &&
                frame->GetStatusBar()->GetStatusText(1) == "Ln 2, Col 7",
            "restored status immediately shows language, encoding and position changed while hidden");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(menus->IsChecked(menu) && menus->IsChecked(status), "restored bars have checked commands");
    NativeKeys::Shortcut(frame, WXK_F11, false, true);
    NativeKeys::Shortcut(frame, 'N', true);
    Require(NativeSearch::Text(editor).empty() && !MenuVisible(frame),
            "File New menu accelerator remains usable with hidden menu after native focus return");
    NativeKeys::Shortcut(frame, WXK_F11, false, true);
    editor->Send(SetSavePoint);
    std::puts("PASS: exact native shortcuts, hidden-menu editing, focus return, status freshness and resize");
}
class VisibilityApp final : public wxApp {
public:
    bool OnInit() override {
        // The standalone layout scenario owns the original default inputs.
        // Persistence scenarios run copied executables with their supplied INI.
        settings_ = argc == LifecycleArgumentCount ? std::make_unique<SettingsStore>()
                                                   : std::make_unique<SettingsStore>(TestFiles::Filename(
                                                         TestFiles::TemporaryDirectory() / "bar-visibility.ini"));
        // Find tests its application accelerator without consulting the shared clipboard.
        if (argc == 1) settings_->values.selectOption &= ~SelectOption_CopyPasteBufferAsFindText;
        auto* frame = new EditorFrame(settings_.get());
        frame->ShowWithoutActivating();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) mutable {
            try {
                Require(ready, "visibility window is presented");
                auto* menus = frame->GetMenuBar();
                const int menu = menus->FindMenuItem("Settings", "Show Menu");
                const int status = menus->FindMenuItem("Settings", "Show Statusbar");
                Require(menu != wxNOT_FOUND && status != wxNOT_FOUND,
                        "Settings Appearance exposes both original visibility commands");
#ifdef __WXMSW__
                if (argc == 2 && argv[1] == "--cleanup") {
                    NativeSearch::Command(frame, menu);
                    const auto nativeMenu = static_cast<HMENU>(menus->GetHMenu());
                    delete std::exchange(frame, nullptr);
                    Require(!IsMenu(nativeMenu), "closing a hidden-menu frame releases its native menu");
                    result_ = 0;
                    std::puts("PASS: closing hidden-menu frame releases native menu");
                    ExitMainLoop();
                    return;
                }
#endif
                if (argc > 1) {
                    Require(argc == LifecycleArgumentCount,
                            "lifecycle requires expected menu/status, action and save policy");
                    Require(MenuVisible(frame) == (argv[1] == "1") &&
                                frame->GetStatusBar()->IsShown() == (argv[2] == "1"),
                            "original menu and status INI values apply to restarted native window");
                    if (argv[3] == "toggle") {
                        NativeKeys::Shortcut(frame, WXK_F11, false, true);
                        NativeKeys::Shortcut(frame, WXK_F11, false, false, true);
                        Require(MenuVisible(frame) != (argv[1] == "1") &&
                                    frame->GetStatusBar()->IsShown() != (argv[2] == "1"),
                                "original gestures restore bars even on a restarted hidden-menu window");
                    }
                    if (argv[4] == "explicit")
                        NativeSearch::Command(frame, menus->FindMenuItem("Settings", "Save Settings Now"));
                    result_ = 0;
                    frame->Close();
                    return;
                }
                Require(MenuVisible(frame) && frame->GetStatusBar()->IsShown(), "both bars default to visible");
                frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
                Require(menus->IsChecked(menu) && menus->IsChecked(status), "both command checks reflect visibility");
                NativePaint::Complete(frame->Editor());
                const auto initial = frame->Editor()->GetSize();
                const auto initialStatus = frame->GetStatusBar()->GetSize();
                std::printf("Initial frame client height=%d\n", frame->GetClientSize().y);
                NativeSearch::Command(frame, status);
                NativePaint::Complete(frame->Editor());
                Require(!frame->GetStatusBar()->IsShown(), "native menu hides status bar");
                WaitForLayout([&] { return frame->Editor()->GetSize().y > initial.y; },
                              "hidden status bar gives editor its client area");
                const auto withMenu = frame->Editor()->GetSize();
                NativeSearch::Command(frame, menu);
                Require(!MenuVisible(frame), "native menu hides menu bar");
                WaitForLayout([&] { return frame->Editor()->GetSize().y > withMenu.y; },
                              "hidden menu gives editor its client area");
                NativeSearch::Command(frame, menu);
                WaitForLayout([&] { return frame->Editor()->GetSize().y == withMenu.y; },
                              "restored menu returns its native client area");
                NativeSearch::Command(frame, status);
                std::printf("Native bar layout: initial editor=%d status=%d, hidden-status editor=%d, "
                            "restored editor=%d status=%d, frame=%d client=%d\n",
                            initial.y, initialStatus.y, withMenu.y, frame->Editor()->GetSize().y,
                            frame->GetStatusBar()->GetSize().y, frame->GetSize().y, frame->GetClientSize().y);
                WaitForLayout([&] { return frame->Editor()->GetSize().y == initial.y; },
                              "restored status returns its native client area");
                Require(MenuVisible(frame) && frame->GetStatusBar()->IsShown(), "native commands restore both bars");
                HiddenMenuEditing(frame, menu, status);
                result_ = 0;
                std::puts("PASS: original native menu controls, check states and client-area layout");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
            }
            if (frame) frame->Destroy();
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
wxIMPLEMENT_APP(VisibilityApp);
