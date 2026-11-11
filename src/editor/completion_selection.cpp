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
    std::string text(notification.text);
    const auto braces = text.find_first_of("([{<");
    const auto caret = editor.Send(Message::GetCurrentPos);
    intptr_t offset;
    bool closeBrace = false;
    if (braces != std::string::npos) {
        auto position = caret;
        auto ch = editor.Send(Message::GetCharAt, position);
        while (ch == ' ' || ch == '\t')
            ch = editor.Send(Message::GetCharAt, ++position);
        offset = braces + 1;
        const char brace = text[braces];
        if (brace == ch) {
            text.resize(braces);
            offset += position - caret;
        } else {
            const int closing = brace == '(' ? ')' : brace + 2;
            if (ch == closing) {
                if (editor.Send(Message::BraceMatch, position, editor.Send(Message::PositionBefore, caret)) < 0)
                    text.resize(braces + 1);
            } else
                closeBrace = brace != '<' && text[braces + 1] == closing;
        }
    } else
        offset = text.size();
    const auto newPosition = notification.position + offset;
    editor.Send(Message::BeginUndoAction);
    editor.Send(Message::SetSel, notification.position, caret);
    editor.Send(Message::ReplaceSel, 0, reinterpret_cast<intptr_t>(text.c_str()));
    editor.Send(Message::SetSel, newPosition, newPosition);
    if (closeBrace && editor.IsOpenBraceMatched(newPosition - 1, newPosition + 1)) editor.Send(Message::Clear);
    editor.Send(Message::EndUndoAction);
    editor.Send(Message::AutoCCancel);
    if (method == CompletionMethods::Tab && settings.latexInputMethod) CompleteLatex(editor);
}
