#include "syntax/style_profile.h"
#include <Scintilla.h>
#include <SciLexer.h>

namespace {
struct StyleSchemeDefinition {
    const char* section;
    std::span<const StyleDefinition> styles;
};
#include "syntax/generated/style_profiles.h"
constexpr StyleDefinition textProfileStyles[] = {{"Default", "", {STYLE_DEFAULT}, 1}};
} // namespace
std::span<const StyleDefinition> GlobalStyleDefinitions() {
    return defaultProfileStyles;
}
const char* OriginalDarkProfile() {
    return DarkProfile;
}
std::span<const StyleDefinition> SyntaxStyleDefinitions(SyntaxLanguage language) {
    const std::string_view section = SyntaxSettingsSection(language);
    for (const auto& scheme : profileSchemes)
        if (section == scheme.section) return scheme.styles;
    return textProfileStyles;
}
