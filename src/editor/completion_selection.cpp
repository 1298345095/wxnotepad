// Original Notepad4 89b8b36 Notepad4.cpp SCN_AUTOCSELECTION and
// ScintillaWin.cpp HandleLaTeXTabCompletion; licenses/Notepad4.txt.
#include "editor/editor_control.h"
#include "editor/platform/core_headers.h"
#include <LaTeXInput.h>
namespace {
using Scintilla::Message;
bool CompleteLatex(EditorControl& editor) {
    if (editor.Send(Message::AutoCActive) || editor.Send(Message::GetSelections) > 1 ||
        !editor.Send(Message::GetSelectionEmpty) || editor.Send(Message::GetReadOnly))
        return false;
    const auto caret = editor.Send(Message::GetCurrentPos);
    if (caret <= MinLaTeXInputSequenceLength) return false;
    auto position = caret - 1;
    char buffer[MaxLaTeXInputBufferLength];
    char* ptr = buffer + sizeof(buffer) - 1;
    *ptr = 0;
    char ch;
    do {
        ch = static_cast<char>(editor.Send(Message::GetCharAt, position));
        if (!IsLaTeXInputSequenceChar(ch)) break;
        --position;
        --ptr;
        *ptr = ch;
    } while (position >= 0 && ptr != buffer);
    if (ch != 92) return false;
    const auto length = buffer + sizeof(buffer) - 1 - ptr;
    const uint32_t character = GetLaTeXInputUnicodeCharacter(ptr, length);
    if (!character) return false;
    const char16_t utf16[] = {static_cast<char16_t>(character & 0xffff), static_cast<char16_t>(character >> 16), 0};
    const auto text = wxString(reinterpret_cast<const char*>(utf16), wxMBConvUTF16LE(), 2 * (1 + (utf16[1] != 0)))
                          .ToStdString(wxConvUTF8);
    editor.Send(Message::SetTargetStart, position);
    editor.Send(Message::SetTargetEnd, caret);
    editor.Send(Message::ReplaceTarget, text.size(), reinterpret_cast<intptr_t>(text.c_str()));
    editor.Send(Message::SetSel, position + text.size(), position + text.size());
    return true;
}
struct CompletionText {
    std::string text;
    intptr_t offset;
    bool closeBrace = false;
};
CompletionText PrepareCompletionText(EditorControl& editor, const char* text, intptr_t caret) {
    CompletionText result{text, 0};
    const auto braces = result.text.find_first_of("([{<");
    if (braces == std::string::npos) {
        result.offset = result.text.size();
        return result;
    }
    auto position = caret;
    auto ch = editor.Send(Message::GetCharAt, position);
    while (ch == ' ' || ch == '\t')
        ch = editor.Send(Message::GetCharAt, ++position);
    result.offset = braces + 1;
    const char brace = result.text[braces];
    if (brace == ch) {
        result.text.resize(braces);
        result.offset += position - caret;
        return result;
    }
    const int closing = brace == '(' ? ')' : brace + 2;
    if (ch == closing) {
        if (editor.Send(Message::BraceMatch, position, editor.Send(Message::PositionBefore, caret)) < 0)
            result.text.resize(braces + 1);
    } else {
        result.closeBrace = brace != '<' && result.text[braces + 1] == closing;
    }
    return result;
}
} // namespace
void AcceptWordCompletion(EditorControl& editor, const WordCompletionSettings& settings,
                          const Scintilla::NotificationData& notification) {
    using Scintilla::CompletionMethods;
    const auto method = notification.listCompletionMethod;
    if ((method == CompletionMethods::Newline && !(settings.fillUpMask & CompletionFillUp::Enter)) ||
        (method == CompletionMethods::Tab && !(settings.fillUpMask & CompletionFillUp::Tab))) {
        editor.Send(Message::AutoCCancel);
        if (method == CompletionMethods::Newline)
            editor.Send(Message::NewLine);
        else if (!settings.latexInputMethod || !CompleteLatex(editor))
            editor.Send(Message::Tab);
        return;
    }
    const auto caret = editor.Send(Message::GetCurrentPos);
    const auto completion = PrepareCompletionText(editor, notification.text, caret);
    const auto newPosition = notification.position + completion.offset;
    editor.Send(Message::BeginUndoAction);
    editor.Send(Message::SetSel, notification.position, caret);
    editor.Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(completion.text.c_str()));
    editor.Send(Message::SetSel, newPosition, newPosition);
    if (completion.closeBrace && editor.IsOpenBraceMatched(newPosition - 1, newPosition + 1))
        editor.Send(Message::Clear);
    editor.Send(Message::EndUndoAction);
    editor.Send(Message::AutoCCancel);
    if (method == CompletionMethods::Tab && settings.latexInputMethod) CompleteLatex(editor);
}
