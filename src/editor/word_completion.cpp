// Adapted from Notepad4 89b8b36 EditAutoC.cpp: WordList and EditCompleteWordCore.
// Copyright and BSD 3-Clause license: licenses/Notepad4.txt.
#include "editor/word_completion.h"
#include "editor/editor_control.h"
#include "editor/completion_styles.h"
#include "editor/platform/core_headers.h"
#include "syntax/syntax_catalog.h"
#include <Scintilla.h>
#include <SciLexer.h>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <type_traits>
using Scintilla::Message;
namespace {
using Sci_Position = Scintilla::Position;
using Sci_PositionU = uintptr_t;
using Sci_Line = intptr_t;
using Sci_TextRangeFull = Scintilla::TextRangeFull;
using Sci_TextToFindFull = Scintilla::TextToFindFull;
using Scintilla::CharacterClass;
void* Allocate(size_t bytes) { return std::calloc(bytes, 1); }
int CompareNoCase(const char* left, const char* right, size_t count) {
    for (size_t i=0; i<count; ++i) {
        const auto a=static_cast<unsigned char>(left[i]), b=static_cast<unsigned char>(right[i]);
        const auto lower=[](unsigned char ch) { return ch>='A' && ch<='Z' ? ch+'a'-'A' : ch; };
        const int difference=lower(a)-lower(b);
        if (difference || !a || !b) return difference;
    }
    return 0;
}
bool StrNotEmpty(const char* text) { return text && *text; }
bool IsASpaceOrTab(int ch) { return ch==' ' || ch=='	'; }
bool IsAlpha(int ch) { return (ch>='a'&&ch<='z') || (ch>='A'&&ch<='Z'); }
int UnsafeLower(int ch) { return ch>='A'&&ch<='Z' ? ch+'a'-'A' : ch; }
bool BitTestEx(const uint32_t* mask, unsigned value) { return value<256 && (mask[value>>5] & (1U<<(value&31))); }
constexpr unsigned LexerAttr_PrintfFormatSpecifier=1U<<6;
constexpr unsigned LexerAttr_CppPreprocessor=1U<<8;
constexpr unsigned LexerAttr_EscapePunctuation=1U<<10;
constexpr unsigned LexerAttr_PlainTextFile=1U<<11;
constexpr int AutoCompletionOption_ScanWordsInDocument=4;
constexpr int AutoCompletionOption_OnlyWordsInDocument=8;
constexpr int AutoCompleteScope_Commont=1;
constexpr int AutoCompleteScope_String=2;
constexpr int AutoCompleteScope_PlainText=4;
#define NP2_AUTOC_CACHE_SORT_KEY	1
#define NP2_AUTOC_USE_WORD_POINTER	0	// used for debug
#define NP2_AUTOC_MAX_WORD_LENGTH	(128 - 3 - 1)	// SP + '(' + ')' + '\0'
#define NP2_AUTOC_WORD_BUFFER_SIZE	128
#define NP2_AUTOC_INIT_BUFFER_SIZE	(4096)

// optimization for small string
template <size_t StackSize = 32>
class CharBuffer {
	char *ptr;
	char buffer[StackSize];
public:
	explicit CharBuffer(size_t size) noexcept {
		if (size <= StackSize) {
			ptr = buffer;
			memset(buffer, 0, StackSize);
		} else {
			ptr = static_cast<char *>(Allocate(size));
		}
	}
	char *data() noexcept {
		return ptr;
	}
	char& operator[](size_t index) noexcept {
		return ptr[index];
	}
	~CharBuffer() {
		if (ptr != buffer) {
			std::free(ptr);
		}
	}
};

// memory buffer
struct WordListBuffer {
	WordListBuffer *next;
};

struct WordNode;
struct WordList {
	int (*WL_strcmp)(const char*, const char*);
	int (*WL_strncmp)(const char*, const char*, size_t);
#if NP2_AUTOC_CACHE_SORT_KEY
	uint32_t (*WL_SortKeyFunc)(const void *, uint32_t);
#endif

	WordNode *pListHead;
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
	WordListBuffer *buffer;

	void Init(const char* pRoot, unsigned iRootLen, bool ignoreCase) noexcept;
	void Free() const noexcept;
	char *GetList() const noexcept;
	void AddBuffer() noexcept;
	void AddWord(const char* pWord, unsigned len) noexcept;
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
#define NP2_AUTOC_SORT_KEY_LENGTH	4

uint32_t WordList_SortKey(const void *pWord, uint32_t len) noexcept {
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
		high = bit_zero_high_u32(high, len*8);
	}
	high = bswap32(high);
#endif
	return high;
}

uint32_t WordList_SortKeyCase(const void *pWord, uint32_t len) noexcept {
#if 1
	uint32_t high = 0;
	const uint8_t *ptr = static_cast<const uint8_t *>(pWord);
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
		high <<= (NP2_AUTOC_SORT_KEY_LENGTH - len)*8;
	}

#else
	uint32_t high = loadle_u32(pWord);
	high |= 0x20202020U; // only works for ASCII letters
	if (len < NP2_AUTOC_SORT_KEY_LENGTH) {
		high = bit_zero_high_u32(high, len*8);
	}
	high = bswap32(high);
#endif
	return high;
}
#endif

// Tree
struct WordNode {
	union {
		WordNode *link[2];
		struct {
			WordNode *left;
			WordNode *right;
		};
	};
#if NP2_AUTOC_USE_WORD_POINTER
	char *word;
#endif
#if NP2_AUTOC_CACHE_SORT_KEY
	unsigned sortKey;
#endif
	unsigned len;
	unsigned level;
};

#define NP2_TREE_HEIGHT_LIMIT	32
// store word right after the node as most word are short.
#define WordNode_GetWord(node)		(reinterpret_cast<char *>(node) + sizeof(WordNode))
// TODO: since the tree is sorted, nodes greater than some level can be deleted to reduce total words.
// or only limit word count in WordList::GetList().

// Andersson Tree, source from https://www.eternallyconfuzzled.com/tuts/datastructures/jsw_tut_andersson.aspx
// see also https://en.wikipedia.org/wiki/AA_tree
#define aa_tree_skew(t) \
	if ((t)->level && (t)->left && (t)->level == (t)->left->level) {\
		WordNode *save = (t)->left;					\
		(t)->left = save->right;							\
		save->right = (t);									\
		(t) = save;											\
	}
#define aa_tree_split(t) \
	if ((t)->level && (t)->right && (t)->right->right && (t)->level == (t)->right->right->level) {\
		WordNode *save = (t)->right;					\
		(t)->right = save->left;							\
		save->left = (t);									\
		(t) = save;											\
		++(t)->level;										\
	}

#define WordList_AddNode()	(reinterpret_cast<WordNode *>(reinterpret_cast<char *>(buffer) + offset))
void WordList::AddBuffer() noexcept {
	WordListBuffer *block = static_cast<WordListBuffer *>(Allocate(capacity));
	block->next = buffer;
	offset = NP2_align_up(sizeof(WordListBuffer), alignof(WordNode));
	buffer = block;
}

void WordList::AddWord(const char* pWord, unsigned len) noexcept {
	WordNode *root = pListHead;
#if NP2_AUTOC_CACHE_SORT_KEY
	const unsigned sortKey = (iStartLen > NP2_AUTOC_SORT_KEY_LENGTH) ? 0 : WL_SortKeyFunc(pWord, len);
#endif
	if (root == nullptr) {
		WordNode *node = WordList_AddNode();
		char *word = WordNode_GetWord(node);
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
		WordNode *iter = root;
		WordNode *path[NP2_TREE_HEIGHT_LIMIT]{};
		int top = 0;
		int dir;

		// find a spot and save the path
		for (;;) {
			path[top++] = iter;
#if NP2_AUTOC_CACHE_SORT_KEY
			dir = static_cast<int>(iter->sortKey - sortKey);
			if (dir == 0 && (len > NP2_AUTOC_SORT_KEY_LENGTH || iter->len > NP2_AUTOC_SORT_KEY_LENGTH || bIgnoreCase)) {
				dir = WL_strcmp(WordNode_GetWord(iter), pWord);
			}
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

		WordNode *node = WordList_AddNode();
		char *word = WordNode_GetWord(node);
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
			aa_tree_skew(path[top]);
			aa_tree_split(path[top]);
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

void WordList::Free() const noexcept {
	WordListBuffer *block = buffer;
	while (block) {
		WordListBuffer * const next = block->next;
		std::free(block);
		block = next;
	}
}

char* WordList::GetList() const noexcept {
	WordNode *root = pListHead;
	WordNode *path[NP2_TREE_HEIGHT_LIMIT]{};
	int top = 0;
	char *buf = static_cast<char *>(Allocate(nTotalLen + 1));// additional separator
	char * const pList = buf;

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
	return pList;
}

void WordList::Init(const char* pRoot, unsigned iRootLen, bool ignoreCase) noexcept {
	memset(this, 0, sizeof(struct WordList));
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
	const uint64_t mask = unsigned32_MAX
		| (unsigned64_C(1) << ' ')
		| (unsigned64_C(1) << '(')
		| (unsigned64_C(1) << ')')
		| (unsigned64_C(1) << ',')
		| (unsigned64_C(1) << '.')
		//| (unsigned64_C(1) << ':')
		| (unsigned64_C(1) << ';');
	return (mask >> ch) & true;
#else
	return ch <= ' '
		|| ch == '('
		|| ch == ')'
		|| ch == ','
		|| ch == '.'
		//|| ch == ':'
		|| ch == ';'
		|| ch == '^';
#endif
#else
	if (ch <= ' ') {
		return true;
	}
	ch -= '(';
	if (ch > ';' - '(') {
		return ch == '^' - '(';
	}
	const uint32_t mask = (1 << ('(' - '('))
		| (1 << (')' - '('))
		| (1 << (',' - '('))
		| (1 << ('.' - '('))
		//| (1 << (':' - '('))
		| (1 << (';' - '('));
	return (mask >> ch) & true;
#endif
}

void WordList::AddListEx(const char* pList) noexcept {
	//StopWatch watch;
	//watch.Start();
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

	//watch.Stop();
	//const double duration = watch.Get();
	//printf("%s duration=%.6f\n", __func__, duration);
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
		if (ch == '.' || ch == '-' || ch == ':') {
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
}


static constexpr bool IsCppCommentStyle(int style) noexcept {
	return style == SCE_C_COMMENT
		|| style == SCE_C_COMMENTLINE
		|| style == SCE_C_COMMENTDOC
		|| style == SCE_C_COMMENTLINEDOC
		|| style == SCE_C_COMMENTDOC_TAG
		|| style == SCE_C_COMMENTDOC_TAG_XML;
}

static constexpr bool IsSpecialStart(int ch) noexcept {
	return ch == ':' || ch == '.' || ch == '#' || ch == '@'
		|| ch == '<' || ch == '\\' || ch == '/' || ch == '-'
		|| ch == '>' || ch == '$' || ch == '%';
}

static constexpr bool IsSpecialStartChar(int ch, int chPrev) noexcept {
	return (ch == '.')	// member
		|| (ch == '#')	// preprocessor
		|| (ch == '@') // Java/PHP/Doxygen Doc Tag
		// ObjC Keyword, Java Annotation, Python Decorator, Cobra Directive
		|| (ch == '<') // HTML/XML Tag, C# Doc Tag
		|| (ch == '\\')// Doxygen Doc Tag, LaTeX Command
		|| (ch == ':') // CSS pseudo class
		|| (ch == '$') // variable
		|| (ch == '`') // VHDL, Verilog directive
		|| (ch == '\'')// VHDL attribute
		|| (ch == '%') // SAS macro
		|| (chPrev == '\\' && (ch == '^' || ch == ':'))// LaTeX input, Emoji input
		// TODO: show emoji list after typing ':'.
		|| (chPrev == '<' && ch == '/')	// HTML/XML Close Tag
		|| (chPrev == '-' && ch == '>')	// member(C/C++/PHP)
		|| (chPrev == ':' && ch == ':');// namespace(C++), static member(C++/Java8/PHP)
}


#include "editor/completion_extra_words.h"
class Collector final {
    EditorControl& editor_;
    const SyntaxDefinition& definition_;
    const WordCompletionSettings& settings_;
    CompletionStyles styles_;
    template<class T> static intptr_t Parameter(T value) {
        if constexpr (std::is_pointer_v<T>) return reinterpret_cast<intptr_t>(value);
        else return static_cast<intptr_t>(value);
    }
    template<class A=int, class B=int> intptr_t Call(Message message, A first=0, B second=0) {
        return editor_.Send(message, static_cast<uintptr_t>(Parameter(first)), Parameter(second));
    }
    bool IsDocWordChar(unsigned ch) const { return CompletionStyleContains(styles_.wordCharacters,ch); }
    static bool IsDefaultWordChar(unsigned ch) {
        static constexpr uint32_t mask[8]={0,0x03ff0000U,0x87fffffeU,0x07fffffeU,0xffffffffU,0xffffffffU,0xffffffffU,0xffffffffU};
        return BitTestEx(mask,ch);
    }
    bool IsRawStringStyle(unsigned style) const { return CompletionStyleContains(styles_.rawStrings,style); }
    bool IsCommentStyle(unsigned style) const { return CompletionStyleContains(styles_.comments,style); }
    bool IsAutoCompletionWordCharacter(unsigned ch) {
        return ch<0x80 ? IsDocWordChar(ch) : Call(Message::GetCharacterClass,ch)==static_cast<int>(CharacterClass::word);
    }
    void AddList(WordList& words,const SyntaxDefinition& syntax,size_t index) {
        if(index<syntax.keywords.size()) words.AddList(syntax.keywords[index].text);
    }
    const NP2_KEYWORDS* ExtraWords() const {
        if(definition_.lexer==SCLEX_CPP) return &kwDoxyDoc;
        if(definition_.language==SyntaxLanguage::Xsd) return &kwXmlSchema;
        if(definition_.language==SyntaxLanguage::Xslt) return &kwXmlStylesheet;
        if(definition_.language==SyntaxLanguage::Plist) return &kwPList;
        return nullptr;
    }
static constexpr bool IsEscapeCharacter(int ch) noexcept {
	return ch == '0'	// '\0'
		|| ch == 'a'	// '\a'
		|| ch == 'b'	// '\b'
		|| ch == 'e'	// '\e', GNU extension
		|| ch == 'f'	// '\f'
		|| ch == 'n'	// '\n'
		|| ch == 'r'	// '\r'
		|| ch == 't'	// '\t'
		|| ch == 'v'	// '\v'
		// other
		|| ch == '$';	// PHP variable
	// x u U ignored as they need to be followed with multiple hex digits.
}

// https://en.wikipedia.org/wiki/Printf_format_string
static constexpr bool IsPrintfFormatSpecifier(int ch) noexcept {
	return IsAlpha(ch);
}

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
			return !(stylePrev == definition_.original.operatorStyle || stylePrev == definition_.original.operatorStyle2);
		}
		return false;
	}

	if (style != 0 && definition_.original.escapeCharacterStyle) {
		if (stylePrev != definition_.original.escapeCharacterStyle) {
			if (definition_.lexer != SCLEX_PHPSCRIPT || definition_.lexer != SCLEX_HTML
				|| !(stylePrev == (SCE_JS_ESCAPECHAR+40) || stylePrev == (SCE_CSS_ESCAPECHAR+90))) {
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

static inline bool NeedSpaceAfterKeyword(const char *word, Sci_Position length) noexcept {
	const char *p = strstr(
		" if for try using while elseif switch foreach synchronized "
		, word);
	return p != nullptr && p[-1] == ' ' && p[length] == ' ';
}

void AutoC_AddDocWord(WordList &pWList, const std::array<uint32_t,8>& ignoredStyleMask, bool bIgnoreCase, char prefix) noexcept {
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
	Sci_TextToFindFull ft = { { 0, iDocLen }, pFind.data(), static_cast<Sci_PositionU>(textLength), { 0, 0 } };

	Sci_Position iPosFind = Call(Message::FindTextFull, findFlag, &ft);
	const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(settings_.scanWordsTimeout);

	while (iPosFind >= 0 && iPosFind < iDocLen && std::chrono::steady_clock::now()<deadline) {
		Sci_Position wordEnd = iPosFind + iRootLen;
		const int style = Call(Message::GetStyleIndexAt, wordEnd - 1);
		wordEnd = ft.chrgText.cpMax;
		if (iPosFind != iCurrentPos && !CompletionStyleContains(ignoredStyleMask, style)) {
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

				const Sci_Position before = wordEnd;
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
				if (wordEnd == before) {
					break;
				}

				while (wordEnd < iDocLen && (chNext < 0x80 && !IsDefaultWordChar(chNext))) {
					wordEnd += width;
					chNext = Call(Message::GetCharacterAndWidth, wordEnd, &width);
					if (!IsAutoCompletionWordCharacter(chNext)) {
						break;
					}
				}

				wordEnd = Call(Message::WordEndPosition, wordEnd, true);
				if (wordEnd - iPosFind > NP2_AUTOC_MAX_WORD_LENGTH) {
					wordEnd = before;
					break;
				}
				bSubWord = true;
			}

			if (wordEnd - iPosFind >= iRootLen) {
				char wordBuf[NP2_AUTOC_WORD_BUFFER_SIZE];
				char *pWord = wordBuf;
				const Sci_TextRangeFull tr = { { iPosFind, std::min(iPosFind + NP2_AUTOC_MAX_WORD_LENGTH, wordEnd) }, pWord };
				int wordLength = static_cast<int>(Call(Message::GetTextRangeFull, 0, &tr));

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

				//if (definition_.lexer == SCLEX_PHPSCRIPT && wordLength >= 2 && pWord[0] == '$' && pWord[1] == '$') {
				//	pWord++;
				//	--wordLength;
				//}
				while (wordLength > 0 && (pWord[wordLength - 1] == '-' || pWord[wordLength - 1] == ':' || pWord[wordLength - 1] == '.')) {
					--wordLength;
					pWord[wordLength] = '\0';
				}

				if (wordLength >= iRootLen && (pWord[0] != ':' || pWord[1] == ':') && pWList.StartsWith(pWord)) {
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
					}
					else if (chWordEnd == '(') {
						if (space && NeedSpaceAfterKeyword(pWord, wordLength)) {
							pWord[wordLength++] = ' ';
						}

						pWord[wordLength++] = '(';
						pWord[wordLength++] = ')';
						wordEnd++;
					}

					if (wordLength >= iRootLen) {
						pWord[wordLength] = '\0';
						pWList.AddWord(pWord, wordLength);
						if (bSubWord) {
							pWList.AddSubWord(pWord, wordLength, iRootLen);
						}
					}
				}
			}
		}

		ft.chrg.cpMin = wordEnd;
		iPosFind = Call(Message::FindTextFull, findFlag, &ft);
	}
}


    void AutoC_AddKeyword(WordList& words,int currentStyle) {
        auto add=[&](const SyntaxDefinition& syntax) {
            for(const auto& group:syntax.keywords)
                if(!(group.attributes & SyntaxKeywordAttribute::NoAutoComp)) words.AddList(group.text);
        };
        add(definition_);
        if(const auto* extra=ExtraWords();extra && !(definition_.lexer==SCLEX_CPP && !IsCppCommentStyle(currentStyle)))
            for(const char* group:*extra) words.AddList(group);
        if(SyntaxSettingsLanguage(definition_.language)==SyntaxLanguage::TypeScript) add(SyntaxFor(SyntaxLanguage::JavaScript));
        if(definition_.lexer==SCLEX_HTML && currentStyle>=40 && currentStyle<90) add(SyntaxFor(SyntaxLanguage::JavaScript));
        if(definition_.lexer==SCLEX_JAVASCRIPT) {
            const auto& html=SyntaxFor(SyntaxLanguage::Html);
            for(const auto index:{0,7,8}) AddList(words,html,index);
        }
    }
    enum AddWordResult { AddWordResult_None, AddWordResult_Finish, AddWordResult_IgnoreLexer };
    AddWordResult AutoC_AddSpecWord(WordList& words,int currentStyle,int,int ch,int previous) {
        const auto scheme=SyntaxSettingsLanguage(definition_.language);
        if(definition_.lexer==SCLEX_BASH && ch=='$') {
            AddList(words,definition_,2); return AddWordResult_IgnoreLexer;
        }
        if(definition_.lexer==SCLEX_CPP) {
            if(IsCppCommentStyle(currentStyle) && (ch=='@'||ch=='\\')) {
                for(const char* group:kwDoxyDoc) words.AddList(group);
                return AddWordResult_Finish;
            }
            if(currentStyle==SCE_C_DEFAULT) {
                if(ch=='#') { AddList(words,definition_,2); return AddWordResult_Finish; }
                if(ch=='@') { AddList(words,definition_,3); return AddWordResult_IgnoreLexer; }
            }
        }
        if(definition_.lexer==SCLEX_HTML && (ch=='<' || (previous=='<'&&ch=='/'))) {
            AddList(words,definition_,0);
            if(const auto* extra=ExtraWords()) words.AddList((*extra)[0]);
            return AddWordResult_IgnoreLexer;
        }
        if(definition_.lexer==SCLEX_JAVASCRIPT && (ch=='@' || (ch=='<'&&scheme==SyntaxLanguage::TypeScript)) &&
           currentStyle>=SCE_JS_COMMENTLINE && currentStyle<=SCE_JS_TASKMARKER) {
            AddList(words,definition_,10);
            if(scheme!=SyntaxLanguage::JavaScript) AddList(words,SyntaxFor(SyntaxLanguage::JavaScript),10);
            return AddWordResult_Finish;
        }
        if(definition_.lexer==SCLEX_PYTHON && ch=='@' && currentStyle==SCE_PY_DEFAULT) {
            AddList(words,definition_,7); return AddWordResult_IgnoreLexer;
        }
        if((ch=='$'||ch=='@'||ch=='#')&&IsDocWordChar(ch)) return AddWordResult_IgnoreLexer;
        return AddWordResult_None;
    }
public:
    Collector(EditorControl& editor,const SyntaxDefinition& definition,const WordCompletionSettings& settings)
        :editor_(editor),definition_(definition),settings_(settings),styles_(CompletionStylesFor(definition)){}
    std::string FillUps() const {
        std::string text;
        if(settings_.fillUpMask&4) text+=' ';
        if(settings_.fillUpMask&8)
            for(const wxUniChar ch:settings_.fillUpPunctuation)
                if(ch.GetValue()<128 && std::ispunct(static_cast<unsigned char>(ch.GetValue()))) text+=static_cast<char>(ch.GetValue());
        return text;
    }
bool Run(bool autoInsert) {
	const Sci_Position iCurrentPos = Call(Message::GetCurrentPos);
	const Sci_Line iLine = Call(Message::LineFromPosition, iCurrentPos);
	const Sci_Position iLineStartPos = Call(Message::PositionFromLine, iLine);

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
	if (iStartWordPos == iCurrentPos) {
		return false;
	}

	// beginning of word
	int ch = Call(Message::GetCharAt, iStartWordPos);

	int chPrev = '\0';
	int chPrev2 = '\0';
	if (ch < 0x80 && iStartWordPos > iLineStartPos) {
		const Sci_Position before = Call(Message::PositionBefore, iStartWordPos);
		if (before + 1 == iStartWordPos) {
			chPrev = Call(Message::GetCharAt, before);
			const Sci_Position before2 = Call(Message::PositionBefore, before);
			if (before2 >= iLineStartPos && before2 + 1 == before) {
				chPrev2 = Call(Message::GetCharAt, before2);
			}
			// word after escape character or format specifier
			if (chPrev == '%' || (chPrev == definition_.original.escapeCharacterStart && (definition_.original.lexerAttributes & LexerAttr_EscapePunctuation) == 0)) {
				const int style = Call(Message::GetStyleIndexAt, iStartWordPos);
				if (IsEscapeCharOrFormatSpecifier(before, ch, chPrev, style, false)) {
					++iStartWordPos;
					ch = Call(Message::GetCharAt, iStartWordPos);
					chPrev = '\0';
				}
			}
		}
	}

	int iRootLen = settings_.minWordLength;
	if (ch >= '0' && ch <= '9') {
		if (settings_.minNumberLength <= 0) { // ignore number
			return false;
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

	if (iCurrentPos - iStartWordPos < iRootLen) {
		return false;
	}

	// preprocessor like: # space preprocessor
	if ((definition_.original.lexerAttributes & LexerAttr_CppPreprocessor) && (chPrev == '#' || IsASpaceOrTab(chPrev))) {
		Sci_Position before = iStartWordPos - 1;
		if (chPrev != '#') {
			while (before >= iLineStartPos) {
				chPrev = Call(Message::GetCharAt, before);
				if (!IsASpaceOrTab(chPrev)) {
					break;
				}
				--before;
			}
		}
		if (chPrev == '#') {
			if (before > iLineStartPos) {
				--before;
				while (before >= iLineStartPos && IsASpaceOrTab(Call(Message::GetCharAt, before))) {
					--before;
				}
				if (before >= iLineStartPos) {
					chPrev = '\0';
				}
			}
			ch = chPrev;
		}
		chPrev = '\0';
	} else if (IsSpecialStartChar(chPrev, chPrev2)) {
		ch = chPrev;
		chPrev = chPrev2;
	}

	CharBuffer<> pRoot(iCurrentPos - iStartWordPos + 1);
	const Sci_TextRangeFull tr = { { iStartWordPos, iCurrentPos }, pRoot.data() };
	Call(Message::GetTextRangeFull, 0, &tr);
	iRootLen = static_cast<int>(strlen(pRoot.data()));

#if 0
	StopWatch watch;
	watch.Start();
#endif

	bool bIgnoreLexer = (settings_.completeOption & AutoCompletionOption_OnlyWordsInDocument) != 0
		|| (pRoot[0] >= '0' && pRoot[0] <= '9'); // number
	const bool bIgnoreCase = bIgnoreLexer || settings_.ignoreCase;
	WordList pWList;
	pWList.Init(pRoot.data(), iRootLen, bIgnoreCase);
	bool bIgnoreDoc = false;
	char prefix = '\0';

	int iCurrentStyle = Call(Message::GetStyleIndexAt, iCurrentPos);
	if (!bIgnoreLexer && IsSpecialStartChar(ch, chPrev)) {
		int iPrevStyle = 0;
		if (ch == ':' && chPrev != ':') {
			const Sci_Position iPos = Call(Message::WordStartPosition, iStartWordPos - 1, false);
			iPrevStyle = Call(Message::GetStyleIndexAt, iPos);
		}

		const AddWordResult result = AutoC_AddSpecWord(pWList, iCurrentStyle, iPrevStyle, ch, chPrev);
		if (result == AddWordResult_Finish) {
			bIgnoreLexer = true;
			bIgnoreDoc = true;
		} else if (result == AddWordResult_IgnoreLexer) {
			bIgnoreLexer = true;
			// HTML/XML Tag
			if (ch == '/' || ch == '>') {
				ch = '<';
			}
			prefix = static_cast<char>(ch);
		}
	}

	if (iCurrentStyle == 0) {
		// word not yet styled, or is plain text
		iCurrentStyle = Call(Message::GetStyleIndexAt, iStartWordPos);
	}

	bool retry = true;
	std::array<uint32_t,8> ignoredStyleMask{};
	const bool bScanWordsInDocument = (settings_.completeOption & AutoCompletionOption_ScanWordsInDocument) != 0;
	if (definition_.original.lexerAttributes & LexerAttr_PlainTextFile) {
		if (!bScanWordsInDocument
			|| !(settings_.completeScope & AutoCompleteScope_PlainText)
			|| !(settings_.scanWordScope & AutoCompleteScope_PlainText)) {
			retry = false;
		}
	} else {
		if ((!(settings_.completeScope & AutoCompleteScope_Commont) && IsCommentStyle(iCurrentStyle))
			|| (!(settings_.completeScope & AutoCompleteScope_String) && CompletionStyleContains(styles_.strings, iCurrentStyle))
			|| (!(settings_.completeScope & AutoCompleteScope_PlainText) && CompletionStyleContains(styles_.plainText, iCurrentStyle))) {
			retry = false;
		}
		if (retry && bScanWordsInDocument) {
			ignoredStyleMask=styles_.ignoredWords;
			if (!(settings_.scanWordScope & AutoCompleteScope_Commont) && !IsCommentStyle(iCurrentStyle)) {
				for (unsigned i = 0; i < 8; i++) {
					ignoredStyleMask[i] |= styles_.comments[i];
				}
			}
			if (!(settings_.scanWordScope & AutoCompleteScope_String) && !CompletionStyleContains(styles_.strings, iCurrentStyle)) {
				for (unsigned i = 0; i < 8; i++) {
					ignoredStyleMask[i] |= styles_.strings[i];
				}
			}
			if (!(settings_.scanWordScope & AutoCompleteScope_PlainText) && !CompletionStyleContains(styles_.plainText, iCurrentStyle)) {
				for (unsigned i = 0; i < 8; i++) {
					ignoredStyleMask[i] |= styles_.plainText[i];
				}
			}
		}
	}

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
		if (pWList.nWordCount == 0 && iRootLen != 0) {
			const char *pSubRoot = strpbrk(pWList.pWordStart, ":.#@<\\/->$%");
			if (pSubRoot) {
				while (IsSpecialStart(*pSubRoot)) {
					pSubRoot++;
				}
				if (*pSubRoot) {
					iRootLen = static_cast<int>(strlen(pSubRoot));
					pWList.UpdateRoot(pSubRoot, iRootLen);
					retry = true;
					bIgnoreLexer = false;
					bIgnoreDoc = false;
					prefix = '\0';
				}
			}
		}
	}

#if 0
	watch.Stop();
	const double elapsed = watch.Get();
	printf("Notepad4 AddDocWord(%u, %u): %.6f\n", pWList.nWordCount, pWList.nTotalLen, elapsed);
#endif

	const bool bShow = pWList.nWordCount > 0 && !(pWList.nWordCount == 1 && pWList.nTotalLen == static_cast<unsigned>(iRootLen + 1));

	if (bShow) {
		char *pList = pWList.GetList();
        const auto fillUps=FillUps();
		Call(Message::AutoCSetOptions, SC_AUTOCOMPLETE_FIXED_SIZE);
		//Call(Message::AutoCSetOrder, SC_ORDER_PRESORTED); // pre-sorted is default
		Call(Message::AutoCSetIgnoreCase, bIgnoreCase); // case sensitivity
		Call(Message::AutoCSetCaseInsensitiveBehaviour, bIgnoreCase);
		//Call(Message::AutoCSetSeparator, '\n');
		//Call(Message::AutoCSetTypeSeparator, '\t');
		Call(Message::AutoCSetFillUps, 0, fillUps.c_str());
		//Call(Message::AutoCSetDropRestOfWord, true); // delete orginal text: pRoot
		Call(Message::AutoCSetMaxHeight, std::min(pWList.nWordCount, static_cast<unsigned>(settings_.visibleItemCount))); // visible rows
		Call(Message::AutoCSetCancelAtStart, false); // don't cancel the list when deleting character
		Call(Message::AutoCSetChooseSingle, autoInsert);
		Call(Message::AutoCSetSeparator, '\n');
        Call(Message::AutoCShow, pWList.iStartLen, pList);
		std::free(pList);
	}

	pWList.Free();
	return bShow;
}

};
}
void CompleteWord(EditorControl& editor,const SyntaxDefinition& definition,const WordCompletionSettings& settings,bool autoInsert) {
 Collector(editor,definition,settings).Run(autoInsert);
}
