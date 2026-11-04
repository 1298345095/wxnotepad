#pragma once

#include "settings/settings_store.h"
#include <optional>

class EditorControl;

// Notepad4 EditFileVars::Apply precedence, without detection or file variables.
class IndentationSettings final {
public:
    explicit IndentationSettings(OriginalSettings& preferences) : preferences_(preferences) {}
    void Apply(EditorControl& editor, SyntaxLanguage language);
    void Reset(EditorControl& editor, SyntaxLanguage language);
    void ToggleSpaces(EditorControl& editor, SyntaxLanguage language);
    bool HasFileSettings() const { return fileWidths_.has_value() || fileSpaces_.has_value(); }
    void SetFileSettings(std::optional<OriginalTabs> tabs);
    OriginalTabs Effective(SyntaxLanguage language) const;

private:
    OriginalSettings& preferences_;
    std::optional<OriginalTabs> fileWidths_;
    std::optional<bool> fileSpaces_;
};
