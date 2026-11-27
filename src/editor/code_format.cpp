// Adapted from Notepad4 89b8b36 Bridge.cpp: AddStyleSeparator, CodePretty and
// EditFormatCode. Original BSD 3-Clause notice: licenses/Notepad4.txt.
#include "editor/editor_control.h"
#include "syntax/syntax_catalog.h"
#include <Scintilla.h>
#include <SciLexer.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace {
// Original EditAutoC.cpp::DefaultWordCharSet (all bytes >= 128 are word bytes).
constexpr bool WordCharacter(unsigned ch) noexcept {
    return ch >= 128 || (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}
enum {
    SpaceOption_None = 0,
    SpaceOption_IndentAfter = 1,
    SpaceOption_SpaceBefore = 2,
    SpaceOption_SpaceAfter = 4,
    SpaceOption_NewLineBefore = 8,
    SpaceOption_NewLineAfter = 16,
    SpaceOption_DanglingStmt = 32,
    SpaceOption_PushBrace = 64,
    SpaceOption_PopBrace = 128,
};

int AddStyleSeparator(const SyntaxDefinition& syntax, int ch, int chPrev, int style) noexcept {
    if ((style >= syntax.original.stringStyleFirst && style <= syntax.original.stringStyleLast) ||
        (syntax.lexer == SCLEX_JSON && (style == SCE_JSON_PROPERTYNAME)) ||
        (syntax.lexer == SCLEX_JAVASCRIPT && (style == SCE_JS_KEY || style == SCE_JS_OPERATOR_PF))) {
        return SpaceOption_None;
    }
    // a++ + ++b, a + +1, a-- - --b, a - -1
    if (ch == '+' || ch == '-') {
        if (style == SCE_CSS_OPERATOR2 && syntax.lexer == SCLEX_CSS) {
            // '+' and '-' inside math function requires space on both side
            return SpaceOption_SpaceBefore | SpaceOption_SpaceAfter;
        }
        if (ch == chPrev) {
            return SpaceOption_SpaceBefore;
        }
    }
    // var name; return .5; CSS property: 1 #1 .5 --name;
    if (WordCharacter(chPrev)) {
        if (WordCharacter(ch) || (ch == '-' && syntax.lexer == SCLEX_CSS)) {
            // TODO: improve CSS An+B when B is negative, https://www.w3.org/TR/css-syntax-3/#anb-microsyntax
            return SpaceOption_SpaceBefore;
        }
        if ((ch == '.' || ch == '$' || ch == '#') && style != syntax.original.operatorStyle &&
            style != syntax.original.operatorStyle2) {
            return SpaceOption_SpaceBefore;
        }
    }
    return SpaceOption_None;
}

void CodePretty(std::string& output, const SyntaxDefinition& syntax, const char* styledText, size_t textLength,
                EditorControl& editor) {
    char fmtbuf[128];
    std::string braceStack(1, '\0'); // sentinel
    memset(fmtbuf, 0, 4);

    unsigned fmtlen = 0;
    uint32_t blockLevel = 0;
    uint32_t indentPrev = 0;
    uint8_t chPrev = 0;
    uint8_t chPrevNonWhite = 0;
    bool commentEndEOL = false;
    bool defaultCase = false;

    unsigned eol = '\r' | ('\n' << 8);
    unsigned eolWidth = static_cast<unsigned>(editor.Send(Scintilla::Message::GetEOLMode));
    eol >>= 8 * (eolWidth >> 1);
    eolWidth = (eolWidth == SC_EOL_CRLF) ? 2 : 1;

    constexpr unsigned maxFmtLen = sizeof(fmtbuf) - 4 - 4; // \r\n + 4 + \r\n
    constexpr uint8_t braceObject = '{' + 1;
    constexpr uint8_t braceTemplate = '{' + 2;
    constexpr uint8_t bracketArray = '[' + 1;

    uint8_t braceTop = '\0';
    uint8_t stylePrev = styledText[0];
    uint8_t styleBefore = stylePrev;
    const char* const textBuffer = styledText + textLength + 1;
    for (size_t offset = 0; offset < textLength; offset++) {
        if (fmtlen >= maxFmtLen) {
            // keep last character and eol in the buffer
            output += std::string_view{fmtbuf, fmtlen - 4};
            memcpy(fmtbuf, &fmtbuf[fmtlen - 4], 4);
            fmtlen = 4;
        }

        const uint8_t style = styledText[offset];
        if (style == 0) {
            styleBefore = 0;
            continue;
        }

        int spaceOption = SpaceOption_None;
        unsigned operatorLen = 0;
        const uint8_t ch = textBuffer[offset];
        if (style <= syntax.original.commentStyleMarker) {
            if (ch == '/' && (chPrev == '\n' || styleBefore == 0 || styleBefore > syntax.original.commentStyleMarker) &&
                textBuffer[offset + 1] == '/') {
                commentEndEOL = true; // fix indentation after comment line
            } else if (styledText[offset + 1] == 0 &&
                       (textBuffer[offset + 1] == '\n' || textBuffer[offset + 1] == '\r')) {
                commentEndEOL = true; // keep new line after block comment
                spaceOption = SpaceOption_NewLineAfter;
            }
        } else if (style != styleBefore) {
            spaceOption = AddStyleSeparator(syntax, ch, chPrev, style);
            if (style == SCE_JS_WORD && syntax.lexer == SCLEX_JAVASCRIPT && (ch == 'c' || ch == 'd')) {
                if ((textBuffer[offset + 1] == 'a' || textBuffer[offset + 1] == 'e') &&
                    (textBuffer[offset + 2] == 's' || textBuffer[offset + 2] == 'f')) {
                    defaultCase = true;
                    spaceOption |= SpaceOption_NewLineBefore;
                }
            }
            if (chPrev == ')') {
                if (syntax.lexer != SCLEX_JSON &&
                    (stylePrev == syntax.original.operatorStyle || stylePrev == syntax.original.operatorStyle2)) {
                    if (syntax.lexer == SCLEX_CSS) {
                        if ((ch == '+' || ch == '-') ||
                            (ch != ':' && style != SCE_CSS_OPERATOR)) { // :not([class]):hover
                            spaceOption |= SpaceOption_SpaceBefore;     // CSS property: function() value
                        }
                    } else if (style != SCE_JS_OPERATOR && style != SCE_JS_OPERATOR2) {
                        spaceOption |= SpaceOption_NewLineBefore | SpaceOption_DanglingStmt;
                        indentPrev++; // if (), for (), while () statement
                    }
                }
            } else if ((stylePrev == SCE_CSS_AT_RULE && syntax.lexer == SCLEX_CSS) ||
                       (stylePrev == SCE_JS_WORD && syntax.lexer == SCLEX_JAVASCRIPT &&
                        (style <= SCE_JS_OPERATOR_PF || ch == '{' || ch == '[' || ch == '!' || ch == '~' ||
                         (ch == '(' && chPrev != 't')))) {
                // not set(), get();
                spaceOption |= SpaceOption_SpaceBefore;
            }
        }
        if (style == syntax.original.operatorStyle || style == syntax.original.operatorStyle2) {
            if (ch == ':') {
                spaceOption |= SpaceOption_SpaceAfter; // property: value
                if (syntax.lexer == SCLEX_JAVASCRIPT) {
                    if (defaultCase && braceTop == '{') {
                        defaultCase = false;
                        spaceOption |= SpaceOption_NewLineAfter;
                    } else if (stylePrev != SCE_JS_KEY && stylePrev != SCE_JS_LABEL &&
                               (stylePrev != SCE_JS_WORD || chPrev != 't')) {
                        spaceOption |= SpaceOption_SpaceBefore; // ternary operator, not set: / get:
                    }
                }
            } else if (ch == ',') {
                if (syntax.lexer == SCLEX_JSON || braceTop == braceObject || braceTop == bracketArray) {
                    spaceOption |= SpaceOption_NewLineAfter;
                } else {
                    spaceOption |= SpaceOption_SpaceAfter;
                }
            } else if (ch == ';') {
                if (braceTop == '(') {
                    spaceOption |= SpaceOption_SpaceAfter; // for (;;)
                } else {
                    spaceOption |= SpaceOption_NewLineAfter;
                }
            } else if (ch == '{' || ch == '[') {
                if (ch == '{' && (chPrev == ')' || syntax.lexer == SCLEX_CSS)) {
                    spaceOption |= SpaceOption_SpaceBefore; // if (...){}, CSS: selector {rule}
                }
                if (syntax.lexer == SCLEX_JAVASCRIPT) {
                    spaceOption |= SpaceOption_PushBrace;
                    if (chPrev == '$' && style == styleBefore) {
                        braceTop = braceTemplate; // ${}
                    } else if (chPrevNonWhite == '=' || chPrevNonWhite == '(' || chPrevNonWhite == ',' ||
                               chPrevNonWhite == '[' ||
                               (chPrevNonWhite == ':' && (braceTop == braceObject || braceTop == bracketArray))) {
                        braceTop = ch + 1;
                        spaceOption |= SpaceOption_NewLineAfter | SpaceOption_IndentAfter;
                    } else {
                        braceTop = ch;
                        if (ch == '{') {
                            spaceOption |= SpaceOption_NewLineAfter | SpaceOption_IndentAfter;
                            if (stylePrev >= SCE_JS_IDENTIFIER) {
                                spaceOption |= SpaceOption_SpaceBefore;
                            }
                        }
                    }
                } else if (ch == '{' || syntax.lexer == SCLEX_JSON) {
                    spaceOption |= SpaceOption_NewLineAfter | SpaceOption_IndentAfter;
                }
            } else if (ch == '}' || ch == ']') {
                if ((ch == '}' && braceTop != braceTemplate) ||
                    (ch == ']' && (braceTop == bracketArray || syntax.lexer == SCLEX_JSON))) {
                    spaceOption |= SpaceOption_NewLineBefore | SpaceOption_NewLineAfter;
                    if (blockLevel > 0) {
                        --blockLevel;
                    }
                }
                if (syntax.lexer == SCLEX_JAVASCRIPT && static_cast<uint8_t>(ch - braceTop) < 3) {
                    spaceOption |= SpaceOption_PopBrace;
                }
            } else if (syntax.lexer != SCLEX_JSON) {
                if (syntax.lexer == SCLEX_CSS) {
                    const uint8_t chNext = textBuffer[offset + 1];
                    if (style == SCE_CSS_OPERATOR2 || ch == '>' // child combinator
                        || (ch == '&' && chNext != ':' && chNext != '.' &&
                            chNext != ')')              // nesting selector, &:hover, &.class, :not(&)
                        || (ch == '~' && chNext != '=') // subsequent-sibling combinator
                        || (ch == '|' && chNext == '|') // column combinator
                        // next-sibling combinator
                        || (ch == '+' && styledText[offset + 1] != SCE_CSS_NUMBER &&
                            styledText[offset + 1] != SCE_CSS_DIMENSION)) {
                        spaceOption |= SpaceOption_SpaceAfter;
                        if (ch == '|') {
                            operatorLen = 2;
                        }
                        if (chPrev != '(') { // :has(> img)
                            spaceOption |= SpaceOption_SpaceBefore;
                        }
                    } else if (ch == '!') {
                        spaceOption |= SpaceOption_SpaceBefore; // !important
                    }
                } else {
                    if (ch == '(') {
                        braceTop = '(';
                        spaceOption |= SpaceOption_PushBrace;
                    } else if (ch == ')') {
                        if (braceTop == '(') {
                            spaceOption |= SpaceOption_PopBrace;
                        }
                    } else if (ch != '.' && ch != '~' && ch != '$') {
                        // https://tc39.es/ecma262/#sec-punctuators
                        // https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Operators
                        // !		!=		!==
                        // %		%=
                        // &	&&	&=		&&=
                        // *	**	*=		**=
                        // +	++	+=
                        // -	--	-=
                        // /		/=
                        // <	<<	<=		<<=
                        // =	=>	==		===
                        // >	>>	>=		>>=	>>>		>>>=
                        // ?	?.	??		??=
                        // ^		^=
                        // |	||	|=		||=
                        uint8_t chNext = textBuffer[offset + 1];
                        if (chNext == '=' || (ch != '!' && ch == chNext)) {
                            operatorLen = 2;
                            spaceOption |= SpaceOption_SpaceBefore | SpaceOption_SpaceAfter;
                            chNext = textBuffer[offset + 2];
                            if (chNext == '=' || chNext == '>') {
                                operatorLen = 3;
                                if (chNext == '>' && textBuffer[offset + 3] == '=') {
                                    operatorLen = 4;
                                }
                            }
                        } else if (ch == '+' || ch == '-') {
                            // detect unary / binary operator, similar to FollowExpression()
                            if (stylePrev == SCE_JS_WORD) {
                                spaceOption |= SpaceOption_SpaceBefore;
                            } else if (chPrevNonWhite == ')' || chPrevNonWhite == ']' ||
                                       (stylePrev >= SCE_JS_NUMBER && stylePrev <= SCE_JS_OPERATOR_PF) ||
                                       (stylePrev >= SCE_JS_IDENTIFIER && stylePrev <= SCE_JS_CONSTANT)) {
                                spaceOption |= SpaceOption_SpaceBefore | SpaceOption_SpaceAfter;
                            }
                        } else if (ch == '*' && stylePrev == SCE_JS_WORD) {
                            // yield*, function*
                            spaceOption |= SpaceOption_SpaceAfter;
                        } else if (ch != '!' && (ch != '?' || chNext != '.')) {
                            spaceOption |= SpaceOption_SpaceBefore | SpaceOption_SpaceAfter;
                            if (ch == '=' && chNext == '>') {
                                operatorLen = 2;
                            }
                        }
                    }
                }
            }
            if (chPrev == '\n' &&
                (ch == ',' || ch == ')'                      // JavaScript: function({})
                 || ch == ';'                                // JavaScript: {};
                 || ch == '='                                // JavaScript: let {} = value, const {} = value
                 || ch == ':'                                // JavaScript: ternary operator
                 || (ch == '.' && syntax.lexer != SCLEX_CSS) // JavaScript: {}.property, [].property; CSS: .class {rule}
                 || ((ch == ']' || ch == '}') && chPrevNonWhite == ch - 2))) { // empty [], {}
                chPrev = '\0';
                fmtlen -= eolWidth;
                if ((spaceOption & SpaceOption_NewLineBefore) == 0) {
                    chPrev = fmtbuf[fmtlen - 1];
                }
            }
            if (spaceOption & SpaceOption_PushBrace) {
                braceStack.push_back(static_cast<char>(braceTop));
            } else if (spaceOption & SpaceOption_PopBrace) {
                braceStack.pop_back();
                braceTop = braceStack.back();
            }
        }
        if (chPrev > ' ') {
            if (spaceOption & SpaceOption_NewLineBefore) {
                chPrev = '\n';
                memcpy(&fmtbuf[fmtlen], &eol, 2);
                fmtlen += eolWidth;
            } else if (spaceOption & SpaceOption_SpaceBefore) {
                fmtbuf[fmtlen++] = ' ';
            }
        }
        if (chPrev == '\n') {
            uint32_t count = (spaceOption & SpaceOption_DanglingStmt) ? indentPrev : blockLevel;
            indentPrev = count;
            char indent = '\t';
            if (!editor.Send(Scintilla::Message::GetUseTabs)) {
                indent = ' ';
                count *= static_cast<unsigned>(editor.Send(Scintilla::Message::GetTabWidth));
            }
            if (count != 0) {
                output += std::string_view{fmtbuf, fmtlen};
                fmtlen = 0;
                output.append(count, indent);
            }
        }
        if (ch == '\r' || ch == '\n') {
            spaceOption |= SpaceOption_NewLineAfter;
            if (ch == '\r' && textBuffer[offset + 1] == '\n') {
                offset += 1;
            }
        } else {
            if (ch > ' ' && style > syntax.original.commentStyleMarker) {
                chPrevNonWhite = ch;
            }
            if (operatorLen > 1) {
                memcpy(&fmtbuf[fmtlen], textBuffer + offset, operatorLen);
                fmtlen += operatorLen;
                offset += operatorLen - 1;
                chPrevNonWhite = chPrev = fmtbuf[fmtlen - 1];
            } else {
                chPrev = ch;
                fmtbuf[fmtlen++] = static_cast<char>(ch);
            }
        }
        if (spaceOption & SpaceOption_NewLineAfter) {
            blockLevel += spaceOption & SpaceOption_IndentAfter;
            chPrev = '\n';
            // don't add indentation inside comment and string
            if ((!commentEndEOL && style <= syntax.original.commentStyleMarker) ||
                (style >= syntax.original.stringStyleFirst && style <= syntax.original.stringStyleLast)) {
                chPrev = '\r';
            }
            commentEndEOL = false;
            defaultCase = false;
            memcpy(&fmtbuf[fmtlen], &eol, 2);
            fmtlen += eolWidth;
        } else if (spaceOption & SpaceOption_SpaceAfter) {
            chPrev = ' ';
            fmtbuf[fmtlen++] = ' ';
        }
        stylePrev = style;
        styleBefore = style;
    }
    if (fmtlen != 0) {
        output += std::string_view{fmtbuf, fmtlen};
    }
}

} // namespace

void EditorControl::FormatCode(CodeFormatting command, const SyntaxDefinition& syntax) {
    using enum Scintilla::Message;
    if (!(syntax.original.lexerAttributes & SyntaxLexerAttribute::CodePretty)) return;
    auto start = Send(GetSelectionStart);
    auto end = Send(GetSelectionEnd);
    const bool wholeDocument = start == end;
    if (wholeDocument) {
        start = 0;
        end = Send(GetLength);
        if (!end) return;
    }
    try {
        // Bring styles through the range end current even when it has never been painted.
        Send(Colourise, 0, end);
        std::string packed(2 * static_cast<size_t>(end - start) + 4, '\0');
        const Sci_TextRangeFull range{{start, end}, packed.data()};
        const auto textLength = static_cast<size_t>(Send(GetStyledTextFull, 0, reinterpret_cast<intptr_t>(&range)));
        std::string output;
        std::string_view result;
        bool changed = false;
        if (command == CodeFormatting::Compress) {
            auto* styledText = packed.data();
            const auto* textBuffer = styledText + textLength + 1;
            size_t index = 0;
            int chPrev = 0;
            int stylePrev = static_cast<uint8_t>(styledText[0]);
            for (size_t offset = 0; offset < textLength; ++offset) {
                const uint8_t style = styledText[offset];
                if (style > syntax.original.commentStyleMarker) {
                    const uint8_t ch = textBuffer[offset];
                    constexpr uint8_t backslash = 0x5c;
                    if (ch == backslash && style != syntax.original.escapeCharacterStyle) {
                        const uint8_t next = textBuffer[offset + 1];
                        if (next == '\n' || next == '\r') {
                            offset += (next == '\r' && textBuffer[offset + 2] == '\n') ? 2 : 1;
                            continue;
                        }
                    }
                    int spaceOption = SpaceOption_None;
                    if (style != stylePrev) {
                        spaceOption = AddStyleSeparator(syntax, ch, chPrev, style);
                        if (spaceOption & SpaceOption_SpaceBefore) styledText[index++] = ' ';
                    }
                    chPrev = ch;
                    styledText[index++] = static_cast<char>(ch);
                    if (spaceOption & SpaceOption_SpaceAfter) {
                        chPrev = ' ';
                        styledText[index++] = ' ';
                    }
                }
                stylePrev = style;
            }
            changed = index < textLength;
            result = {styledText, index};
        } else {
            CodePretty(output, syntax, packed.data(), textLength, *this);
            changed = output.size() != textLength;
            result = output;
        }
        // The original deliberately compares lengths, including Pretty's equal-length no-op.
        if (changed) {
            if (wholeDocument) {
                Send(TargetWholeDocument);
                Send(ReplaceTarget, result.size(), reinterpret_cast<intptr_t>(result.data()));
            } else {
                ReplaceRangeAndSelect(start, end, result);
            }
        }
    } catch (...) {
        // Original EditFormatCode ignores formatter/allocation exceptions.
        return;
    }
}
