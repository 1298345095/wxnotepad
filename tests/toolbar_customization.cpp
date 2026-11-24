#include "diagnostics/print.h"
#include "native_key_helpers.h"
#include <print>
#include "native_paint_helpers.h"
#include "settings/settings_store.h"
#include "file_helpers.h"
#include "app/localization.h"
#include "status_helpers.h"
#include <wx/log.h>
#include <wx/toolbar.h>
#include <wx/listbox.h>
#include <wx/display.h>
#include <wx/statusbr.h>
#include <wx/treectrl.h>
#include <wx/textctrl.h>
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
    const auto nativeClient = [frame] {
#ifdef __WXMSW__
        RECT rectangle{};
        Require(GetClientRect(static_cast<HWND>(frame->GetHandle()), &rectangle),
                "native full client bounds are readable");
        return wxSize(rectangle.right, rectangle.bottom);
#elif defined(__WXGTK__)
        int width = 0, height = 0;
        gtk_window_get_size(GTK_WINDOW(frame->GetHandle()), &width, &height);
        return wxSize(width, height);
#else
        return frame->GetClientSize();
#endif
    };
    const auto layoutDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (frame->Editor()->GetSize() != nativeClient() && std::chrono::steady_clock::now() < layoutDeadline) {
        wxYield();
        wxMilliSleep(1);
    }
    Require(frame->Editor()->GetSize() == nativeClient(),
            "all-hidden baseline reaches the actual native full client allocation");
    const auto editorSize = frame->Editor()->GetSize();
    const auto window = frame->GetRect();
    NativeSearch::Command(frame, large);
    NativePaint::Complete(frame->Editor());
    if (frame->Editor()->GetSize() != editorSize)
        std::println(
            stderr,
            "Hidden toolbar geometry: editor={:d}x{:d} -> {:d}x{:d} frame={:d}x{:d} toolbarShown={:d} statusShown={:d}",
            editorSize.x, editorSize.y, frame->Editor()->GetSize().x, frame->Editor()->GetSize().y, frame->GetSize().x,
            frame->GetSize().y, frame->GetToolBar()->IsShown(), frame->GetStatusBar()->IsShown());
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
    const auto check = [frame, automatic, large, &settingsPath](int scale, int pixels) {
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
class ToolbarDialogDriver final : public wxModalDialogHook {
public:
    explicit ToolbarDialogDriver(std::function<void(wxDialog*)> action, wxString title = _("Customize Toolbar"))
        : action_(std::move(action)), title_(std::move(title)) {
        Register();
    }
    int Enter(wxDialog* dialog) override {
        if (dialog->GetTitle() != title_) return wxID_NONE;
        opened = true;
        wxTheApp->CallAfter([this, dialog] {
            NativeKeys::AfterPresentation(dialog, [this, dialog](bool ready) {
                try {
                    Require(ready, "toolbar command dialog is natively presented");
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
    wxString title_;
};
void ActivateToolbar(EditorFrame* frame, int command) {
    auto* toolbar = frame->GetToolBar();
    const int position = toolbar->GetToolPos(command);
    Require(position != wxNOT_FOUND && toolbar->GetToolEnabled(command), "native toolbar command is available");
#ifdef __WXMSW__
    SendMessageW(static_cast<HWND>(frame->GetHandle()), WM_COMMAND, MAKEWPARAM(command, 0),
                 reinterpret_cast<LPARAM>(toolbar->GetHandle()));
#elif defined(__WXGTK__)
    auto* tool = gtk_toolbar_get_nth_item(toolbar->GTKGetToolbar(), position);
    Require(GTK_IS_TOOL_BUTTON(tool), "native toolbar command has a GTK tool button");
    g_signal_emit_by_name(tool, "clicked");
#endif
    wxYield();
}
void SchemeToolbarCancel(EditorFrame* frame, const wxString& settingsPath) {
    const int command = frame->GetMenuBar()->FindMenuItem(_("Sche&me"), _("&Syntax Scheme...\tF12"));
    Require(command != wxNOT_FOUND, "delivered Syntax Scheme menu command exists");
    const auto* tool = frame->GetToolBar()->FindById(command);
    Require(tool && tool->GetLabel() == wxMenuItem::GetLabelText(_("&Syntax Scheme...\tF12")) &&
                tool->GetShortHelp() == wxStripMenuCodes(_("&Syntax Scheme...\tF12"), wxStrip_Mnemonics),
            "original toolbar identity15 shares the delivered F12 menu label, command and tooltip");
    const auto metadata = StatusMetadata(frame);
    SettingsStore before(settingsPath);
    ToolbarDialogDriver driver(
        [](wxDialog* dialog) {
            auto* tree = wxDynamicCast(wxWindow::FindWindowByName("Syntax schemes", dialog), wxTreeCtrl);
            Require(tree && NativeDialogs::SelectTreeLabel(*tree, wxGetTranslation("Python Script")),
                    "toolbar opens the real chooser and permits a different delivered scheme");
            auto* selected = wxDynamicCast(wxWindow::FindWindowByName("Default scheme", dialog), wxCheckBox);
            auto* automatic = wxDynamicCast(wxWindow::FindWindowByName("AutoSelect", dialog), wxCheckBox);
            Require(selected && automatic, "toolbar chooser exposes the original scheme preferences");
            NativeSearch::Toggle(selected, !selected->GetValue());
            NativeSearch::Toggle(automatic, !automatic->GetValue());
            NativeSearch::Click(dialog, wxID_CANCEL);
        },
        _("Select Scheme"));
    ActivateToolbar(frame, command);
    if (driver.failure) std::rethrow_exception(driver.failure);
    Require(driver.opened && frame->Editor()->HasFocus() && StatusMetadata(frame) == metadata,
            "native toolbar chooser Cancel preserves the current scheme and editor focus");
    NativeSearch::Command(frame, AppearanceCommand(frame, "Save Settings &Now"));
    SettingsStore after(settingsPath);
    Require(after.values.defaultScheme == before.values.defaultScheme &&
                after.values.autoSelect == before.values.autoSelect,
            "toolbar chooser Cancel discards temporary default and automatic-selection preferences");
}
void ConfigureSchemesToolbarCancel(EditorFrame* frame) {
    const int command = frame->GetMenuBar()->FindMenuItem(_("Sche&me"), _("&Customize Schemes...\tCtrl+F12"));
    Require(command != wxNOT_FOUND, "delivered Customize Schemes menu command exists");
    const auto* tool = frame->GetToolBar()->FindById(command);
    Require(tool && tool->GetLabel() == wxMenuItem::GetLabelText(_("&Customize Schemes...\tCtrl+F12")) &&
                tool->GetShortHelp() == wxStripMenuCodes(_("&Customize Schemes...\tCtrl+F12"), wxStrip_Mnemonics),
            "original toolbar identity16 shares the delivered Ctrl+F12 menu command, label and tooltip");
    const auto fonts = frame->Editor()->Fonts();
    const auto metadata = StatusMetadata(frame);
    ToolbarDialogDriver driver(
        [](wxDialog* dialog) {
            auto* tree = wxDynamicCast(wxWindow::FindWindowByName("Scheme styles", dialog), wxTreeCtrl);
            auto* edit = wxDynamicCast(wxWindow::FindWindowByName("Style settings", dialog), wxTextCtrl);
            Require(tree && edit && NativeDialogs::SelectTreeLabel(*tree, wxGetTranslation("Default Text Style")),
                    "toolbar identity16 opens the actual shared style customization dialog");
            edit->SetValue("size:22; fore:#123456");
            NativeSearch::Click(dialog, wxID_CANCEL);
        },
        _("Customize Schemes - Default Theme"));
    ActivateToolbar(frame, command);
    if (driver.failure) std::rethrow_exception(driver.failure);
    Require(driver.opened && frame->Editor()->HasFocus() && StatusMetadata(frame) == metadata &&
                frame->Editor()->Fonts() == fonts,
            "native toolbar style Cancel discards edited styles and preserves scheme, profile and editor focus");
}
void AddAndRemove(EditorFrame* frame) {
    ToolbarDialogDriver driver([frame](wxDialog* dialog) {
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
    const auto history = editor->Send(GetUndoActions);
    const auto historyPosition = editor->Send(GetUndoCurrent);
    const auto unchanged = [frame, editor, text, encoding, bom, eol, history, historyPosition] {
        Require(NativeSearch::Text(editor) == text && frame->DocumentEncoding() == encoding &&
                    frame->DocumentHasBom() == bom && editor->Send(GetEOLMode) == eol &&
                    editor->Send(GetSelectionStart) == 2 && editor->Send(GetSelectionEnd) == 7 &&
                    editor->Send(CanUndo) && !editor->Send(CanRedo) && editor->Send(GetModify) &&
                    editor->Send(GetUndoActions) == history && editor->Send(GetUndoCurrent) == historyPosition,
                "layout changes preserve text, selection, format, save point and history");
    };
    SchemeToolbarCancel(frame, settingsPath);
    unchanged();
    ConfigureSchemesToolbarCancel(frame);
    unchanged();
    for (int cycle = 0; cycle < 3; ++cycle) {
        NativeSearch::Command(frame, AppearanceCommand(frame, "Use Lar&ge Toolbar"));
        unchanged();
    }
    ToolbarDialogDriver driver([frame, &settingsPath, &unchanged](wxDialog* dialog) {
        const auto area = wxDisplay(wxDisplay::GetFromWindow(dialog)).GetClientArea();
        Require(area.Contains(dialog->GetScreenRect()), "localized customization dialog fits the native work area");
        for (auto* child : dialog->GetChildren())
            if (child->IsShown()) {
                Require(dialog->GetScreenRect().Contains(child->GetScreenRect()),
                        "customization controls remain visible");
                if (auto* button = wxDynamicCast(child, wxButton)) {
                    Require(!button->GetLabel().empty(),
                            "every toolbar customization action has a visible native label");
                    Require(button->GetSize().x >= button->GetBestSize().x, "localized toolbar button label fits");
                }
            }
        auto* available = wxDynamicCast(wxWindow::FindWindowByName("Available toolbar buttons", dialog), wxListBox);
        auto* current = wxDynamicCast(wxWindow::FindWindowByName("Current toolbar buttons", dialog), wxListBox);
        Require(available && current, "customization lists remain accessible");
        const auto click = [dialog, &unchanged](const char* name) {
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
        Require(reset.values.toolbarButtons ==
                    "1 27 2 0 4 18 19 0 5 6 0 7 8 9 20 0 10 11 0 12 0 24 0 13 14 0 15 16 0 17",
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
    ActivateToolbar(frame, wxID_UNDO);
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
     {"unsupported", "3 0 22 8 0 16 999", "0 8 0 16 27"},
     {"invalid", "junk 8", "1 27 2 0 4 18 19 0 5 6 0 7 8 9 20 0 10 11 0 12 0 24 0 13 14 0 15 16 0 17"},
     {"duplicate", "8 8 0 4", "8 8 0 4"},
     {"bounded",
      "12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 "
      "12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 "
      "12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 1 1 1",
      "12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 "
      "12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 "
      "12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 12 1 1"},
     {"numeric", "1 0 5", "1 0 5", 120},
     {"negative", "1 0 5", "1 0 5", -7}});
void ImportedSizeCycle(EditorFrame* frame, const wxString& settingsPath, int importedScale) {
    if (importedScale == 96) return;
    NativeSearch::Command(frame, AppearanceCommand(frame, "Save Settings &Now"));
    Require(SettingsStore(settingsPath).values.autoScaleToolbar == importedScale,
            "INI loading and saving retains nonstandard numeric toolbar preferences");
    Require(frame->GetMenuBar()->IsChecked(AppearanceCommand(frame, "&Auto Scale Toolbar")) &&
                frame->GetMenuBar()->IsChecked(AppearanceCommand(frame, "Use Lar&ge Toolbar")) == (importedScale > 96),
            "toolbar menu checks use the full numeric contract");
    const auto expected = importedScale == 120 ? std::vector{168, 216, 96} : std::vector{96, 144, 192, 96};
    for (const int scale : expected) {
        NativeSearch::Command(frame, AppearanceCommand(frame, "Use Lar&ge Toolbar"));
        NativeSearch::Command(frame, AppearanceCommand(frame, "Save Settings &Now"));
        Require(SettingsStore(settingsPath).values.autoScaleToolbar == scale,
                "repeated Large operations retain the original arithmetic cycle for every imported numeric mode");
    }
}
void DuplicateToolbarCommands(EditorFrame* frame) {
    auto* toolbar = frame->GetToolBar();
    frame->Editor()->Send(Scintilla::Message::SetSel, 0, 0);
    frame->UpdateWindowUI(wxUPDATE_UI_RECURSE);
    Require(toolbar->GetToolsCount() == 4 && toolbar->GetToolByPos(0)->GetId() == wxID_COPY &&
                toolbar->GetToolByPos(1)->GetId() == wxID_COPY && !toolbar->GetToolByPos(0)->IsEnabled() &&
                !toolbar->GetToolByPos(1)->IsEnabled(),
            "repeated original identities preserve native command availability for every configured button");
    const int large = AppearanceCommand(frame, "Use Lar&ge Toolbar");
    NativeSearch::Command(frame, large);
    const int dpi = frame->GetDPI().x;
    const int pixels = dpi == 192 ? 40 : dpi == 144 ? 32 : dpi == 120 ? 28 : 24;
    for (int index : {0, 1})
        Require(toolbar->GetToolByPos(index)->GetNormalBitmapBundle().GetBitmapFor(toolbar).GetSize() ==
                    wxSize(pixels, pixels),
                "every repeated button uses the requested original size");
}
void VerifyRestoredLayout(EditorFrame* frame, SettingsStore& saved, const ConfigurationCase& fixture) {
    auto* toolbar = frame->GetToolBar();
    // This hidden synchronous fixture never enters an event loop. Destroy it
    // before its local settings, including when an assertion throws.
    EditorFrame restored(&saved);
    if (wxString(fixture.name) == "bounded")
        Require(toolbar->GetToolsCount() == 54 && restored.GetToolBar()->GetToolsCount() == 53,
                "original bounded save retains its 53-button prefix without changing the 54 live buttons");
    else
        Require(restored.GetToolBar()->GetToolsCount() == toolbar->GetToolsCount(),
                "settings reload restores the complete toolbar layout");
    for (size_t index = 0; index < restored.GetToolBar()->GetToolsCount(); ++index) {
        const auto* first = toolbar->GetToolByPos(static_cast<int>(index));
        const auto* second = restored.GetToolBar()->GetToolByPos(static_cast<int>(index));
        Require(first->IsSeparator() == second->IsSeparator() &&
                    (first->IsSeparator() || frame->GetMenuBar()->FindItem(first->GetId())->GetItemLabel() ==
                                                 restored.GetMenuBar()->FindItem(second->GetId())->GetItemLabel()),
                "restored native toolbar preserves original command order and separators across generated menu IDs");
    }
}
void ConfigurationLifecycle(EditorFrame* frame, const wxString& settingsPath, const ConfigurationCase& fixture) {
    ImportedSizeCycle(frame, settingsPath, fixture.scale);
    if (wxString(fixture.name) == "duplicate") DuplicateToolbarCommands(frame);
    NativeSearch::Command(frame, AppearanceCommand(frame, "Save Settings &Now"));
    SettingsStore saved(settingsPath);
    Require(saved.values.toolbarButtons == fixture.saved,
            "configuration parsing follows the original stable identity contract");
    wxFFile file(settingsPath, "rb");
    wxString contents;
    Require(file.IsOpened() && file.ReadAll(&contents, wxMBConvUTF16LE()), "saved toolbar INI is readable");
    Require(contents.Contains("Unrelated=value") && contents.Contains("[Private section]"),
            "toolbar changes retain unrelated INI settings and sections");
    VerifyRestoredLayout(frame, saved, fixture);
}
class ToolbarApp final : public wxApp {
public:
    bool OnInit() override {
        // wx owns the replacement; SetActiveTarget returns the old target to us.
        delete wxLog::SetActiveTarget(new wxLogStderr);
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
    const ConfigurationCase* configuration_ = nullptr;
    int result_ = 1;
};
} // namespace
wxIMPLEMENT_APP(ToolbarApp);
