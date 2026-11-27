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
#include <memory>
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
std::unique_ptr<wxImageList> MakeCheckStateImages(wxTreeCtrl& tree) {
    auto& renderer = wxRendererNative::Get();
    const auto size = renderer.GetCheckBoxSize(&tree);
    auto images = std::make_unique<wxImageList>(size.x, size.y, true);
    for (const bool checked : {false, true}) {
        wxBitmap bitmap(size);
        wxMemoryDC dc(bitmap);
        dc.SetBackground(wxBrush(tree.GetBackgroundColour()));
        dc.Clear();
        renderer.DrawCheckBox(&tree, dc, wxRect(size), checked ? wxCONTROL_CHECKED : 0);
        dc.SelectObject(wxNullBitmap);
        images->Add(bitmap);
    }
    return images;
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
class SchemeChooserDialog final : public wxDialog {
public:
    SchemeChooserDialog(wxWindow* parent, SyntaxLanguage current, OriginalSettings& settings, bool favorite)
        : wxDialog(parent, wxID_ANY, favorite ? _("Favorite Schemes") : _("Select Scheme"), wxDefaultPosition,
                   wxDefaultSize, DialogLayout::Style(DialogLayout::Resize::Both)),
          settings_(settings), favoriteMode_(favorite), favorites_(FavoriteSyntaxSchemes(settings.favoriteSchemes)),
          favoriteConfiguration_(settings.favoriteSchemes),
          temporaryDefault_(SyntaxFromOriginalSchemeId(settings.defaultScheme) ? settings.defaultScheme : 0) {
        // Adapted from pinned Styles.cpp::Style_AddAllLexerToTreeView and IDD_STYLESELECT.
        auto contents = std::make_unique<wxBoxSizer>(wxVERTICAL);
        tree_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(300, 300)),
                               wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE);
        tree_->SetName("Syntax schemes");
        AppLocalization::SetAccessibleName(*tree_, _("Syntax schemes"));
        if (favoriteMode_) tree_->AssignStateImageList(MakeCheckStateImages(*tree_).release());
        PopulateTree(current);
        contents->Add(tree_, 1, wxEXPAND | wxALL, FromDIP(DialogLayout::MarginDip));
        AddOptions(*contents, current);
        contents->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                      FromDIP(DialogLayout::MarginDip));
        BindTreeEvents();
        DialogLayout::Fit(*this, std::move(contents), DialogLayout::Resize::Both);
        CentreOnParent();
        tree_->SetFocus();
    }

    std::optional<SyntaxLanguage> Choose() {
        if (ShowModal() != wxID_OK) return std::nullopt;
        if (defaultEdited_) settings_.defaultScheme = temporaryDefault_;
        settings_.autoSelect = automatic_->GetValue();
        if (favoriteMode_) settings_.favoriteSchemes = favoriteConfiguration_;
        return favoriteMode_ ? std::nullopt : Selected(*tree_);
    }

private:
    wxTreeItemId AddScheme(wxTreeItemId group, SyntaxLanguage language) {
        const auto item = tree_->AppendItem(group, wxGetTranslation(SyntaxFor(language).chooserCaption), -1, -1,
                                            new SchemeData(language));
        if (favoriteMode_ && language != SyntaxLanguage::PlainText)
            tree_->SetItemState(item, std::ranges::find(favorites_, language) != favorites_.end() ? 1 : 0);
        return item;
    }

    void PopulateTree(SyntaxLanguage current) {
        const auto root = tree_->AddRoot({});
        wxTreeItemId selection;
        const auto add = [this, current, &selection](wxTreeItemId group, SyntaxLanguage language) {
            const auto item = AddScheme(group, language);
            if (!selection.IsOk() && language == SyntaxSettingsLanguage(current)) selection = item;
            return item;
        };
        add(root, SyntaxLanguage::PlainText);
        favoriteGroup_ = tree_->AppendItem(root, _("Favorite Schemes"));
        for (const auto language : favorites_)
            add(favoriteGroup_, language);
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
        for (const auto language : languages) {
            const auto initial = wxString(SyntaxSettingsSection(language)).Left(1).Upper();
            if (initial != previous) {
                group = tree_->AppendItem(root, initial);
                previous = initial;
            }
            generalItems_.emplace_back(language, add(group, language));
        }
        tree_->ExpandAll();
        tree_->SelectItem(selection);
        tree_->EnsureVisible(selection);
        if (favoriteMode_) tree_->EnsureVisible(favoriteGroup_);
    }

    void AddOptions(wxSizer& contents, SyntaxLanguage current) {
        // These controls edit only temporary state until the native OK response.
        defaultScheme_ = new wxCheckBox(this, wxID_ANY, _("Set selected scheme as &default."));
        defaultScheme_->SetName("Default scheme");
        defaultScheme_->SetValue(SyntaxFor(SyntaxSettingsLanguage(current)).original.schemeId == temporaryDefault_);
        automatic_ = new wxCheckBox(this, wxID_ANY, _("&Auto-select by filename extension."));
        automatic_->SetName("AutoSelect");
        automatic_->SetValue(settings_.autoSelect);
        for (auto* control : {defaultScheme_, automatic_})
            contents.Add(control, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(DialogLayout::GapDip));
        defaultScheme_->Bind(wxEVT_CHECKBOX, &SchemeChooserDialog::OnDefaultScheme, this);
    }

    void BindTreeEvents() {
        tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, &SchemeChooserDialog::OnActivate, this);
        tree_->Bind(wxEVT_TREE_SEL_CHANGED, &SchemeChooserDialog::OnSelection, this);
        if (favoriteMode_) {
            tree_->Bind(wxEVT_TREE_STATE_IMAGE_CLICK, &SchemeChooserDialog::OnFavoriteCheck, this);
            tree_->Bind(wxEVT_TREE_BEGIN_DRAG, &SchemeChooserDialog::OnBeginDrag, this);
            tree_->Bind(wxEVT_TREE_END_DRAG, &SchemeChooserDialog::OnEndDrag, this);
        }
    }

    void OnDefaultScheme(wxCommandEvent&) {
        defaultEdited_ = true;
        temporaryDefault_ = 0;
        if (defaultScheme_->GetValue()) {
            if (const auto language = Selected(*tree_)) temporaryDefault_ = SyntaxFor(*language).original.schemeId;
        }
    }

    void OnActivate(wxTreeEvent&) {
        if (!favoriteMode_ && Selected(*tree_)) EndModal(wxID_OK);
    }

    void OnSelection(wxTreeEvent& event) {
        const auto language = Selected(*tree_);
        defaultScheme_->Enable(language.has_value());
        if (language)
            defaultScheme_->SetValue(SyntaxFor(*language).original.schemeId == temporaryDefault_);
        else
            tree_->Expand(event.GetItem());
    }

    void RebuildFavorites(SyntaxLanguage selected, bool selectFavorite) {
        tree_->DeleteChildren(favoriteGroup_);
        wxTreeItemId selectedFavorite;
        for (const auto value : favorites_) {
            const auto item = AddScheme(favoriteGroup_, value);
            if (value == selected) selectedFavorite = item;
        }
        for (const auto& [value, general] : generalItems_) {
            tree_->SetItemState(general, std::ranges::find(favorites_, value) != favorites_.end() ? 1 : 0);
            if (value == selected && (!selectFavorite || !selectedFavorite.IsOk())) tree_->SelectItem(general);
        }
        if (selectFavorite && selectedFavorite.IsOk()) tree_->SelectItem(selectedFavorite);
        tree_->Expand(favoriteGroup_);
    }

    void OnFavoriteCheck(wxTreeEvent& event) {
        const auto item = event.GetItem();
        const auto* data = item.IsOk() ? static_cast<SchemeData*>(tree_->GetItemData(item)) : nullptr;
        if (!data || data->language == SyntaxLanguage::PlainText) return;
        const auto language = data->language;
        const auto previous = favorites_;
        const auto found = std::ranges::find(favorites_, language);
        if (found != favorites_.end())
            favorites_.erase(found);
        else
            favorites_.push_back(language);
        if (const auto configuration = FavoriteConfiguration(settings_.favoriteSchemes, favorites_))
            favoriteConfiguration_ = *configuration;
        else
            favorites_ = previous;
        RebuildFavorites(language, false);
    }

    void OnBeginDrag(wxTreeEvent& event) {
        dragging_.reset();
        const auto item = event.GetItem();
        const auto* data = item.IsOk() ? static_cast<SchemeData*>(tree_->GetItemData(item)) : nullptr;
        if (!data || data->language == SyntaxLanguage::PlainText) {
            event.Veto();
            return;
        }
        dragging_ = data->language;
        draggingFavorite_ = tree_->GetItemParent(item) == favoriteGroup_;
        tree_->SelectItem(item);
        event.Allow();
    }

    void OnEndDrag(wxTreeEvent& event) {
        const auto language = std::exchange(dragging_, std::nullopt);
        const auto target = event.GetItem();
        if (!language || !target.IsOk() || (target != favoriteGroup_ && tree_->GetItemParent(target) != favoriteGroup_))
            return;
        const auto* data = static_cast<SchemeData*>(tree_->GetItemData(target));
        const auto after = data ? std::optional(data->language) : std::nullopt;
        if (after == language) return;
        const auto previous = favorites_;
        const auto found = std::ranges::find(favorites_, *language);
        if (found != favorites_.end()) {
            // A drag from the alphabetic copy of an existing favorite just checks it.
            if (!draggingFavorite_) return;
            favorites_.erase(found);
        } else if (favorites_.size() >= SyntaxSchemeLimits::FavoriteCount) {
            return;
        }
        auto position = favorites_.begin();
        if (after) {
            position = std::ranges::find(favorites_, *after);
            if (position != favorites_.end()) ++position;
        } else if (!tree_->IsExpanded(favoriteGroup_)) {
            position = favorites_.end();
        }
        favorites_.insert(position, *language);
        if (const auto configuration = FavoriteConfiguration(settings_.favoriteSchemes, favorites_))
            favoriteConfiguration_ = *configuration;
        else
            favorites_ = previous;
        RebuildFavorites(*language, tree_->IsExpanded(favoriteGroup_));
    }

    OriginalSettings& settings_;
    const bool favoriteMode_;
    std::vector<SyntaxLanguage> favorites_;
    wxString favoriteConfiguration_;
    int temporaryDefault_;
    bool defaultEdited_ = false;
    std::optional<SyntaxLanguage> dragging_;
    bool draggingFavorite_ = false;
    wxTreeCtrl* tree_ = nullptr;
    wxCheckBox* defaultScheme_ = nullptr;
    wxCheckBox* automatic_ = nullptr;
    wxTreeItemId favoriteGroup_;
    std::vector<std::pair<SyntaxLanguage, wxTreeItemId>> generalItems_;
};
} // namespace

std::optional<SyntaxLanguage> ChooseSyntaxScheme(wxWindow* parent, SyntaxLanguage current, OriginalSettings& settings,
                                                 bool favorite) {
    SchemeChooserDialog dialog(parent, current, settings, favorite);
    return dialog.Choose();
}
