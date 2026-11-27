#include "editor/typing_assistance.h"
#include "editor/editor_control.h"
#include <Scintilla.h>
#include <ScintillaStructures.h>
#include <ScintillaTypes.h>
#include <ILexer.h>

using Scintilla::Message;

void TypingAssistance::SetCompletionHandler(std::function<void(WordCompletionCondition)> handler) {
    editor_.Send(Message::AutoCCancel);
    ResetCompletionState();
    completionHandler_ = std::move(handler);
}

bool TypingAssistance::HandleNotification(const Scintilla::NotificationData& notification) {
    using enum Scintilla::Notification;
    if (notification.nmhdr.code == AutoCSelection) {
        AcceptWordCompletion(editor_, completionSettings_, notification);
        return true;
    }
    if (notification.nmhdr.code == AutoCCharDeleted && completionHandler_)
        completionHandler_(WordCompletionCondition::CharacterDeleted);
    if (notification.nmhdr.code == AutoCCompleted || notification.nmhdr.code == AutoCCancelled) ResetCompletionState();
    if (notification.nmhdr.code == CharAdded && !notification.listType) CharacterAdded(notification);
    return false;
}

// Original Notepad4.cpp SCN_CHARADDED: listType excludes tentative input
// and multiple-selection typing. Bracket and tag paths return before words.
void TypingAssistance::CharacterAdded(const Scintilla::NotificationData& notification) {
    const int ch = notification.ch;
    if (ch == '\r' || ch == '\n') {
        if (autoIndent_ &&
            (editor_.Send(Message::GetEOLMode) != std::to_underlying(Scintilla::EndOfLine::CrLf) || ch != '\n'))
            IndentNewLine();
        return;
    }
    if (ch == '>') {
        CompleteTagOrMemberAccess();
        return;
    }
    if (InsertMatchingCharacter(ch)) return;
    if (ShouldCompleteTypedWord(notification))
        completionHandler_(editor_.Send(Message::AutoCActive) ? WordCompletionCondition::CharacterAdded
                                                              : WordCompletionCondition::Normal);
}

void TypingAssistance::CompleteTagOrMemberAccess() {
    if (!(completionSettings_.completeOption & (WordCompletionOption::CloseTags | WordCompletionOption::CompleteWord)))
        return;
    if (CloseTag()) return;
    if ((completionSettings_.completeOption & WordCompletionOption::CompleteWord) &&
        editor_.Send(Message::GetCharAt, editor_.Send(Message::GetCurrentPos) - 2) == '-' && completionHandler_)
        completionHandler_(WordCompletionCondition::Normal);
}

bool TypingAssistance::ShouldCompleteTypedWord(const Scintilla::NotificationData& notification) {
    const int ch = notification.ch;
    return (completionSettings_.completeOption & WordCompletionOption::CompleteWord) &&
           (notification.characterSource == Scintilla::CharacterSource::DirectInput ||
            (ch < 0x80 && !(completionSettings_.completeOption & WordCompletionOption::EnglishIMEModeOnly))) &&
           (ch < 0x80 ? CompletionStyleContains(styles_.wordCharacters, ch)
                      : editor_.Send(Message::GetCharacterClass, ch) ==
                            std::to_underlying(Scintilla::CharacterClass::word)) &&
           completionHandler_;
}
