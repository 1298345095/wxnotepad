#include "app/editor_frame.h"
#include "app/toolbar_art.h"
#include "settings/settings_store.h"
#include "editor/editor_control.h"
#include <wx/menu.h>
#include <wx/imagpng.h>
#include <wx/toolbar.h>
#include <wx/translation.h>
#include <wx/settings.h>
#ifdef __WXGTK__
#include <wx/clipbrd.h>
#endif

namespace {
wxBitmapBundle DisabledArtwork(const wxBitmapBundle& normal, const wxColour& background) {
    // Notepad4 CreateBars/BitmapAlphaBlend, 89b8b36, for light chrome. Its dark
    // strokes disappear on dark chrome, where native disabled text supplies
    // the foreground while the source alpha retains the original shape.
    constexpr unsigned OriginalDisabledOpacity = 0x60;
    constexpr unsigned ChannelMaximum = 255;
    const bool dark = wxSystemSettings::GetAppearance().IsDark();
    const auto disabled = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    const std::array<unsigned, 3> foreground{disabled.Red(), disabled.Green(), disabled.Blue()};
    const std::array<unsigned, 3> backdrop{background.Red(), background.Green(), background.Blue()};
    wxVector<wxBitmap> images;
    for (const int size : {16, 24, 32, 40, 48}) {
        auto image = normal.GetBitmap(wxSize(size, size)).ConvertToImage();
        auto* data = image.GetData();
        for (int byte = 0; byte < image.GetWidth() * image.GetHeight() * 3; ++byte)
            data[byte] =
                static_cast<unsigned char>(dark ? foreground[byte % 3]
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
    const auto add = [this, toolbar](int image, const wxString& label) {
        const auto id = FindMenuCommand(label);
        if (id == wxNOT_FOUND) return;
        auto* command = GetMenuBar()->FindItem(id);
        const auto art = ToolbarArt::Icon(image);
        toolbar->AddTool(command->GetId(), command->GetItemLabelText(), art,
                         DisabledArtwork(art, wxSystemSettings::GetColour(wxSYS_COLOUR_3DFACE)),
                         command->IsCheckable() ? wxITEM_CHECK : wxITEM_NORMAL,
                         wxStripMenuCodes(label, wxStrip_Mnemonics), command->GetHelp());
    };
    add(0, _("&New\tCtrl+N"));
    add(26, _("New &Window\tAlt+N"));
    add(1, _("&Open...\tCtrl+O"));
    toolbar->AddSeparator();
    add(3, _("&Save\tCtrl+S"));
    add(17, _("Save &As...\tF6"));
    add(18, _("Save Cop&y...\tCtrl+F6"));
    toolbar->AddSeparator();
    add(4, _("&Undo\tCtrl+Z"));
    add(5, _("&Redo\tCtrl+Y"));
    toolbar->AddSeparator();
    add(6, _("Cu&t\tCtrl+X"));
    add(7, _("&Copy\tCtrl+C"));
    add(8, _("&Paste\tCtrl+V"));
    toolbar->AddSeparator();
    add(9, _("&Find...\tCtrl+F"));
    add(10, _("R&eplace...\tCtrl+H"));
    toolbar->AddSeparator();
    add(11, _("&Word Wrap\tCtrl+Shift+W"));
    toolbar->AddSeparator();
    add(23, _("&Default Levels\tShift+Alt+D"));
    toolbar->AddSeparator();
    add(12, _("Zoom &In\tCtrl++"));
    add(13, _("Zoom &Out\tCtrl+-"));
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
