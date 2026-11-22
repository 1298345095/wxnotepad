#include "app/editor_frame.h"
#include "app/toolbar_art.h"
#include "app/dialogs/toolbar_customization_dialog.h"
#include "settings/settings_store.h"
#include "editor/editor_control.h"
#include <utility>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <wx/menu.h>
#include <wx/imagpng.h>
#include <wx/toolbar.h>
#include <wx/translation.h>
#include <wx/settings.h>
#ifdef __WXGTK__
#include <wx/clipbrd.h>
#endif
#ifdef __WXMSW__
#include <commctrl.h>
#endif

namespace {
// Notepad4 89b8b36 tbbMainWnd identities. Artwork index is identity minus one.
// Resolve this single catalog against actual menu commands; unavailable actions
// cannot appear in either the toolbar or its customization choices.
struct ToolbarCommand {
    int identity;
    wxMenuItem* menu;
};
constexpr int ToolbarSeparator = 0;
constexpr int LastToolbarIdentity = 27;
constexpr size_t MaximumSavedToolbarButtons = 64;
constexpr size_t ToolbarConfigBufferCharacters = 160;

class OriginalToolbar final : public wxToolBar {
public:
    OriginalToolbar(wxWindow* parent, wxWindowID id, long style, const wxString& name)
        : wxToolBar(parent, id, wxDefaultPosition, wxDefaultSize, style, name) {}
    void UpdateWindowUI(long flags = wxUPDATE_UI_NONE) override {
        wxToolBar::UpdateWindowUI(flags);
        // Original INI layouts may repeat a command. wx updates by ID and finds
        // only the first tool; synchronize the remaining native instances.
        for (size_t index = 0; index < GetToolsCount(); ++index) {
            auto* tool = GetToolByPos(static_cast<int>(index));
            if (tool->IsSeparator()) continue;
            const auto* first = FindById(tool->GetId());
            if (first == tool) continue;
            tool->Enable(first->IsEnabled());
            if (tool->CanBeToggled()) tool->Toggle(first->IsToggled());
#ifdef __WXMSW__
            TBBUTTONINFOW info{};
            info.cbSize = sizeof(info);
            info.dwMask = TBIF_BYINDEX | TBIF_STATE;
            const auto window = static_cast<HWND>(GetHandle());
            if (::SendMessageW(window, TB_GETBUTTONINFOW, index, reinterpret_cast<LPARAM>(&info)) < 0) continue;
            info.fsState &= ~(TBSTATE_ENABLED | TBSTATE_CHECKED);
            if (tool->IsEnabled()) info.fsState |= TBSTATE_ENABLED;
            if (tool->IsToggled()) info.fsState |= TBSTATE_CHECKED;
            ::SendMessageW(window, TB_SETBUTTONINFOW, index, reinterpret_cast<LPARAM>(&info));
#else
            DoEnableTool(tool, tool->IsEnabled());
            if (tool->CanBeToggled()) DoToggleTool(tool, tool->IsToggled());
#endif
        }
    }
};

// wxGTK uses the bundle's default size and wxMSW its preferred physical size.
// Both must use the same original pixel size, including at fractional DPI.
class SizedToolbarArtwork final : public wxBitmapBundleImpl {
public:
    SizedToolbarArtwork(const wxBitmapBundle& source, int pixels, double contentScale)
        : source_(source), physical_(pixels, pixels) {
        const int logicalPixels = static_cast<int>(std::lround(pixels / contentScale));
        logical_ = wxSize(logicalPixels, logicalPixels);
    }
    wxSize GetDefaultSize() const override { return logical_; }
    wxSize GetPreferredBitmapSizeAtScale(double) const override { return physical_; }
    wxBitmap GetBitmap(const wxSize& size) override { return source_.GetBitmap(size); }

private:
    wxBitmapBundle source_;
    wxSize logical_;
    wxSize physical_;
};

int ToolbarPixels(int dpi, int scale) {
    // Notepad4 CreateBars, Helpers.h::GetBitmapResourceIdForDPI and
    // Helpers.cpp::ResizeImageForDPI, pinned 89b8b36.
    constexpr int BasePixels = 16;
    const auto effectiveDpi =
        static_cast<int64_t>(dpi) + std::max<int64_t>(0, static_cast<int64_t>(scale) - DisplayUnits::DefaultDpi);
    if (scale == 0) {
        const auto resource = std::clamp(
            (effectiveDpi + DisplayUnits::DefaultDpi / 4 - 1) / (DisplayUnits::DefaultDpi / 2), int64_t{2}, int64_t{6});
        return static_cast<int>(resource * BasePixels / 2);
    }
    auto pixels = effectiveDpi * BasePixels / DisplayUnits::DefaultDpi;
    pixels += pixels & 1;
    return static_cast<int>(pixels);
}

wxBitmapBundle DisabledArtwork(const wxBitmapBundle& normal, const wxColour& background) {
    // Notepad4 CreateBars/BitmapAlphaBlend, 89b8b36, for light chrome. Its dark
    // strokes disappear on dark chrome. Map source luminance between native
    // disabled text and background to retain interior detail as well as alpha.
    constexpr unsigned OriginalDisabledOpacity = 0x60;
    constexpr unsigned ChannelMaximum = 255;
    const bool dark = wxSystemSettings::GetAppearance().IsDark();
    const auto disabled = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    const std::array<unsigned, 3> foreground{disabled.Red(), disabled.Green(), disabled.Blue()};
    const std::array<unsigned, 3> backdrop{background.Red(), background.Green(), background.Blue()};
    wxVector<wxBitmap> images;
    for (const int size : {16, 24, 32, 40, 48}) {
        auto image = normal.GetBitmap(wxSize(size, size)).ConvertToImage();
        if (dark) image = image.ConvertToGreyscale();
        auto* data = image.GetData();
        for (int byte = 0; byte < image.GetWidth() * image.GetHeight() * 3; ++byte)
            data[byte] = static_cast<unsigned char>(
                dark ? (foreground[byte % 3] * (ChannelMaximum - data[byte]) + backdrop[byte % 3] * data[byte]) /
                           ChannelMaximum
                     : (data[byte] * OriginalDisabledOpacity +
                        backdrop[byte % 3] * (ChannelMaximum - OriginalDisabledOpacity)) /
                           ChannelMaximum);
        images.emplace_back(image);
    }
    return wxBitmapBundle::FromBitmaps(images);
}

// Resolve the already-created native menu command, including translated labels.
wxMenuItem* FindCommand(wxMenu* menu, const wxString& label) {
    for (auto* item : menu->GetMenuItems()) {
        if (item->GetSubMenu()) {
            if (auto* found = FindCommand(item->GetSubMenu(), label)) return found;
        } else if (item->GetItemLabel() == label) {
            return item;
        }
    }
    return nullptr;
}

std::vector<ToolbarCommand> ToolbarCommands(wxMenuBar* menuBar) {
    static const auto specifications =
        std::to_array<std::pair<int, const char*>>({{1, wxTRANSLATE("&New\tCtrl+N")},
                                                    {2, wxTRANSLATE("&Open...\tCtrl+O")},
                                                    {4, wxTRANSLATE("&Save\tCtrl+S")},
                                                    {5, wxTRANSLATE("&Undo\tCtrl+Z")},
                                                    {6, wxTRANSLATE("&Redo\tCtrl+Y")},
                                                    {7, wxTRANSLATE("Cu&t\tCtrl+X")},
                                                    {8, wxTRANSLATE("&Copy\tCtrl+C")},
                                                    {9, wxTRANSLATE("&Paste\tCtrl+V")},
                                                    {10, wxTRANSLATE("&Find...\tCtrl+F")},
                                                    {11, wxTRANSLATE("R&eplace...\tCtrl+H")},
                                                    {12, wxTRANSLATE("&Word Wrap\tCtrl+Shift+W")},
                                                    {13, wxTRANSLATE("Zoom &In\tCtrl++")},
                                                    {14, wxTRANSLATE("Zoom &Out\tCtrl+-")},
                                                    {15, wxTRANSLATE("&Syntax Scheme...\tF12")},
                                                    {17, wxTRANSLATE("E&xit\tAlt+F4")},
                                                    {18, wxTRANSLATE("Save &As...\tF6")},
                                                    {19, wxTRANSLATE("Save Cop&y...\tCtrl+F6")},
                                                    {20, wxTRANSLATE("&Delete")},
                                                    {21, wxTRANSLATE("&Print...\tCtrl+P")},
                                                    {24, wxTRANSLATE("&Default Levels\tShift+Alt+D")},
                                                    {25, wxTRANSLATE("Execute &Document\tCtrl+L")},
                                                    {27, wxTRANSLATE("New &Window\tAlt+N")}});
    std::vector<ToolbarCommand> commands;
    for (const auto& [identity, label] : specifications)
        for (size_t index = 0; index < menuBar->GetMenuCount(); ++index)
            if (auto* menu = FindCommand(menuBar->GetMenu(index), wxGetTranslation(label))) {
                commands.push_back({identity, menu});
                break;
            }
    return commands;
}

const ToolbarCommand* ToolbarCommandFor(const std::vector<ToolbarCommand>& commands, int identity) {
    const auto found = std::ranges::find(commands, identity, &ToolbarCommand::identity);
    return found == commands.end() ? nullptr : &*found;
}

std::vector<int> ParseToolbarButtons(const wxString& value, const std::vector<ToolbarCommand>& commands) {
    // Helpers.cpp::Toolbar_SetButtons: bounded INI buffer, wcstol prefix parse,
    // clamp to the original table, stop at the first malformed token.
    const auto text = value.Left(ToolbarConfigBufferCharacters - 1).ToStdWstring();
    const wchar_t* cursor = text.c_str();
    std::vector<int> result;
    while (*cursor) {
        wchar_t* end = nullptr;
        const long number = std::wcstol(cursor, &end, 10);
        if (end == cursor) break;
        const int identity = static_cast<int>(std::clamp(number, 0L, static_cast<long>(LastToolbarIdentity)));
        if (identity == ToolbarSeparator || ToolbarCommandFor(commands, identity)) result.push_back(identity);
        cursor = end;
    }
    return result;
}

std::vector<int> DefaultToolbarLayout(const std::vector<ToolbarCommand>& commands) {
    const auto original = ParseToolbarButtons(OriginalSettings::DefaultToolbarButtons, commands);
    std::vector<int> result;
    for (const int identity : original)
        if (identity != ToolbarSeparator || (!result.empty() && result.back() != ToolbarSeparator))
            result.push_back(identity);
    if (!result.empty() && result.back() == ToolbarSeparator) result.pop_back();
    return result;
}

wxString EncodeToolbarButtons(const std::vector<int>& buttons) {
    wxString result;
    for (size_t index = 0; index < std::min(buttons.size(), MaximumSavedToolbarButtons); ++index) {
        // Toolbar_GetButtons counts the trailing space before testing room for
        // the next original two-digit identity and its terminator.
        const size_t originalLength = result.length() + (result.empty() ? 0 : 1);
        if (originalLength >= ToolbarConfigBufferCharacters - 3) break;
        if (!result.empty()) result += ' ';
        result += wxString::Format("%d", buttons[index]);
    }
    return result;
}
} // namespace

int EditorFrame::FindMenuCommand(const wxString& label) const {
    for (size_t menu = 0; menu < GetMenuBar()->GetMenuCount(); ++menu)
        if (auto* item = FindCommand(GetMenuBar()->GetMenu(menu), label)) return item->GetId();
    return wxNOT_FOUND;
}

void EditorFrame::AppendToolbarCommands(wxMenu* appearance) {
    const int customize = wxWindow::NewControlId();
    appearance->Append(customize, _("Customi&ze Toolbar..."));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CustomizeToolbar(); }, customize);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) { event.Enable((settings_ ? settings_->values : defaults_).showToolbar); },
        customize);
    const int automatic = wxWindow::NewControlId();
    appearance->AppendCheckItem(automatic, _("&Auto Scale Toolbar"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& scale = (settings_ ? settings_->values : defaults_).autoScaleToolbar;
            scale = scale ? 0 : DisplayUnits::DefaultDpi;
            RebuildToolbar();
        },
        automatic);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Check((settings_ ? settings_->values : defaults_).autoScaleToolbar != 0);
        },
        automatic);
    const int large = wxWindow::NewControlId();
    appearance->AppendCheckItem(large, _("Use Lar&ge Toolbar"));
    Bind(
        wxEVT_MENU,
        [this](wxCommandEvent&) {
            auto& scale = (settings_ ? settings_->values : defaults_).autoScaleToolbar;
            scale = scale >= DisplayUnits::DefaultDpi && scale < DisplayUnits::DefaultDpi * 2
                        ? scale + DisplayUnits::DefaultDpi / 2
                        : DisplayUnits::DefaultDpi;
            RebuildToolbar();
        },
        large);
    Bind(
        wxEVT_UPDATE_UI,
        [this](wxUpdateUIEvent& event) {
            event.Check((settings_ ? settings_->values : defaults_).autoScaleToolbar > DisplayUnits::DefaultDpi);
        },
        large);
}

void EditorFrame::CreateDefaultToolbar() {
    if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG)) wxImage::AddHandler(new wxPNGHandler);
    auto* toolbar = CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
    toolbar->SetName("Main toolbar");
    const auto commands = ToolbarCommands(GetMenuBar());
    const auto& configured = (settings_ ? settings_->values : defaults_).toolbarButtons;
    toolbarButtons_ = ParseToolbarButtons(configured, commands);
    if (toolbarButtons_.empty() || configured == OriginalSettings::DefaultToolbarButtons)
        toolbarButtons_ = DefaultToolbarLayout(commands);
    RebuildToolbar();
#ifdef __WXGTK__
    // A synchronous clipboard query yields inside GTK frame-clock dispatch:
    // a pending Wayland configure can then be committed before GTK allocates
    // its new geometry. Idle toolbar updates consume the asynchronous offer.
    auto clipboardHasText = std::make_shared<bool>(false);
    toolbar->Bind(wxEVT_CLIPBOARD_CHANGED, [clipboardHasText](wxClipboardEvent& event) {
        *clipboardHasText = event.SupportsFormat(wxDF_UNICODETEXT) || event.SupportsFormat(wxDF_TEXT);
    });
    toolbar->Bind(
        wxEVT_UPDATE_UI,
        [this, toolbar, clipboardHasText](wxUpdateUIEvent& event) {
            event.Enable(editor_->CanPasteText(*clipboardHasText));
            if (!wxTheClipboard->IsOpened()) {
                wxTheClipboard->UsePrimarySelection(false);
                wxTheClipboard->IsSupportedAsync(toolbar);
            }
        },
        wxID_PASTE);
#endif
    toolbar->Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent& event) {
        CallAfter([this] { RebuildToolbar(); });
        event.Skip();
    });
    // The shared IDs let wx's native toolbar route activation and UpdateUI to
    // the same command handlers as the menu. Return keyboard input to the editor.
    Bind(wxEVT_TOOL, [this, toolbar](wxCommandEvent& event) {
        // wxEVT_TOOL and wxEVT_MENU share an event type. Menu commands from a
        // modeless Find dialog must retain their existing focus/seeding rules.
        if (event.GetEventObject() == toolbar) editor_->SetFocus();
        event.Skip();
    });
}

wxToolBar* EditorFrame::OnCreateToolBar(long style, wxWindowID id, const wxString& name) {
    return new OriginalToolbar(this, id, style, name);
}

void EditorFrame::RebuildToolbar() {
    auto* toolbar = GetToolBar();
    toolbar->ClearTools();
    const auto commands = ToolbarCommands(GetMenuBar());
    const int pixels = ToolbarPixels(GetDPI().x, (settings_ ? settings_->values : defaults_).autoScaleToolbar);
    const double contentScale = toolbar->GetContentScaleFactor();
    for (const int identity : toolbarButtons_) {
        if (identity == ToolbarSeparator) {
            toolbar->AddSeparator();
        } else if (const auto* entry = ToolbarCommandFor(commands, identity)) {
            auto* menu = entry->menu;
            const auto source = ToolbarArt::Icon(identity - 1);
            const auto disabled = DisabledArtwork(source, wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE));
            toolbar->AddTool(menu->GetId(), menu->GetItemLabelText(),
                             wxBitmapBundle::FromImpl(new SizedToolbarArtwork(source, pixels, contentScale)),
                             wxBitmapBundle::FromImpl(new SizedToolbarArtwork(disabled, pixels, contentScale)),
                             menu->IsCheckable() ? wxITEM_CHECK : wxITEM_NORMAL,
                             wxStripMenuCodes(menu->GetItemLabel(), wxStrip_Mnemonics), menu->GetHelp());
            toolbar->EnableTool(menu->GetId(), menu->IsEnabled());
            if (menu->IsCheckable()) toolbar->ToggleTool(menu->GetId(), menu->IsChecked());
        }
    }
    (settings_ ? settings_->values : defaults_).toolbarButtons = EncodeToolbarButtons(toolbarButtons_);
    toolbar->Realize();
    if (GetStatusBar()) ApplyBarVisibility();
    toolbar->UpdateWindowUI();
}

void EditorFrame::CustomizeToolbar() {
    if (!(settings_ ? settings_->values : defaults_).showToolbar) return;
    const auto commands = ToolbarCommands(GetMenuBar());
    std::vector<ToolbarButtonChoice> choices{{ToolbarSeparator, _("Separator")}};
    for (const auto& command : commands)
        choices.push_back({command.identity, command.menu->GetItemLabelText()});
    const auto defaults = DefaultToolbarLayout(commands);
    ShowToolbarCustomizationDialog(this, choices, toolbarButtons_, defaults, [this] { RebuildToolbar(); });
    editor_->SetFocus();
}
