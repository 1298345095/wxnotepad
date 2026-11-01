#include "editor/indentation_settings.h"
#include "editor/editor_control.h"

OriginalTabs IndentationSettings::Effective(SyntaxLanguage language) const {
    const auto& scheme = preferences_.schemeTabs[static_cast<size_t>(language)];
    OriginalTabs tabs = scheme.useGlobal ? preferences_.globalTabs : scheme;
    if (fileWidths_) {
        tabs.tabWidth = fileWidths_->tabWidth;
        tabs.indentWidth = fileWidths_->indentWidth;
    }
    if (fileSpaces_) tabs.tabsAsSpaces = *fileSpaces_;
    return tabs;
}

void IndentationSettings::Apply(EditorControl& editor, SyntaxLanguage language) {
    using Scintilla::Message;
    const auto tabs = Effective(language);
    editor.SetIndentationWidths(tabs.tabWidth, tabs.indentWidth);
    editor.Send(Message::SetUseTabs, !tabs.tabsAsSpaces);
    editor.Send(Message::SetTabIndents, preferences_.tabIndents);
    editor.Send(Message::SetBackSpaceUnIndents, preferences_.backspaceUnindents);
    editor.Send(Message::ScrollCaret);
}

void IndentationSettings::Reset(EditorControl& editor, SyntaxLanguage language) {
    SetFileSettings(std::nullopt);
    Apply(editor, language);
}

void IndentationSettings::ToggleSpaces(EditorControl& editor, SyntaxLanguage language) {
    fileSpaces_ = !Effective(language).tabsAsSpaces;
    Apply(editor, language);
}

void IndentationSettings::SetFileSettings(std::optional<OriginalTabs> tabs) {
    fileWidths_ = tabs;
    fileSpaces_ = tabs ? std::optional<bool>(tabs->tabsAsSpaces) : std::nullopt;
}
