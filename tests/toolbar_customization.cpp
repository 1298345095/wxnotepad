#include "native_key_helpers.h"
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include "app/localization.h"
#include <wx/log.h>
#include <wx/toolbar.h>
#include <wx/listbox.h>
#include <wx/display.h>
#include <wx/statusbr.h>
#include <array>
#ifdef __WXGTK__
#include <gdk/gdkkeysyms.h>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int AppearanceCommand(EditorFrame* frame, const char* label) {
    const int id = frame->GetMenuBar()->FindMenuItem(_("Sett&ings"), wxGetTranslation(label));
    Require(id != wxNOT_FOUND, "original toolbar appearance command exists in Settings");
    return id;
}
void AppearanceAvailability(EditorFrame* frame) {
    const int customize = AppearanceCommand(frame, "Customi&ze Toolbar...");
    AppearanceCommand(frame, "&Auto Scale Toolbar");
    AppearanceCommand(frame, "Use Lar&ge Toolbar");
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetMenuBar()->FindItem(customize)->IsEnabled(), "shown toolbar can be customized");
    NativeKeys::Shortcut(frame, WXK_F11, true);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(!frame->GetMenuBar()->FindItem(customize)->IsEnabled(), "hidden toolbar cannot be customized");
    NativeKeys::Shortcut(frame, WXK_F11, true);
}
void RebuildVisibilityAndFullscreen(EditorFrame* frame) {
    const int showMenu = AppearanceCommand(frame, "Show &Menu\tAlt+F11");
    const int showToolbar = AppearanceCommand(frame, "Show &Toolbar\tCtrl+F11");
    const int showStatus = AppearanceCommand(frame, "Show Stat&usbar\tShift+F11");
    const int large = AppearanceCommand(frame, "Use Lar&ge Toolbar");
    NativeSearch::Command(frame, showMenu);
    NativeSearch::Command(frame, showToolbar);
    NativeSearch::Command(frame, showStatus);
    NativePaint::Complete(frame->Editor());
    const auto editorSize = frame->Editor()->GetSize();
    const auto window = frame->GetRect();
    NativeSearch::Command(frame, large);
    NativePaint::Complete(frame->Editor());
    Require(!frame->GetToolBar()->IsShown() && !frame->GetStatusBar()->IsShown() &&
                frame->Editor()->GetSize() == editorSize,
            "size rebuilding retains hidden bars and the editor's native geometry");
    NativeKeys::Shortcut(frame, WXK_F11, false);
    NativePaint::Complete(frame->Editor());
    const auto fullscreen = frame->GetRect();
    NativeSearch::Command(frame, large);
    NativePaint::Complete(frame->Editor());
    Require(frame->GetRect() == fullscreen && !frame->GetToolBar()->IsShown() && !frame->GetStatusBar()->IsShown(),
            "toolbar rebuilding preserves fullscreen geometry and hidden bars");
    NativeKeys::Shortcut(frame, WXK_F11, false);
    NativePaint::Complete(frame->Editor());
    Require(frame->GetRect() == window, "returning from full screen restores native window placement");
    NativeSearch::Command(frame, large);
    NativeSearch::Command(frame, showMenu);
    NativeSearch::Command(frame, showToolbar);
    NativeSearch::Command(frame, showStatus);
    Require(frame->GetToolBar()->IsShown() && frame->GetStatusBar()->IsShown() && !frame->DocumentIsModified(),
            "visibility preferences remain independently restorable after toolbar rebuilding");
}
void KeyboardClick(wxButton* button) {
    button->SetFocus();
#ifdef __WXMSW__
    const auto window = static_cast<HWND>(button->GetHandle());
    SendMessageW(window, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(window, WM_KEYUP, VK_SPACE, 0);
#elif defined(__WXGTK__)
    wxYield();
    auto* dialog = wxDynamicCast(wxGetTopLevelParent(button), wxDialog);
    wxString focusFile;
    Require(wxGetEnv("WXNOTEPAD_CLIPBOARD_PRIVATE_FOCUS", &focusFile),
            "dialog keyboard uses the existing private native driver");
    const auto focusDeadline = std::chrono::steady_clock::now() + NativeKeys::FocusAcknowledgementTimeout;
    {
        wxFFile request(focusFile, "wb");
        const auto expiry =
            g_get_monotonic_time() +
            std::chrono::duration_cast<std::chrono::microseconds>(NativeKeys::FocusAcknowledgementTimeout).count();
        Require(request.IsOpened() &&
                    request.Write(wxString::Format("focus-until:%lld:", static_cast<long long>(expiry)) +
                                  dialog->GetTitle()),
                "owned dialog focus is requested through the native driver");
    }
    bool focused = false;
    do {
        wxYield();
        button->SetFocus();
        wxString pending;
        wxFFile request(focusFile, "rb");
        focused = request.IsOpened() && request.ReadAll(&pending) && pending.empty() && dialog->IsActive() &&
                  button->HasFocus();
        if (focused) break;
        wxMilliSleep(10);
    } while (std::chrono::steady_clock::now() < focusDeadline);
    Require(focused, "physical keyboard targets the owned active customization button");
    bool released = false;
    NativeKeys::KeyRelease observer(' ', released, nullptr);
    {
        wxFFile request(focusFile, "wb");
        Require(request.IsOpened() && request.Write("key:space"), "native Space request is published");
    }
    const auto deadline = std::chrono::steady_clock::now() + NativeKeys::FocusAcknowledgementTimeout;
    while (!released && std::chrono::steady_clock::now() < deadline) {
        wxYield();
        wxMilliSleep(10);
    }
    Require(released, "native Space release reaches the owned customization dialog");
#endif
    wxYield();
}
void SizeCycle(EditorFrame* frame, const wxString& settingsPath) {
    const auto dpi = frame->GetDPI().x;
    const auto sizes = dpi == 192   ? std::array{32, 40, 48}
                       : dpi == 144 ? std::array{24, 32, 40}
                       : dpi == 120 ? std::array{20, 28, 36}
                                    : std::array{16, 24, 32};
    const int automatic = AppearanceCommand(frame, "&Auto Scale Toolbar");
    const int large = AppearanceCommand(frame, "Use Lar&ge Toolbar");
    const auto check = [&](int scale, int pixels) {
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        auto* toolbar = frame->GetToolBar();
        const auto bitmap = toolbar->FindById(wxID_NEW)->GetNormalBitmapBundle().GetBitmapFor(toolbar);
        Require(bitmap.GetSize() == wxSize(pixels, pixels), "real toolbar uses the original physical size cycle");
        Require(frame->GetMenuBar()->IsChecked(automatic) == (scale != 0) &&
                    frame->GetMenuBar()->IsChecked(large) == (scale > 96),
                "Auto Scale and Large checks retain the original numeric preference");
        NativeSearch::Command(frame, AppearanceCommand(frame, "Save Settings &Now"));
        SettingsStore saved(settingsPath);
        Require(saved.values.autoScaleToolbar == scale, "settings save retains the full numeric toolbar scale");
    };
    check(96, sizes[0]);
    NativeSearch::Command(frame, large);
    check(144, sizes[1]);
    NativeSearch::Command(frame, large);
    check(192, sizes[2]);
    NativeSearch::Command(frame, large);
    check(96, sizes[0]);
    NativeSearch::Command(frame, automatic);
    check(0, dpi == 192 ? 32 : dpi == 144 ? 24 : 16);
    NativeSearch::Command(frame, large);
    check(96, sizes[0]);
    NativeSearch::Command(frame, automatic);
    NativeSearch::Command(frame, automatic);
    check(96, sizes[0]);
}
class CustomizeDriver final : public wxModalDialogHook {
public:
    explicit CustomizeDriver(std::function<void(wxDialog*)> action) : action_(std::move(action)) { Register(); }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetName() != "Customize toolbar") return wxID_NONE;
        opened = true;
        wxTheApp->CallAfter([this, dialog] {
            NativeKeys::AfterPresentation(dialog, [this, dialog](bool ready) {
                try {
                    Require(ready, "customization dialog is natively presented");
                    action_(dialog);
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
    std::function<void(wxDialog*)> action_;
};
void AddAndRemove(EditorFrame* frame) {
    CustomizeDriver driver([frame](wxDialog* dialog) {
        auto* available = wxDynamicCast(wxWindow::FindWindowByName("Available toolbar buttons", dialog), wxListBox);
        auto* current = wxDynamicCast(wxWindow::FindWindowByName("Current toolbar buttons", dialog), wxListBox);
        Require(available && current, "customization exposes native available/current button lists");
        const auto printLabel = wxMenuItem::GetLabelText(_("&Print...\tCtrl+P"));
        const int print = available->FindString(printLabel);
        Require(print != wxNOT_FOUND && !frame->GetToolBar()->FindById(wxID_PRINT),
                "original Print button is an available delivered command outside the original default layout");
        available->SetSelection(print);
        auto* add = wxWindow::FindWindowByName("Add toolbar button", dialog);
        Require(add, "native Add button exists");
        NativeSearch::Click(dialog, add->GetId());
        Require(frame->GetToolBar()->FindById(wxID_PRINT), "Add updates the real main toolbar immediately");
        const int added = current->FindString(printLabel);
        Require(added != wxNOT_FOUND, "added command appears in current buttons");
        current->SetSelection(added);
        auto* remove = wxWindow::FindWindowByName("Remove toolbar button", dialog);
        Require(remove, "native Remove button exists");
        NativeSearch::Click(dialog, remove->GetId());
        Require(!frame->GetToolBar()->FindById(wxID_PRINT), "Remove updates the real main toolbar immediately");
        NativeSearch::Click(dialog, wxID_CLOSE);
    });
    NativeSearch::Command(frame, AppearanceCommand(frame, "Customi&ze Toolbar..."));
    if (driver.failure) std::rethrow_exception(driver.failure);
    Require(driver.opened, "Customize Toolbar opens its real native dialog");
}
void ReorderResetAndHistory(EditorFrame* frame, const wxString& settingsPath) {
    using enum Scintilla::Message;
    auto* editor = frame->Editor();
    editor->Send(SetText, 0, reinterpret_cast<intptr_t>("toolbar document\r\n"));
    editor->Send(EmptyUndoBuffer);
    editor->Send(SetSavePoint);
    editor->Send(AppendText, 1, reinterpret_cast<intptr_t>("!"));
    editor->Send(SetSel, 2, 7);
    const auto text = NativeSearch::Text(editor);
    const auto encoding = frame->DocumentEncoding();
    const auto bom = frame->DocumentHasBom();
    const auto eol = editor->Send(GetEOLMode);
    const auto unchanged = [&] {
        Require(NativeSearch::Text(editor) == text && frame->DocumentEncoding() == encoding &&
                    frame->DocumentHasBom() == bom && editor->Send(GetEOLMode) == eol &&
                    editor->Send(GetSelectionStart) == 2 && editor->Send(GetSelectionEnd) == 7 &&
                    editor->Send(CanUndo) && !editor->Send(CanRedo) && editor->Send(GetModify),
                "layout changes preserve text, selection, format, save point and history");
    };
    CustomizeDriver driver([&](wxDialog* dialog) {
        const auto area = wxDisplay(wxDisplay::GetFromWindow(dialog)).GetClientArea();
        Require(area.Contains(dialog->GetScreenRect()), "localized customization dialog fits the native work area");
        for (auto* child : dialog->GetChildren())
            if (child->IsShown()) {
                Require(dialog->GetScreenRect().Contains(child->GetScreenRect()),
                        "customization controls remain visible");
                if (auto* button = wxDynamicCast(child, wxButton))
                    Require(button->GetSize().x >= button->GetBestSize().x, "localized toolbar button label fits");
            }
        auto* available = wxDynamicCast(wxWindow::FindWindowByName("Available toolbar buttons", dialog), wxListBox);
        auto* current = wxDynamicCast(wxWindow::FindWindowByName("Current toolbar buttons", dialog), wxListBox);
        Require(available && current, "customization lists remain accessible");
        const auto click = [&](const char* name) {
            auto* control = wxWindow::FindWindowByName(name, dialog);
            Require(control, "native toolbar customization action exists");
            NativeSearch::Click(dialog, control->GetId());
            unchanged();
        };
        current->SetSelection(current->FindString(wxMenuItem::GetLabelText(_("&Undo\tCtrl+Z"))));
        const int beforeKeyboard = current->GetSelection();
        auto* moveUp = wxDynamicCast(wxWindow::FindWindowByName("Move toolbar button up", dialog), wxButton);
        Require(moveUp && beforeKeyboard > 0, "keyboard reorder action is accessible");
        KeyboardClick(moveUp);
        Require(current->GetSelection() == beforeKeyboard - 1,
                "native Space activates toolbar reordering from the keyboard");
        unchanged();
        while (current->GetSelection() > 0)
            click("Move toolbar button up");
        Require(frame->GetToolBar()->GetToolByPos(0)->GetId() == wxID_UNDO,
                "reordering moves the original Undo command to the first real toolbar position");
        current->SetSelection(1);
        available->SetSelection(0);
        click("Add toolbar button");
        Require(frame->GetToolBar()->GetToolByPos(1)->IsSeparator(), "available separator can be inserted repeatedly");
        click("Move toolbar button down");
        Require(frame->GetToolBar()->GetToolByPos(2)->IsSeparator(), "Move Down reorders separators");
        click("Move toolbar button up");
        click("Remove toolbar button");
        click("Reset toolbar buttons");
        NativeSearch::Command(frame, AppearanceCommand(frame, "Save Settings &Now"));
        SettingsStore reset(settingsPath);
        Require(reset.values.toolbarButtons == "1 27 2 0 4 18 19 0 5 6 0 7 8 9 20 0 10 11 0 12 0 24 0 13 14 0 17",
                "Reset restores the pinned projected default stable identities and separators");
        current->SetSelection(current->FindString(wxMenuItem::GetLabelText(_("&Undo\tCtrl+Z"))));
        while (current->GetSelection() > 0)
            click("Move toolbar button up");
        NativeSearch::Click(dialog, wxID_CLOSE);
    });
    NativeSearch::Command(frame, AppearanceCommand(frame, "Customi&ze Toolbar..."));
    if (driver.failure) std::rethrow_exception(driver.failure);
    Require(driver.opened && editor->HasFocus(), "closing customization returns keyboard focus to the editor");
    unchanged();
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(frame->GetToolBar()->GetToolEnabled(wxID_UNDO), "reordered Undo retains its enabled state");
    wxCommandEvent undo(wxEVT_TOOL, wxID_UNDO);
    undo.SetEventObject(frame->GetToolBar());
    frame->GetToolBar()->GetEventHandler()->ProcessEvent(undo);
    Require(!editor->Send(GetModify) && editor->Send(CanRedo),
            "reordered toolbar command executes Undo at the original save point");
    NativeSearch::Command(frame, wxID_REDO);
    NativeSearch::Command(frame, AppearanceCommand(frame, "Save Settings &Now"));
    SettingsStore saved(settingsPath);
    Require(saved.values.toolbarButtons.StartsWith("5 1 27 2 0 4"), "custom order persists stable original identities");
}
struct ConfigurationCase {
    const char* name;
    const char* buttons;
    const char* saved;
    int scale = 96;
};
const auto ConfigurationCases = std::to_array<ConfigurationCase>(
    {{"malformed", "8 0 5 junk 9", "8 0 5"},
     {"clamped", "-1 999 0 4", "0 27 0 4"},
     {"unsupported", "3 0 22 8 0 999", "0 8 0 27"},
     {"invalid", "junk 8", "1 27 2 0 4 18 19 0 5 6 0 7 8 9 20 0 10 11 0 12 0 24 0 13 14 0 17"},
     {"duplicate", "8 8 0 4", "8 8 0 4"},
     {"numeric", "1 0 5", "1 0 5", 120},
     {"negative", "1 0 5", "1 0 5", -7}});
void ConfigurationLifecycle(EditorFrame* frame, const wxString& settingsPath, const ConfigurationCase& fixture) {
    using enum Scintilla::Message;
    auto* toolbar = frame->GetToolBar();
    if (fixture.scale != 96) {
        NativeSearch::Command(frame, AppearanceCommand(frame, "Save Settings &Now"));
        Require(SettingsStore(settingsPath).values.autoScaleToolbar == fixture.scale,
                "INI loading and saving retains nonstandard numeric toolbar preferences");
        Require(frame->GetMenuBar()->IsChecked(AppearanceCommand(frame, "&Auto Scale Toolbar")) &&
                    frame->GetMenuBar()->IsChecked(AppearanceCommand(frame, "Use Lar&ge Toolbar")) ==
                        (fixture.scale > 96),
                "toolbar menu checks use the full numeric contract");
        const auto expected = fixture.scale == 120 ? std::vector{168, 216, 96} : std::vector{96, 144, 192, 96};
        for (const int scale : expected) {
            NativeSearch::Command(frame, AppearanceCommand(frame, "Use Lar&ge Toolbar"));
            NativeSearch::Command(frame, AppearanceCommand(frame, "Save Settings &Now"));
            Require(SettingsStore(settingsPath).values.autoScaleToolbar == scale,
                    "repeated Large operations retain the original arithmetic cycle for every imported numeric mode");
        }
    }
    if (wxString(fixture.name) == "duplicate") {
        frame->Editor()->Send(SetSel, 0, 0);
        frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
        Require(toolbar->GetToolsCount() == 4 && toolbar->GetToolByPos(0)->GetId() == wxID_COPY &&
                    toolbar->GetToolByPos(1)->GetId() == wxID_COPY && !toolbar->GetToolByPos(0)->IsEnabled() &&
                    !toolbar->GetToolByPos(1)->IsEnabled(),
                "repeated original identities preserve native command availability for every configured button");
        const int large = AppearanceCommand(frame, "Use Lar&ge Toolbar");
        NativeSearch::Command(frame, large);
        for (int index : {0, 1})
            Require(toolbar->GetToolByPos(index)->GetNormalBitmapBundle().GetBitmapFor(toolbar).GetSize() ==
                        wxSize(frame->GetDPI().x == 192 ? 40 : 24, frame->GetDPI().x == 192 ? 40 : 24),
                    "every repeated button uses the requested original size");
    }
    NativeSearch::Command(frame, AppearanceCommand(frame, "Save Settings &Now"));
    SettingsStore saved(settingsPath);
    Require(saved.values.toolbarButtons == fixture.saved,
            "configuration parsing follows the original stable identity contract");
    wxFFile file(settingsPath, "rb");
    wxString contents;
    Require(file.IsOpened() && file.ReadAll(&contents, wxMBConvUTF16LE()), "saved toolbar INI is readable");
    Require(contents.Contains("Unrelated=value") && contents.Contains("[Private section]"),
            "toolbar changes retain unrelated INI settings and sections");
    auto* restored = new EditorFrame(&saved);
    Require(restored->GetToolBar()->GetToolsCount() == toolbar->GetToolsCount(),
            "settings reload restores the complete toolbar layout");
    for (size_t index = 0; index < toolbar->GetToolsCount(); ++index) {
        const auto* first = toolbar->GetToolByPos(static_cast<int>(index));
        const auto* second = restored->GetToolBar()->GetToolByPos(static_cast<int>(index));
        Require(first->IsSeparator() == second->IsSeparator() &&
                    (first->IsSeparator() || frame->GetMenuBar()->FindItem(first->GetId())->GetItemLabel() ==
                                                 restored->GetMenuBar()->FindItem(second->GetId())->GetItemLabel()),
                "restored native toolbar preserves original command order and separators across generated menu IDs");
    }
    restored->Destroy();
}
class ToolbarApp final : public wxApp {
public:
    bool OnInit() override {
        wxLog::SetActiveTarget(new wxLogStderr);
        if (argc > 1) {
            long language = 0;
            Require(wxString(argv[1]).ToLong(&language), "toolbar fixture language is numeric");
            AppLocalization::Initialize(static_cast<uint16_t>(language));
        }
        if (argc > 2) {
            for (const auto& fixture : ConfigurationCases)
                if (wxString(argv[2]) == fixture.name) configuration_ = &fixture;
            Require(configuration_, "requested toolbar configuration fixture exists");
            TestFiles::WriteBytes(TestFiles::TemporaryDirectory() / "toolbar.ini",
                                  std::string("[Settings]\nAutoScaleToolbar=") + std::to_string(configuration_->scale) +
                                      "\nToolbarButtons=" + configuration_->buttons +
                                      "\nUnrelated=value\n[Private section]\nKeep=present\n");
        }
        settings_ =
            std::make_unique<SettingsStore>(TestFiles::Filename(TestFiles::TemporaryDirectory() / "toolbar.ini"));
        auto* frame = new EditorFrame(settings_.get());
        frame->Show();
        NativeKeys::AfterPresentation(frame, [this, frame](bool ready) {
            try {
                Require(ready, "native toolbar window is presented");
                if (configuration_)
                    ConfigurationLifecycle(frame, settings_->Path(), *configuration_);
                else {
                    AppearanceAvailability(frame);
                    RebuildVisibilityAndFullscreen(frame);
                    SizeCycle(frame, settings_->Path());
                    AddAndRemove(frame);
                    ReorderResetAndHistory(frame, settings_->Path());
                }
                result_ = 0;
                std::puts("PASS: original toolbar customization and sizing");
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: %s\n", error.what());
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
    const ConfigurationCase* configuration_ = nullptr;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(ToolbarApp);
