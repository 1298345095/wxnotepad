#include "app/dialogs/style_customization.h"
#include "app/dialogs/dialog_layout.h"
#include "app/localization.h"
#include "settings/settings_store.h"
#include <wx/colordlg.h>
#include <wx/fontdlg.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/treectrl.h>
#include <wx/tokenzr.h>
#include <algorithm>
#include <utility>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <commdlg.h>
#endif

namespace {
class StyleData final : public wxTreeItemData {
public:
    StyleData(bool global, SyntaxLanguage language, const StyleDefinition* style = nullptr)
        : global(global), language(language), style(style) {}
    bool global;
    SyntaxLanguage language;
    const StyleDefinition* style;
};
class StyleDialog final : public wxDialog {
public:
    StyleDialog(wxWindow* parent, SyntaxLanguage current, OriginalSettings& settings,
                SettingsStore* store, const std::function<void()>& preview)
        : wxDialog(parent, wxID_ANY,
                   settings.fonts.theme == StyleTheme::Dark ? _("Customize Schemes - Dark Theme")
                                                           : _("Customize Schemes - Default Theme"),
                   wxDefaultPosition, wxDefaultSize, DialogLayout::Style(DialogLayout::Resize::Both)),
          settings_(settings), store_(store), preview_(preview), current_(current) {
        auto layout = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
        tree_ = new wxTreeCtrl(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(220, 350)),
                               wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_LINES_AT_ROOT | wxTR_SINGLE);
        tree_->SetName("Scheme styles");
        AppLocalization::SetAccessibleName(*tree_, _("Scheme styles"));
        layout->Add(tree_, 0, wxEXPAND | wxALL, FromDIP(DialogLayout::MarginDip));
        auto* fields = new wxBoxSizer(wxVERTICAL);
        auto* help = new wxTextCtrl(this, wxID_ANY,
            _("Select a scheme to edit its filename extensions, or select a style.\n"
              "font:$(Code); size:+1; bold; italic; fore:#008000; back:#FFFFFF\n"
              "Preview applies the settings to the document. Cancel restores the original styles."),
            wxDefaultPosition, FromDIP(wxSize(410, 100)), wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
        fields->Add(help, 0, wxEXPAND | wxBOTTOM, FromDIP(DialogLayout::GapDip));
        help->DisableFocusFromKeyboard();
        label_ = new wxStaticText(this, wxID_ANY, _("Style &settings:"));
        fields->Add(label_, 0, wxBOTTOM, FromDIP(DialogLayout::GapDip));
        edit_ = new wxTextCtrl(this, wxID_ANY, {}, wxDefaultPosition, FromDIP(wxSize(410, 55)), wxTE_MULTILINE);
        edit_->SetName("Style settings");
        edit_->SetMaxLength(StyleProfileLimits::ExtensionCharacters);
        AppLocalization::SetAccessibleName(*edit_, _("Style settings"));
        fields->Add(edit_, 1, wxEXPAND | wxBOTTOM, FromDIP(DialogLayout::GapDip));
        fields->Add(new wxStaticText(this, wxID_ANY, _("Built-in default value:")), 0, wxBOTTOM,
                    FromDIP(DialogLayout::GapDip));
        defaults_ = new wxTextCtrl(this, wxID_ANY, {}, wxDefaultPosition, FromDIP(wxSize(410, 55)),
                                   wxTE_MULTILINE | wxTE_READONLY);
        defaults_->SetName("Built-in default value");
        fields->Add(defaults_, 1, wxEXPAND | wxBOTTOM, FromDIP(DialogLayout::GapDip));
        auto* tools = new wxBoxSizer(wxHORIZONTAL);
        const auto button = [this, tools](int id, const wxString& caption, const wxString& name) {
            auto* control = new wxButton(this, id, caption);
            control->SetName(name);
            tools->Add(control, 0, wxRIGHT, FromDIP(DialogLayout::GapDip));
            return control;
        };
        fore_ = button(wxID_ANY, _("For&e..."), "Style foreground");
        back_ = button(wxID_ANY, _("&Back..."), "Style background");
        font_ = button(wxID_ANY, _("&Font..."), "Style font");
        button(wxID_APPLY, _("&Preview"), "Preview");
        reset_ = button(wxID_RESET, _("&Reset"), "Reset style");
        auto* previous = button(wxID_ANY, "↑", "Previous style");
        auto* next = button(wxID_ANY, "↓", "Next style");
        fields->Add(tools, 0, wxALIGN_RIGHT | wxBOTTOM, FromDIP(DialogLayout::MarginDip));
        auto* footer = new wxBoxSizer(wxHORIZONTAL);
        auto* import = new wxButton(this, wxID_OPEN, _("&Import..."));
        import->SetName("Import schemes");
        auto* exportStyles = new wxButton(this, wxID_SAVEAS, _("E&xport..."));
        exportStyles->SetName("Export schemes");
        import->Enable(store_ != nullptr);
        exportStyles->Enable(store_ != nullptr);
        footer->Add(import, 0, wxRIGHT, FromDIP(DialogLayout::GapDip));
        footer->Add(exportStyles, 0, wxRIGHT, FromDIP(DialogLayout::GapDip));
        auto* resetAll = new wxButton(this, wxID_ANY, _("Reset All"));
        resetAll->SetName("Reset all styles");
        footer->Add(resetAll, 0, wxRIGHT, FromDIP(DialogLayout::GapDip));
        footer->AddStretchSpacer();
        footer->Add(CreateButtonSizer(wxOK | wxCANCEL));
        fields->Add(footer, 0, wxEXPAND);
        layout->Add(fields, 1, wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM, FromDIP(DialogLayout::MarginDip));
        tree_->Bind(wxEVT_TREE_SEL_CHANGING, [this](wxTreeEvent&) { CommitEdit(); });
        tree_->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent&) { ShowSelection(); });
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CommitEdit(); preview_(); }, wxID_APPLY);
        Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CommitEdit(); EndModal(wxID_OK); }, wxID_OK);
        reset_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (const auto* data = Selected(); data && data->style) {
                const auto defaults = store_ ? store_->ResetStyles() : FontSettings{};
                Value(*data) = data->global ? defaults.GlobalStyle(wxString::FromUTF8(data->style->key))
                                           : defaults.TokenStyle(data->language, *data->style);
                ShowSelection();
            } else if (const auto* data = Selected(); data && !data->global) {
                settings_.schemeExtensions.erase(data->language);
                ShowSelection();
            }
            edit_->SetFocus();
        });
        resetAll->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            settings_.fonts = store_ ? store_->ResetStyles() : FontSettings{};
            for (const auto& scheme : SyntaxCatalog())
                settings_.fonts.Scheme(scheme.language).useCode = SyntaxSettingsLanguage(scheme.language) == SyntaxLanguage::PlainText;
            settings_.schemeExtensions.clear();
            ShowSelection();
            edit_->SetFocus();
        });
        font_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ChooseFont(); });
        fore_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ChooseColour(true); });
        back_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ChooseColour(false); });
        previous->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Navigate(false); });
        next->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Navigate(true); });
        import->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SchemeFile(true); });
        exportStyles->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SchemeFile(false); });
        Populate(current);
        DialogLayout::Fit(*this, std::move(layout), DialogLayout::Resize::Both);
        CentreOnParent();
        tree_->SetFocus();
    }
private:
    const StyleData* Selected() const {
        const auto item = tree_->GetSelection();
        return item.IsOk() ? static_cast<StyleData*>(tree_->GetItemData(item)) : nullptr;
    }
    wxString& Value(const StyleData& data) {
        const auto key = wxString::FromUTF8(data.style->key);
        if (data.global) {
            if (key == "Default Code Style") return settings_.fonts.codeStyle;
            if (key == "Default Text Style") return settings_.fonts.textStyle;
            return settings_.fonts.globalStyles.try_emplace(key, settings_.fonts.GlobalStyle(key)).first->second;
        }
        auto& scheme = settings_.fonts.Scheme(data.language);
        if (key == "Default") return scheme.defaultStyle;
        return scheme.tokenStyles.try_emplace(key, settings_.fonts.TokenStyle(data.language, *data.style))
            .first->second;
    }
    void CommitEdit() {
        const auto* data = Selected();
        if (!data) return;
        if (data->style) Value(*data) = edit_->GetValue().Left(StyleProfileLimits::StyleCharacters);
        else if (!data->global) {
            const auto value = edit_->GetValue();
            settings_.schemeExtensions[data->language] = value.empty() ? SyntaxExtensionsFor(data->language) : value;
        }
    }
    void ShowSelection() {
        const auto* data = Selected();
        const bool style = data && data->style;
        label_->SetLabel(style ? _("Style &settings:") : _("Associated file&name extensions:"));
        edit_->ChangeValue(style ? Value(*data) : data && !data->global ? SyntaxExtensionsFor(data->language, settings_.schemeExtensions) : wxString{});
        defaults_->ChangeValue(style ? wxString::FromUTF8(data->style->defaultValue) : data && !data->global ? SyntaxExtensionsFor(data->language) : wxString{});
        edit_->Enable(data && !data->global || style);
        reset_->Enable(data != nullptr);
        bool font = style, fore = style, back = style;
        if (style && data->global) {
            const wxString key = wxString::FromUTF8(data->style->key);
            if (key == "Extra Line Spacing") font = fore = back = false;
            else if (key == "Matching Brace" || key == "Matching Brace Error" || key == "IME Indicator" ||
                     key == "Mark Occurrences") { font = back = false; }
            else if (key == "Indentation Guide" || key == "Whitespace" || key == "Current Line" || key == "Caret" ||
                     key == "Long Line Marker" || key == "Code Folding" || key == "Folding Marker" ||
                     key == "Bookmark" || key == "Selected Text") font = false;
        }
        font_->Enable(font); fore_->Enable(fore); back_->Enable(back);
    }
    void Populate(SyntaxLanguage current) {
        const auto root = tree_->AddRoot({});
        const auto globals = tree_->AppendItem(root, _("Global Styles"), -1, -1,
                                               new StyleData(true, SyntaxLanguage::PlainText));
        for (const auto& style : GlobalStyleDefinitions())
            tree_->AppendItem(globals, wxGetTranslation(style.key), -1, -1,
                              new StyleData(true, SyntaxLanguage::PlainText, &style));
        wxTreeItemId selection;
        const auto add = [this, current, &selection](wxTreeItemId group, SyntaxLanguage language) {
            const auto item = tree_->AppendItem(group, wxGetTranslation(SyntaxFor(language).chooserCaption), -1, -1,
                                                new StyleData(false, language));
            for (const auto& style : SyntaxStyleDefinitions(language))
                tree_->AppendItem(item, wxGetTranslation(style.key), -1, -1, new StyleData(false, language, &style));
            if (!selection.IsOk() && language == SyntaxSettingsLanguage(current)) selection = item;
            return item;
        };
        add(root, SyntaxLanguage::PlainText);
        const auto favorites = tree_->AppendItem(root, _("Favorite Schemes"));
        for (const auto language : FavoriteSyntaxSchemes(settings_.favoriteSchemes)) add(favorites, language);
        std::vector<SyntaxLanguage> languages;
        for (const auto& definition : SyntaxCatalog())
            if (definition.language != SyntaxLanguage::PlainText &&
                SyntaxSettingsLanguage(definition.language) == definition.language) languages.push_back(definition.language);
        std::ranges::sort(languages, [](auto first, auto second) {
            return wxString(SyntaxSettingsSection(first)).CmpNoCase(SyntaxSettingsSection(second)) < 0;
        });
        wxString previous;
        wxTreeItemId group;
        for (auto language : languages) {
            const auto initial = wxString(SyntaxSettingsSection(language)).Left(1).Upper();
            if (initial != previous) { group = tree_->AppendItem(root, initial); previous = initial; }
            add(group, language);
        }
        tree_->Expand(globals); tree_->Expand(favorites);
        wxTreeItemIdValue cookie;
        for (auto item = tree_->GetFirstChild(root, cookie); item.IsOk(); item = tree_->GetNextChild(root, cookie))
            if (!tree_->GetItemData(item)) tree_->Expand(item);
        tree_->SelectItem(selection); tree_->Expand(selection); tree_->EnsureVisible(selection);
    }
    void Navigate(bool next) {
        const auto item = next ? tree_->GetNextVisible(tree_->GetSelection()) : tree_->GetPrevVisible(tree_->GetSelection());
        if (item.IsOk()) { tree_->SelectItem(item); tree_->EnsureVisible(item); }
        edit_->SetFocus();
    }
    void ChooseFont() {
        const auto* selected = Selected();
        if (!selected || !selected->style) return;
        const bool globalBase = selected->global &&
            (wxString::FromUTF8(selected->style->key) == "Default Code Style" || wxString::FromUTF8(selected->style->key) == "Default Text Style");
        const auto initial = StyleFont(settings_.fonts, edit_->GetValue(), settings_.fonts.Scheme(current_).useCode, globalBase);
        wxFontData data;
        data.SetInitialFont(initial);
        const wxColour foreground(StyleProperty(edit_->GetValue(), "fore"));
        data.SetColour(foreground.IsOk() ? foreground : DefaultStyleForeground(settings_.fonts, settings_.fonts.Scheme(current_).useCode));
        data.EnableEffects(true);
        wxFontDialog dialog(this, data);
        if (dialog.ShowModal() == wxID_OK) edit_->SetValue(MergeChosenFont(edit_->GetValue(), dialog.GetFontData().GetChosenFont()));
        edit_->SetFocus();
    }
    void ChooseColour(bool fore) {
        const wxString key = fore ? "fore" : "back";
        wxColourData data;
        auto initial = wxColour(StyleProperty(edit_->GetValue(), key));
        data.SetColour(initial.IsOk() ? initial : wxSystemSettings::GetColour(fore ? wxSYS_COLOUR_WINDOWTEXT : wxSYS_COLOUR_WINDOW));
        data.SetChooseFull(true);
        for (size_t index = 0; index < settings_.fonts.customColours.size(); ++index)
            data.SetCustomColour(static_cast<int>(index), wxColour(wxString::Format("#%06X", settings_.fonts.customColours[index])));
        wxColour chosen;
#ifdef __WXMSW__
        // wxMSW copies custom colors out only on OK. The original retains edits
        // on chooser Cancel, and restores them at the outer dialog boundary.
        std::array<COLORREF, DefaultCustomColours.size()> palette;
        for (size_t index = 0; index < palette.size(); ++index)
            palette[index] = data.GetCustomColour(static_cast<int>(index)).GetRGB();
        CHOOSECOLORW chooser{};
        chooser.lStructSize = sizeof(chooser);
        chooser.hwndOwner = static_cast<HWND>(GetHandle());
        chooser.rgbResult = data.GetColour().GetRGB();
        chooser.lpCustColors = palette.data();
        chooser.Flags = CC_FULLOPEN | CC_RGBINIT | CC_SOLIDCOLOR;
        const bool accepted = ::ChooseColorW(&chooser) != 0;
        chosen.SetRGB(chooser.rgbResult);
        for (size_t index = 0; index < palette.size(); ++index) {
            wxColour colour;
            colour.SetRGB(palette[index]);
            settings_.fonts.customColours[index] = (colour.Red() << 16U) | (colour.Green() << 8U) | colour.Blue();
        }
#else
        wxColourDialog dialog(this, &data);
        const bool accepted = dialog.ShowModal() == wxID_OK;
        chosen = dialog.GetColourData().GetColour();
        for (size_t index = 0; index < settings_.fonts.customColours.size(); ++index) {
            const auto colour = dialog.GetColourData().GetCustomColour(static_cast<int>(index));
            if (colour.IsOk()) settings_.fonts.customColours[index] = (colour.Red() << 16U) | (colour.Green() << 8U) | colour.Blue();
        }
#endif
        if (accepted) {
            wxString result;
            wxStringTokenizer tokens(edit_->GetValue(), ";");
            while (tokens.HasMoreTokens()) {
                const auto token = tokens.GetNextToken().Trim().Trim(false);
                if (!token.empty() && token.BeforeFirst(':') != key) { if (!result.empty()) result += "; "; result += token; }
            }
            if (!result.empty()) result += "; ";
            edit_->SetValue(result + key + ":" + chosen.GetAsString(wxC2S_HTML_SYNTAX));
        }
        edit_->SetFocus();
    }
    void SchemeFile(bool import) {
        CommitEdit();
        wxFileDialog dialog(this, import ? _("Import Schemes") : _("Export Schemes"), {}, {},
                            _("INI Files (*.ini)|*.ini|All Files (*)|*"),
                            import ? wxFD_OPEN | wxFD_FILE_MUST_EXIST : wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dialog.ShowModal() == wxID_OK) {
            const bool success = import ? store_->ImportStyles(dialog.GetPath()) : store_->ExportStyles(dialog.GetPath());
            if (!success) wxMessageBox(wxString::Format(_("Could not access %s"), dialog.GetPath()),
                                       import ? _("Could not import schemes") : _("Could not export schemes"),
                                       wxOK | wxICON_ERROR, this);
            if (import && success) {
                ShowSelection();
                wxTreeItemIdValue cookie;
                tree_->SelectItem(tree_->GetFirstChild(tree_->GetRootItem(), cookie));
            }
        }
        edit_->SetFocus();
    }
    OriginalSettings& settings_;
    SettingsStore* store_;
    std::function<void()> preview_;
    SyntaxLanguage current_;
    wxTreeCtrl* tree_;
    wxStaticText* label_;
    wxTextCtrl *edit_, *defaults_;
    wxButton *font_, *fore_, *back_, *reset_;
};
} // namespace
void CustomizeSyntaxStyles(wxWindow* parent, SyntaxLanguage current, OriginalSettings& settings,
                           SettingsStore* store, const std::function<void()>& preview) {
    const auto backup = settings.fonts;
    const auto extensions = settings.schemeExtensions;
    StyleDialog dialog(parent, current, settings, store, preview);
    if (dialog.ShowModal() == wxID_CANCEL) {
        auto restored = backup;
        for (const auto& scheme : SyntaxCatalog()) restored.Scheme(scheme.language).useCode = settings.fonts.Scheme(scheme.language).useCode;
        settings.fonts = std::move(restored);
        settings.schemeExtensions = extensions;
    }
    preview();
}
