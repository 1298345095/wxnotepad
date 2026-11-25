#pragma once

#include "settings/original_settings_constants.h"
#include <ScintillaTypes.h>
#include <optional>

class EditorControl;
struct OriginalSettings;

// The dialog edits only wrapping choices, independently of the settings store.
struct WordWrapOptions {
    bool enabled = false;
    Scintilla::Wrap mode = Scintilla::Wrap::Auto;
    WrapContinuationIndent indent = WrapContinuationIndent::None;
    bool showSymbols = false;
    int symbols = 2;
    bool selectSubLine = false;
    bool highlightSubLine = false;
};

// Owns window-level wrapping preferences while a file can override the live mode.
// EditorControl remains the owner of the preferred nonzero mode and layout.
class WordWrapState final {
public:
    explicit WordWrapState(EditorControl& editor) : editor_(editor) {}
    void Restore(const OriginalSettings& preferences);
    void Capture(OriginalSettings& preferences, std::optional<bool> fileOverride) const;
    void RememberBeforeLoad(std::optional<bool> fileOverride);
    void ApplyFileOverride(std::optional<bool> fileOverride);
    void Toggle();
    void ToggleSymbols();
    bool SymbolsVisible() const { return showSymbols_; }
    WordWrapOptions Options() const;
    void ApplyOptions(const WordWrapOptions& options, OriginalSettings& preferences);

private:
    EditorControl& editor_;
    bool enabled_ = false;
    bool showSymbols_ = false;
    int symbols_ = 2;
    void ApplySymbols();
};
