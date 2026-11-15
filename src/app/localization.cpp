#include "app/localization.h"
#include "settings/ui_language.h"
#include "ui_language_data.h"
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/translation.h>
#include <wx/window.h>
#include <utility>
#ifdef __WXMSW__
#include <wx/access.h>
#elif defined(__WXGTK__)
#include <gtk/gtk.h>
#endif

namespace {
wxString Text(std::string_view text) {
    return wxString::FromUTF8(text.data(), text.size());
}

#ifdef __WXMSW__
class NamedWindowAccessible final : public wxWindowAccessible {
public:
    explicit NamedWindowAccessible(wxString name) : wxWindowAccessible(nullptr), name_(std::move(name)) {}
    wxAccStatus GetName(int childId, wxString* name) override {
        if (childId != wxACC_SELF) return wxWindowAccessible::GetName(childId, name);
        *name = name_;
        return wxACC_OK;
    }

private:
    wxString name_;
};
#endif
} // namespace

namespace AppLocalization {
uint16_t MenuPreference(uint16_t preference) {
    if (preference == UILanguage::System) return preference;
    const auto primary = preference & UILanguage::PrimaryLanguageMask;
    const auto sublanguage = preference >> UILanguage::SublanguageShift;
    // Original IsChineseTraditionalSubLang: Taiwan, Hong Kong and Macau.
    const bool traditional = sublanguage == UILanguage::ChineseTaiwanSublanguage ||
                             sublanguage == UILanguage::ChineseHongKongSublanguage ||
                             sublanguage == UILanguage::ChineseMacauSublanguage;
    for (const auto& language : LanguageDefinitions) {
        if ((language.langId & UILanguage::PrimaryLanguageMask) != primary) continue;
        if (primary != UILanguage::ChinesePrimaryLanguage ||
            ((language.langId >> UILanguage::SublanguageShift) == UILanguage::ChineseTaiwanSublanguage) == traditional)
            return language.langId;
    }
    return preference;
}

void Initialize(uint16_t preference) {
    const wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
    wxFileTranslationsLoader::AddCatalogLookupPathPrefix(wxFileName(executable.GetPath(), "locale").GetFullPath());
#ifdef WXNOTEPAD_LOCALE_DIR
    wxFileTranslationsLoader::AddCatalogLookupPathPrefix(wxString::FromUTF8(WXNOTEPAD_LOCALE_DIR));
#endif
    const wxString domain = Text(CatalogDomain);
    auto* translations = new wxTranslations;
    wxTranslations::Set(translations);
    wxString sourceLanguage;
    for (const auto& language : LanguageDefinitions)
        if (!language.hasCatalog) sourceLanguage = Text(language.code);
    wxString effective = sourceLanguage;
    if (preference == UILanguage::System) {
        const auto best = translations->GetBestTranslation(domain, wxLANGUAGE_ENGLISH);
        for (const auto& language : LanguageDefinitions)
            if (best == Text(language.code)) effective = best;
    } else {
        for (const auto& language : LanguageDefinitions)
            if (language.langId == MenuPreference(preference)) effective = Text(language.code);
    }
    translations->SetLanguage(effective);
    // AddCatalog() may succeed without loading an MO. Both domains must actually
    // load in the same language before exposing a translated interface.
    // wxWidgets searches the most recently loaded catalog first. Load stock first
    // so application wording takes precedence while other toolkit keys fall back.
    const wxLogNull noGuiLog;
    if (effective != sourceLanguage &&
        (!translations->AddAvailableCatalog("wxstd", wxLANGUAGE_ENGLISH) || !translations->IsLoaded("wxstd") ||
         !translations->AddAvailableCatalog(domain, wxLANGUAGE_ENGLISH) || !translations->IsLoaded(domain))) {
        translations = new wxTranslations;
        translations->SetLanguage(sourceLanguage);
        wxTranslations::Set(translations);
    }
}

void SetAccessibleName(wxWindow& window, const wxString& name) {
#ifdef __WXMSW__
    window.SetAccessible(new NamedWindowAccessible(name));
#elif defined(__WXGTK__)
    if (auto* accessible = gtk_widget_get_accessible(GTK_WIDGET(window.GetHandle())))
        atk_object_set_name(accessible, name.utf8_str());
#endif
}
} // namespace AppLocalization
