#include "editor/indentation_settings.h"
#include "editor/editor_control.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string_view>

namespace {
// Scalar port of Notepad4 89b8b36 Edit.cpp::EditDetectIndentation (SciTE-derived).
// Keep its 1 MiB sample, delta counts, Javadoc correction and lowest-index ties.
int DetectIndentation(std::string_view text) {
    text = text.substr(0, 1024 * 1024);
    std::array<unsigned, 10> counts{};
    int previousIndent = 0;
    int previousWidth = 0;
    size_t position = 0;
    while (position < text.size()) {
        switch (text[position++]) {
        case '\t':
            ++counts[9];
            break;
        case ' ': {
            int indent = 1;
            while (position < text.size() && text[position] == ' ') {
                ++position;
                ++indent;
            }
            if ((indent & 1) && position < text.size() && text[position] == '*') {
                if (--indent == 0) break;
            }
            if (indent != previousIndent) {
                const int delta = std::abs(indent - previousIndent);
                previousIndent = indent;
                previousWidth = delta <= 8 ? std::min(delta, indent) : 0;
            }
            ++counts[previousWidth];
            break;
        }
        case '\r':
        case '\n':
            continue;
        default:
            previousIndent = 0;
            break;
        }
        while (position < text.size()) {
            const char ch = text[position++];
            if (ch == '\r' || ch == '\n') break;
        }
    }
    return static_cast<int>(std::max_element(counts.begin(), counts.end()) - counts.begin());
}
} // namespace

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
    editor.Send(Message::SetTabIndents, TabIndents());
    editor.Send(Message::SetBackSpaceUnIndents, preferences_.backspaceUnindents);
    editor.SetAutoIndent(preferences_.autoIndent);
    editor.Send(Message::ScrollCaret);
}

void IndentationSettings::Reset(EditorControl& editor, SyntaxLanguage language) {
    SetFileSettings(std::nullopt);
    fileTabIndents_.reset();
    Apply(editor, language);
}

void IndentationSettings::Load(EditorControl& editor, SyntaxLanguage language, const OriginalFileVariables& variables) {
    SetFileSettings(std::nullopt);
    if (variables.tabWidth) fileWidths_ = OriginalTabs{*variables.tabWidth, *variables.indentWidth};
    fileSpaces_ = variables.tabsAsSpaces;
    fileTabIndents_ = variables.tabIndents;
    if (preferences_.detectIndentation && !(fileWidths_ && fileSpaces_)) {
        const auto length = editor.Send(Scintilla::Message::GetLength);
        const auto* bytes = reinterpret_cast<const char*>(editor.Send(Scintilla::Message::GetRangePointer, 0, length));
        const int width = length ? DetectIndentation({bytes, static_cast<size_t>(length)}) : 0;
        if (width) {
            fileSpaces_ = width <= 8;
            if (*fileSpaces_) fileWidths_ = OriginalTabs{width, width, true};
        }
    }
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
