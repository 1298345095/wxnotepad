#pragma once
#include "syntax/syntax_catalog.h"
#include <array>
#include <span>

// Stable original INI keys and unchanged source defaults, including grouped tokens.
struct StyleDefinition {
    const char* key;
    const char* defaultValue;
    std::array<int, 4> ids;
    size_t count;
};
enum class StyleTheme { Default = 0, Dark = 1 };
std::span<const StyleDefinition> GlobalStyleDefinitions();
std::span<const StyleDefinition> SyntaxStyleDefinitions(SyntaxLanguage language);
const char* OriginalDarkProfile();
