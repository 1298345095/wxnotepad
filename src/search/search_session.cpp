#include "search/search_session.h"
#include "search/find_text.h"
#include "editor/editor_control.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <algorithm>

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
int ReadHexEscape(std::string_view input, size_t start, size_t count) {
    if (start + count > input.size()) return -1;
    int value = 0;
    for (size_t index = start; index < start + count; ++index) {
        const auto digit = HexDigit(input[index]);
        if (digit < 0) return -1;
        value = (value << 4) | digit;
    }
    return value;
}

void AppendUnicodeEscape(std::string_view input, size_t& index, int value, std::string& output) {
    if (value < 0xd800 || value > 0xdbff || index + 2 >= input.size() || input[index + 1] != '\\' ||
        input[index + 2] != 'u') {
        AppendUtf8(output, value);
        return;
    }
    const int second = ReadHexEscape(input, index + 3, 4);
    if (second < 0 || (second >= 0xd800 && second <= 0xdbff)) {
        AppendUtf8(output, value);
        return;
    }
    index += 6;
    if (second >= 0xdc00 && second <= 0xdfff) {
        AppendUtf8(output, 0x10000 + ((value - 0xd800) << 10) + second - 0xdc00);
    } else {
        AppendUtf8(output, value);
        AppendUtf8(output, second);
    }
}

bool TransformEscape(std::string_view input, size_t& index, std::string& output) {
    const char next = input[index + 1];
    constexpr std::string_view escapes = "\\abefnrtv";
    constexpr std::string_view values = "\\\a\b\x1b\f\n\r\t\v";
    const auto found = escapes.find(next);
    if (found != std::string_view::npos) {
        output.push_back(values[found]);
        ++index;
        return true;
    }
    if (next != 'x' && next != 'u') return false;
    const size_t digits = next == 'x' ? 2 : 4;
    const int value = ReadHexEscape(input, index + 2, digits);
    if (value < 0) return false;
    index += digits + 1;
    if (next == 'x')
        output.push_back(static_cast<char>(value));
    else
        AppendUnicodeEscape(input, index, value, output);
    return true;
}

std::string TransformBackslashes(const wxString& text, bool transform) {
    const auto utf8 = text.utf8_str();
    std::string input(utf8.data(), utf8.length());
    if (!transform) return input;
    std::string output;
    for (size_t index = 0; index < input.size() && input[index]; ++index) {
        const char ch = input[index];
        if (ch == '\\' && index + 1 < input.size() && TransformEscape(input, index, output)) continue;
        output.push_back(ch);
    }
    return output;
}
// Adapted from Notepad4 89b8b36, src/Edit.cpp EscapeWildcards
// (BSD-3-Clause; see licenses/Notepad4.txt). Documents use UTF-8, without DBCS pairs.
std::string EscapeWildcards(const char* input) {
    std::string output;
    while (*input) {
        char ch = *input++;
        if (ch == '*') {
            output.push_back('.');
        } else if (ch == '?') {
            ch = '.';
        } else if (ch == '.' || ch == '^' || ch == '$' || ch == '\\' || ch == '[' || ch == ']' || ch == '+') {
            output.push_back('\\');
        }
        output.push_back(ch);
    }
    return output;
}

// Target validation precedes both callers. Keep capture expansion against that
// current target, and avoid creating an undo action for byte-identical results.
enum class ReplacementEffect : std::uint8_t { Unchanged, Changed, ExpansionFailed };

struct ReplacementCursor {
    intptr_t position;
    intptr_t limit;
};
struct MatchRange {
    intptr_t start;
    intptr_t end;
};

bool AdvanceReplacement(EditorControl& editor, ReplacementCursor& cursor, MatchRange match, bool regexStartOfLine) {
    using Scintilla::Message;
    if (cursor.position == cursor.limit) return false;
    if (match.start == match.end && !regexStartOfLine)
        cursor.position = editor.Send(Message::PositionAfter, cursor.position);
    // Keep the original special handling for patterns whose first byte is '^'.
    if (regexStartOfLine) {
        const auto line = editor.Send(Message::LineFromPosition, cursor.position);
        if (editor.Send(Message::PositionFromLine, line) == cursor.position)
            cursor.position = editor.Send(Message::PositionFromLine, line + 1);
        if (cursor.position == cursor.limit) return false;
    }
    return true;
}

void RepairReplacementSelection(EditorControl& editor, intptr_t replacementEnd) {
    using Scintilla::Message;
    if (editor.Send(Message::GetSelectionEnd) >= replacementEnd) return;
    const auto anchor = editor.Send(Message::GetAnchor);
    const auto caret = editor.Send(Message::GetCurrentPos);
    editor.Send(Message::SetSel, anchor > caret ? replacementEnd : anchor, anchor > caret ? caret : replacementEnd);
}

ReplacementEffect EvaluateTargetReplacement(EditorControl& editor, const std::string& replacement, bool regex) {
    using Scintilla::Message;
    using enum ReplacementEffect;
    const auto length = static_cast<size_t>(editor.Send(Message::GetTargetEnd) - editor.Send(Message::GetTargetStart));
    std::string matched(length + 1, '\0');
    editor.Send(Message::GetTargetText, 0, reinterpret_cast<intptr_t>(matched.data()));
    matched.resize(length);
    if (!regex) return matched == replacement ? Unchanged : Changed;
    const auto expanded = editor.ExpandRegexReplacement(replacement);
    if (!expanded) return ExpansionFailed;
    return matched == *expanded ? Unchanged : Changed;
}

class DeferredUndoAction {
public:
    explicit DeferredUndoAction(EditorControl& editor) : editor_(editor) {}
    DeferredUndoAction(const DeferredUndoAction&) = delete;
    DeferredUndoAction& operator=(const DeferredUndoAction&) = delete;
    ~DeferredUndoAction() {
        if (started_) editor_.Send(Scintilla::Message::EndUndoAction);
    }
    void Begin() {
        if (!started_) {
            editor_.Send(Scintilla::Message::BeginUndoAction);
            started_ = true;
        }
    }
    bool Started() const noexcept { return started_; }

private:
    EditorControl& editor_;
    bool started_ = false;
};

} // namespace

std::string SearchSession::QueryBytes() const {
    const auto bytes = TransformBackslashes(request_.query, request_.options.transformBackslashes);
    return request_.options.wildcardSearch ? EscapeWildcards(bytes.c_str()) : bytes;
}

std::string SearchSession::ReplacementBytes() const {
    return TransformBackslashes(request_.replacement, request_.options.transformBackslashes);
}

bool SearchSession::UsesRegex() const {
    return request_.options.regularExpression || request_.options.wildcardSearch;
}

SearchSession::SearchSession(EditorControl& editor) : editor_(editor) {
}

void SearchSession::Configure(SearchRequest request) {
    request_ = std::move(request);
}

void SearchSession::RestoreHistory(std::vector<wxString> find, std::vector<wxString> replace, bool lastReplaceEmpty) {
    recentFind_ = std::move(find);
    recentReplace_ = std::move(replace);
    lastReplaceEmpty_ = lastReplaceEmpty;
}

void SearchSession::Remember(bool replacement) {
    // Original SaveFindReplaceText/MRUList::Add: submitted nonempty values,
    // fewer than 2048 UTF-16 code units, exact comparison, 32 entries.
    const auto add = [](std::vector<wxString>& history, const wxString& text) {
        size_t length = 0;
        for (const auto ch : text)
            length += ch.GetValue() > 0xffff ? 2 : 1;
        if (text.empty() || length >= 2048) return;
        const auto escaped = EscapeFindText(text).text;
        std::erase(history, escaped);
        history.insert(history.begin(), escaped);
        if (history.size() > 32) history.resize(32);
    };
    if (!request_.query.empty()) {
        hasFindText_ = true;
        add(recentFind_, request_.query);
        if (replacement) {
            add(recentReplace_, request_.replacement);
            lastReplaceEmpty_ = request_.replacement.empty();
        }
    }
}

void SearchSession::ClearFindAll() {
    if (editor_.Occurrences().FindAllCount().has_value()) editor_.Occurrences().Clear();
}

std::optional<size_t> SearchSession::MatchCount() {
    return editor_.Occurrences().FindAllCount();
}

FindAllResult SearchSession::FindAll() {
    using Scintilla::Message;
    const auto query = QueryBytes();
    if (!query.length()) return {FindStatus::EmptyQuery};
    editor_.Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
    const bool multiline = request_.options.transformBackslashes && query.find_first_of("\r\n") != std::string::npos;
    editor_.Occurrences().FindAll(query, SearchFlags(), multiline, request_.bookmarkMatchedLines);
    if (editor_.Send(Message::GetStatus) == static_cast<intptr_t>(Scintilla::Status::RegEx)) {
        ClearFindAll();
        return {FindStatus::InvalidPattern};
    }
    return {FindStatus::Found, editor_.Occurrences().FindAllCount().value_or(0)};
}

void SearchSession::SetWrapConfirmation(std::function<bool(SearchDirection)> confirm) {
    confirmWrap_ = std::move(confirm);
}

FindResult SearchSession::Find(SearchDirection direction, bool extendSelection) {
    const auto start = editor_.Send(direction == SearchDirection::Forward ? Scintilla::Message::GetSelectionEnd
                                                                          : Scintilla::Message::GetSelectionStart);
    return FindFrom(start, direction, extendSelection);
}

FindResult SearchSession::FindFrom(intptr_t start, SearchDirection direction, bool extendSelection) {
    const bool forward = direction == SearchDirection::Forward;
    const auto query = QueryBytes();
    if (!query.length()) return {FindStatus::EmptyQuery};
    using Scintilla::Message;
    editor_.Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    editor_.Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
    const auto length = editor_.Send(Message::GetLength);
    const auto search = [this, &query](intptr_t from, intptr_t to) {
        editor_.Send(Message::SetTargetStart, from);
        editor_.Send(Message::SetTargetEnd, to);
        return editor_.Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data())) >= 0;
    };
    bool wrapped = false;
    // EditFindNext/EditFindPrev use the selection endpoint directly, even
    // after an empty match. Let the pinned matcher choose the backward path.
    bool found = search(start, forward ? length : 0);
    if (!found && request_.wrapAround && !extendSelection && (forward ? start > 0 : start < length)) {
        if (confirmWrap_ && !confirmWrap_(direction)) return {FindStatus::Cancelled};
        wrapped = true;
        found = search(forward ? 0 : length, forward ? length : 0);
    }
    if (editor_.Send(Message::GetStatus) == static_cast<intptr_t>(Scintilla::Status::RegEx)) {
        return {FindStatus::InvalidPattern};
    }
    if (!found) {
        return {FindStatus::NotFound};
    }
    if (extendSelection) {
        const auto anchor = editor_.Send(forward ? Message::GetSelectionStart : Message::GetSelectionEnd);
        const auto end = editor_.Send(forward ? Message::GetTargetEnd : Message::GetTargetStart);
        const auto result = SelectTarget(wrapped);
        editor_.Send(Message::EnsureVisible, editor_.Send(Message::LineFromPosition, anchor));
        editor_.Send(Message::SetSel, anchor, end);
        editor_.Send(Message::ScrollCaret);
        return result;
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
    else if (request_.options.wildcardSearch)
        flags = flags | Scintilla::FindOption::RegExp;
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
    editor_.Send(Message::SetTargetEnd, editor_.Send(Message::GetLength));
    // Pinned EditReplace revalidates captures from selection start to EOF.
    const auto found = editor_.Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data()));
    if (found >= 0 && (found != start || editor_.Send(Message::GetTargetEnd) != end)) {
        return {false, SelectTarget(false)};
    }
    if (found != start || editor_.Send(Message::GetTargetEnd) != end) {
        return {false, FindFrom(start, SearchDirection::Forward)};
    }
    const auto replacement = ReplacementBytes();
    const auto effect = EvaluateTargetReplacement(editor_, replacement, UsesRegex());
    if (effect == ReplacementEffect::ExpansionFailed) return {};
    const bool changesBytes = effect == ReplacementEffect::Changed;
    auto after = end;
    if (changesBytes) {
        editor_.Send(UsesRegex() ? Message::ReplaceTargetRE : Message::ReplaceTarget, replacement.length(),
                     reinterpret_cast<intptr_t>(replacement.c_str()));
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
    const bool regexStartOfLine = UsesRegex() && query.length() && query.data()[0] == '^';
    ReplacementCursor cursor{.position = inSelection ? editor_.Send(Message::GetSelectionStart) : 0,
                             .limit = editor_.Send(Message::GetLength)};
    intptr_t replacementEnd = cursor.position;
    editor_.Send(Message::SetSearchFlags, static_cast<uintptr_t>(SearchFlags()));
    const auto replacementMessage = UsesRegex() ? Message::ReplaceTargetRE : Message::ReplaceTarget;
    {
        DeferredUndoAction undo(editor_);
        // An initial empty document is eligible; original EditReplaceAll stops
        // once a replacement reaches the updated document end.
        while (query.length() && (cursor.position < cursor.limit || (UsesRegex() && cursor.position == cursor.limit))) {
            editor_.Send(Message::SetStatus, static_cast<uintptr_t>(Scintilla::Status::Ok));
            editor_.Send(Message::SetTargetStart, cursor.position);
            editor_.Send(Message::SetTargetEnd, cursor.limit);
            if (editor_.Send(Message::SearchInTarget, query.length(), reinterpret_cast<intptr_t>(query.data())) < 0) {
                if (editor_.Send(Message::GetStatus) == static_cast<intptr_t>(Scintilla::Status::RegEx))
                    return {ReplaceAllStatus::InvalidPattern, count, undo.Started()};
                break;
            }
            const MatchRange match{.start = editor_.Send(Message::GetTargetStart),
                                   .end = editor_.Send(Message::GetTargetEnd)};
            // Keep document-end matching context and the live selection limit.
            if (inSelection && match.end > editor_.Send(Message::GetSelectionEnd)) break;
            const auto effect = EvaluateTargetReplacement(editor_, replacement, UsesRegex());
            if (effect == ReplacementEffect::ExpansionFailed)
                return {ReplaceAllStatus::ExpansionFailed, count, undo.Started()};
            if (effect == ReplacementEffect::Changed) {
                undo.Begin();
                const auto inserted = editor_.Send(replacementMessage, replacement.size(),
                                                   reinterpret_cast<intptr_t>(replacement.c_str()));
                cursor.limit += inserted - (match.end - match.start);
                cursor.position = match.start + inserted;
            } else {
                cursor.position = match.end;
            }
            ++count;
            replacementEnd = cursor.position;
            if (!AdvanceReplacement(editor_, cursor, match, regexStartOfLine)) break;
        }
        editing = undo.Started();
    }
    // End the original undo group BEFORE repairing selection endpoints.
    if (inSelection && count) RepairReplacementSelection(editor_, replacementEnd);
    return {ReplaceAllStatus::Completed, count, editing};
}
