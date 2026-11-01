#include "search/search_session.h"
#include "editor/editor_control.h"
#include <string>
#include <string_view>
#include <utility>

namespace {
// Adapted from Notepad4 89b8b36, src/EditEncoding.cpp TransformBackslashes
// (BSD-3-Clause; see licenses/Notepad4.txt). The editor uses UTF-8 on every host.
int HexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}
void AppendUtf8(std::string& output, unsigned value) {
    if (value >= 0xd800 && value <= 0xdfff) value = 0xfffd;
    if (value < 0x80) {
        output.push_back(static_cast<char>(value));
    } else if (value < 0x800) {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value < 0x10000) {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (value >> 18)));
        output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}
std::string TransformBackslashes(const wxString& text, bool transform) {
    const auto utf8 = text.utf8_str();
    const std::string input(utf8.data(), utf8.length());
    if (!transform) return input;
    std::string output;
    const auto hex = [&](size_t start, size_t count) {
        int value = 0;
        if (start + count > input.size()) return -1;
        for (size_t index = start; index < start + count; ++index) {
            const auto digit = HexDigit(input[index]);
            if (digit < 0) return -1;
            value = (value << 4) | digit;
        }
        return value;
    };
    for (size_t index = 0; index < input.size() && input[index]; ++index) {
        const char ch = input[index];
        if (ch == '\\' && index + 1 < input.size()) {
            const char next = input[index + 1];
            const char* escapes = "\\abefnrtv";
            const char* values = "\\\a\b\x1b\f\n\r\t\v";
            const auto found = std::string_view(escapes).find(next);
            if (found != std::string_view::npos) {
                output.push_back(values[found]);
                ++index;
                continue;
            }
            if (next == 'x' || next == 'u') {
                const size_t digits = next == 'x' ? 2 : 4;
                const int value = hex(index + 2, digits);
                if (value >= 0) {
                    index += digits + 1;
                    if (next == 'x') {
                        output.push_back(static_cast<char>(value));
                    } else if (value >= 0xd800 && value <= 0xdbff && index + 2 < input.size() &&
                               input[index + 1] == '\\' && input[index + 2] == 'u') {
                        const int second = hex(index + 3, 4);
                        if (second >= 0 && !(second >= 0xd800 && second <= 0xdbff)) {
                            index += 6;
                            if (second >= 0xdc00 && second <= 0xdfff) {
                                AppendUtf8(output, 0x10000 + ((value - 0xd800) << 10) + second - 0xdc00);
                            } else {
                                AppendUtf8(output, value);
                                AppendUtf8(output, second);
                            }
                        } else {
                            AppendUtf8(output, value);
                        }
                    } else {
                        AppendUtf8(output, value);
                    }
                    continue;
                }
            }
        }
        output.push_back(ch);
    }
    return output;
}
} // namespace

std::string SearchSession::QueryBytes() const {
    return TransformBackslashes(request_.query, request_.options.transformBackslashes);
}

std::string SearchSession::ReplacementBytes() const {
    return TransformBackslashes(request_.replacement, request_.options.transformBackslashes);
}

void SearchSession::Configure(SearchRequest request) {
    request_ = std::move(request);
}

FindResult SearchSession::Find(SearchDirection direction) {
    const bool forward = direction == SearchDirection::Forward;
    const auto query = QueryBytes();
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
    const auto query = QueryBytes();
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
    // The original formatter reads the byte after a trailing dollar/backslash.
    const auto replacement = ReplacementBytes();
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
    const auto query = QueryBytes();
    const auto replacement = ReplacementBytes();
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
