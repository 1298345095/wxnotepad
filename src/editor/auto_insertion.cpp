#include "editor/editor_control.h"
#include <SciLexer.h>
#include <optional>

namespace {
enum AutoInsertCharacter { Parenthesis, Brace, SquareBracket, AngleBracket, DoubleQuote, SingleQuote, Backtick, Comma };
// Original EditAutoC.cpp::DefaultWordCharSet; GetCharAt returns an unsigned byte.
bool DefaultWordCharacter(unsigned ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_' ||
           (ch >= 128 && ch <= 255);
}
bool CanCloseSingleQuote(EditorControl& editor, const CompletionStyles& styles, int previous, int style) {
    if ((style == 0 && styles.lexer == SCLEX_VISUALBASIC) ||
        (style != 0 &&
         (style == styles.noneSingleQuotedStyle || (styles.lexer == SCLEX_HTML && style == SCE_HB_DEFAULT))))
        return false;
    if (!DefaultWordCharacter(previous))
        return styles.lexer != SCLEX_RUST && styles.lexer != SCLEX_REBOL && styles.lexer != SCLEX_VERILOG;
    if (!styles.characterPrefix) return false;
    const int previous2 = static_cast<int>(
        editor.Send(Scintilla::Message::GetCharAt, editor.Send(Scintilla::Message::GetCurrentPos) - 3));
    if (!DefaultWordCharacter(previous2)) return CompletionStyleContains(styles.characterPrefixes, previous);
    if (styles.lexer == SCLEX_CPP) return previous2 == 'u' && previous == '8';
    return styles.lexer == SCLEX_PYTHON && (previous | 32) != (previous2 | 32) &&
           CompletionStyleContains(styles.characterPrefixes, previous) &&
           CompletionStyleContains(styles.characterPrefixes, previous2);
}
bool IsEscapedInsertion(EditorControl& editor, const CompletionStyles& styles, intptr_t position) {
    using Scintilla::Message;
    const int previous = static_cast<int>(editor.Send(Message::GetCharAt, position - 2));
    const int previousStyle = static_cast<int>(editor.Send(Message::GetStyleIndexAt, position - 2));
    if (!previousStyle || !previous || previous != styles.escapeCharacterStart) return false;
    const int style = static_cast<int>(editor.Send(Message::GetStyleIndexAt, position - 1));
    // Original IsEscapeCharOrFormatSpecifier(..., punctuation=true).
    if ((style != 0 && styles.escapeCharacterStyle && previousStyle != styles.escapeCharacterStyle) ||
        CompletionStyleContains(styles.rawStrings, previousStyle))
        return false;
    const auto before2 = editor.Send(Message::PositionBefore, position - 2);
    const int previous2 = before2 + 1 == position - 2 ? static_cast<int>(editor.Send(Message::GetCharAt, before2)) : 0;
    return previous != previous2;
}
bool InCharacterLiteral(const CompletionStyles& styles, int previousStyle, int nextStyle) {
    if (previousStyle == 0 || !styles.characterLiteralStyle) return false;
    if (previousStyle == styles.characterLiteralStyle && nextStyle == styles.characterLiteralStyle) return true;
    return styles.lexer == SCLEX_RUST && previousStyle == SCE_RUST_BYTE_CHARACTER &&
           nextStyle == SCE_RUST_BYTE_CHARACTER;
}
std::optional<AutoInsertCharacter> AutoInsertIndex(int ch) {
    switch (ch) {
    case '(':
        return Parenthesis;
    case '{':
        return Brace;
    case '[':
        return SquareBracket;
    case '<':
        return AngleBracket;
    case '"':
        return DoubleQuote;
    case '\'':
        return SingleQuote;
    case '`':
        return Backtick;
    case ',':
        return Comma;
    default:
        return std::nullopt;
    }
}
} // namespace

void EditorControl::SetAutoInsertMask(int mask) {
    autoInsertMask_ = mask;
    Send(Scintilla::Message::SetAutoInsertMask, mask);
}

bool EditorControl::IsOpenBraceMatched(intptr_t position, intptr_t startPosition) {
    using Scintilla::Message;
    constexpr intptr_t BraceLookAheadBytes = 4 * 1024;
    Send(Message::Colourise, 0, position + BraceLookAheadBytes);
    const auto match = Send(Message::BraceMatch, position, startPosition);
    return match >= 0 && Send(Message::GetStyleIndexAt, position) == Send(Message::GetStyleIndexAt, match) &&
           (position == 0 || Send(Message::BraceMatch, match, Send(Message::PositionBefore, position)) < 0);
}

// Port of Notepad4 89b8b36 EditAutoCloseBraceQuote and EditIsOpenBraceMatched.
// Called from the original core's character notification, after its insertion.
void EditorControl::AutoInsertCharacter(int ch) {
    using Scintilla::Message;
    const auto character = AutoInsertIndex(ch);
    if (!character) return;
    const int index = *character;
    if (!(autoInsertMask_ & (1U << index))) return;
    const auto position = Send(Message::GetCurrentPos);
    const int previous = static_cast<int>(Send(Message::GetCharAt, position - 2));
    const int next = static_cast<int>(Send(Message::GetCharAt, position));
    const int previousStyle = static_cast<int>(Send(Message::GetStyleIndexAt, position - 2));
    const int nextStyle = static_cast<int>(Send(Message::GetStyleIndexAt, position));
    const auto& styles = completionStyles_;
    if (InCharacterLiteral(styles, previousStyle, nextStyle)) return;
    if (ch != ',' && IsEscapedInsertion(*this, styles, position)) return;
    // Original GenerateAutoInsertMask(): matching ASCII closing character offsets.
    ch += (169U >> (2 * index)) & 3;
    if (index == SquareBracket && styles.lexer == SCLEX_SMALI) return;
    if (index == AngleBracket && !CompletionStyleContains(styles.genericTypes, previousStyle)) return;
    if (index == SingleQuote && !CanCloseSingleQuote(*this, styles, previous, previousStyle)) return;
    if (index == Backtick && (styles.lexer == SCLEX_VERILOG || styles.lexer == SCLEX_VHDL)) return;
    if (index == Comma) {
        ch = ' ';
        if (next == ' ' || next == '\t' || (previous == '\'' && next == '\'') || (previous == '"' && next == '"'))
            return;
    }
    if (index < AngleBracket && IsOpenBraceMatched(position - 1, position)) return;
    const char insertion[] = {static_cast<char>(ch), '\0'};
    Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(insertion));
    const auto caret = index == Comma ? position + 1 : position;
    Send(Message::SetSel, caret, caret);
    if (index < AngleBracket) Send(Message::Colourise, 0, position + 1);
}
