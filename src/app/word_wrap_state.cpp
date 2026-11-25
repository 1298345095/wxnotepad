#include "app/word_wrap_state.h"
#include "editor/editor_control.h"
#include "settings/original_settings.h"
#include <utility>

void WordWrapState::Restore(const OriginalSettings& preferences) {
    enabled_ = preferences.wordWrap;
    showSymbols_ = preferences.showWordWrapSymbols;
    symbols_ = preferences.wordWrapSymbols;
    ApplySymbols();
    editor_.SetWordWrapMode(static_cast<Scintilla::Wrap>(preferences.wordWrapMode));
    if (!enabled_) editor_.SetWordWrapMode(Scintilla::Wrap::None);
    editor_.SetWordWrapIndent(preferences.wordWrapIndent);
    editor_.Send(Scintilla::Message::SetMarginOptions, preferences.wordWrapSelectSubLine);
}

void WordWrapState::Capture(OriginalSettings& preferences, std::optional<bool> fileOverride) const {
    const auto options = Options();
    preferences.wordWrap = fileOverride.has_value() ? enabled_ : editor_.Send(Scintilla::Message::GetWrapMode) != 0;
    preferences.wordWrapMode = std::to_underlying(options.mode);
    preferences.wordWrapIndent = options.indent;
    preferences.wordWrapSelectSubLine = options.selectSubLine;
    preferences.showWordWrapSymbols = showSymbols_;
    preferences.wordWrapSymbols = symbols_;
}

void WordWrapState::RememberBeforeLoad(std::optional<bool> fileOverride) {
    if (!fileOverride.has_value()) enabled_ = editor_.Send(Scintilla::Message::GetWrapMode) != 0;
}

void WordWrapState::ApplyFileOverride(std::optional<bool> fileOverride) {
    editor_.SetWordWrapMode(fileOverride.value_or(enabled_) ? editor_.PreferredWordWrapMode() : Scintilla::Wrap::None);
}

void WordWrapState::Toggle() {
    editor_.ToggleWordWrap();
    enabled_ = editor_.Send(Scintilla::Message::GetWrapMode) != 0;
}

void WordWrapState::ToggleSymbols() {
    showSymbols_ = !showSymbols_;
    ApplySymbols();
}

WordWrapOptions WordWrapState::Options() const {
    return {enabled_,
            editor_.PreferredWordWrapMode(),
            editor_.WordWrapIndent(),
            showSymbols_,
            symbols_,
            editor_.Send(Scintilla::Message::GetMarginOptions) != 0,
            editor_.Send(Scintilla::Message::GetCaretLineHighlightSubLine) != 0};
}

void WordWrapState::ApplyOptions(const WordWrapOptions& options, OriginalSettings& preferences) {
    preferences.wordWrap = enabled_ = options.enabled;
    preferences.wordWrapMode = std::to_underlying(options.mode);
    preferences.wordWrapIndent = options.indent;
    preferences.wordWrapSymbols = symbols_ = options.symbols;
    preferences.showWordWrapSymbols = showSymbols_ = options.showSymbols;
    preferences.wordWrapSelectSubLine = options.selectSubLine;
    preferences.highlightCurrentLine =
        std::to_underlying(editor_.GetCurrentLineHighlightMode()) + 10 * static_cast<int>(options.highlightSubLine);
    editor_.SetWordWrapMode(options.mode);
    if (!enabled_) editor_.SetWordWrapMode(Scintilla::Wrap::None);
    editor_.SetWordWrapIndent(options.indent);
    editor_.Send(Scintilla::Message::SetMarginOptions, options.selectSubLine);
    editor_.Send(Scintilla::Message::SetCaretLineHighlightSubLine, options.highlightSubLine);
    ApplySymbols();
}

void WordWrapState::ApplySymbols() {
    // Notepad4.cpp::SetWrapVisualFlags at 89b8b36; see licenses/Notepad4.txt.
    using Scintilla::Message;
    if (!showSymbols_) {
        editor_.Send(Message::SetWrapVisualFlags, 0);
        return;
    }
    if (symbols_ == 0) symbols_ = 2;
    uintptr_t flags = 0;
    uintptr_t location = 0;
    switch (symbols_ % 10) {
    case 1:
        location |= static_cast<uintptr_t>(std::to_underlying(Scintilla::WrapVisualLocation::EndByText));
        [[fallthrough]];
    case 2:
        flags |= static_cast<uintptr_t>(std::to_underlying(Scintilla::WrapVisualFlag::End));
        break;
    default:
        break;
    }
    switch (symbols_ / 10) {
    case 1:
        location |= static_cast<uintptr_t>(std::to_underlying(Scintilla::WrapVisualLocation::StartByText));
        [[fallthrough]];
    case 2:
        flags |= static_cast<uintptr_t>(std::to_underlying(Scintilla::WrapVisualFlag::Start));
        break;
    case 3:
        flags |= static_cast<uintptr_t>(std::to_underlying(Scintilla::WrapVisualFlag::Margin));
        if (!editor_.LineNumbersVisible()) editor_.SetLineNumbersVisible(true);
        break;
    default:
        break;
    }
    editor_.Send(Message::SetWrapVisualFlagsLocation, location);
    editor_.Send(Message::SetWrapVisualFlags, flags);
}
