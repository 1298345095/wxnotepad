#include "editor/completion_styles.h"
#include <SciLexer.h>
CompletionStyles CompletionStylesFor(const SyntaxDefinition& definition) {
    CompletionStyles result;
    result.lexer = definition.lexer;
    const auto& original = definition.original;
    result.escapeCharacterStart = original.escapeCharacterStart;
    result.escapeCharacterStyle = original.escapeCharacterStyle;
    result.characterLiteralStyle = original.characterLiteralStyle;
    result.noneSingleQuotedStyle = original.noneSingleQuotedStyle;
    result.characterPrefix = (original.lexerAttributes & (1U << 9)) != 0;
    result.wordCharacters = {0x00000000U, 0x03ff0000U, 0x87fffffeU, 0x07fffffeU,
                             0xffffffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU};
    // Original pinned generated cache cases. Keep masks independent of display styles.
    switch (definition.language) {
    case SyntaxLanguage::Go:
        result.rawStrings[SCE_GO_RAW_STRING >> 5] |= (1U << (SCE_GO_RAW_STRING & 31));
        break;
    case SyntaxLanguage::Lua:
        result.rawStrings[SCE_LUA_LITERALSTRING >> 5] |= (1U << (SCE_LUA_LITERALSTRING & 31));
        break;
    case SyntaxLanguage::Rust:
        result.wordCharacters['$' >> 5] |= (1 << ('$' & 31));
        result.wordCharacters[':' >> 5] |= (1 << (':' & 31));
        result.characterPrefixes['b' >> 5] |= (1 << ('b' & 31));
        result.rawStrings[SCE_RUST_RAW_STRING >> 5] |= (1U << (SCE_RUST_RAW_STRING & 31));
        result.rawStrings[SCE_RUST_RAW_BYTESTRING >> 5] |= (1U << (SCE_RUST_RAW_BYTESTRING & 31));
        result.genericTypes[SCE_RUST_TYPE >> 5] |= (1U << (SCE_RUST_TYPE & 31));
        result.genericTypes[SCE_RUST_STRUCT >> 5] |= (1U << (SCE_RUST_STRUCT & 31));
        result.genericTypes[SCE_RUST_TRAIT >> 5] |= (1U << (SCE_RUST_TRAIT & 31));
        result.genericTypes[SCE_RUST_ENUMERATION >> 5] |= (1U << (SCE_RUST_ENUMERATION & 31));
        result.genericTypes[SCE_RUST_UNION >> 5] |= (1U << (SCE_RUST_UNION & 31));
        break;
    case SyntaxLanguage::PlainText:

        result.wordCharacters['-' >> 5] |= (1 << ('-' & 31));
        break;
    case SyntaxLanguage::TypeScript:
    case SyntaxLanguage::Tsx:

        result.wordCharacters['#' >> 5] |= (1 << ('#' & 31));
        result.wordCharacters['$' >> 5] |= (1 << ('$' & 31));
        result.wordCharacters['@' >> 5] |= (1 << ('@' & 31));
        result.genericTypes[SCE_JS_CLASS >> 5] |= (1U << (SCE_JS_CLASS & 31));
        result.genericTypes[SCE_JS_INTERFACE >> 5] |= (1U << (SCE_JS_INTERFACE & 31));
        result.genericTypes[SCE_JS_ENUM >> 5] |= (1U << (SCE_JS_ENUM & 31));
        result.genericTypes[SCE_JS_WORD2 >> 5] |= (1U << (SCE_JS_WORD2 & 31));
        break;
    case SyntaxLanguage::Bash:
    case SyntaxLanguage::CShell:
    case SyntaxLanguage::M4:

        result.wordCharacters['$' >> 5] |= (1 << ('$' & 31));
        result.wordCharacters['-' >> 5] |= (1 << ('-' & 31));
        result.rawStrings[SCE_SH_STRING_SQ >> 5] |= (1U << (SCE_SH_STRING_SQ & 31));
        break;
    case SyntaxLanguage::Cpp:

        result.wordCharacters[':' >> 5] |= (1 << (':' & 31));
        result.characterPrefixes['L' >> 5] |= (1 << ('L' & 31));
        result.characterPrefixes['U' >> 5] |= (1 << ('U' & 31));
        result.characterPrefixes['u' >> 5] |= (1 << ('u' & 31));
        result.rawStrings[SCE_C_STRINGRAW >> 5] |= (1U << (SCE_C_STRINGRAW & 31));
        result.rawStrings[SCE_C_COMMENTDOC_TAG >> 5] |= (1U << (SCE_C_COMMENTDOC_TAG & 31));
        result.genericTypes[SCE_C_CLASS >> 5] |= (1U << (SCE_C_CLASS & 31));
        result.genericTypes[SCE_C_INTERFACE >> 5] |= (1U << (SCE_C_INTERFACE & 31));
        result.genericTypes[SCE_C_STRUCT >> 5] |= (1U << (SCE_C_STRUCT & 31));
        result.genericTypes[SCE_C_WORD2 >> 5] |= (1U << (SCE_C_WORD2 & 31));
        result.ignoredWords[SCE_C_WORD >> 5] |= (1U << (SCE_C_WORD & 31));
        result.ignoredWords[SCE_C_WORD2 >> 5] |= (1U << (SCE_C_WORD2 & 31));
        result.ignoredWords[SCE_C_PREPROCESSOR >> 5] |= (1U << (SCE_C_PREPROCESSOR & 31));
        result.ignoredWords[SCE_C_ASM_REGISTER >> 5] |= (1U << (SCE_C_ASM_REGISTER & 31));
        result.ignoredWords[SCE_C_ASM_INSTRUCTION >> 5] |= (1U << (SCE_C_ASM_INSTRUCTION & 31));
        np2_LexKeyword = &kwDoxyDoc;
        break;
    case SyntaxLanguage::Html:
    case SyntaxLanguage::AspVb:
    case SyntaxLanguage::AspJs:
    case SyntaxLanguage::AspxCs:
    case SyntaxLanguage::AspxVb:
    case SyntaxLanguage::Jsp:
    case SyntaxLanguage::Jspx:
    case SyntaxLanguage::Xml:
    case SyntaxLanguage::Svg:
    case SyntaxLanguage::Xsd:
    case SyntaxLanguage::Xslt:
    case SyntaxLanguage::Dtd:
    case SyntaxLanguage::Plist:

        result.wordCharacters['-' >> 5] |= (1 << ('-' & 31));
        result.wordCharacters[':' >> 5] |= (1 << (':' & 31));
        result.comments[SCE_H_COMMENT >> 5] |= (1U << (SCE_H_COMMENT & 31));
        result.comments[SCE_H_XCCOMMENT >> 5] |= (1U << (SCE_H_XCCOMMENT & 31));
        result.comments[SCE_H_SGML_COMMENT >> 5] |= (1U << (SCE_H_SGML_COMMENT & 31));
        result.comments[SCE_H_SGML_1ST_PARAM_COMMENT >> 5] |= (1U << (SCE_H_SGML_1ST_PARAM_COMMENT & 31));
        result.comments[js_style(SCE_JS_COMMENTLINE) >> 5] |= (1U << (js_style(SCE_JS_COMMENTLINE) & 31));
        result.comments[js_style(SCE_JS_COMMENTBLOCK) >> 5] |= (1U << (js_style(SCE_JS_COMMENTBLOCK) & 31));
        result.comments[js_style(SCE_JS_COMMENTBLOCKDOC) >> 5] |= (1U << (js_style(SCE_JS_COMMENTBLOCKDOC) & 31));
        result.comments[js_style(SCE_JS_COMMENTTAGAT) >> 5] |= (1U << (js_style(SCE_JS_COMMENTTAGAT) & 31));
        result.comments[js_style(SCE_JS_TASKMARKER) >> 5] |= (1U << (js_style(SCE_JS_TASKMARKER) & 31));
        result.comments[css_style(SCE_CSS_COMMENTBLOCK) >> 5] |= (1U << (css_style(SCE_CSS_COMMENTBLOCK) & 31));
        result.comments[css_style(SCE_CSS_CDO_CDC) >> 5] |= (1U << (css_style(SCE_CSS_CDO_CDC) & 31));
        result.comments[SCE_HB_COMMENTLINE >> 5] |= (1U << (SCE_HB_COMMENTLINE & 31));
        result.strings[SCE_H_DOUBLESTRING >> 5] |= (1U << (SCE_H_DOUBLESTRING & 31));
        result.strings[SCE_H_SINGLESTRING >> 5] |= (1U << (SCE_H_SINGLESTRING & 31));
        result.strings[SCE_H_SGML_DOUBLESTRING >> 5] |= (1U << (SCE_H_SGML_DOUBLESTRING & 31));
        result.strings[SCE_H_SGML_SIMPLESTRING >> 5] |= (1U << (SCE_H_SGML_SIMPLESTRING & 31));
        result.strings[js_style(SCE_JS_STRING_SQ) >> 5] |= (1U << (js_style(SCE_JS_STRING_SQ) & 31));
        result.strings[js_style(SCE_JS_STRING_DQ) >> 5] |= (1U << (js_style(SCE_JS_STRING_DQ) & 31));
        result.strings[js_style(SCE_JS_TEMPLATELITERAL) >> 5] |= (1U << (js_style(SCE_JS_TEMPLATELITERAL) & 31));
        result.strings[js_style(SCE_JS_REGEX) >> 5] |= (1U << (js_style(SCE_JS_REGEX) & 31));
        result.strings[js_style(SCE_JS_ESCAPECHAR) >> 5] |= (1U << (js_style(SCE_JS_ESCAPECHAR) & 31));
        result.strings[css_style(SCE_CSS_ESCAPECHAR) >> 5] |= (1U << (css_style(SCE_CSS_ESCAPECHAR) & 31));
        result.strings[css_style(SCE_CSS_STRING_SQ) >> 5] |= (1U << (css_style(SCE_CSS_STRING_SQ) & 31));
        result.strings[css_style(SCE_CSS_STRING_DQ) >> 5] |= (1U << (css_style(SCE_CSS_STRING_DQ) & 31));
        result.strings[css_style(SCE_CSS_URL) >> 5] |= (1U << (css_style(SCE_CSS_URL) & 31));
        result.strings[SCE_HB_STRING >> 5] |= (1U << (SCE_HB_STRING & 31));
        result.plainText[SCE_H_DEFAULT >> 5] |= (1U << (SCE_H_DEFAULT & 31));
        break;
    case SyntaxLanguage::JavaScript:
    case SyntaxLanguage::Jsx:

        result.wordCharacters['#' >> 5] |= (1 << ('#' & 31));
        result.wordCharacters['$' >> 5] |= (1 << ('$' & 31));
        result.wordCharacters['@' >> 5] |= (1 << ('@' & 31));
        break;
    case SyntaxLanguage::Python:

        result.wordCharacters['$' >> 5] |= (1 << ('$' & 31));
        result.characterPrefixes['B' >> 5] |= (1 << ('B' & 31));
        result.characterPrefixes['F' >> 5] |= (1 << ('F' & 31));
        result.characterPrefixes['R' >> 5] |= (1 << ('R' & 31));
        result.characterPrefixes['T' >> 5] |= (1 << ('T' & 31));
        result.characterPrefixes['U' >> 5] |= (1 << ('U' & 31));
        result.characterPrefixes['b' >> 5] |= (1 << ('b' & 31));
        result.characterPrefixes['f' >> 5] |= (1 << ('f' & 31));
        result.characterPrefixes['r' >> 5] |= (1 << ('r' & 31));
        result.characterPrefixes['t' >> 5] |= (1 << ('t' & 31));
        result.characterPrefixes['u' >> 5] |= (1 << ('u' & 31));
        result.rawStrings[SCE_PY_RAWSTRING_SQ >> 5] |= (1U << (SCE_PY_RAWSTRING_SQ & 31));
        result.rawStrings[SCE_PY_RAWSTRING_DQ >> 5] |= (1U << (SCE_PY_RAWSTRING_DQ & 31));
        result.rawStrings[SCE_PY_TRIPLE_RAWSTRING_SQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWSTRING_SQ & 31));
        result.rawStrings[SCE_PY_TRIPLE_RAWSTRING_DQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWSTRING_DQ & 31));
        result.rawStrings[SCE_PY_RAWFMTSTRING_SQ >> 5] |= (1U << (SCE_PY_RAWFMTSTRING_SQ & 31));
        result.rawStrings[SCE_PY_RAWFMTSTRING_DQ >> 5] |= (1U << (SCE_PY_RAWFMTSTRING_DQ & 31));
        result.rawStrings[SCE_PY_TRIPLE_RAWFMTSTRING_SQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWFMTSTRING_SQ & 31));
        result.rawStrings[SCE_PY_TRIPLE_RAWFMTSTRING_DQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWFMTSTRING_DQ & 31));
        result.rawStrings[SCE_PY_RAWBYTES_SQ >> 5] |= (1U << (SCE_PY_RAWBYTES_SQ & 31));
        result.rawStrings[SCE_PY_RAWBYTES_DQ >> 5] |= (1U << (SCE_PY_RAWBYTES_DQ & 31));
        result.rawStrings[SCE_PY_TRIPLE_RAWBYTES_SQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWBYTES_SQ & 31));
        result.rawStrings[SCE_PY_TRIPLE_RAWBYTES_DQ >> 5] |= (1U << (SCE_PY_TRIPLE_RAWBYTES_DQ & 31));
        result.ignoredWords[SCE_PY_WORD >> 5] |= (1U << (SCE_PY_WORD & 31));
        result.ignoredWords[SCE_PY_WORD2 >> 5] |= (1U << (SCE_PY_WORD2 & 31));
        result.ignoredWords[SCE_PY_BUILTIN_CONSTANT >> 5] |= (1U << (SCE_PY_BUILTIN_CONSTANT & 31));
        result.ignoredWords[SCE_PY_BUILTIN_FUNCTION >> 5] |= (1U << (SCE_PY_BUILTIN_FUNCTION & 31));
        result.ignoredWords[SCE_PY_ATTRIBUTE >> 5] |= (1U << (SCE_PY_ATTRIBUTE & 31));
        result.ignoredWords[SCE_PY_OBJECT_FUNCTION >> 5] |= (1U << (SCE_PY_OBJECT_FUNCTION & 31));
        result.strings[SCE_PY_ESCAPECHAR >> 5] |= (1U << (SCE_PY_ESCAPECHAR & 31));
        result.strings[SCE_PY_FORMAT_SPECIFIER >> 5] |= (1U << (SCE_PY_FORMAT_SPECIFIER & 31));
        break;
    default:
        break;
    }
    result.wordCharacters['.' >> 5] |= 1U << ('.' & 31);
    if (original.commentStyleMarker) result.comments[0] |= (1U << (original.commentStyleMarker + 1)) - 2;
    if (original.stringStyleLast) {
        for (int style = original.stringStyleFirst; style <= original.stringStyleLast; ++style)
            result.strings[style >> 5] |= 1U << (style & 31);
    }
    return result;
}
