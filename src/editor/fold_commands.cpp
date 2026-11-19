#include "editor/editor_control.h"
#include "syntax/syntax_catalog.h"
#include <wx/wupdlock.h>
#include <algorithm>
#include <bit>
#include <optional>
#include <utility>
#include <vector>

namespace {
using enum Scintilla::Message;
using Scintilla::FoldAction;
using Scintilla::FoldLevel;
constexpr int originalMaximumIndentLevel = 63;
constexpr intptr_t originalHeaderStyleScanBytes = 64;

struct FoldHeader {
    intptr_t line;
    FoldLevel level;
};

std::optional<FoldHeader> CurrentFoldHeader(EditorControl& editor) {
    auto line = editor.Send(LineFromPosition, editor.Send(GetCurrentPos));
    auto level = static_cast<FoldLevel>(editor.Send(GetFoldLevel, line));
    if (!Scintilla::LevelIsHeader(level)) {
        line = editor.Send(GetFoldParent, line);
        if (line < 0) return std::nullopt;
        level = static_cast<FoldLevel>(editor.Send(GetFoldLevel, line));
    }
    return FoldHeader{line, level};
}

FoldAction FoldToggleNode(EditorControl& editor, intptr_t line, FoldAction direction) {
    using enum FoldAction;
    const bool expanded = editor.Send(GetFoldExpanded, line) != 0;
    auto action = direction;
    if (action == Toggle) action = expanded ? Contract : Expand;
    if ((action == Expand) != expanded) {
        editor.Send(FoldLine, line, std::to_underlying(action));
        if (direction == Toggle && (editor.Send(GetFoldExpanded, line) != 0) != expanded) return action;
    }
    return direction;
}

int CurrentFoldDepth(EditorControl& editor, FoldHeader header, bool indentation) {
    const int level = Scintilla::LevelNumber(header.level) - std::to_underlying(FoldLevel::Base);
    if (level == 0 || !indentation) return level;
    int depth = 0;
    while (header.line != 0 && depth < originalMaximumIndentLevel - 1) {
        header.line = editor.Send(GetFoldParent, header.line);
        if (header.line < 0) break;
        ++depth;
    }
    return depth;
}

class FoldLevelStack {
public:
    int Push(int level) {
        while (!parents_.empty() && level <= parents_.back())
            parents_.pop_back();
        parents_.push_back(level);
        return static_cast<int>(parents_.size());
    }

private:
    std::vector<int> parents_;
};

template <typename Visit> void ScanFoldHeaders(EditorControl& editor, bool indentation, Visit&& visit) {
    editor.Send(Colourise, 0, -1);
    const auto lineCount = editor.Send(GetLineCount);
    const wxWindowUpdateLocker redraw(&editor);
    FoldLevelStack parents;
    for (intptr_t line = 0; line < lineCount; ++line) {
        const auto header = static_cast<FoldLevel>(editor.Send(GetFoldLevel, line));
        if (!Scintilla::LevelIsHeader(header)) continue;
        const int level = Scintilla::LevelNumber(header);
        const int depth = indentation ? parents.Push(level) : level - std::to_underlying(FoldLevel::Base);
        if (visit(line, depth)) line = editor.Send(GetLastChild, line, level);
    }
}

void FoldToggleLevel(EditorControl& editor, int level, bool indentation) {
    const int targetDepth = indentation ? level + 1 : level;
    auto direction = FoldAction::Toggle;
    ScanFoldHeaders(editor, indentation, [&](intptr_t line, int depth) {
        if (depth != targetDepth) return false;
        direction = FoldToggleNode(editor, line, direction);
        return true;
    });
}

bool HeaderUsesStyle(EditorControl& editor, intptr_t line, int style) {
    auto position = editor.Send(PositionFromLine, line);
    const auto end = std::min(editor.Send(PositionFromLine, line + 1), position + originalHeaderStyleScanBytes);
    do {
        if (editor.Send(GetStyleIndexAt, position) == style) return true;
        ++position;
    } while (position < end);
    return false;
}

void FoldToggleDefault(EditorControl& editor, const SyntaxOriginalMetadata& syntax, bool indentation) {
    const auto mask = static_cast<unsigned>(syntax.foldLevelMask);
    const auto maxDepth = static_cast<int>(std::bit_width(mask)) - 1;
    auto direction = FoldAction::Toggle;
    ScanFoldHeaders(editor, indentation, [&](intptr_t line, int depth) {
        if (depth < 0 || depth > maxDepth || (mask & (1u << depth)) == 0) return false;
        direction = FoldToggleNode(editor, line, direction);
        return depth == maxDepth || (syntax.foldIgnoreInner && HeaderUsesStyle(editor, line, syntax.foldIgnoreInner));
    });
}
} // namespace

void EditorControl::ToggleFolds(FoldCommand command, const SyntaxOriginalMetadata& syntax, int level) {
    if (!codeFoldingVisible_) return;
    const bool indentation = (syntax.lexerAttributes & SyntaxLexerAttribute::IndentBasedFolding) != 0;
    switch (command) {
    case FoldCommand::AllLevels:
        ToggleAllFolds();
        break;
    case FoldCommand::CurrentBlock:
        if (const auto header = CurrentFoldHeader(*this)) {
            FoldToggleNode(*this, header->line, FoldAction::Toggle);
            FinishBatchFold();
        }
        break;
    case FoldCommand::CurrentLevel:
        if (const auto header = CurrentFoldHeader(*this)) {
            FoldToggleLevel(*this, CurrentFoldDepth(*this, *header, indentation), indentation);
            FinishBatchFold();
        }
        break;
    case FoldCommand::NumericLevel:
        FoldToggleLevel(*this, level, indentation);
        FinishBatchFold();
        break;
    case FoldCommand::DefaultLevels:
        FoldToggleDefault(*this, syntax, indentation);
        FinishBatchFold();
        break;
    }
}
