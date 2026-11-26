#pragma once

#include "editor/completion_styles.h"
#include "editor/word_completion.h"
#include "settings/original_settings_constants.h"
#include <functional>
#include <utility>

class EditorControl;
namespace Scintilla {
struct NotificationData;
}

// Owns the original automatic-typing policy and its per-editor completion state.
// The core forwards notifications; all edits use the editor's public message path.
class TypingAssistance final {
public:
    explicit TypingAssistance(EditorControl& editor) : editor_(editor) {}

    void SetCompletionSettings(const WordCompletionSettings& settings) { completionSettings_ = settings; }
    const WordCompletionSettings& CompletionSettings() const { return completionSettings_; }
    WordCompletionState& CompletionState() { return completionState_; }
    void SetCompletionHandler(std::function<void(WordCompletionCondition)> handler);
    void ResetCompletionState() noexcept { completionState_ = {}; }
    void SetStyles(CompletionStyles styles) { styles_ = std::move(styles); }
    void SetAutoInsertMask(int mask);
    bool AutoIndentEnabled() const { return autoIndent_; }
    void SetAutoIndent(bool enabled) { autoIndent_ = enabled; }

    // True means selection acceptance consumed the notification. Other events
    // continue through the core's ordinary UI-state notification path.
    bool HandleNotification(const Scintilla::NotificationData& notification);

private:
    void CharacterAdded(const Scintilla::NotificationData& notification);
    void CompleteTagOrMemberAccess();
    bool ShouldCompleteTypedWord(const Scintilla::NotificationData& notification);
    void InsertMatchingCharacter(int ch);
    // False retains the original failed-tag member-arrow completion path.
    bool CloseTag();
    void IndentNewLine();

    EditorControl& editor_;
    WordCompletionSettings completionSettings_;
    WordCompletionState completionState_;
    std::function<void(WordCompletionCondition)> completionHandler_;
    CompletionStyles styles_;
    int autoInsertMask_ = AutoInsertMask_Default;
    bool autoIndent_ = true;
};
