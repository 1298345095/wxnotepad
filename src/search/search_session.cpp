#include "search/search_session.h"
#include "editor/editor_control.h"
#include <string>
#include <utility>

void SearchSession::Configure(SearchRequest request) {
    if (request.query != request_.query || request.options != request_.options) {
        ResetMatch();
    } else if (request.wrapAround != request_.wrapAround) {
        regexMatch_.reset();
    }
    request_ = std::move(request);
}

void SearchSession::ResetMatch() {
    regexMatch_.reset();
    lastEmptyMatch_.reset();
}

FindResult SearchSession::Find(SearchDirection direction) {
    const bool forward = direction == SearchDirection::Forward;
    regexMatch_.reset();
    const wxString queryText = request_.query;
    const auto query = queryText.utf8_str();
    if (!query.length()) return {FindStatus::EmptyQuery};
    using Scintilla::Message;
    editor_.Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    editor_.Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
    const auto start = editor_.Send(forward ? Message::GetSelectionEnd : Message::GetSelectionStart);
    const auto length = editor_.Send(Message::GetLength);
    const bool afterEmpty = request_.options.regularExpression && lastEmptyMatch_ &&
                            start == lastEmptyMatch_->position &&
                            editor_.Send(Message::GetSelectionStart) == editor_.Send(Message::GetSelectionEnd) &&
                            lastEmptyMatch_->documentRevision == editor_.DocumentRevision() &&
                            lastEmptyMatch_->selectionRevision == editor_.SelectionRevision();
    const auto search = [&](intptr_t from, intptr_t to) {
        editor_.Send(Message::SetTargetStart, from);
        editor_.Send(Message::SetTargetEnd, to);
        return editor_.Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data())) >= 0;
    };
    bool wrapped = false;
    bool found = false;
    if (request_.options.regularExpression && !forward) {
        // The pinned Boost backward path is line-by-line. Scan forward so
        // Previous sees the same explicit cross-line matches as Next.
        intptr_t previousStart = -1, previousEnd = -1;
        intptr_t lastStart = -1, lastEnd = -1;
        for (intptr_t position = 0; position <= length;) {
            if (!search(position, length)) break;
            lastStart = editor_.Send(Message::GetTargetStart);
            lastEnd = editor_.Send(Message::GetTargetEnd);
            if (lastEnd <= start && !(afterEmpty && lastStart == start && lastEnd == start)) {
                previousStart = lastStart;
                previousEnd = lastEnd;
            }
            if (lastStart == lastEnd) {
                if (lastEnd == length) break;
                position = editor_.Send(Message::PositionAfter, lastEnd);
            } else {
                position = lastEnd;
            }
        }
        if (previousStart < 0 && request_.wrapAround) {
            wrapped = true;
            previousStart = lastStart;
            previousEnd = lastEnd;
        }
        found = previousStart >= 0;
        if (found) {
            editor_.Send(Message::SetTargetStart, previousStart);
            editor_.Send(Message::SetTargetEnd, previousEnd);
        }
    } else {
        found = !(afterEmpty && start == length) &&
                search(afterEmpty ? editor_.Send(Message::PositionAfter, start) : start, forward ? length : 0);
    }
    if (!found && request_.wrapAround && !(request_.options.regularExpression && !forward)) {
        wrapped = true;
        found = search(forward ? 0 : length, request_.options.regularExpression ? length : start);
        if (request_.options.regularExpression && found && editor_.Send(Message::GetTargetStart) > start) found = false;
    }
    if (editor_.Send(Message::GetStatus) == static_cast<intptr_t>(Scintilla::Status::RegEx)) {
        return {FindStatus::InvalidPattern};
    }
    if (!found) {
        return {FindStatus::NotFound};
    }
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
    lastEmptyMatch_.reset();
    if (request_.options.regularExpression && matchStart == matchEnd)
        lastEmptyMatch_ = EmptyMatch{matchStart, editor_.DocumentRevision(), editor_.SelectionRevision()};
    if (request_.options.regularExpression)
        regexMatch_ = RegexMatch{matchStart, matchEnd, editor_.DocumentRevision(), editor_.SelectionRevision()};
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
    if (request_.options.regularExpression && (!regexMatch_ || regexMatch_->start != start || regexMatch_->end != end ||
                                               regexMatch_->documentRevision != editor_.DocumentRevision() ||
                                               regexMatch_->selectionRevision != editor_.SelectionRevision())) {
        return {false, Find(SearchDirection::Forward)};
    }
    editor_.Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    editor_.Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
    editor_.Send(Message::SetTargetStart, start);
    editor_.Send(Message::SetTargetEnd, request_.options.regularExpression ? editor_.Send(Message::GetLength) : end);
    // Regex assertions retain document-end context; the core patch also retains
    // preceding context. Both result endpoints must still equal the selection.
    if ((start == end &&
         !(request_.options.regularExpression && lastEmptyMatch_ && lastEmptyMatch_->position == start &&
           lastEmptyMatch_->documentRevision == editor_.DocumentRevision())) ||
        editor_.Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data())) != start ||
        editor_.Send(Message::GetTargetEnd) != end) {
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
    if (request_.options.regularExpression && start == end) {
        // Skip inserted bytes, then one original character. EOF is processed once.
        lastEmptyMatch_ = EmptyMatch{after, editor_.DocumentRevision(), editor_.SelectionRevision()};
    }
    return {changesBytes, Find(SearchDirection::Forward)};
}

ReplaceAllResult SearchSession::ReplaceAll() {
    const wxString queryText = request_.query;
    const auto query = queryText.utf8_str();
    const wxString replacementText = request_.replacement;
    const auto replacement = replacementText.utf8_str();
    using Scintilla::Message;
    size_t count = 0;
    bool editing = false;
    intptr_t start = 0;
    auto limit = editor_.Send(Message::GetLength);
    editor_.Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    // Regex may match the empty document or the boundary after a consuming match.
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
        if (matchStart == matchEnd) {
            if (start == limit) break; // This EOF boundary has now been visited.
            // Skip inserted bytes, then one original character. Never split UTF-8;
            // the core normalizes a candidate between CR and LF to the next line.
            start = editor_.Send(Message::PositionAfter, start);
        }
    }
    if (editing) editor_.Send(Message::EndUndoAction);
    return {ReplaceAllStatus::Completed, count, editing};
}
