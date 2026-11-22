#include "app/dialogs/scheme_chooser.h"
#include "app/dialogs/dialog_layout.h"
#include "app/localization.h"
#include "settings/original_settings.h"
#include <wx/checkbox.h>
#include <wx/imaglist.h>
#include <wx/dcmemory.h>
#include <wx/renderer.h>
#include <wx/treectrl.h>
#include <wx/translation.h>
#include <algorithm>
#include <vector>
#include <utility>

namespace {
class SchemeData final : public wxTreeItemData {
public:
    explicit SchemeData(SyntaxLanguage value) : language(value) {}
    SyntaxLanguage language;
};
std::optional<SyntaxLanguage> Selected(wxTreeCtrl& tree) {
    const auto item = tree.GetSelection();
    const auto* data = item.IsOk() ? static_cast<SchemeData*>(tree.GetItemData(item)) : nullptr;
    return data ? std::optional(data->language) : std::nullopt;
}
void AddCheckStates(wxTreeCtrl& tree) {
    auto& renderer = wxRendererNative::Get();
    const auto size = renderer.GetCheckBoxSize(&tree);
    auto* images = new wxImageList(size.x, size.y, true);
    for (const bool checked : {false, true}) {
        wxBitmap bitmap(size);
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(tree.GetBackgroundColour()));
        dc.Clear();
        renderer.DrawCheckBox(&tree, dc, wxRect(size), checked ? wxCONTROL_CHECKED : 0);
        dc.SelectObject(wxNullBitmap);
        images->Add(bitmap);
    }
    tree.AssignStateImageList(images);
}
std::optional<wxString> FavoriteConfiguration(const wxString& original, const std::vector<SyntaxLanguage>& favorites) {
    if (FavoriteSyntaxSchemes(original) == favorites) return original;
    // Replace supported slots in their new order; unavailable original IDs stay
    // in the compatibility value rather than being mistaken for enum ordinals.
    std::vector<int> identities;
    size_t next = 0;
    for (const int identity : OriginalFavoriteSchemeIds(original)) {
        if (identity > 0 && SyntaxFromOriginalSchemeId(identity)) {
            if (next < favorites.size()) identities.push_back(SyntaxFor(favorites[next++]).original.schemeId);
        } else {
            identities.push_back(identity);
        }
    }
    while (next < favorites.size())
        identities.push_back(SyntaxFor(favorites[next++]).original.schemeId);
    if (identities.size() > SyntaxSchemeLimits::FavoriteCount) return std::nullopt;
    wxString configuration;
    for (size_t index = 0; index < identities.size(); ++index) {
        const auto value = wxString::Format("%d", identities[index]);
        if (!configuration.empty()) configuration += ' ';
        configuration += value;
    }
    return configuration.length() <= SyntaxSchemeLimits::FavoriteConfigCharacters ? std::optional(configuration)
                                                                                  : std::nullopt;
}
} // namespace

std::optional<SyntaxLanguage> ChooseSyntaxScheme(wxWindow* parent, SyntaxLanguage current, OriginalSettings& settings,
                                                 bool favorite) {
    // Adapted from pinned Styles.cpp::Style_AddAllLexerToTreeView and IDD_STYLESELECT.
    wxDialog dialog(parent, wxID_ANY, favorite ? _("Favorite Schemes") : _("Select Scheme"), wxDefaultPosition,
                    wxDefaultSize, DialogLayout::Style(DialogLayout::Resize::Both));
    auto* contents = new wxBoxSizer(wxVERTICAL);
    auto* tree = new wxTreeCtrl(&dialog, wxID_ANY, wxDefaultPosition, dialog.FromDIP(wxSize(300, 300)),
                                wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE);
    tree->SetName("Syntax schemes");
    AppLocalization::SetAccessibleName(*tree, _("Syntax schemes"));
    if (favorite) AddCheckStates(*tree);
    const auto root = tree->AddRoot({});
    wxTreeItemId selection;
    wxTreeItemId favoriteGroup;
    auto favorites = FavoriteSyntaxSchemes(settings.favoriteSchemes);
    const auto add = [&](wxTreeItemId group, SyntaxLanguage language) {
        const auto item = tree->AppendItem(group, wxGetTranslation(SyntaxFor(language).chooserCaption), -1, -1,
                                           new SchemeData(language));
        if (!selection.IsOk() && language == SyntaxSettingsLanguage(current)) selection = item;
        if (favorite && language != SyntaxLanguage::PlainText)
            tree->SetItemState(item, std::ranges::find(favorites, language) != favorites.end() ? 1 : 0);
        return item;
    };
    add(root, SyntaxLanguage::PlainText);
    favoriteGroup = tree->AppendItem(root, _("Favorite Schemes"));
    for (const auto language : favorites)
        add(favoriteGroup, language);
    std::vector<SyntaxLanguage> languages;
    for (const auto& definition : SyntaxCatalog()) {
        if (definition.language != SyntaxLanguage::PlainText &&
            SyntaxSettingsLanguage(definition.language) == definition.language)
            languages.push_back(definition.language);
    }
    std::ranges::sort(languages, [](SyntaxLanguage first, SyntaxLanguage second) {
        return wxString(SyntaxSettingsSection(first)).CmpNoCase(SyntaxSettingsSection(second)) < 0;
    });
    wxString previous;
    wxTreeItemId group;
    std::vector<std::pair<SyntaxLanguage, wxTreeItemId>> generalItems;
    for (const auto language : languages) {
        const auto initial = wxString(SyntaxSettingsSection(language)).Left(1).Upper();
        if (initial != previous) {
            group = tree->AppendItem(root, initial);
            previous = initial;
        }
        generalItems.emplace_back(language, add(group, language));
    }
    tree->ExpandAll();
    tree->SelectItem(selection);
    tree->EnsureVisible(selection);
    if (favorite) tree->EnsureVisible(favoriteGroup);
    contents->Add(tree, 1, wxEXPAND | wxALL, dialog.FromDIP(DialogLayout::MarginDip));
    // These controls edit only temporary state until the native OK response.
    int temporaryDefault = SyntaxFromOriginalSchemeId(settings.defaultScheme) ? settings.defaultScheme : 0;
    bool defaultEdited = false;
    auto* defaultScheme = new wxCheckBox(&dialog, wxID_ANY, _("Set selected scheme as &default."));
    defaultScheme->SetName("Default scheme");
    defaultScheme->SetValue(SyntaxFor(SyntaxSettingsLanguage(current)).original.schemeId == temporaryDefault);
    auto* automatic = new wxCheckBox(&dialog, wxID_ANY, _("&Auto-select by filename extension."));
    automatic->SetName("AutoSelect");
    automatic->SetValue(settings.autoSelect);
    for (auto* control : {defaultScheme, automatic})
        contents->Add(control, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, dialog.FromDIP(DialogLayout::GapDip));
    defaultScheme->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent&) {
        defaultEdited = true;
        temporaryDefault = 0;
        if (defaultScheme->GetValue()) {
            if (const auto language = Selected(*tree)) temporaryDefault = SyntaxFor(*language).original.schemeId;
        }
    });
    contents->Add(dialog.CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                  dialog.FromDIP(DialogLayout::MarginDip));
    tree->Bind(wxEVT_TREE_ITEM_ACTIVATED, [&](wxTreeEvent&) {
        if (!favorite && Selected(*tree)) dialog.EndModal(wxID_OK);
    });
    tree->Bind(wxEVT_TREE_SEL_CHANGED, [&](wxTreeEvent& event) {
        const auto language = Selected(*tree);
        defaultScheme->Enable(language.has_value());
        if (language)
            defaultScheme->SetValue(SyntaxFor(*language).original.schemeId == temporaryDefault);
        else
            tree->Expand(event.GetItem());
    });
    const auto rebuildFavorites = [&](SyntaxLanguage selected, bool selectFavorite) {
        tree->DeleteChildren(favoriteGroup);
        wxTreeItemId selectedFavorite;
        for (const auto value : favorites) {
            const auto item = add(favoriteGroup, value);
            if (value == selected) selectedFavorite = item;
        }
        for (const auto& [value, general] : generalItems) {
            tree->SetItemState(general, std::ranges::find(favorites, value) != favorites.end() ? 1 : 0);
            if (value == selected && (!selectFavorite || !selectedFavorite.IsOk())) tree->SelectItem(general);
        }
        if (selectFavorite && selectedFavorite.IsOk()) tree->SelectItem(selectedFavorite);
        tree->Expand(favoriteGroup);
    };
    std::optional<SyntaxLanguage> dragging;
    bool draggingFavorite = false;
    if (favorite) {
        tree->Bind(wxEVT_TREE_STATE_IMAGE_CLICK, [&](wxTreeEvent& event) {
            const auto item = event.GetItem();
            const auto* data = item.IsOk() ? static_cast<SchemeData*>(tree->GetItemData(item)) : nullptr;
            if (!data || data->language == SyntaxLanguage::PlainText) return;
            const auto language = data->language;
            const auto previous = favorites;
            const auto found = std::ranges::find(favorites, language);
            if (found != favorites.end())
                favorites.erase(found);
            else if (favorites.size() < SyntaxSchemeLimits::FavoriteCount)
                favorites.push_back(language);
            if (!FavoriteConfiguration(settings.favoriteSchemes, favorites)) favorites = previous;
            rebuildFavorites(language, false);
        });
        tree->Bind(wxEVT_TREE_BEGIN_DRAG, [&](wxTreeEvent& event) {
            dragging.reset();
            const auto item = event.GetItem();
            const auto* data = item.IsOk() ? static_cast<SchemeData*>(tree->GetItemData(item)) : nullptr;
            if (!data || data->language == SyntaxLanguage::PlainText) {
                event.Veto();
                return;
            }
            dragging = data->language;
            draggingFavorite = tree->GetItemParent(item) == favoriteGroup;
            tree->SelectItem(item);
            event.Allow();
        });
        tree->Bind(wxEVT_TREE_END_DRAG, [&](wxTreeEvent& event) {
            const auto language = std::exchange(dragging, std::nullopt);
            const auto target = event.GetItem();
            if (!language || !target.IsOk() ||
                (target != favoriteGroup && tree->GetItemParent(target) != favoriteGroup))
                return;
            const auto* data = static_cast<SchemeData*>(tree->GetItemData(target));
            const auto after = data ? std::optional(data->language) : std::nullopt;
            if (after == language) return;
            const auto previous = favorites;
            const auto found = std::ranges::find(favorites, *language);
            if (found != favorites.end()) {
                // A drag from the alphabetic copy of an existing favorite just checks it.
                if (!draggingFavorite) return;
                favorites.erase(found);
            } else if (favorites.size() >= SyntaxSchemeLimits::FavoriteCount) {
                return;
            }
            auto position = favorites.begin();
            if (after) {
                position = std::ranges::find(favorites, *after);
                if (position != favorites.end()) ++position;
            } else if (!tree->IsExpanded(favoriteGroup)) {
                position = favorites.end();
            }
            favorites.insert(position, *language);
            if (!FavoriteConfiguration(settings.favoriteSchemes, favorites)) favorites = previous;
            rebuildFavorites(*language, tree->IsExpanded(favoriteGroup));
        });
    }
    DialogLayout::Fit(dialog, contents, DialogLayout::Resize::Both);
    dialog.CentreOnParent();
    tree->SetFocus();
    if (dialog.ShowModal() != wxID_OK) return std::nullopt;
    if (defaultEdited) settings.defaultScheme = temporaryDefault;
    settings.autoSelect = automatic->GetValue();
    if (favorite) {
        if (const auto configuration = FavoriteConfiguration(settings.favoriteSchemes, favorites))
            settings.favoriteSchemes = *configuration;
    }
    return favorite ? std::nullopt : Selected(*tree);
}
