// Adapted from Notepad4 89b8b36 EditAutoC.cpp: WordList and EditCompleteWordCore.
// Copyright and BSD 3-Clause license: licenses/Notepad4.txt.
#include "editor/word_completion.h"
#include "editor/editor_control.h"
#include "editor/completion_styles.h"
#include <ScintillaTypes.h>
#include <ILexer.h>
#include <ScintillaStructures.h>
#include <VectorISA.h>
#include <algorithm>
#include <cassert>
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
#include <vector>
using Scintilla::Message;
namespace {
using Sci_Position = Scintilla::Position;
using Sci_PositionU = uintptr_t;
using Sci_Line = intptr_t;
using Sci_TextRangeFull = Scintilla::TextRangeFull;
using Sci_TextToFindFull = Scintilla::TextToFindFull;
using Scintilla::CharacterClass;
int LowerAscii(unsigned char ch) {
    return ch >= 'A' && ch <= 'Z' ? ch + 'a' - 'A' : ch;
}
int CompareNoCase(const char* left, const char* right, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const auto a = static_cast<unsigned char>(left[i]), b = static_cast<unsigned char>(right[i]);
        const int difference = LowerAscii(a) - LowerAscii(b);
        if (difference || !a || !b) return difference;
    }
    return 0;
}
bool StrNotEmpty(const char* text) {
    return text && *text;
}
bool IsASpaceOrTab(int ch) {
    return ch == ' ' || ch == '	';
}
bool IsAlpha(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}
int UnsafeLower(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch + 'a' - 'A' : ch;
}
bool BitTestEx(const uint32_t* mask, unsigned value) {
    return value < 256 && (mask[value >> 5] & (1U << (value & 31)));
}
constexpr unsigned LexerAttr_PrintfFormatSpecifier = 1U << 6;
constexpr unsigned LexerAttr_CppPreprocessor = 1U << 8;
constexpr unsigned LexerAttr_EscapePunctuation = 1U << 10;
constexpr unsigned LexerAttr_PlainTextFile = 1U << 11;
constexpr int AutoCompletionOption_ScanWordsInDocument = 4;
constexpr int AutoCompletionOption_OnlyWordsInDocument = 8;
constexpr int AutoCompleteScope_Commont = 1;
constexpr int AutoCompleteScope_String = 2;
constexpr int AutoCompleteScope_PlainText = 4;
#define NP2_AUTOC_CACHE_SORT_KEY 1
#define NP2_AUTOC_USE_WORD_POINTER 0            // used for debug
#define NP2_AUTOC_MAX_WORD_LENGTH (128 - 3 - 1) // SP + '(' + ')' + '\0'
constexpr unsigned NP2_AUTOC_WORD_BUFFER_SIZE = 128;
#define NP2_AUTOC_INIT_BUFFER_SIZE (4096)

// optimization for small string
template <size_t StackSize = 32> class CharBuffer {
    char* ptr;
    char buffer[StackSize]{};
    std::unique_ptr<char[]> heap;

public:
    explicit CharBuffer(size_t size) noexcept {
        if (size <= StackSize) {
            ptr = buffer;
        } else {
            heap = std::make_unique<char[]>(size);
            ptr = heap.get();
        }
    }
    CharBuffer(const CharBuffer&) = delete;
    CharBuffer& operator=(const CharBuffer&) = delete;
    char* data() noexcept { return ptr; }
    char& operator[](size_t index) noexcept { return ptr[index]; }
};

struct WordNode;
struct WordList {
    int (*WL_strcmp)(const char*, const char*);
    int (*WL_strncmp)(const char*, const char*, size_t);
#if NP2_AUTOC_CACHE_SORT_KEY
    uint32_t (*WL_SortKeyFunc)(const char*, uint32_t);
#endif

    WordNode* pListHead;
    const char* pWordStart;
    unsigned iStartLen;
#if NP2_AUTOC_CACHE_SORT_KEY
    unsigned startSortKey;
    bool bIgnoreCase;
#endif
    unsigned nWordCount;
    unsigned nTotalLen;

    unsigned offset;
    unsigned capacity;
    std::byte* buffer = nullptr;
    std::vector<std::unique_ptr<std::byte[]>> buffers;

    WordList() = default;
    WordList(const WordList&) = delete;
    WordList& operator=(const WordList&) = delete;

    void Init(const char* pRoot, unsigned iRootLen, bool ignoreCase) noexcept;
    std::unique_ptr<char[]> GetList() const;
    void AddBuffer() noexcept;
    void AddWord(const char* pWord, unsigned len) noexcept;
    int CompareNode(const WordNode& node, const char* word, unsigned length, unsigned sortKey) const noexcept;
    void UpdateRoot(const char* pRoot, unsigned iRootLen) noexcept;
    bool StartsWith(const char* pWord) const noexcept;
    void AddListEx(const char* pList) noexcept;
    void AddList(const char* pList) noexcept {
        if (StrNotEmpty(pList)) {
            AddListEx(pList);
        }
    }
    void AddSubWord(char* pWord, unsigned wordLength, unsigned iRootLen) noexcept;
};

// TODO: replace _stricmp() and CompareNoCase() with other functions
// which correctly case insensitively compares UTF-8 string and ANSI/DBCS string.

#if NP2_AUTOC_CACHE_SORT_KEY
constexpr unsigned NP2_AUTOC_SORT_KEY_LENGTH = 4;

uint32_t WordList_SortKey(const char* pWord, uint32_t len) noexcept {
#if 0
	uint32_t high = 0;
	const uint8_t *ptr = static_cast<const uint8_t *>(pWord);
	len = std::min<uint32_t>(len, NP2_AUTOC_SORT_KEY_LENGTH);
	for (uint32_t i = 0; i < len; i++) {
		high = (high << 8) | *ptr++;
	}
	if (len < NP2_AUTOC_SORT_KEY_LENGTH) {
		assert(len != 0); // suppress [clang-analyzer-core.uninitialized.Assign]
		high <<= (NP2_AUTOC_SORT_KEY_LENGTH - len)*8;
	}

#else
    uint32_t high = loadle_u32(pWord);
    if (len < NP2_AUTOC_SORT_KEY_LENGTH) {
        high = bit_zero_high_u32(high, len * 8);
    }
    high = bswap32(high);
#endif
    return high;
}

uint32_t WordList_SortKeyCase(const char* pWord, uint32_t len) noexcept {
#if 1
    uint32_t high = 0;
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(pWord);
    len = std::min<uint32_t>(len, NP2_AUTOC_SORT_KEY_LENGTH);
    for (uint32_t i = 0; i < len; i++) {
        const uint8_t ch = *ptr++;
        high = (high << 8) | ch;
        // convert to lower case to match _stricmp() / strcasecmp().
        if (ch >= 'A' && ch <= 'Z') {
            high += 'a' - 'A';
        }
    }
    if (len < NP2_AUTOC_SORT_KEY_LENGTH) {
        assert(len != 0); // suppress [clang-analyzer-core.uninitialized.Assign]
        high <<= (NP2_AUTOC_SORT_KEY_LENGTH - len) * 8;
    }

#else
    uint32_t high = loadle_u32(pWord);
    high |= 0x20202020U; // only works for ASCII letters
    if (len < NP2_AUTOC_SORT_KEY_LENGTH) {
        high = bit_zero_high_u32(high, len * 8);
    }
    high = bswap32(high);
#endif
    return high;
}
#endif

// Tree
struct WordNode {
    union {
        WordNode* link[2];
        struct {
            WordNode* left;
            WordNode* right;
        };
    };
#if NP2_AUTOC_USE_WORD_POINTER
    char* word;
#endif
#if NP2_AUTOC_CACHE_SORT_KEY
    unsigned sortKey;
#endif
    unsigned len;
    unsigned level;
};

constexpr unsigned NP2_TREE_HEIGHT_LIMIT = 32;
// store word right after the node as most word are short.
#define WordNode_GetWord(node) (reinterpret_cast<char*>(node) + sizeof(WordNode))
// TODO: since the tree is sorted, nodes greater than some level can be deleted to reduce total words.
// or only limit word count in WordList::GetList().

// Andersson Tree, source from https://www.eternallyconfuzzled.com/tuts/datastructures/jsw_tut_andersson.aspx
// see also https://en.wikipedia.org/wiki/AA_tree
WordNode* SkewTree(WordNode* node) noexcept {
    if (node->level && node->left && node->level == node->left->level) {
        WordNode* save = node->left;
        node->left = save->right;
        save->right = node;
        return save;
    }
    return node;
}
WordNode* SplitTree(WordNode* node) noexcept {
    if (node->level && node->right && node->right->right && node->level == node->right->right->level) {
        WordNode* save = node->right;
        node->right = save->left;
        save->left = node;
        ++save->level;
        return save;
    }
    return node;
}

#define WordList_AddNode() (reinterpret_cast<WordNode*>(reinterpret_cast<char*>(buffer) + offset))
void WordList::AddBuffer() noexcept {
    buffers.push_back(std::make_unique<std::byte[]>(capacity));
    offset = 0;
    buffer = buffers.back().get();
}

int WordList::CompareNode(const WordNode& node, const char* word, unsigned length, unsigned sortKey) const noexcept {
    int difference = static_cast<int>(node.sortKey - sortKey);
    if (difference == 0 && (length > NP2_AUTOC_SORT_KEY_LENGTH || node.len > NP2_AUTOC_SORT_KEY_LENGTH || bIgnoreCase))
        difference = WL_strcmp(reinterpret_cast<const char*>(&node) + sizeof(WordNode), word);
    return difference;
}

void WordList::AddWord(const char* pWord, unsigned len) noexcept {
    WordNode* root = pListHead;
#if NP2_AUTOC_CACHE_SORT_KEY
    const unsigned sortKey = (iStartLen > NP2_AUTOC_SORT_KEY_LENGTH) ? 0 : WL_SortKeyFunc(pWord, len);
#endif
    if (root == nullptr) {
        WordNode* node = WordList_AddNode();
        char* word = WordNode_GetWord(node);
        memcpy(word, pWord, len);
#if NP2_AUTOC_USE_WORD_POINTER
        node->word = word;
#endif
#if NP2_AUTOC_CACHE_SORT_KEY
        node->sortKey = sortKey;
#endif
        node->len = len;
        node->level = 1;
        root = node;
    } else {
        WordNode* iter = root;
        WordNode* path[NP2_TREE_HEIGHT_LIMIT]{};
        int top = 0;
        int dir;

        // find a spot and save the path
        for (;;) {
            path[top++] = iter;
#if NP2_AUTOC_CACHE_SORT_KEY
            dir = CompareNode(*iter, pWord, len, sortKey);
#else
            dir = WL_strcmp(WordNode_GetWord(iter), pWord);
#endif
            if (dir == 0) {
                return;
            }
            dir = dir < 0;
            if (iter->link[dir] == nullptr) {
                break;
            }
            iter = iter->link[dir];
        }

        if (capacity < offset + len + 1 + sizeof(WordNode)) {
            capacity <<= 1;
            AddBuffer();
        }

        WordNode* node = WordList_AddNode();
        char* word = WordNode_GetWord(node);
        memcpy(word, pWord, len);
#if NP2_AUTOC_USE_WORD_POINTER
        node->word = word;
#endif
#if NP2_AUTOC_CACHE_SORT_KEY
        node->sortKey = sortKey;
#endif
        node->len = len;
        node->level = 1;
        iter->link[dir] = node;

        // walk back and rebalance
        while (--top >= 0) {
            // which child?
            if (top != 0) {
                dir = path[top - 1]->right == path[top];
            }
            path[top] = SplitTree(SkewTree(path[top]));
            // fix the parent
            if (top != 0) {
                path[top - 1]->link[dir] = path[top];
            } else {
                root = path[top];
            }
        }
    }

    pListHead = root;
    nWordCount++;
    nTotalLen += len + 1;
    offset += NP2_align_up(len + 1 + sizeof(WordNode), alignof(WordNode));
}

std::unique_ptr<char[]> WordList::GetList() const {
    WordNode* root = pListHead;
    WordNode* path[NP2_TREE_HEIGHT_LIMIT]{};
    int top = 0;
    auto list = std::make_unique<char[]>(nTotalLen + 1); // additional separator
    char* buf = list.get();
    char* const pList = buf;

    while (root || top > 0) {
        if (root) {
            path[top++] = root;
            root = root->left;
        } else {
            root = path[--top];
            memcpy(buf, WordNode_GetWord(root), root->len);
            buf += root->len;
            *buf++ = '\n'; // the separator char
            root = root->right;
        }
    }
    // trim last separator char
    if (buf != pList) {
        *(--buf) = '\0';
    }
    return list;
}

void WordList::Init(const char* pRoot, unsigned iRootLen, bool ignoreCase) noexcept {
    pListHead = nullptr;
    nWordCount = 0;
    nTotalLen = 0;
    offset = 0;
    capacity = 0;
    pWordStart = pRoot;
    iStartLen = iRootLen;

    if (ignoreCase) {
        // use strcmp() instead of _stricmp() to keep all matched string instead of just first one
        WL_strcmp = strcmp;
        WL_strncmp = CompareNoCase;
#if NP2_AUTOC_CACHE_SORT_KEY
        WL_SortKeyFunc = WordList_SortKeyCase;
#endif
    } else {
        WL_strcmp = strcmp;
        WL_strncmp = strncmp;
#if NP2_AUTOC_CACHE_SORT_KEY
        WL_SortKeyFunc = WordList_SortKey;
#endif
    }
#if NP2_AUTOC_CACHE_SORT_KEY
    startSortKey = WL_SortKeyFunc(pRoot, iRootLen);
    bIgnoreCase = ignoreCase;
#endif

    capacity = NP2_AUTOC_INIT_BUFFER_SIZE;
    AddBuffer();
}

void WordList::UpdateRoot(const char* pRoot, unsigned iRootLen) noexcept {
    pWordStart = pRoot;
    iStartLen = iRootLen;
#if NP2_AUTOC_CACHE_SORT_KEY
    startSortKey = WL_SortKeyFunc(pRoot, iRootLen);
#endif
}

bool WordList::StartsWith(const char* pWord) const noexcept {
#if NP2_AUTOC_CACHE_SORT_KEY
    if (iStartLen <= NP2_AUTOC_SORT_KEY_LENGTH) {
        return startSortKey == WL_SortKeyFunc(pWord, iStartLen);
    }
#endif
    return WL_strncmp(pWordStart, pWord, iStartLen) == 0;
}

static constexpr bool WordList_IsSeparator(uint8_t ch) noexcept {
#if defined(_WIN64)
    // directly complied into bit test
#if defined(__clang__) || defined(__GNUC__)
    if (ch > ';') {
        return ch == '^';
    }
    const uint64_t mask = UINT32_MAX | (UINT64_C(1) << ' ') | (UINT64_C(1) << '(') | (UINT64_C(1) << ')') |
                          (UINT64_C(1) << ',') |
                          (UINT64_C(1) << '.')
                          //| (UINT64_C(1) << ':')
                          | (UINT64_C(1) << ';');
    return (mask >> ch) & true;
#else
    return ch <= ' ' || ch == '(' || ch == ')' || ch == ',' ||
           ch == '.'
           //|| ch == ':'
           || ch == ';' || ch == '^';
#endif
#else
    if (ch <= ' ') {
        return true;
    }
    ch -= '(';
    if (ch > ';' - '(') {
        return ch == '^' - '(';
    }
    const uint32_t mask = (1 << ('(' - '(')) | (1 << (')' - '(')) | (1 << (',' - '(')) |
                          (1 << ('.' - '('))
                          //| (1 << (':' - '('))
                          | (1 << (';' - '('));
    return (mask >> ch) & true;
#endif
}

void WordList::AddListEx(const char* pList) noexcept {
    // StopWatch watch;
    // watch.Start();
    char word[NP2_AUTOC_WORD_BUFFER_SIZE];
    const unsigned iRootLen = iStartLen;
    unsigned len = 0;
    bool ok = false;
    while (true) {
        uint8_t ch;
        const char* sub = pList;
        do {
            ch = *sub++;
        } while (!WordList_IsSeparator(ch));

        unsigned lenSub = static_cast<unsigned>(sub - pList - 1);
        // Prefix markers can use the suffix reserve. Restore the word limit
        // before subtracting, copying another segment, or appending "()".
        len = std::min<unsigned>(len, NP2_AUTOC_MAX_WORD_LENGTH);
        lenSub = std::min<unsigned>(NP2_AUTOC_MAX_WORD_LENGTH - len, lenSub);
        memcpy(word + len, pList, lenSub);
        len += lenSub;
        pList = sub;
        if (len >= iRootLen) {
            if (ch == '(') {
                word[len++] = '(';
                word[len++] = ')';
            }
            word[len] = '\0';
            if (ok || StartsWith(word)) {
                AddWord(word, len);
                ok = ch == '.';
            }
        }
        if (ch == '\0') {
            break;
        }
        if (ch == '^') {
            // ^ is used for prefix match in lexer (see scintilla/lexlib/WordList.cxx)
            word[len++] = ' ';
        } else if (!ok && ch != '.') {
            len = 0;
        } else {
            word[len++] = '.';
        }
    }

    // watch.Stop();
    // const double duration = watch.Get();
    // printf("%s duration=%.6f\n", __func__, duration);
}

void WordList::AddSubWord(char* pWord, unsigned wordLength, unsigned iRootLen) noexcept {
    /*
    when pRoot is 'b', split 'bugprone-branch-clone' as following:
    1. first hyphen: 'bugprone-branch-clone' => 'bugprone', 'branch-clone'.
    2. second hyphen: 'bugprone-branch-clone' => 'bugprone-branch'; 'branch-clone' => 'branch'.
    */

    const char* words[8];
    unsigned starts[8];
    unsigned count = 0;

    for (unsigned i = 0; i < wordLength - 1; i++) {
        const char ch = pWord[i];
        if (ch != '.' && ch != '-' && ch != ':') continue;
        if (i >= iRootLen) {
            pWord[i] = '\0';
            AddWord(pWord, i);
            for (unsigned j = 0; j < count; j++) {
                const unsigned subLen = i - starts[j];
                if (subLen >= iRootLen) {
                    AddWord(words[j], subLen);
                }
            }
            pWord[i] = ch;
        }
        if (ch != '.' && (pWord[i + 1] == '>' || pWord[i + 1] == ':')) {
            ++i;
        }

        const unsigned subLen = wordLength - (i + 1);
        const char* pSubRoot = pWord + i + 1;
        if (subLen >= iRootLen && StartsWith(pSubRoot)) {
            AddWord(pSubRoot, subLen);
            if (count < std::size(words)) {
                words[count] = pSubRoot;
                starts[count] = i + 1;
                ++count;
            }
        }
    }
}

static constexpr bool IsCppCommentStyle(int style) noexcept {
    return style == SCE_C_COMMENT || style == SCE_C_COMMENTLINE || style == SCE_C_COMMENTDOC ||
           style == SCE_C_COMMENTLINEDOC || style == SCE_C_COMMENTDOC_TAG || style == SCE_C_COMMENTDOC_TAG_XML;
}

static constexpr bool IsSpecialStart(int ch) noexcept {
    return ch == ':' || ch == '.' || ch == '#' || ch == '@' || ch == '<' || ch == '\\' || ch == '/' || ch == '-' ||
           ch == '>' || ch == '$' || ch == '%';
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
    bool IsDocWordChar(unsigned ch) const { return CompletionStyleContains(styles_.wordCharacters, ch); }
    static bool IsDefaultWordChar(unsigned ch) {
        static constexpr uint32_t mask[8] = {0,           0x03ff0000U, 0x87fffffeU, 0x07fffffeU,
                                             0xffffffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU};
        return BitTestEx(mask, ch);
    }
    bool IsRawStringStyle(unsigned style) const { return CompletionStyleContains(styles_.rawStrings, style); }
    bool IsCommentStyle(unsigned style) const { return CompletionStyleContains(styles_.comments, style); }
    bool IsAutoCompletionWordCharacter(unsigned ch) {
        return ch < 0x80 ? IsDocWordChar(ch)
                         : Call(Message::GetCharacterClass, ch) == static_cast<int>(CharacterClass::word);
    }
    void AddList(WordList& words, const SyntaxDefinition& syntax, size_t index) {
        if (index < syntax.keywords.size()) words.AddList(syntax.keywords[index].text);
    }
    const NP2_KEYWORDS* ExtraWords() const {
        if (definition_.lexer == SCLEX_CPP) return &kwDoxyDoc;
        if (definition_.language == SyntaxLanguage::Xsd) return &kwXmlSchema;
        if (definition_.language == SyntaxLanguage::Xslt) return &kwXmlStylesheet;
        if (definition_.language == SyntaxLanguage::Plist) return &kwPList;
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
            if (definition_.original.lexerAttributes & LexerAttr_PrintfFormatSpecifier) {
                return !(stylePrev == definition_.original.operatorStyle ||
                         stylePrev == definition_.original.operatorStyle2);
            }
            return false;
        }

        if (style != 0 && definition_.original.escapeCharacterStyle) {
            if (stylePrev != definition_.original.escapeCharacterStyle) {
                if (definition_.lexer != SCLEX_PHPSCRIPT || definition_.lexer != SCLEX_HTML ||
                    !(stylePrev == (SCE_JS_ESCAPECHAR + 40) || stylePrev == (SCE_CSS_ESCAPECHAR + 90))) {
                    return false;
                }
            }
        } else if (!punctuation) {
            // legacy lexer without escape character highlighting
            if (!IsEscapeCharacter(ch)) {
                return false;
            }
        }

        if (!IsRawStringStyle(stylePrev)) {
            int chPrev2 = 0;
            const Sci_Position before2 = Call(Message::PositionBefore, before);
            if (before2 + 1 == before) {
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
        const Sci_Position before = wordEnd;
        const int ch = Call(Message::GetCharAt, wordEnd);
        Sci_Position width = 0;
        int chNext = Call(Message::GetCharacterAndWidth, wordEnd + 1, &width);
        if ((ch == '-' && chNext == '>') || (ch == ':' && chNext == ':')) {
            chNext = Call(Message::GetCharacterAndWidth, wordEnd + 2, &width);
            if (IsAutoCompletionWordCharacter(chNext)) {
                wordEnd += 2;
            }
        } else if (ch == '.' || (ch == '-' && style == Call(Message::GetStyleIndexAt, wordEnd))) {
            if (IsAutoCompletionWordCharacter(chNext)) {
                ++wordEnd;
            }
        }
        if (wordEnd == before) return wordEnd;

        while (wordEnd < iDocLen && (chNext < 0x80 && !IsDefaultWordChar(chNext))) {
            wordEnd += width;
            chNext = Call(Message::GetCharacterAndWidth, wordEnd, &width);
            if (!IsAutoCompletionWordCharacter(chNext)) {
                break;
            }
        }

        wordEnd = Call(Message::WordEndPosition, wordEnd, true);
        return wordEnd;
    }

    std::pair<Sci_Position, bool> ExtendDocumentWord(Sci_Position iPosFind, Sci_Position wordEnd, Sci_Position iDocLen,
                                                     int style) noexcept {
        // find all word after '::', '->', '.' and '-'
        bool bSubWord = false;
        while (wordEnd < iDocLen) {
            const int ch = Call(Message::GetCharAt, wordEnd);
            if (!(ch == ':' || ch == '.' || ch == '-')) {
                if (ch == '!' && definition_.lexer == SCLEX_RUST && style == SCE_RUST_MACRO) {
                    // macro: println!()
                    ++wordEnd;
                }
                break;
            }

            const auto before = wordEnd;
            wordEnd = MemberWordEnd(wordEnd, iDocLen, style);
            if (wordEnd == before) break;
            if (wordEnd - iPosFind > NP2_AUTOC_MAX_WORD_LENGTH) {
                wordEnd = before;
                break;
            }
            bSubWord = true;
        }

        return {wordEnd, bSubWord};
    }

    void SkipDocumentPrefix(char*& pWord, int& wordLength, Sci_Position iPosFind, int style, char prefix) noexcept {
        const Sci_Position before = Call(Message::PositionBefore, iPosFind);
        if (before + 1 == iPosFind) {
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

    void CollectDocumentWord(WordList& pWList, Sci_Position iPosFind, Sci_Position& wordEnd, int style, char prefix,
                             bool bSubWord) noexcept {
        const int iRootLen = pWList.iStartLen;
        if (wordEnd - iPosFind < iRootLen) return;
        char wordBuf[NP2_AUTOC_WORD_BUFFER_SIZE];
        char* pWord = wordBuf;
        const Sci_TextRangeFull tr = {{iPosFind, std::min(iPosFind + NP2_AUTOC_MAX_WORD_LENGTH, wordEnd)}, pWord};
        int wordLength = static_cast<int>(Call(Message::GetTextRangeFull, 0, &tr));

        SkipDocumentPrefix(pWord, wordLength, iPosFind, style, prefix);
        // if (definition_.lexer == SCLEX_PHPSCRIPT && wordLength >= 2 && pWord[0] == '$' && pWord[1] ==
        // '$') { 	pWord++;
        //	--wordLength;
        // }
        while (wordLength > 0 &&
               (pWord[wordLength - 1] == '-' || pWord[wordLength - 1] == ':' || pWord[wordLength - 1] == '.')) {
            --wordLength;
            pWord[wordLength] = '\0';
        }

        if (wordLength >= iRootLen && (pWord[0] != ':' || pWord[1] == ':') && pWList.StartsWith(pWord)) {
            AppendDocumentSuffix(pWord, wordLength, wordEnd, style);

            if (wordLength >= iRootLen) {
                pWord[wordLength] = '\0';
                pWList.AddWord(pWord, wordLength);
                if (bSubWord) {
                    pWList.AddSubWord(pWord, wordLength, iRootLen);
                }
            }
        }
    }

    void AutoC_AddDocWord(WordList& pWList, const std::array<uint32_t, 8>& ignoredStyleMask, bool bIgnoreCase,
                          char prefix) noexcept {
        const char* const pRoot = pWList.pWordStart;
        const int iRootLen = pWList.iStartLen;

        CharBuffer<> pFind(iRootLen + 2);
        pFind[0] = prefix;
        memcpy(pFind.data() + (prefix != '\0'), pRoot, iRootLen);
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

    void AutoC_AddKeyword(WordList& words, int currentStyle) {
        auto add = [&](std::span<const SyntaxKeywords> groups) {
            for (const auto& group : groups)
                if (!(group.attributes & SyntaxKeywordAttribute::NoAutoComp)) words.AddList(group.text);
        };
        add(definition_.keywords);
        if (const auto* extra = ExtraWords();
            extra && !(definition_.lexer == SCLEX_CPP && !IsCppCommentStyle(currentStyle)))
            for (const char* group : *extra)
                words.AddList(group);
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

    AddWordResult AddCssSpecialWord(WordList& words, int currentStyle, int previousStyle, int ch, int previous) {
        currentStyle -= css_style(SCE_CSS_DEFAULT);
        previousStyle -= css_style(SCE_CSS_DEFAULT);
        if (ch == '@' && currentStyle == SCE_CSS_DEFAULT) {
            words.AddList(cssKeywords[1]);
            return AddWordResult_IgnoreLexer;
        }
        if (ch == ':' && (currentStyle == SCE_CSS_DEFAULT || currentStyle == SCE_CSS_OPERATOR)) {
            if (previous == ':') {
                words.AddList(cssKeywords[3]);
                return AddWordResult_IgnoreLexer;
            }
            if (previousStyle != SCE_CSS_PROPERTY && previousStyle != SCE_CSS_UNKNOWN_PROPERTY) {
                words.AddList(cssKeywords[2]);
                return AddWordResult_IgnoreLexer;
            }
        }
        if ((ch == '$' || ch == '@' || ch == '#') && IsDocWordChar(ch)) return AddWordResult_IgnoreLexer;
        return AddWordResult_None;
    }
    AddWordResult AddJavaScriptSpecialWord(WordList& words, int currentStyle, int ch) {
        currentStyle -= js_style(SCE_JS_DEFAULT);
        if (ch == '@' && currentStyle >= SCE_JS_COMMENTLINE && currentStyle <= SCE_JS_TASKMARKER) {
            AddList(words, SyntaxFor(SyntaxLanguage::JavaScript), 10);
            return AddWordResult_Finish;
        }
        if ((ch == '$' || ch == '@' || ch == '#') && IsDocWordChar(ch)) return AddWordResult_IgnoreLexer;
        return AddWordResult_None;
    }
    AddWordResult AddCppSpecialWord(WordList& words, int currentStyle, int ch) {
        if (IsCppCommentStyle(currentStyle) && (ch == '@' || ch == '\\')) {
            for (const char* group : kwDoxyDoc)
                words.AddList(group);
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

    AddWordResult AddJavaScriptCommentWord(WordList& words, int currentStyle, int ch) {
        const auto scheme = SyntaxSettingsLanguage(definition_.language);
        if ((ch == '@' || (ch == '<' && scheme == SyntaxLanguage::TypeScript)) && currentStyle >= SCE_JS_COMMENTLINE &&
            currentStyle <= SCE_JS_TASKMARKER) {
            AddList(words, definition_, 10);
            if (scheme != SyntaxLanguage::JavaScript) AddList(words, SyntaxFor(SyntaxLanguage::JavaScript), 10);
            return AddWordResult_Finish;
        }
        return AddWordResult_None;
    }
    AddWordResult AddMarkupTagWords(WordList& words, int ch, int previous) {
        if (ch != '<' && !(previous == '<' && ch == '/')) return AddWordResult_None;
        if (definition_.lexer == SCLEX_MARKDOWN) {
            AddList(words, SyntaxFor(SyntaxLanguage::Html), 0);
        } else {
            AddList(words, definition_, 0);
            if (const auto* extra = ExtraWords()) words.AddList((*extra)[0]);
        }
        return AddWordResult_IgnoreLexer;
    }
    AddWordResult AddSchemeSpecialWord(WordList& words, int currentStyle, int ch, int previous) {
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
    AddWordResult AutoC_AddSpecWord(WordList& words, int currentStyle, int previousStyle, int ch, int previous) {
        if ((ch == '\\' || (previous == '\\' && (ch == '^' || ch == ':'))) && settings_.latexInputMethod)
            words.AddList(ch == ':' ? EmojiInputSequenceString : LaTeXInputSequenceString);
        if (definition_.lexer == SCLEX_HTML && currentStyle >= css_style(SCE_CSS_DEFAULT) &&
            currentStyle < SCE_PHP_DEFAULT)
            return AddCssSpecialWord(words, currentStyle, previousStyle, ch, previous);
        if (definition_.lexer == SCLEX_HTML && currentStyle >= js_style(SCE_JS_DEFAULT) &&
            currentStyle < css_style(SCE_CSS_DEFAULT))
            return AddJavaScriptSpecialWord(words, currentStyle, ch);
        const auto result = AddSchemeSpecialWord(words, currentStyle, ch, previous);
        if (result != AddWordResult_None) return result;
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

                const int ch = Call(Message::GetCharAt, before);
                if (!IsDocWordChar(ch) || IsSpecialStartChar(ch, '\0')) {
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
        chPrev = Call(Message::GetCharAt, before);
        const Sci_Position before2 = Call(Message::PositionBefore, before);
        if (before2 >= iLineStartPos && before2 + 1 == before) chPrev2 = Call(Message::GetCharAt, before2);
        // Word after an escape character or format specifier.
        if (chPrev == '%' || (chPrev == definition_.original.escapeCharacterStart &&
                              (definition_.original.lexerAttributes & LexerAttr_EscapePunctuation) == 0)) {
            const int style = Call(Message::GetStyleIndexAt, iStartWordPos);
            if (IsEscapeCharOrFormatSpecifier(before, ch, chPrev, style, false)) {
                ++iStartWordPos;
                ch = Call(Message::GetCharAt, iStartWordPos);
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
                const int chNext = UnsafeLower(Call(Message::GetCharAt, iStartWordPos + 1));
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
        if ((definition_.original.lexerAttributes & LexerAttr_CppPreprocessor) &&
            (chPrev == '#' || IsASpaceOrTab(chPrev))) {
            if (const auto previous = ReadPreprocessorPrefix(iStartWordPos, iLineStartPos, chPrev)) ch = *previous;
            chPrev = 0;
        } else if (IsSpecialStartChar(chPrev, chPrev2)) {
            ch = chPrev;
            chPrev = chPrev2;
        }
    }

    bool CompletionScopeEnabled(int iCurrentStyle, bool bScanWordsInDocument) {
        if (definition_.lexer == SCLEX_NULL || (definition_.original.lexerAttributes & LexerAttr_PlainTextFile)) {
            if (!bScanWordsInDocument || !(settings_.completeScope & AutoCompleteScope_PlainText) ||
                !(settings_.scanWordScope & AutoCompleteScope_PlainText)) {
                return false;
            }
        } else {
            if ((!(settings_.completeScope & AutoCompleteScope_Commont) && IsCommentStyle(iCurrentStyle)) ||
                (!(settings_.completeScope & AutoCompleteScope_String) &&
                 CompletionStyleContains(styles_.strings, iCurrentStyle)) ||
                (!(settings_.completeScope & AutoCompleteScope_PlainText) &&
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
            if (!(settings_.scanWordScope & AutoCompleteScope_Commont) && !IsCommentStyle(iCurrentStyle)) {
                for (unsigned i = 0; i < 8; i++) {
                    ignoredStyleMask[i] |= styles_.comments[i];
                }
            }
            if (!(settings_.scanWordScope & AutoCompleteScope_String) &&
                !CompletionStyleContains(styles_.strings, iCurrentStyle)) {
                for (unsigned i = 0; i < 8; i++) {
                    ignoredStyleMask[i] |= styles_.strings[i];
                }
            }
            if (!(settings_.scanWordScope & AutoCompleteScope_PlainText) &&
                !CompletionStyleContains(styles_.plainText, iCurrentStyle)) {
                for (unsigned i = 0; i < 8; i++) {
                    ignoredStyleMask[i] |= styles_.plainText[i];
                }
            }
        }
        return ignoredStyleMask;
    }

    bool RetrySubRoot(WordList& words) {
        if (words.iStartLen == 0) return false;
        const char* root = strpbrk(words.pWordStart, ":.#@<\\/->$%");
        if (!root) return false;
        while (IsSpecialStart(*root))
            ++root;
        if (!*root) return false;
        words.UpdateRoot(root, static_cast<unsigned>(strlen(root)));
        return true;
    }

    void CollectCandidates(WordList& pWList, int iCurrentStyle, bool bIgnoreLexer, bool bIgnoreDoc, char prefix) {
        const bool bScanWordsInDocument = (settings_.completeOption & AutoCompletionOption_ScanWordsInDocument) != 0;
        const bool bIgnoreCase = pWList.bIgnoreCase;
        bool retry = CompletionScopeEnabled(iCurrentStyle, bScanWordsInDocument);
        const auto ignoredStyleMask =
            definition_.lexer == SCLEX_NULL || (definition_.original.lexerAttributes & LexerAttr_PlainTextFile)
                ? std::array<uint32_t, 8>{}
                : IgnoredDocumentStyles(iCurrentStyle, retry && bScanWordsInDocument);

        while (retry) {
            if (!bIgnoreLexer) {
                // keywords
                AutoC_AddKeyword(pWList, iCurrentStyle);
            }
            if (bScanWordsInDocument) {
                if (!bIgnoreDoc || pWList.nWordCount == 0) {
                    AutoC_AddDocWord(pWList, ignoredStyleMask, bIgnoreCase, prefix);
                }
                if (prefix && pWList.nWordCount == 0) {
                    prefix = '\0';
                    AutoC_AddDocWord(pWList, ignoredStyleMask, bIgnoreCase, prefix);
                }
            }

            retry = false;
            if (pWList.nWordCount == 0 && RetrySubRoot(pWList)) {
                retry = true;
                bIgnoreLexer = false;
                bIgnoreDoc = false;
                prefix = 0;
            }
        }
    }

    void ShowCandidates(const WordList& pWList, bool bIgnoreCase, bool autoInsert) {
        const auto pList = pWList.GetList();
        const auto fillUps = FillUps();
        Call(Message::AutoCSetOptions, SC_AUTOCOMPLETE_FIXED_SIZE);
        // Call(Message::AutoCSetOrder, SC_ORDER_PRESORTED); // pre-sorted is default
        Call(Message::AutoCSetIgnoreCase, bIgnoreCase); // case sensitivity
        Call(Message::AutoCSetCaseInsensitiveBehaviour, bIgnoreCase);
        // Call(Message::AutoCSetSeparator, '\n');
        // Call(Message::AutoCSetTypeSeparator, '\t');
        Call(Message::AutoCSetFillUps, 0, fillUps.c_str());
        // Call(Message::AutoCSetDropRestOfWord, true); // delete orginal text: pRoot
        Call(Message::AutoCSetMaxHeight,
             std::min(pWList.nWordCount, static_cast<unsigned>(settings_.visibleItemCount))); // visible rows
        Call(Message::AutoCSetCancelAtStart, false); // don't cancel the list when deleting character
        Call(Message::AutoCSetChooseSingle, autoInsert);
        Call(Message::AutoCSetSeparator, '\n');
        Call(Message::AutoCShow, pWList.iStartLen, pList.get());
    }

    struct SpecialSources {
        bool ignoreLexer;
        bool ignoreDocument = false;
        char prefix = 0;
    };
    SpecialSources PrepareSpecialSources(WordList& pWList, int iCurrentStyle, Sci_Position iStartWordPos, int ch,
                                         int chPrev, bool ignoreLexer) {
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

#if 0
	StopWatch watch;
	watch.Start();
#endif

        bool bIgnoreLexer = (settings_.completeOption & AutoCompletionOption_OnlyWordsInDocument) != 0 ||
                            (pRoot[0] >= '0' && pRoot[0] <= '9'); // number
        const bool bIgnoreCase = bIgnoreLexer || settings_.ignoreCase;
        WordList pWList;
        pWList.Init(pRoot.data(), iRootLen, bIgnoreCase);

        int iCurrentStyle = Call(Message::GetStyleIndexAt, iCurrentPos);
        const auto sources = PrepareSpecialSources(pWList, iCurrentStyle, iStartWordPos, ch, chPrev, bIgnoreLexer);

        if (iCurrentStyle == 0) {
            // word not yet styled, or is plain text
            iCurrentStyle = Call(Message::GetStyleIndexAt, iStartWordPos);
        }

        CollectCandidates(pWList, iCurrentStyle, sources.ignoreLexer, sources.ignoreDocument, sources.prefix);
        iRootLen = static_cast<int>(pWList.iStartLen);

#if 0
	watch.Stop();
	const double elapsed = watch.Get();
	printf("Notepad4 AddDocWord(%u, %u): %.6f\n", pWList.nWordCount, pWList.nTotalLen, elapsed);
#endif

        const bool bShow = pWList.nWordCount > 0 &&
                           !(pWList.nWordCount == 1 && pWList.nTotalLen == static_cast<unsigned>(iRootLen + 1));

        const bool updated =
            state.previousItemCount == 0 ||
            (condition == WordCompletionCondition::CharacterAdded &&
             state.previousItemCount - pWList.nWordCount > static_cast<unsigned>(settings_.visibleItemCount)) ||
            (condition == WordCompletionCondition::CharacterDeleted && state.previousItemCount < pWList.nWordCount);
        if (bShow && updated) {
            state.previousItemCount = pWList.nWordCount;
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
    editor.SetCompletionSettings(settings);
    if (!Collector(editor, definition, settings).Run(autoInsert, condition, state)) {
        state.previousItemCount = 0;
        if (condition != WordCompletionCondition::Normal) editor.Send(Message::AutoCCancel);
    }
}
