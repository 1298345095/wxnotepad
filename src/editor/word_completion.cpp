// Adapted from Notepad4 89b8b36 EditAutoC.cpp: EditCompleteWordCore.
// Copyright and BSD 3-Clause license: licenses/Notepad4.txt.
#include "editor/word_completion.h"
#include "editor/ascii_character.h"
#include "editor/completion_candidates.h"
#include "editor/editor_control.h"
#include "editor/completion_styles.h"
#include <ScintillaTypes.h>
#include <ILexer.h>
#include <ScintillaStructures.h>
#include <algorithm>
#include <array>
#include <cstring>

#include "syntax/syntax_catalog.h"
#include <Scintilla.h>
#include <SciLexer.h>
#include <LaTeXInput.h>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <type_traits>
#include <memory>
#include <new>
#include <utility>
#include <vector>
using Scintilla::Message;
namespace {
using Sci_Position = Scintilla::Position;
using Sci_PositionU = uintptr_t;
using Sci_Line = intptr_t;
using Sci_TextRangeFull = Scintilla::TextRangeFull;
using Sci_TextToFindFull = Scintilla::TextToFindFull;
using Scintilla::CharacterClass;
bool IsASpaceOrTab(int ch) {
    return ch == ' ' || ch == '\t';
}
bool IsAlpha(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}
// optimization for small string
template <size_t StackSize = 32> class CharBuffer {
    char* ptr;
    std::array<char, StackSize> buffer{};
    std::vector<char> heap;

public:
    explicit CharBuffer(size_t size) {
        if (size <= StackSize) {
            ptr = buffer.data();
        } else {
            heap.resize(size);
            ptr = heap.data();
        }
    }
    CharBuffer(const CharBuffer&) = delete;
    CharBuffer& operator=(const CharBuffer&) = delete;
    char* data() noexcept { return ptr; }
    char& operator[](size_t index) noexcept { return ptr[index]; }
};

static constexpr bool IsCppCommentStyle(int style) noexcept {
    return style == SCE_C_COMMENT || style == SCE_C_COMMENTLINE || style == SCE_C_COMMENTDOC ||
           style == SCE_C_COMMENTLINEDOC || style == SCE_C_COMMENTDOC_TAG || style == SCE_C_COMMENTDOC_TAG_XML;
}

static constexpr bool IsSpecialStartChar(int ch, int chPrev) noexcept {
    return (ch == '.')    // member
           || (ch == '#') // preprocessor
           || (ch == '@') // Java/PHP/Doxygen Doc Tag
           // ObjC Keyword, Java Annotation, Python Decorator, Cobra Directive
           || (ch == '<')                                  // HTML/XML Tag, C# Doc Tag
           || (ch == '\\')                                 // Doxygen Doc Tag, LaTeX Command
           || (ch == ':')                                  // CSS pseudo class
           || (ch == '$')                                  // variable
           || (ch == '`')                                  // VHDL, Verilog directive
           || (ch == '\'')                                 // VHDL attribute
           || (ch == '%')                                  // SAS macro
           || (chPrev == '\\' && (ch == '^' || ch == ':')) // LaTeX input, Emoji input
           // TODO: show emoji list after typing ':'.
           || (chPrev == '<' && ch == '/')  // HTML/XML Close Tag
           || (chPrev == '-' && ch == '>')  // member(C/C++/PHP)
           || (chPrev == ':' && ch == ':'); // namespace(C++), static member(C++/Java8/PHP)
}

#include "editor/completion_extra_words.h"
#include "syntax/generated/css_scheme_data.h"
#include "syntax/generated/vbs_scheme_data.h"
#include "syntax/generated/php_scheme_data.h"
class Collector final {
    EditorControl& editor_;
    const SyntaxDefinition& definition_;
    const WordCompletionSettings& settings_;
    CompletionStyles styles_;
    template <class T> static intptr_t Parameter(T value) {
        if constexpr (std::is_pointer_v<T>)
            return reinterpret_cast<intptr_t>(value);
        else
            return static_cast<intptr_t>(value);
    }
    template <class A = int, class B = int> intptr_t Call(Message message, A first = 0, B second = 0) {
        return editor_.Send(message, static_cast<uintptr_t>(Parameter(first)), Parameter(second));
    }
    unsigned char CharacterAt(Sci_Position position) {
        // The pinned core's UCharAt returns an unsigned char through the message ABI.
        return static_cast<unsigned char>(Call(Message::GetCharAt, position));
    }
    unsigned char StyleIndexAt(Sci_Position position) {
        // The pinned core's StyleIndexAt has the same unsigned-char result contract.
        return static_cast<unsigned char>(Call(Message::GetStyleIndexAt, position));
    }
    bool IsDocWordChar(unsigned ch) const { return CompletionStyleContains(styles_.wordCharacters, ch); }
    static bool IsDefaultWordChar(unsigned ch) {
        static constexpr std::array<uint32_t, 8> mask = {0,           0x03ff0000U, 0x87fffffeU, 0x07fffffeU,
                                                         0xffffffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU};
        return CompletionStyleContains(mask, ch);
    }
    bool IsRawStringStyle(unsigned style) const { return CompletionStyleContains(styles_.rawStrings, style); }
    bool IsCommentStyle(unsigned style) const { return CompletionStyleContains(styles_.comments, style); }
    bool IsAutoCompletionWordCharacter(unsigned ch) {
        return ch < 0x80 ? IsDocWordChar(ch)
                         : Call(Message::GetCharacterClass, ch) == std::to_underlying(CharacterClass::word);
    }
    void AddList(CompletionCandidates& words, const SyntaxDefinition& syntax, size_t index) {
        if (index < syntax.keywords.size()) words.AddKeywords(syntax.keywords[index].text);
    }
    const NP2_KEYWORDS* ExtraWords() const {
        using enum SyntaxLanguage;
        if (definition_.lexer == SCLEX_CPP) return &kwDoxyDoc;
        if (definition_.language == Xsd) return &kwXmlSchema;
        if (definition_.language == Xslt) return &kwXmlStylesheet;
        if (definition_.language == Plist) return &kwPList;
        return nullptr;
    }
    static constexpr bool IsEscapeCharacter(int ch) noexcept {
        return ch == '0'    // '\0'
               || ch == 'a' // '\a'
               || ch == 'b' // '\b'
               || ch == 'e' // '\e', GNU extension
               || ch == 'f' // '\f'
               || ch == 'n' // '\n'
               || ch == 'r' // '\r'
               || ch == 't' // '\t'
               || ch == 'v' // '\v'
               // other
               || ch == '$'; // PHP variable
                             // x u U ignored as they need to be followed with multiple hex digits.
    }

    // https://en.wikipedia.org/wiki/Printf_format_string
    static constexpr bool IsPrintfFormatSpecifier(int ch) noexcept { return IsAlpha(ch); }

    bool IsEscapeCharOrFormatSpecifier(Sci_Position before, int ch, int chPrev, int style, bool punctuation) noexcept {
        // style for chPrev, style for ch is zero on typing
        const int stylePrev = Call(Message::GetStyleIndexAt, before);
        if (stylePrev == 0) {
            return false;
        }
        if (chPrev == '%') {
            if (!IsPrintfFormatSpecifier(ch)) {
                return false;
            }
            if (style != 0 && definition_.original.formatSpecifierStyle) {
                return stylePrev == definition_.original.formatSpecifierStyle;
            }
            // legacy lexer without format specifier highlighting
            if (definition_.original.lexerAttributes & SyntaxLexerAttribute::PrintfFormatSpecifier) {
                return !(stylePrev == definition_.original.operatorStyle ||
                         stylePrev == definition_.original.operatorStyle2);
            }
            return false;
        }

        if (style != 0 && definition_.original.escapeCharacterStyle) {
            const bool embeddedEscape =
                (definition_.lexer == SCLEX_PHPSCRIPT || definition_.lexer == SCLEX_HTML) &&
                (stylePrev == js_style(SCE_JS_ESCAPECHAR) || stylePrev == css_style(SCE_CSS_ESCAPECHAR));
            if (stylePrev != definition_.original.escapeCharacterStyle && !embeddedEscape) return false;
        } else if (!punctuation && !IsEscapeCharacter(ch)) {
            // Legacy lexer without escape character highlighting.
            return false;
        }

        if (!IsRawStringStyle(stylePrev)) {
            int chPrev2 = 0;
            if (const Sci_Position before2 = Call(Message::PositionBefore, before); before2 + 1 == before) {
                chPrev2 = Call(Message::GetCharAt, before2);
            }
            // simply treat chPrev == chPrev2 as escape escapeCharacterStart self
            return chPrev != chPrev2;
        }

        return false;
    }

    static inline bool NeedSpaceAfterKeyword(const char* word, Sci_Position length) noexcept {
        const char* p = strstr(" if for try using while elseif switch foreach synchronized ", word);
        return p != nullptr && p[-1] == ' ' && p[length] == ' ';
    }

    Sci_Position MemberWordEnd(Sci_Position wordEnd, Sci_Position iDocLen, int style) noexcept {
        using enum Scintilla::Message;
        const Sci_Position before = wordEnd;
        const int ch = Call(GetCharAt, wordEnd);
        Sci_Position width = 0;
        int chNext = Call(GetCharacterAndWidth, wordEnd + 1, &width);
        if ((ch == '-' && chNext == '>') || (ch == ':' && chNext == ':')) {
            chNext = Call(GetCharacterAndWidth, wordEnd + 2, &width);
            if (IsAutoCompletionWordCharacter(chNext)) {
                wordEnd += 2;
            }
        } else if (ch == '.' || (ch == '-' && style == Call(GetStyleIndexAt, wordEnd))) {
            if (IsAutoCompletionWordCharacter(chNext)) {
                ++wordEnd;
            }
        }
        if (wordEnd == before) return wordEnd;

        while (wordEnd < iDocLen && (chNext < 0x80 && !IsDefaultWordChar(chNext))) {
            wordEnd += width;
            chNext = Call(GetCharacterAndWidth, wordEnd, &width);
            if (!IsAutoCompletionWordCharacter(chNext)) {
                break;
            }
        }

        wordEnd = Call(WordEndPosition, wordEnd, true);
        return wordEnd;
    }

    std::pair<Sci_Position, bool> ExtendDocumentWord(Sci_Position iPosFind, Sci_Position wordEnd, Sci_Position iDocLen,
                                                     int style) noexcept {
        // find all word after '::', '->', '.' and '-'
        bool bSubWord = false;
        while (wordEnd < iDocLen) {
            if (const auto ch = CharacterAt(wordEnd); !(ch == ':' || ch == '.' || ch == '-')) {
                if (ch == '!' && definition_.lexer == SCLEX_RUST && style == SCE_RUST_MACRO) {
                    // macro: println!()
                    ++wordEnd;
                }
                break;
            }

            const auto before = wordEnd;
            wordEnd = MemberWordEnd(wordEnd, iDocLen, style);
            if (wordEnd == before) break;
            if (wordEnd - iPosFind > CompletionWordLimit) {
                wordEnd = before;
                break;
            }
            bSubWord = true;
        }

        return {wordEnd, bSubWord};
    }

    void SkipDocumentPrefix(char*& pWord, int& wordLength, Sci_Position iPosFind, int style, char prefix) noexcept {
        if (const Sci_Position before = Call(Message::PositionBefore, iPosFind); before + 1 == iPosFind) {
            const int chPrev = Call(Message::GetCharAt, before);
            // word after escape character or format specifier
            if (chPrev == '%' || chPrev == definition_.original.escapeCharacterStart) {
                if (IsEscapeCharOrFormatSpecifier(before, static_cast<uint8_t>(pWord[0]), chPrev, style, false)) {
                    pWord++;
                    --wordLength;
                }
            }
        }
        if (prefix && prefix == pWord[0]) {
            pWord++;
            --wordLength;
        }
    }

    void AppendDocumentSuffix(char* pWord, int& wordLength, Sci_Position& wordEnd, int style) noexcept {
        bool space = false;
        if (!(definition_.lexer == SCLEX_CPP && style == SCE_C_MACRO)) {
            while (IsASpaceOrTab(Call(Message::GetCharAt, wordEnd))) {
                space = true;
                wordEnd++;
            }
        }

        const int chWordEnd = Call(Message::GetCharAt, wordEnd);
        if ((definition_.lexer == SCLEX_JULIA || definition_.lexer == SCLEX_RUST) && chWordEnd == '!') {
            const int chNext = Call(Message::GetCharAt, wordEnd + 1);
            if (chNext == '(') {
                wordEnd += 2;
                pWord[wordLength++] = '!';
                pWord[wordLength++] = '(';
                pWord[wordLength++] = ')';
            }
        } else if (chWordEnd == '(') {
            if (space && NeedSpaceAfterKeyword(pWord, wordLength)) {
                pWord[wordLength++] = ' ';
            }

            pWord[wordLength++] = '(';
            pWord[wordLength++] = ')';
            wordEnd++;
        }
    }

    void CollectDocumentWord(CompletionCandidates& pWList, Sci_Position iPosFind, Sci_Position& wordEnd, int style,
                             char prefix, bool bSubWord) {
        const int iRootLen = static_cast<int>(pWList.Root().size());
        if (wordEnd - iPosFind < iRootLen) return;
        std::array<char, CompletionWordBufferSize> wordBuf;
        char* pWord = wordBuf.data();
        const Sci_TextRangeFull tr = {{iPosFind, std::min(iPosFind + CompletionWordLimit, wordEnd)}, pWord};
        int wordLength = static_cast<int>(Call(Message::GetTextRangeFull, 0, &tr));

        SkipDocumentPrefix(pWord, wordLength, iPosFind, style, prefix);
        while (wordLength > 0 &&
               (pWord[wordLength - 1] == '-' || pWord[wordLength - 1] == ':' || pWord[wordLength - 1] == '.')) {
            --wordLength;
            pWord[wordLength] = '\0';
        }

        if (wordLength >= iRootLen && (pWord[0] != ':' || pWord[1] == ':') &&
            pWList.MatchesPrefix({pWord, static_cast<size_t>(wordLength)})) {
            AppendDocumentSuffix(pWord, wordLength, wordEnd, style);

            if (wordLength >= iRootLen) {
                pWord[wordLength] = '\0';
                pWList.AddDocumentWord({pWord, static_cast<size_t>(wordLength)}, bSubWord);
            }
        }
    }

    void AutoC_AddDocWord(CompletionCandidates& pWList, const std::array<uint32_t, 8>& ignoredStyleMask,
                          bool bIgnoreCase, char prefix) {
        const char* const pRoot = pWList.Root().data();
        const int iRootLen = static_cast<int>(pWList.Root().size());

        CharBuffer<> pFind(iRootLen + 2);
        pFind[0] = prefix;
        memcpy(pFind.data() + (prefix ? 1 : 0), pRoot, iRootLen);
        unsigned findFlag = (bIgnoreCase ? SCFIND_NONE : SCFIND_MATCHCASE) | SCFIND_MATCH_TO_WORD_END;
        if (IsDefaultWordChar(static_cast<uint8_t>(pRoot[0]))) {
            findFlag |= SCFIND_WORDSTART;
        }

        const Sci_Position textLength = iRootLen + (prefix ? 1 : 0);
        const Sci_Position iCurrentPos = Call(Message::GetCurrentPos) - textLength;
        const Sci_Position iDocLen = Call(Message::GetLength);
        Sci_TextToFindFull ft = {{0, iDocLen}, pFind.data(), textLength, {0, 0}};

        Sci_Position iPosFind = Call(Message::FindTextFull, findFlag, &ft);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(settings_.scanWordsTimeout);

        while (iPosFind >= 0 && iPosFind < iDocLen && std::chrono::steady_clock::now() < deadline) {
            Sci_Position wordEnd = iPosFind + iRootLen;
            const int style = Call(Message::GetStyleIndexAt, wordEnd - 1);
            wordEnd = ft.chrgText.cpMax;
            if (iPosFind != iCurrentPos && !CompletionStyleContains(ignoredStyleMask, style)) {
                const auto extended = ExtendDocumentWord(iPosFind, wordEnd, iDocLen, style);
                wordEnd = extended.first;
                CollectDocumentWord(pWList, iPosFind, wordEnd, style, prefix, extended.second);
            }

            ft.chrg.cpMin = wordEnd;
            iPosFind = Call(Message::FindTextFull, findFlag, &ft);
        }
    }

    void AutoC_AddKeyword(CompletionCandidates& words, int currentStyle) {
        auto add = [&](std::span<const SyntaxKeywords> groups) {
            for (const auto& group : groups)
                if (!(group.attributes & SyntaxKeywordAttribute::NoAutoComp)) words.AddKeywords(group.text);
        };
        add(definition_.keywords);
        if (const auto* extra = ExtraWords();
            extra && !(definition_.lexer == SCLEX_CPP && !IsCppCommentStyle(currentStyle)))
            for (const char* group : *extra)
                words.AddKeywords(group);
        if (SyntaxSettingsLanguage(definition_.language) == SyntaxLanguage::TypeScript)
            add(SyntaxFor(SyntaxLanguage::JavaScript).keywords);
        if (definition_.lexer == SCLEX_HTML) {
            if (currentStyle >= SCE_PHP_DEFAULT)
                add(phpLists);
            else if (currentStyle >= css_style(SCE_CSS_DEFAULT))
                add(cssLists);
            else if (currentStyle >= js_style(SCE_JS_DEFAULT))
                add(SyntaxFor(SyntaxLanguage::JavaScript).keywords);
            else if (currentStyle >= SCE_HB_DEFAULT)
                add(vbsLists);
        }
        if (definition_.lexer == SCLEX_JAVASCRIPT || definition_.lexer == SCLEX_MARKDOWN) {
            const auto& html = SyntaxFor(SyntaxLanguage::Html);
            for (const auto index : {0, 7, 8})
                AddList(words, html, index);
        }
    }
    enum AddWordResult { AddWordResult_None, AddWordResult_Finish, AddWordResult_IgnoreLexer };

    AddWordResult AddCssSpecialWord(CompletionCandidates& words, int currentStyle, int previousStyle, int ch,
                                    int previous) {
        if (definition_.lexer == SCLEX_HTML) {
            currentStyle -= css_style(SCE_CSS_DEFAULT);
            previousStyle -= css_style(SCE_CSS_DEFAULT);
        }
        if (ch == '@' && currentStyle == SCE_CSS_DEFAULT) {
            words.AddKeywords(cssKeywords[1]);
            return AddWordResult_IgnoreLexer;
        }
        if (ch == ':' && (currentStyle == SCE_CSS_DEFAULT || currentStyle == SCE_CSS_OPERATOR)) {
            if (previous == ':') {
                words.AddKeywords(cssKeywords[3]);
                return AddWordResult_IgnoreLexer;
            }
            if (previousStyle != SCE_CSS_PROPERTY && previousStyle != SCE_CSS_UNKNOWN_PROPERTY) {
                words.AddKeywords(cssKeywords[2]);
                return AddWordResult_IgnoreLexer;
            }
        }
        if ((ch == '$' || ch == '@' || ch == '#') && IsDocWordChar(ch)) return AddWordResult_IgnoreLexer;
        return AddWordResult_None;
    }
    AddWordResult AddJavaScriptSpecialWord(CompletionCandidates& words, int currentStyle, int ch) {
        currentStyle -= js_style(SCE_JS_DEFAULT);
        if (ch == '@' && currentStyle >= SCE_JS_COMMENTLINE && currentStyle <= SCE_JS_TASKMARKER) {
            AddList(words, SyntaxFor(SyntaxLanguage::JavaScript), 10);
            return AddWordResult_Finish;
        }
        if ((ch == '$' || ch == '@' || ch == '#') && IsDocWordChar(ch)) return AddWordResult_IgnoreLexer;
        return AddWordResult_None;
    }
    AddWordResult AddCppSpecialWord(CompletionCandidates& words, int currentStyle, int ch) {
        if (IsCppCommentStyle(currentStyle) && (ch == '@' || ch == '\\')) {
            for (const char* group : kwDoxyDoc)
                words.AddKeywords(group);
            return AddWordResult_Finish;
        }
        if (currentStyle == SCE_C_DEFAULT) {
            if (ch == '#') {
                AddList(words, definition_, 2);
                return AddWordResult_Finish;
            }
            if (ch == '@') {
                AddList(words, definition_, 3);
                return AddWordResult_IgnoreLexer;
            }
        }
        return AddWordResult_None;
    }

    AddWordResult AddJavaScriptCommentWord(CompletionCandidates& words, int currentStyle, int ch) {
        using enum SyntaxLanguage;
        if (const auto scheme = SyntaxSettingsLanguage(definition_.language);
            (ch == '@' || (ch == '<' && scheme == TypeScript)) && currentStyle >= SCE_JS_COMMENTLINE &&
            currentStyle <= SCE_JS_TASKMARKER) {
            AddList(words, definition_, 10);
            if (scheme != JavaScript) AddList(words, SyntaxFor(JavaScript), 10);
            return AddWordResult_Finish;
        }
        return AddWordResult_None;
    }
    AddWordResult AddMarkupTagWords(CompletionCandidates& words, int ch, int previous) {
        if (ch != '<' && !(previous == '<' && ch == '/')) return AddWordResult_None;
        if (definition_.lexer == SCLEX_MARKDOWN) {
            AddList(words, SyntaxFor(SyntaxLanguage::Html), 0);
        } else {
            AddList(words, definition_, 0);
            if (const auto* extra = ExtraWords()) words.AddKeywords((*extra)[0]);
        }
        return AddWordResult_IgnoreLexer;
    }
    AddWordResult AddSchemeSpecialWord(CompletionCandidates& words, int currentStyle, int ch, int previous) {
        switch (definition_.lexer) {
        case SCLEX_BASH:
            if (ch == '$') {
                AddList(words, definition_, 2);
                return AddWordResult_IgnoreLexer;
            }
            break;
        case SCLEX_CPP:
            return AddCppSpecialWord(words, currentStyle, ch);
        case SCLEX_HTML:
        case SCLEX_MARKDOWN:
            return AddMarkupTagWords(words, ch, previous);
        case SCLEX_JAVASCRIPT:
            return AddJavaScriptCommentWord(words, currentStyle, ch);
        case SCLEX_PYTHON:
            if (ch == '@' && currentStyle == SCE_PY_DEFAULT) {
                AddList(words, definition_, 7);
                return AddWordResult_IgnoreLexer;
            }
            break;
        default:
            break;
        }
        return AddWordResult_None;
    }
    AddWordResult AutoC_AddSpecWord(CompletionCandidates& words, int currentStyle, int previousStyle, int ch,
                                    int previous) {
        if ((ch == '\\' || (previous == '\\' && (ch == '^' || ch == ':'))) && settings_.latexInputMethod)
            words.AddKeywords(ch == ':' ? EmojiInputSequenceString : LaTeXInputSequenceString);
        if (definition_.lexer == SCLEX_CSS ||
            (definition_.lexer == SCLEX_HTML && currentStyle >= css_style(SCE_CSS_DEFAULT) &&
             currentStyle < SCE_PHP_DEFAULT))
            return AddCssSpecialWord(words, currentStyle, previousStyle, ch, previous);
        if (definition_.lexer == SCLEX_HTML && currentStyle >= js_style(SCE_JS_DEFAULT) &&
            currentStyle < css_style(SCE_CSS_DEFAULT))
            return AddJavaScriptSpecialWord(words, currentStyle, ch);
        if (const auto result = AddSchemeSpecialWord(words, currentStyle, ch, previous); result != AddWordResult_None)
            return result;
        if ((ch == '$' || ch == '@' || ch == '#') && IsDocWordChar(ch)) return AddWordResult_IgnoreLexer;
        return AddWordResult_None;
    }

    Sci_Position FindWordStart(Sci_Position iCurrentPos, Sci_Position iLineStartPos) {
        // word before current position
        Sci_Position iStartWordPos = iCurrentPos;
        do {
            Sci_Position before = iStartWordPos;
            iStartWordPos = Call(Message::WordStartPosition, before, true);
            const bool nonWord = iStartWordPos == before;
            before = Call(Message::PositionBefore, iStartWordPos);
            if (nonWord) {
                // non-word
                if (before + 1 != iStartWordPos) {
                    break;
                }

                if (const auto ch = CharacterAt(before); !IsDocWordChar(ch) || IsSpecialStartChar(ch, '\0')) {
                    break;
                }

                iStartWordPos = before;
            } else {
                const Sci_Position iPos = Call(Message::WordEndPosition, before, true);
                if (iPos == iStartWordPos) {
                    // after CJK word
                    break;
                }
            }
        } while (iStartWordPos > iLineStartPos);
        return iStartWordPos;
    }

    void ReadWordPrefix(Sci_Position& iStartWordPos, Sci_Position iLineStartPos, int& ch, int& chPrev, int& chPrev2) {
        chPrev = 0;
        chPrev2 = 0;
        if (ch >= 0x80 || iStartWordPos <= iLineStartPos) return;
        const Sci_Position before = Call(Message::PositionBefore, iStartWordPos);
        if (before + 1 != iStartWordPos) return;
        chPrev = CharacterAt(before);
        if (const Sci_Position before2 = Call(Message::PositionBefore, before);
            before2 >= iLineStartPos && before2 + 1 == before)
            chPrev2 = CharacterAt(before2);
        // Word after an escape character or format specifier.
        if (chPrev == '%' || (chPrev == definition_.original.escapeCharacterStart &&
                              (definition_.original.lexerAttributes & SyntaxLexerAttribute::EscapePunctuation) == 0)) {
            const auto style = StyleIndexAt(iStartWordPos);
            if (IsEscapeCharOrFormatSpecifier(before, ch, chPrev, style, false)) {
                ++iStartWordPos;
                ch = CharacterAt(iStartWordPos);
                chPrev = 0;
            }
        }
    }

    int MinimumRootLength(int ch, Sci_Position iStartWordPos) {
        int iRootLen = settings_.minWordLength;
        if (ch >= '0' && ch <= '9') {
            if (settings_.minNumberLength <= 0) { // ignore number
                return -1;
            }

            iRootLen = settings_.minNumberLength;
            if (ch == '0') {
                // number prefix
                const int chNext = Ascii::Lower(Call(Message::GetCharAt, iStartWordPos + 1));
                if (chNext == 'x' || chNext == 'b' || chNext == 'o') {
                    iRootLen += 2;
                }
            }
        }
        return iRootLen;
    }

    std::optional<int> ReadPreprocessorPrefix(Sci_Position iStartWordPos, Sci_Position iLineStartPos, int chPrev) {
        Sci_Position before = iStartWordPos - 1;
        if (chPrev != '#') {
            while (before >= iLineStartPos) {
                chPrev = Call(Message::GetCharAt, before);
                if (!IsASpaceOrTab(chPrev)) break;
                --before;
            }
        }
        if (chPrev != '#') return std::nullopt;
        if (before > iLineStartPos) {
            --before;
            while (before >= iLineStartPos && IsASpaceOrTab(Call(Message::GetCharAt, before)))
                --before;
            if (before >= iLineStartPos) chPrev = 0;
        }
        return chPrev;
    }

    void AdjustSpecialPrefix(Sci_Position iStartWordPos, Sci_Position iLineStartPos, int& ch, int& chPrev,
                             int chPrev2) {
        if ((definition_.original.lexerAttributes & SyntaxLexerAttribute::CppPreprocessor) &&
            (chPrev == '#' || IsASpaceOrTab(chPrev))) {
            if (const auto previous = ReadPreprocessorPrefix(iStartWordPos, iLineStartPos, chPrev);
                previous.has_value())
                ch = *previous;
            chPrev = 0;
        } else if (IsSpecialStartChar(chPrev, chPrev2)) {
            ch = chPrev;
            chPrev = chPrev2;
        }
    }

    bool CompletionScopeEnabled(int iCurrentStyle, bool bScanWordsInDocument) {
        if (definition_.lexer == SCLEX_NULL ||
            (definition_.original.lexerAttributes & SyntaxLexerAttribute::PlainTextFile)) {
            if (!bScanWordsInDocument || !(settings_.completeScope & WordCompletionScope::PlainText) ||
                !(settings_.scanWordScope & WordCompletionScope::PlainText)) {
                return false;
            }
        } else {
            if ((!(settings_.completeScope & WordCompletionScope::Comment) && IsCommentStyle(iCurrentStyle)) ||
                (!(settings_.completeScope & WordCompletionScope::String) &&
                 CompletionStyleContains(styles_.strings, iCurrentStyle)) ||
                (!(settings_.completeScope & WordCompletionScope::PlainText) &&
                 CompletionStyleContains(styles_.plainText, iCurrentStyle))) {
                return false;
            }
        }

        return true;
    }

    std::array<uint32_t, 8> IgnoredDocumentStyles(int iCurrentStyle, bool bScanWordsInDocument) {
        std::array<uint32_t, 8> ignoredStyleMask{};
        if (bScanWordsInDocument) {
            ignoredStyleMask = styles_.ignoredWords;
            if (!(settings_.scanWordScope & WordCompletionScope::Comment) && !IsCommentStyle(iCurrentStyle)) {
                for (unsigned i = 0; i < 8; i++) {
                    ignoredStyleMask[i] |= styles_.comments[i];
                }
            }
            if (!(settings_.scanWordScope & WordCompletionScope::String) &&
                !CompletionStyleContains(styles_.strings, iCurrentStyle)) {
                for (unsigned i = 0; i < 8; i++) {
                    ignoredStyleMask[i] |= styles_.strings[i];
                }
            }
            if (!(settings_.scanWordScope & WordCompletionScope::PlainText) &&
                !CompletionStyleContains(styles_.plainText, iCurrentStyle)) {
                for (unsigned i = 0; i < 8; i++) {
                    ignoredStyleMask[i] |= styles_.plainText[i];
                }
            }
        }
        return ignoredStyleMask;
    }

    void CollectCandidates(CompletionCandidates& pWList, int iCurrentStyle, bool bIgnoreLexer, bool bIgnoreDoc,
                           char prefix) {
        const bool bScanWordsInDocument = (settings_.completeOption & WordCompletionOption::ScanWordsInDocument) != 0;
        const bool bIgnoreCase = pWList.IgnoreCase();
        bool retry = CompletionScopeEnabled(iCurrentStyle, bScanWordsInDocument);
        const auto ignoredStyleMask = definition_.lexer == SCLEX_NULL || (definition_.original.lexerAttributes &
                                                                          SyntaxLexerAttribute::PlainTextFile)
                                          ? std::array<uint32_t, 8>{}
                                          : IgnoredDocumentStyles(iCurrentStyle, retry && bScanWordsInDocument);

        while (retry) {
            if (!bIgnoreLexer) {
                // keywords
                AutoC_AddKeyword(pWList, iCurrentStyle);
            }
            if (bScanWordsInDocument) {
                if (!bIgnoreDoc || pWList.Count() == 0) {
                    AutoC_AddDocWord(pWList, ignoredStyleMask, bIgnoreCase, prefix);
                }
                if (prefix && pWList.Count() == 0) {
                    prefix = '\0';
                    AutoC_AddDocWord(pWList, ignoredStyleMask, bIgnoreCase, prefix);
                }
            }

            retry = false;
            if (pWList.RetrySubRoot()) {
                retry = true;
                bIgnoreLexer = false;
                bIgnoreDoc = false;
                prefix = 0;
            }
        }
    }

    void ShowCandidates(const CompletionCandidates& pWList, bool bIgnoreCase, bool autoInsert) {
        using enum Scintilla::Message;
        const auto pList = pWList.Serialize();
        const auto fillUps = FillUps();
        Call(AutoCSetOptions, SC_AUTOCOMPLETE_FIXED_SIZE);
        Call(AutoCSetIgnoreCase, bIgnoreCase); // case sensitivity
        Call(AutoCSetCaseInsensitiveBehaviour, bIgnoreCase);
        Call(AutoCSetFillUps, 0, fillUps.c_str());
        Call(AutoCSetMaxHeight,
             std::min(pWList.Count(), static_cast<unsigned>(settings_.visibleItemCount))); // visible rows
        Call(AutoCSetCancelAtStart, false); // don't cancel the list when deleting character
        Call(AutoCSetChooseSingle, autoInsert);
        Call(AutoCSetSeparator, '\n');
        Call(AutoCShow, pWList.Root().size(), pList.get());
    }

    struct SpecialSources {
        bool ignoreLexer;
        bool ignoreDocument = false;
        char prefix = 0;
    };
    SpecialSources PrepareSpecialSources(CompletionCandidates& pWList, int iCurrentStyle, Sci_Position iStartWordPos,
                                         int ch, int chPrev, bool ignoreLexer) {
        SpecialSources sources{ignoreLexer};

        if (!sources.ignoreLexer && IsSpecialStartChar(ch, chPrev)) {
            int iPrevStyle = 0;
            if (ch == ':' && chPrev != ':') {
                const Sci_Position iPos = Call(Message::WordStartPosition, iStartWordPos - 1, false);
                iPrevStyle = Call(Message::GetStyleIndexAt, iPos);
            }

            const AddWordResult result = AutoC_AddSpecWord(pWList, iCurrentStyle, iPrevStyle, ch, chPrev);
            if (result == AddWordResult_Finish) {
                sources.ignoreLexer = true;
                sources.ignoreDocument = true;
            } else if (result == AddWordResult_IgnoreLexer) {
                sources.ignoreLexer = true;
                // HTML/XML Tag
                if (ch == '/' || ch == '>') {
                    ch = '<';
                }
                sources.prefix = static_cast<char>(ch);
            }
        }
        return sources;
    }

public:
    Collector(EditorControl& editor, const SyntaxDefinition& definition, const WordCompletionSettings& settings)
        : editor_(editor), definition_(definition), settings_(settings), styles_(CompletionStylesFor(definition)) {}
    std::string FillUps() const {
        std::string text;
        if (settings_.fillUpMask & CompletionFillUp::Space) text += ' ';
        if (settings_.fillUpMask & CompletionFillUp::Punctuation)
            for (const wxUniChar ch : settings_.fillUpPunctuation)
                if (ch.GetValue() < 128 && std::ispunct(static_cast<unsigned char>(ch.GetValue())))
                    text += static_cast<char>(ch.GetValue());
        return text;
    }
    bool Run(bool autoInsert, WordCompletionCondition condition, WordCompletionState& state) {
        const Sci_Position iCurrentPos = Call(Message::GetCurrentPos);
        const Sci_Line iLine = Call(Message::LineFromPosition, iCurrentPos);
        const Sci_Position iLineStartPos = Call(Message::PositionFromLine, iLine);

        Sci_Position iStartWordPos = FindWordStart(iCurrentPos, iLineStartPos);
        if (iStartWordPos == iCurrentPos) {
            return false;
        }

        // beginning of word
        int ch = Call(Message::GetCharAt, iStartWordPos);

        int chPrev = 0;
        int chPrev2 = 0;
        ReadWordPrefix(iStartWordPos, iLineStartPos, ch, chPrev, chPrev2);

        int iRootLen = MinimumRootLength(ch, iStartWordPos);
        if (iRootLen < 0) return false;

        if (iCurrentPos - iStartWordPos < iRootLen) {
            return false;
        }

        AdjustSpecialPrefix(iStartWordPos, iLineStartPos, ch, chPrev, chPrev2);

        CharBuffer<> pRoot(iCurrentPos - iStartWordPos + 1);
        const Sci_TextRangeFull tr = {{iStartWordPos, iCurrentPos}, pRoot.data()};
        Call(Message::GetTextRangeFull, 0, &tr);
        iRootLen = static_cast<int>(strlen(pRoot.data()));

        bool bIgnoreLexer = (settings_.completeOption & WordCompletionOption::OnlyWordsInDocument) != 0 ||
                            (pRoot[0] >= '0' && pRoot[0] <= '9'); // number
        const bool bIgnoreCase = bIgnoreLexer || settings_.ignoreCase;
        CompletionCandidates pWList({pRoot.data(), static_cast<size_t>(iRootLen)}, bIgnoreCase);

        int iCurrentStyle = Call(Message::GetStyleIndexAt, iCurrentPos);
        const auto sources = PrepareSpecialSources(pWList, iCurrentStyle, iStartWordPos, ch, chPrev, bIgnoreLexer);

        if (iCurrentStyle == 0) {
            // word not yet styled, or is plain text
            iCurrentStyle = Call(Message::GetStyleIndexAt, iStartWordPos);
        }

        CollectCandidates(pWList, iCurrentStyle, sources.ignoreLexer, sources.ignoreDocument, sources.prefix);

        const bool bShow = pWList.HasCompletion();

        if (const bool updated =
                state.previousItemCount == 0 ||
                (condition == WordCompletionCondition::CharacterAdded &&
                 state.previousItemCount - pWList.Count() > static_cast<unsigned>(settings_.visibleItemCount)) ||
                (condition == WordCompletionCondition::CharacterDeleted && state.previousItemCount < pWList.Count());
            bShow && updated) {
            state.previousItemCount = pWList.Count();
            ShowCandidates(pWList, bIgnoreCase, autoInsert);
        }

        return bShow;
    }
};
} // namespace
void CompleteWord(EditorControl& editor, const SyntaxDefinition& definition, const WordCompletionSettings& settings,
                  bool autoInsert, WordCompletionCondition condition) {
    auto& state = editor.CompletionState();
    if (condition == WordCompletionCondition::CharacterAdded &&
        state.previousItemCount <= 2U * static_cast<unsigned>(settings.visibleItemCount))
        return;
    if (condition == WordCompletionCondition::Normal) state.previousItemCount = 0;
    try {
        editor.SetCompletionSettings(settings);
        if (!Collector(editor, definition, settings).Run(autoInsert, condition, state)) {
            state.previousItemCount = 0;
            if (condition != WordCompletionCondition::Normal) editor.Send(Message::AutoCCancel);
        }
    } catch (const std::bad_alloc&) {
        // Completion is optional; keep the edit that triggered the native
        // notification and stop before crossing its noexcept boundary.
        state = {};
        editor.Send(Message::AutoCCancel);
        editor.Send(Message::SetStatus, static_cast<uintptr_t>(std::to_underlying(Scintilla::Status::BadAlloc)));
    }
}
