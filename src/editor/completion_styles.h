#pragma once
#include "syntax/syntax_catalog.h"
#include <array>
#include <cstdint>

// Original EditAutoC.cpp::InitAutoCompletionCache, shared by insertion and completion.
struct CompletionStyles {
    std::array<uint32_t, 8> wordCharacters{}, characterPrefixes{}, rawStrings{}, genericTypes{}, ignoredWords{},
        comments{}, strings{}, plainText{};
    int lexer = 0;
    int escapeCharacterStart = 0;
    int escapeCharacterStyle = 0;
    int characterLiteralStyle = 0;
    int noneSingleQuotedStyle = 0;
    bool characterPrefix = false;
};
CompletionStyles CompletionStylesFor(const SyntaxDefinition& definition);
inline bool CompletionStyleContains(const std::array<uint32_t, 8>& mask, unsigned value) {
    return value < 256 && (mask[value >> 5] & (1U << (value & 31)));
}
