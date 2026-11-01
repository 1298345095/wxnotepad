#pragma once

#include "settings/settings_store.h"
#include "document/file_variables.h"
#include <optional>

class EditorControl;

// Notepad4 EditFileVars::Apply and EditDetectIndentation precedence.
class IndentationSettings final {
public:
    explicit IndentationSettings(OriginalSettings& preferences) : preferences_(preferences) {}
    void Apply(EditorControl& editor, SyntaxLanguage language);
    void Reset(EditorControl& editor, SyntaxLanguage language);
    void Load(EditorControl& editor, SyntaxLanguage language, const OriginalFileVariables& variables);
    void ToggleSpaces(EditorControl& editor, SyntaxLanguage language);
    bool HasFileSettings() const { return fileWidths_.has_value() || fileSpaces_.has_value(); }
    void SetFileSettings(std::optional<OriginalTabs> tabs);
    OriginalTabs Effective(SyntaxLanguage language) const;
    bool TabIndents() const { return fileTabIndents_.value_or(preferences_.tabIndents); }
    void SetTabIndents(bool value) { fileTabIndents_ = preferences_.tabIndents = value; }

private:
    OriginalSettings& preferences_;
    std::optional<OriginalTabs> fileWidths_;
    std::optional<bool> fileSpaces_;
    std::optional<bool> fileTabIndents_;
};
