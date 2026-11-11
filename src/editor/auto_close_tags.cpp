// Adapted from Notepad4 89b8b36 EditAutoC.cpp::EditAutoCloseXMLTag.
#include "editor/editor_control.h"
#include <SciLexer.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace {
constexpr intptr_t TagLookBackBytes = 511;
bool IsTagCharacter(unsigned ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == ':' || ch == '_' ||
           ch == '-' || ch == '.';
}
bool IsVoidTag(std::string tag) {
    // Original Helpers.h::IsHtmlVoidTag. The caller excludes one-letter <p>.
    constexpr std::string_view VoidTags = " area base basefont br col command embed frame hr img input isindex keygen "
                                          "link meta p param source track wbr ";
    for (auto& ch : tag)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return VoidTags.find(" " + tag + " ") != std::string_view::npos;
}
} // namespace

bool EditorControl::AutoCloseTag() {
    using Scintilla::Message;
    const auto position = Send(Message::GetCurrentPos);
    const auto start = std::max<intptr_t>(0, position - TagLookBackBytes);
    const auto size = position - start;
    if (size < 3 || !(completionSettings_.completeOption & WordCompletionOption::CloseTags)) return false;
    const auto& styles = completionStyles_;
    int currentStyle = static_cast<int>(Send(Message::GetStyleIndexAt, position));
    if (styles.angleBracketGeneric &&
        (currentStyle == 0 || currentStyle == styles.operatorStyle || currentStyle == styles.operatorStyle2))
        return false;
    if (styles.lexer == SCLEX_CPP) {
        auto lineStart = Send(Message::PositionFromLine, Send(Message::LineFromPosition, position));
        int ch = 0;
        while (lineStart < position) {
            ch = static_cast<int>(Send(Message::GetCharAt, lineStart));
            if (ch != ' ' && ch != '\t') break;
            ++lineStart;
        }
        if (ch == '#' && Send(Message::GetStyleIndexAt, lineStart) == SCE_C_PREPROCESSOR) return false;
    }
    std::array<char, TagLookBackBytes + 1> text{};
    for (intptr_t index = 0; index < size; ++index)
        text[index] = static_cast<char>(Send(Message::GetCharAt, start + index));
    if (text[size - 2] == '/') return false;
    intptr_t opening = size - 2;
    while (opening > 0 && text[opening] != '<' && text[opening] != '>')
        --opening;
    if (text[opening] != '<') return false;
    const auto style = Send(Message::GetStyleIndexAt, start + opening);
    if (style &&
        (style == styles.operatorStyle || style == styles.operatorStyle2 ||
         (styles.lexer == SCLEX_HTML && (style == js_style(SCE_JS_OPERATOR) || style == js_style(SCE_JS_OPERATOR2)))))
        return true;
    std::string tag;
    for (++opening; IsTagCharacter(static_cast<unsigned char>(text[opening])); ++opening)
        tag += text[opening];
    if (tag.empty()) return false;
    if (tag.size() > 1 && !styles.xml && IsVoidTag(tag)) return true;
    const auto insertion = "</" + tag + ">";
    Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(insertion.c_str()));
    Send(Message::SetSel, position, position);
    return true;
}
