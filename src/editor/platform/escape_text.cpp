#include "editor/platform/escape_text.h"
#include "editor/platform/custom_case.h"

// Adapted from Notepad4 89b8b36 Edit.cpp::EditEscapeChars.
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
// The editor buffer is UTF-8 and all currently supported languages use the
// original non-resource-script quote rules. UTF-8 needs no DBCS byte pairing.
namespace {
char UnescapeBackslash(char next, size_t& index) {
    if (next == '\\' || next == '"' || next == '\'') {
        ++index;
        return next;
    }
    // The original deliberately leaves e, x, u, U and octal
    // forms literal, alongside unknown/trailing backslashes.
    constexpr std::string_view names = "abtnvfr";
    const auto offset = names.find(next);
    if (offset == std::string_view::npos) return '\\';
    ++index;
    return static_cast<char>('\a' + offset);
}
char EntityCharacter(std::string_view name) {
    if (name == "amp") return '&';
    if (name == "quot") return '"';
    if (name == "apos") return '\'';
    if (name == "lt") return '<';
    if (name == "gt") return '>';
    return ';';
}
char UnescapeAmpersand(std::string_view text, size_t& index) {
    const char next = index + 1 < text.size() ? text[index + 1] : '\0';
    if (next < 'a' || next > 'z') return '&';
    char entity[6]{};
    entity[0] = next;
    size_t length = 1;
    size_t offset = index + 2;
    char following = '\0';
    do {
        following = offset < text.size() ? text[offset] : '\0';
        ++offset;
        if (following >= 'a' && following <= 'z')
            entity[length++] = following;
        else
            break;
    } while (length < sizeof(entity) - 1 && offset < text.size());
    if (following != ';') return '&';
    following = EntityCharacter(entity);
    if (following == ';') return '&';
    index += length + 1;
    return following;
}
char EscapeHtmlCharacter(std::string& output, char ch) {
    switch (ch) {
    case '"':
        output += "&quot";
        break;
    case '\'':
        output += "&apos";
        break;
    case '&':
        output += "&amp";
        break;
    case '<':
        output += "&lt";
        break;
    case '>':
        output += "&gt";
        break;
    default:
        return ch;
    }
    return ';';
}
char EscapeCCharacter(std::string& output, char ch) {
    if (ch == '\\' || ch == '"' || ch == '\'') {
        output += '\\';
    } else if (ch >= '\a' && ch <= '\r') {
        ch = "abtnvfr"[ch - '\a'];
        output += '\\';
    }
    return ch;
}
} // namespace

std::string MapEscapeText(std::string_view text, int command) {
    std::string output;
    for (size_t index = 0; index < text.size(); ++index) {
        char ch = text[index];
        const char next = index + 1 < text.size() ? text[index + 1] : '\0';
        if (command == CustomCaseCommand::EscapeC)
            ch = EscapeCCharacter(output, ch);
        else if (command == CustomCaseCommand::EscapeHtml)
            ch = EscapeHtmlCharacter(output, ch);
        else if (command == CustomCaseCommand::UnescapeC && ch == '\\')
            ch = UnescapeBackslash(next, index);
        else if (command == CustomCaseCommand::UnescapeHtml && ch == '&')
            ch = UnescapeAmpersand(text, index);
        output += ch;
    }
    return output;
}
