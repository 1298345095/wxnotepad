#include "search/search_session.h"
#include "editor/editor_control.h"
#include <array>
#include <string>
#include <utility>

SearchSession::SearchSession(EditorControl& editor) : editor_(editor) {
    editor_.Bind(wxEVT_EDITOR_STATE_CHANGED, &SearchSession::OnEditorState, this);
}

SearchSession::~SearchSession() {
    editor_.Unbind(wxEVT_EDITOR_STATE_CHANGED, &SearchSession::OnEditorState, this);
}

void SearchSession::Configure(SearchRequest request) {
    if (request.query != request_.query || request.options != request_.options) ClearFindAll();
    request_ = std::move(request);
}

void SearchSession::ClearFindAll() {
    if (matchCount_) {
        editor_.Send(Scintilla::Message::SetIndicatorCurrent, 8);
        editor_.Send(Scintilla::Message::IndicatorClearRange, 0, editor_.Send(Scintilla::Message::GetLength));
        matchCount_.reset();
    }
}

std::optional<size_t> SearchSession::MatchCount() {
    if (markedRevision_ != editor_.DocumentRevision()) ClearFindAll();
    return matchCount_;
}

void SearchSession::OnEditorState(wxCommandEvent& event) {
    MatchCount();
    event.Skip();
}

FindAllResult SearchSession::FindAll() {
    using Scintilla::Message;
    ClearFindAll();
    // EditMarkAll::Reset retains Find All bookmarks on ordinary invalidation,
    // and replaces them when another Find All operation starts.
    if (bookmarkForFindAll_) editor_.Send(Message::MarkerDeleteAll, 0);
    const auto query = request_.query.utf8_str();
    if (!query.length()) return {FindStatus::EmptyQuery};
    markedRevision_ = editor_.DocumentRevision();
    matchCount_ = 0;
    if (request_.options.regularExpression && query.length() == 1 &&
        (query.data()[0] == '^' || query.data()[0] == '$')) {
        // Pinned EditMarkAll::Start counts these anchors without indicators or bookmarks.
        *matchCount_ = static_cast<size_t>(editor_.Send(Message::GetLineCount) - (query.data()[0] == '^'));
        return {FindStatus::Found, *matchCount_};
    }
    bookmarkForFindAll_ = request_.bookmarkMatchedLines;
    editor_.Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    editor_.Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
    editor_.Send(Message::SetIndicatorCurrent, 8);
    const auto limit = editor_.Send(Message::GetLength);
    intptr_t start = 0;
    const bool multiline = request_.query.find_first_of("\r\n") != wxString::npos;
    const bool searchForLineEnd = request_.options.regularExpression && query.length() > 1 &&
                                  query.data()[query.length() - 1] == '$' && query.data()[query.length() - 2] != '\\';
    // Keep EditMarkAll's 240-range cache and adjacent-range aggregation. This
    // also determines which lines receive bookmarks for multiline regex ranges.
    std::array<intptr_t, 480> ranges{};
    size_t index = 0;
    const auto markRanges = [&] {
        for (size_t i = 0; i < index; i += 2) {
            editor_.Send(Message::IndicatorFillRange, ranges[i], ranges[i + 1]);
            if (request_.bookmarkMatchedLines) {
                auto line = editor_.Send(Message::LineFromPosition, ranges[i]);
                const auto last = multiline ? editor_.Send(Message::LineFromPosition, ranges[i] + ranges[i + 1]) : line;
                for (; line <= last; ++line) {
                    if (!(editor_.Send(Message::MarkerGet, line) & 1)) editor_.Send(Message::MarkerAdd, line, 0);
                }
            }
        }
        index = 0;
    };
    // EditMarkAll::Continue excludes EOF and advances zero-width matches by a
    // complete character; empty matches count but have no visible mark/bookmark.
    while (start < limit) {
        editor_.Send(Message::SetTargetStart, start);
        editor_.Send(Message::SetTargetEnd, limit);
        const auto found =
            editor_.Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data()));
        if (found < 0) {
            if (editor_.Send(Message::GetStatus) == static_cast<intptr_t>(Scintilla::Status::RegEx)) {
                ClearFindAll();
                return {FindStatus::InvalidPattern};
            }
            break;
        }
        ++*matchCount_;
        const auto end = editor_.Send(Message::GetTargetEnd);
        if (end == found) {
            start = editor_.Send(Message::PositionAfter, found);
            if (start <= found) break;
            continue;
        }
        if (index && found == start && !searchForLineEnd) {
            ranges[index - 1] += end - found;
        } else {
            ranges[index++] = found;
            ranges[index++] = end - found;
            if (index == ranges.size()) markRanges();
        }
        start = end;
    }
    markRanges();
    return {FindStatus::Found, *matchCount_};
}

FindResult SearchSession::Find(SearchDirection direction) {
    const bool forward = direction == SearchDirection::Forward;
    const wxString queryText = request_.query;
    const auto query = queryText.utf8_str();
    if (!query.length()) return {FindStatus::EmptyQuery};
    using Scintilla::Message;
    editor_.Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    editor_.Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
    const auto start = editor_.Send(forward ? Message::GetSelectionEnd : Message::GetSelectionStart);
    const auto length = editor_.Send(Message::GetLength);
    const auto search = [&](intptr_t from, intptr_t to) {
        editor_.Send(Message::SetTargetStart, from);
        editor_.Send(Message::SetTargetEnd, to);
        return editor_.Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data())) >= 0;
    };
    bool wrapped = false;
    // EditFindNext/EditFindPrev use the selection endpoint directly, even
    // after an empty match. Let the pinned matcher choose the backward path.
    bool found = search(start, forward ? length : 0);
    if (!found && request_.wrapAround && (forward ? start > 0 : start < length)) {
        wrapped = true;
        found = search(forward ? 0 : length, forward ? length : 0);
    }
    if (editor_.Send(Message::GetStatus) == static_cast<intptr_t>(Scintilla::Status::RegEx)) {
        return {FindStatus::InvalidPattern};
    }
    if (!found) {
        return {FindStatus::NotFound};
    }
    return SelectTarget(wrapped);
}

FindResult SearchSession::SelectTarget(bool wrapped) {
    using Scintilla::Message;
    const auto matchStart = editor_.Send(Message::GetTargetStart);
    const auto matchEnd = editor_.Send(Message::GetTargetEnd);
    const auto firstLine = editor_.Send(Message::LineFromPosition, matchStart);
    // The target is end-exclusive. A zero-width match still needs its own line;
    // a match ending at the next line start need not open that line's folds.
    const auto lastPosition = matchEnd > matchStart ? editor_.Send(Message::PositionBefore, matchEnd) : matchStart;
    const auto lastLine = editor_.Send(Message::LineFromPosition, lastPosition);
    for (auto line = firstLine; line <= lastLine; ++line)
        editor_.Send(Message::EnsureVisible, line);
    editor_.Send(Message::SetSel, matchStart, matchEnd);
    editor_.Send(Message::ScrollCaret);
    return {FindStatus::Found, wrapped, matchStart, matchEnd};
}

Scintilla::FindOption SearchSession::SearchFlags() const {
    auto flags = Scintilla::FindOption::None;
    if (request_.options.matchCase) flags = flags | Scintilla::FindOption::MatchCase;
    if (request_.options.wholeWord) flags = flags | Scintilla::FindOption::WholeWord;
    if (request_.options.regularExpression)
        flags = flags | Scintilla::FindOption::RegExp | Scintilla::FindOption::Cxx11RegEx;
    return flags;
}

ReplaceOneResult SearchSession::ReplaceOne() {
    const wxString queryText = request_.query;
    const auto query = queryText.utf8_str();
    if (!query.length()) return {false, FindResult{FindStatus::EmptyQuery}};
    using Scintilla::Message;
    const auto start = editor_.Send(Message::GetSelectionStart);
    const auto end = editor_.Send(Message::GetSelectionEnd);
    editor_.Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    editor_.Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
    editor_.Send(Message::SetTargetStart, start);
    editor_.Send(Message::SetTargetEnd, request_.options.regularExpression ? editor_.Send(Message::GetLength) : end);
    // EditReplace re-runs the original forward matcher from selection start to
    // document end, then uses those captures only if both endpoints match.
    if (!request_.options.regularExpression && start == end) return {false, Find(SearchDirection::Forward)};
    const auto found = editor_.Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data()));
    if (request_.options.regularExpression && found >= 0 &&
        (found != start || editor_.Send(Message::GetTargetEnd) != end)) {
        // EditReplace selects this candidate without replacing it; restarting
        // at selection end would skip matches within the original selection.
        return {false, SelectTarget(false)};
    }
    if (found != start || editor_.Send(Message::GetTargetEnd) != end) {
        return {false, Find(SearchDirection::Forward)};
    }
    const wxString replacementText = request_.replacement;
    const auto replacementUtf8 = replacementText.utf8_str();
    // The original formatter reads the byte after a trailing dollar/backslash.
    const std::string replacement(replacementUtf8.data(), replacementUtf8.length());
    auto after = end;
    std::string selected(static_cast<size_t>(end - start) + 1, '\0');
    editor_.Send(Message::GetTargetText, 0, reinterpret_cast<intptr_t>(selected.data()));
    selected.pop_back();
    bool changesBytes = replacement != selected;
    if (request_.options.regularExpression) {
        const auto expanded = editor_.ExpandRegexReplacement(replacement);
        if (!expanded) return {};
        changesBytes = *expanded != selected;
    }
    if (changesBytes) {
        editor_.Send(request_.options.regularExpression ? Message::ReplaceTargetRE : Message::ReplaceTarget,
                     replacement.length(), reinterpret_cast<intptr_t>(replacement.c_str()));
        after = editor_.Send(Message::GetTargetEnd);
    }
    editor_.Send(Message::SetSel, after, after);
    return {changesBytes, Find(SearchDirection::Forward)};
}

ReplaceAllResult SearchSession::ReplaceAll() {
    return ReplaceRange(false);
}

ReplaceAllResult SearchSession::ReplaceInSelection() {
    if (editor_.Send(Scintilla::Message::SelectionIsRectangle)) return {ReplaceAllStatus::RectangularSelection};
    return ReplaceRange(true);
}

ReplaceAllResult SearchSession::ReplaceRange(bool inSelection) {
    const wxString queryText = request_.query;
    const auto query = queryText.utf8_str();
    const wxString replacementText = request_.replacement;
    const auto replacement = replacementText.utf8_str();
    using Scintilla::Message;
    size_t count = 0;
    bool editing = false;
    const bool regexStartOfLine = request_.options.regularExpression && query.length() && query.data()[0] == '^';
    intptr_t start = inSelection ? editor_.Send(Message::GetSelectionStart) : 0;
    intptr_t replacementEnd = start;
    auto limit = editor_.Send(Message::GetLength);
    editor_.Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    // An initial empty document is eligible; EditReplaceAll stops as soon as a
    // replacement reaches the updated document end.
    while (query.length() && (start < limit || (request_.options.regularExpression && start == limit))) {
        editor_.Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
        editor_.Send(Message::SetTargetStart, start);
        editor_.Send(Message::SetTargetEnd, limit);
        if (editor_.Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data())) < 0) {
            if (editor_.Send(Message::GetStatus) == static_cast<intptr_t>(Scintilla::Status::RegEx)) {
                if (editing) editor_.Send(Message::EndUndoAction);
                return {ReplaceAllStatus::InvalidPattern, count, editing};
            }
            break;
        }
        const auto matchStart = editor_.Send(Message::GetTargetStart);
        const auto matchEnd = editor_.Send(Message::GetTargetEnd);
        // EditReplaceAllInSelection retains document-end search context and
        // stops at the first match crossing the live, end-exclusive selection.
        if (inSelection && matchEnd > editor_.Send(Message::GetSelectionEnd)) break;
        const auto length = static_cast<size_t>(matchEnd - matchStart);
        std::string matched(length + 1, '\0');
        editor_.Send(Message::GetTargetText, 0, reinterpret_cast<intptr_t>(matched.data()));
        matched.resize(length);
        const std::string replacementBytes(replacement.data(), replacement.length());
        const auto expanded = request_.options.regularExpression ? editor_.ExpandRegexReplacement(replacementBytes)
                                                                 : std::optional<std::string>(replacementBytes);
        if (!expanded) {
            if (editing) editor_.Send(Message::EndUndoAction);
            return {ReplaceAllStatus::ExpansionFailed, count, editing};
        }
        if (matched != *expanded) {
            if (!editing) {
                editor_.Send(Message::BeginUndoAction);
                editing = true;
            }
            const auto inserted =
                editor_.Send(request_.options.regularExpression ? Message::ReplaceTargetRE : Message::ReplaceTarget,
                             replacementBytes.size(), reinterpret_cast<intptr_t>(replacementBytes.c_str()));
            limit += inserted - (matchEnd - matchStart);
            start = matchStart + inserted;
        } else {
            start = matchEnd;
        }
        ++count;
        replacementEnd = start;
        if (start == limit) break;
        if (matchStart == matchEnd && !regexStartOfLine) {
            start = editor_.Send(Message::PositionAfter, start);
        }
        // Preserve the original pattern-leading '^' progress rule when the
        // replacement ends at a line start. Other anchor spellings use
        // ordinary empty-match character progress (Edit.cpp:5828-5842).
        if (regexStartOfLine) {
            const auto line = editor_.Send(Message::LineFromPosition, start);
            if (editor_.Send(Message::PositionFromLine, line) == start)
                start = editor_.Send(Message::PositionFromLine, line + 1);
            if (start == limit) break;
        }
    }
    if (editing) editor_.Send(Message::EndUndoAction);
    if (inSelection && count && editor_.Send(Message::GetSelectionEnd) < replacementEnd) {
        const auto anchor = editor_.Send(Message::GetAnchor);
        const auto caret = editor_.Send(Message::GetCurrentPos);
        editor_.Send(Message::SetSel, anchor > caret ? replacementEnd : anchor,
                     anchor > caret ? caret : replacementEnd);
    }
    return {ReplaceAllStatus::Completed, count, editing};
}
