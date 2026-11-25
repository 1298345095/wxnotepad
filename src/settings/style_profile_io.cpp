#include "settings/style_profile_io.h"
#include <wx/filefn.h>
#include <wx/log.h>

namespace {
wxString ReadStyle(const IniDocument& document, const char* key, const char* fallback, const char* section) {
    // An explicitly empty original style resets every optional attribute.
    const auto value = document.FindValue(wxString::FromUTF8(key), wxString::FromUTF8(section));
    return value ? value->Strip(wxString::both) : wxString::FromUTF8(fallback);
}

void ReadCustomColours(const IniDocument& document, FontSettings& profile) {
    for (size_t index = 0; index < profile.customColours.size(); ++index) {
        const auto value = document.FindValue(wxString::Format("%02zu", index + 1), "Custom Colors");
        unsigned long rgb;
        if (value && value->StartsWith('#') && value->Mid(1).ToULong(&rgb, 16))
            profile.customColours[index] = static_cast<unsigned>(rgb) & 0xffffffU;
    }
}

} // namespace

FontSettings StyleProfileIO::Load(const wxString& path, StyleTheme theme, const FontSettings& current) {
    IniDocument profile(path);
    profile.Load(theme == StyleTheme::Dark ? wxString::FromUTF8(OriginalDarkProfile()) : wxString{});
    FontSettings result;
    result.theme = theme;
    ReadCustomColours(profile, result);
    for (const auto& definition : GlobalStyleDefinitions()) {
        const auto key = wxString::FromUTF8(definition.key);
        // Preserve the established global font string contract: remove outer
        // double quotes, retain enclosed spaces and use defaults for empty.
        if (key == "Default Code Style")
            result.codeStyle = profile.ReadString(key, wxString::FromUTF8(definition.defaultValue), "Global Styles");
        else if (key == "Default Text Style")
            result.textStyle = profile.ReadString(key, wxString::FromUTF8(definition.defaultValue), "Global Styles");
        else
            result.globalStyles[key] = ReadStyle(profile, definition.key, definition.defaultValue, "Global Styles");
    }
    for (const auto& scheme : SyntaxCatalog()) {
        if (SyntaxSettingsLanguage(scheme.language) != scheme.language) continue;
        const auto* section = SyntaxSettingsSection(scheme.language);
        auto& style = result.Scheme(scheme.language);
        style.useCode = profile.ReadBool("UseDefaultCodeStyle", current.Scheme(scheme.language).useCode, section);
        for (const auto& definition : SyntaxStyleDefinitions(scheme.language)) {
            const auto value = ReadStyle(profile, definition.key, definition.defaultValue, section);
            if (wxString::FromUTF8(definition.key) == "Default")
                style.defaultStyle = value;
            else
                style.tokenStyles[wxString::FromUTF8(definition.key)] = value;
        }
    }
    return result;
}

void StyleProfileIO::WriteChanges(IniDocument& document, const FontSettings& current, const FontSettings& saved) {
    for (size_t index = 0; index < current.customColours.size(); ++index) {
        if (current.customColours[index] != saved.customColours[index])
            document.WriteString(wxString::Format("%02zu", index + 1),
                                 wxString::Format("#%06X", current.customColours[index]),
                                 wxString::Format("#%06X", DefaultCustomColours[index]), "Custom Colors");
    }
    for (const auto& definition : GlobalStyleDefinitions()) {
        const auto key = wxString::FromUTF8(definition.key);
        const auto value = current.GlobalStyle(key);
        if (value != saved.GlobalStyle(key))
            document.WriteString(key, value, wxString::FromUTF8(definition.defaultValue), "Global Styles");
    }
    for (const auto& scheme : SyntaxCatalog()) {
        if (SyntaxSettingsLanguage(scheme.language) != scheme.language) continue;
        const auto* section = SyntaxSettingsSection(scheme.language);
        if (current.Scheme(scheme.language).useCode != saved.Scheme(scheme.language).useCode)
            document.WriteBool("UseDefaultCodeStyle", current.Scheme(scheme.language).useCode,
                               scheme.language != SyntaxLanguage::PlainText, section);
        for (const auto& definition : SyntaxStyleDefinitions(scheme.language)) {
            const auto value = current.TokenStyle(scheme.language, definition);
            if (value != saved.TokenStyle(scheme.language, definition))
                document.WriteString(wxString::FromUTF8(definition.key), value,
                                     wxString::FromUTF8(definition.defaultValue), section);
        }
    }
}

bool StyleProfileIO::Import(const wxString& path, FontSettings& fonts, SyntaxExtensions& extensions) {
    const wxLogNull noGuiLog;
    IniDocument profile(path);
    const bool success = wxFileExists(path) && profile.Load();
    if (success) {
        ReadCustomColours(profile, fonts);
        for (const auto& definition : GlobalStyleDefinitions()) {
            const auto key = wxString::FromUTF8(definition.key);
            const auto value = profile.FindValue(key, "Global Styles");
            if (!value) continue;
            if (key == "Default Code Style")
                fonts.codeStyle = value->Left(StyleProfileLimits::StyleCharacters);
            else if (key == "Default Text Style")
                fonts.textStyle = value->Left(StyleProfileLimits::StyleCharacters);
            else
                fonts.globalStyles[key] = value->Left(StyleProfileLimits::StyleCharacters);
        }
        for (const auto& scheme : SyntaxCatalog()) {
            if (SyntaxSettingsLanguage(scheme.language) != scheme.language) continue;
            const auto* section = SyntaxSettingsSection(scheme.language);
            if (const auto extension = profile.FindValue(section, "File Extensions"); extension && !extension->empty())
                extensions[scheme.language] = extension->Left(StyleProfileLimits::ExtensionCharacters);
            auto& styles = fonts.Scheme(scheme.language);
            styles.useCode = profile.ReadBool("UseDefaultCodeStyle", styles.useCode, section);
            for (const auto& definition : SyntaxStyleDefinitions(scheme.language)) {
                const auto key = wxString::FromUTF8(definition.key);
                const auto value = profile.FindValue(key, section);
                if (!value) continue;
                if (key == "Default")
                    styles.defaultStyle = value->Left(StyleProfileLimits::StyleCharacters);
                else
                    styles.tokenStyles[key] = value->Left(StyleProfileLimits::StyleCharacters);
            }
        }
    }
    return success;
}

bool StyleProfileIO::Export(const wxString& path, const FontSettings& fonts, const SyntaxExtensions& extensions) {
    const wxLogNull noGuiLog;
    IniDocument profile(path);
    bool success = (!wxFileExists(path) || wxIsWritable(path)) && profile.Load();
    if (success) {
        for (const auto& definition : GlobalStyleDefinitions()) {
            const auto key = wxString::FromUTF8(definition.key);
            profile.WriteString(key, fonts.GlobalStyle(key), {}, "Global Styles", true);
        }
        for (const auto& scheme : SyntaxCatalog()) {
            if (SyntaxSettingsLanguage(scheme.language) != scheme.language) continue;
            const auto* section = SyntaxSettingsSection(scheme.language);
            profile.WriteString(section, SyntaxExtensionsFor(scheme.language, extensions), {}, "File Extensions", true);
            profile.WriteString("UseDefaultCodeStyle", fonts.Scheme(scheme.language).useCode ? "1" : "0", {}, section,
                                true);
            for (const auto& definition : SyntaxStyleDefinitions(scheme.language))
                profile.WriteString(wxString::FromUTF8(definition.key), fonts.TokenStyle(scheme.language, definition),
                                    {}, section, true);
        }
        for (size_t index = 0; index < fonts.customColours.size(); ++index)
            profile.WriteString(wxString::Format("%02zu", index + 1),
                                wxString::Format("#%06X", fonts.customColours[index]), {}, "Custom Colors", true);
        success = profile.Save();
    }
    return success;
}
