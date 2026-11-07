#include "editor/platform/escape_text.h"
#include "editor/platform/custom_case.h"

// Adapted from Notepad4 89b8b36 Edit.cpp::EditEscapeChars.
// Copyright (c) Notepad4 contributors; see licenses/Notepad4.txt.
// The editor buffer is UTF-8 and all currently supported languages use the
// original non-resource-script quote rules. UTF-8 needs no DBCS byte pairing.
std::string MapEscapeText(std::string_view text, int command) {
    std::string output;
    for (size_t index = 0; index < text.size(); ++index) {
        char ch = text[index];
        const char next = index + 1 < text.size() ? text[index + 1] : '\0';
        switch (ch) {
        case '\\':
            if (command == CustomCaseCommand::EscapeC) {
                output += '\\';
            } else if (command == CustomCaseCommand::UnescapeC) {
                if (next == '\\' || next == '"' || next == '\'') {
                    ch = next;
                    ++index;
                } else {
                    // The original deliberately leaves e, x, u, U and octal
                    // forms literal, alongside unknown/trailing backslashes.
                    constexpr std::string_view names = "abtnvfr";
                    const auto offset = names.find(next);
                    if (offset != std::string_view::npos) {
                        ch = static_cast<char>('\a' + offset);
                        ++index;
                    }
                }
            }
            break;
        case '"':
            if (command == CustomCaseCommand::EscapeC)
                output += '\\';
            else if (command == CustomCaseCommand::EscapeHtml) {
                output += "&quot";
                ch = ';';
            }
            break;
        case '\'':
            if (command == CustomCaseCommand::EscapeC)
                output += '\\';
            else if (command == CustomCaseCommand::EscapeHtml) {
                output += "&apos";
                ch = ';';
            }
            break;
        case '&':
            if (command == CustomCaseCommand::EscapeHtml) {
                output += "&amp";
                ch = ';';
            } else if (command == CustomCaseCommand::UnescapeHtml && next >= 'a' && next <= 'z') {
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
                if (following == ';') {
                    const std::string_view name(entity);
                    if (name == "amp")
                        following = '&';
                    else if (name == "quot")
                        following = '"';
                    else if (name == "apos")
                        following = '\'';
                    else if (name == "lt")
                        following = '<';
                    else if (name == "gt")
                        following = '>';
                    if (following != ';') {
                        ch = following;
                        index += length + 1;
                    }
                }
            }
            break;
        default:
            if (command == CustomCaseCommand::EscapeC && ch >= '\a' && ch <= '\r') {
                ch = "abtnvfr"[ch - '\a'];
                output += '\\';
            } else if (command == CustomCaseCommand::EscapeHtml && (ch == '<' || ch == '>')) {
                output += ch == '<' ? "&lt" : "&gt";
                ch = ';';
            }
            break;
        }
        output += ch;
    }
    return output;
}
