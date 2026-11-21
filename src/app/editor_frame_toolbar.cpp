#include "app/editor_frame.h"
#include "app/toolbar_art.h"
#include "settings/settings_store.h"
#include "editor/editor_control.h"
#include <utility>
#include <wx/menu.h>
#include <wx/imagpng.h>
#include <wx/toolbar.h>
#include <wx/translation.h>
#include <wx/settings.h>
#ifdef __WXGTK__
#include <wx/clipbrd.h>
#endif

namespace {
// Indices in Notepad4 89b8b36's toolbar bitmap strips.
enum class ToolbarIcon {
    New = 0,
    Open = 1,
    Save = 3,
    Undo = 4,
    Redo = 5,
    Cut = 6,
    Copy = 7,
    Paste = 8,
    Find = 9,
    Replace = 10,
    WordWrap = 11,
    ZoomIn = 12,
    ZoomOut = 13,
    SaveAs = 17,
    SaveCopy = 18,
    DefaultFoldLevels = 23,
    NewWindow = 26,
};

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
} // namespace

int EditorFrame::FindMenuCommand(const wxString& label) const {
    for (size_t menu = 0; menu < GetMenuBar()->GetMenuCount(); ++menu)
        if (auto* item = FindCommand(GetMenuBar()->GetMenu(menu), label)) return item->GetId();
    return wxNOT_FOUND;
}

void EditorFrame::CreateDefaultToolbar() {
    if (!wxImage::FindHandler(wxBITMAP_TYPE_PNG)) wxImage::AddHandler(new wxPNGHandler);
    auto* toolbar = CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
    toolbar->SetName("Main toolbar");
    // Notepad4 89b8b36 DefaultToolbarButtons order/groups. Only commands already
    // delivered by this host are included; images use that original strip index.
    const auto add = [this, toolbar](ToolbarIcon image, const wxString& label) {
        const auto id = FindMenuCommand(label);
        if (id == wxNOT_FOUND) return;
        auto* command = GetMenuBar()->FindItem(id);
        const auto art = ToolbarArt::Icon(std::to_underlying(image));
        toolbar->AddTool(command->GetId(), command->GetItemLabelText(), art,
                         DisabledArtwork(art, wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)),
                         command->IsCheckable() ? wxITEM_CHECK : wxITEM_NORMAL,
                         wxStripMenuCodes(label, wxStrip_Mnemonics), command->GetHelp());
    };
    add(ToolbarIcon::New, _("&New\tCtrl+N"));
    add(ToolbarIcon::NewWindow, _("New &Window\tAlt+N"));
    add(ToolbarIcon::Open, _("&Open...\tCtrl+O"));
    toolbar->AddSeparator();
    add(ToolbarIcon::Save, _("&Save\tCtrl+S"));
    add(ToolbarIcon::SaveAs, _("Save &As...\tF6"));
    add(ToolbarIcon::SaveCopy, _("Save Cop&y...\tCtrl+F6"));
    toolbar->AddSeparator();
    add(ToolbarIcon::Undo, _("&Undo\tCtrl+Z"));
    add(ToolbarIcon::Redo, _("&Redo\tCtrl+Y"));
    toolbar->AddSeparator();
    add(ToolbarIcon::Cut, _("Cu&t\tCtrl+X"));
    add(ToolbarIcon::Copy, _("&Copy\tCtrl+C"));
    add(ToolbarIcon::Paste, _("&Paste\tCtrl+V"));
    toolbar->AddSeparator();
    add(ToolbarIcon::Find, _("&Find...\tCtrl+F"));
    add(ToolbarIcon::Replace, _("R&eplace...\tCtrl+H"));
    toolbar->AddSeparator();
    add(ToolbarIcon::WordWrap, _("&Word Wrap\tCtrl+Shift+W"));
    toolbar->AddSeparator();
    add(ToolbarIcon::DefaultFoldLevels, _("&Default Levels\tShift+Alt+D"));
    toolbar->AddSeparator();
    add(ToolbarIcon::ZoomIn, _("Zoom &In\tCtrl++"));
    add(ToolbarIcon::ZoomOut, _("Zoom &Out\tCtrl+-"));
    toolbar->Realize();
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
    toolbar->Bind(wxEVT_SYS_COLOUR_CHANGED, [toolbar](wxSysColourChangedEvent& event) {
        for (size_t index = 0; index < toolbar->GetToolsCount(); ++index) {
            auto* tool = toolbar->GetToolByPos(static_cast<int>(index));
            if (!tool->IsSeparator())
                toolbar->SetToolDisabledBitmap(
                    tool->GetId(),
                    DisabledArtwork(tool->GetNormalBitmapBundle(), wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)));
        }
        toolbar->Realize();
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
