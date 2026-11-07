#include "editor/occurrence_marking.h"
#include "editor/editor_control.h"
#include <ScintillaStructures.h>
#include <algorithm>
#include <array>
#include <chrono>

// Adapted from Notepad4 89b8b36, Edit.cpp::EditMarkAll and EditEncoding.cpp.
// BSD-3-Clause; see licenses/Notepad4.txt.

namespace {
using Scintilla::Message;
constexpr unsigned Multiline = 0x1000;
constexpr unsigned Bookmark = 0x2000;
constexpr unsigned FromFindAll = 0x8000;
constexpr unsigned SearchForLineEnd = 0x10000;
constexpr intptr_t MeasuredSize = 1024 * 1024;
constexpr double TimeSlot = 100;
struct Range {
    intptr_t start;
    intptr_t length;
};

// Pinned EditEncoding.cpp::IsStringCaseSensitiveW and its Unicode 17 table.
// This optimization also preserves Start's unchanged-query/bookmark semantics.
constexpr uint8_t CaseSensitivityIndex[] = {
    0,  0,   0, 0,  144, 145, 0,   0,  0,  0,  0, 0,  146, 147, 0,  0,  1,  0,  0,  2,  0,  0,  0,  0,  148, 0,
    0,  0,   3, 4,  5,   6,   0,   7,  0,  0,  8, 0,  75,  76,  0,  0,  0,  77, 9,  10, 0,  0,  0,  0,  0,   0,
    0,  0,   0, 0,  0,   0,   0,   0,  0,  0,  0, 0,  0,   0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,   0,
    0,  0,   0, 0,  0,   0,   0,   0,  0,  0,  0, 0,  0,   0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,   0,
    0,  214, 0, 0,  0,   0,   149, 0,  0,  0,  0, 0,  0,   0,   0,  0,  0,  0,  0,  78, 0,  0,  0,  79, 0,   0,
    0,  0,   0, 8,  48,  49,  0,   0,  0,  0,  0, 8,  8,   50,  0,  0,  0,  0,  51, 49, 0,  0,  0,  0,  0,   52,
    53, 0,   0, 0,  8,   8,   8,   8,  54, 8,  8, 8,  50,  8,   55, 56, 8,  57, 58, 59, 0,  60, 53, 8,  61,  0,
    0,  0,   0, 0,  0,   0,   0,   62, 8,  63, 8, 8,  8,   64,  8,  8,  8,  65, 8,  66, 0,  0,  0,  0,  0,   0,
    0,  0,   8, 67, 68,  0,   0,   0,  0,  69, 8, 70, 71,  72,  73, 74, 0,  0,  75, 76, 8,  8,  0,  0,  77,  0,
    0,  0,   0, 0,  0,   0,   0,   2,  2,  0,  0, 0,  0,   0,   8,  8,  78, 0,  0,  76, 79, 68, 0,  0,  0,   80,
    81, 82,  0, 0,  0,   0,   0,   0,  8,  83, 8, 83, 0,   0,   76, 84, 85, 0,  0,  0,  0,  0,  0,  0,  0,   8,
    8,  0,   0, 0,  8,   8,   0,   86, 87, 0,  8, 8,  88,  0,   0,  0,  0,  0,
};
constexpr uint32_t CaseSensitivityMask[] = {
    0x00000000U, 0x00000000U, 0x07fffffeU, 0x07fffffeU, 0x00000000U, 0x00200000U, 0xff7fffffU, 0xff7fffffU, 0xffffffffU,
    0xfeffffffU, 0xffffffffU, 0xffffffffU, 0xffffdfffU, 0xb3fff3ffU, 0xfffffff0U, 0xffffffffU, 0xffffffffU, 0xfc0ffffdU,
    0x1adfffffU, 0x20269f7bU, 0x60041f8dU, 0x50000000U, 0x00000000U, 0x00000000U, 0x00081583U, 0x00020000U, 0x00000024U,
    0xb8cf0000U, 0xffffd740U, 0xfffffffbU, 0xffe3ffffU, 0xefbfffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU,
    0xfffffc03U, 0xffffffffU, 0xffffffffU, 0xffffffffU, 0xffffffffU, 0xfffeffffU, 0x007fffffU, 0xfffffffeU, 0x000000ffU,
    0x00000000U, 0x00000000U, 0x00000000U, 0xffff20bfU, 0xe7ffffffU, 0x3f3fffffU, 0xffff07ffU, 0x22000000U, 0x00004000U,
    0x4fffffffU, 0xaaff3f3fU, 0x3fffffffU, 0x5fdfffffU, 0x0fcf1fdcU, 0x1fdc1fffU, 0x00040c40U, 0x00000018U, 0xffc00000U,
    0x000003ffU, 0xc06dffffU, 0x000c780fU, 0x000020bfU, 0x00003fffU, 0x0fffffffU, 0xfffcfffcU, 0xfe00ffffU, 0xffdf38ffU,
    0xffff7fffU, 0x1fffffffU, 0x00600000U, 0x00080000U, 0xffff0000U, 0x00f8007fU, 0x0000ffffU, 0xff0fffffU, 0xf7ff0000U,
    0xffb7f7ffU, 0x1bfbfffbU, 0x0007ffffU, 0xffff003fU, 0x0000003fU, 0xf9ffffffU, 0x000fffffU, 0x0000000fU,
};
bool CaseSensitiveSelection(const std::string& text) {
    for (const auto character : wxString::FromUTF8(text.data(), text.size())) {
        auto ch = character.GetValue();
        if (!ch) break;
        if (ch < 0x600) {
            if ((CaseSensitivityMask[ch >> 5] >> (ch & 31)) & 1) return true;
        } else if (ch <= 0x1e943) {
            auto block = ch >> 8;
            unsigned index = CaseSensitivityIndex[block & 0x7f];
            block = index ^ (block >> 1);
            index &= ((block - 0x40) >> 8) & 0x3f;
            if (index) {
                ch &= 0xff;
                index = CaseSensitivityIndex[120 + (index << 3) + (ch >> 5)];
                if ((CaseSensitivityMask[index] >> (ch & 31)) & 1) return true;
            }
        }
    }
    return false;
}
} // namespace

OccurrenceMarking::OccurrenceMarking(EditorControl& editor) : editor_(editor), timer_(this) {
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) {
        if (pending_) Continue();
    });
}

OccurrenceMarking::~OccurrenceMarking() {
    timer_.Stop();
}

void OccurrenceMarking::Reset(unsigned flags, std::string text) {
    const bool hadResult = matchCount_ || (flags_ & FromFindAll);
    timer_.Stop();
    if (matchCount_) {
        editor_.Send(Message::SetIndicatorCurrent, 8);
        editor_.Send(Message::IndicatorClearRange, 0, editor_.Send(Message::GetLength));
    }
    if ((bookmarkLine_ >= 0 || bookmarkForFindAll_) && ((flags | flags_) & Bookmark) &&
        ((flags & (Bookmark | FromFindAll)) || !bookmarkForFindAll_))
        editor_.Send(Message::MarkerDeleteAll, 0);
    flags_ = flags;
    text_ = std::move(text);
    pending_ = false;
    ignoreSelectionUpdate_ = false;
    incrementSize_ = 1;
    duration_ = 64;
    matchCount_ = 0;
    lastMatch_ = 0;
    stopPosition_ = 0;
    bookmarkLine_ = -1;
    if (hadResult) {
        auto* event = new wxCommandEvent(wxEVT_EDITOR_STATE_CHANGED, editor_.GetId());
        event->SetEventObject(&editor_);
        wxQueueEvent(&editor_, event);
    }
}

void OccurrenceMarking::Clear() {
    Reset(0, {});
}

void OccurrenceMarking::Stop() {
    // EditMarkAll::Stop is called after the old document has been released.
    matchCount_ = 0;
    Clear();
}

void OccurrenceMarking::SetOptions(int options) {
    options_ = options;
    if (options & 1)
        MarkSelection(false);
    else
        Clear();
}

void OccurrenceMarking::Update(Scintilla::Update updated) {
    const auto value = static_cast<unsigned>(updated);
    if (value & static_cast<unsigned>(Scintilla::Update::Selection)) {
        if (ignoreSelectionUpdate_) {
            ignoreSelectionUpdate_ = false;
            return;
        }
        if (options_ & 1) {
            if (editor_.Send(Message::GetSelectionEmpty)) {
                if (matchCount_) Clear();
            } else {
                MarkSelection(value & static_cast<unsigned>(Scintilla::Update::Content));
            }
        }
    } else if ((value & static_cast<unsigned>(Scintilla::Update::Content)) && matchCount_) {
        MarkSelection(true);
    }
}

void OccurrenceMarking::MarkSelection(bool changed) {
    const auto start = editor_.Send(Message::GetSelectionStart);
    const auto end = editor_.Send(Message::GetSelectionEnd);
    if (start == end ||
        editor_.Send(Message::LineFromPosition, start) != editor_.Send(Message::LineFromPosition, end)) {
        Clear();
        return;
    }
    unsigned flags = (options_ & 8) << 10;
    if (options_ & 4) {
        flags |= static_cast<unsigned>(Scintilla::FindOption::WholeWord);
        const auto wordEnd = editor_.Send(Message::WordEndPosition, start, true);
        if (wordEnd != end || !editor_.Send(Message::IsRangeWord, start, wordEnd)) {
            Clear();
            return;
        }
    }
    const auto length = editor_.Send(Message::GetSelText);
    std::string text(static_cast<size_t>(length) + 1, '\0');
    editor_.Send(Message::GetSelText, 0, reinterpret_cast<intptr_t>(text.data()));
    text.resize(static_cast<size_t>(length));
    if ((options_ & 2) || !CaseSensitiveSelection(text))
        flags |= static_cast<unsigned>(Scintilla::FindOption::MatchCase);
    Start(changed, flags, std::move(text));
}

void OccurrenceMarking::Start(bool changed, unsigned flags, std::string text) {
    if (!changed && flags == (flags_ & (SearchForLineEnd - 1)) && text == text_) return;
    Reset(flags, std::move(text));
    if ((flags & static_cast<unsigned>(Scintilla::FindOption::RegExp)) && text_.size() == 1 &&
        (text_[0] == '^' || text_[0] == '$')) {
        matchCount_ = static_cast<size_t>(editor_.Send(Message::GetLineCount) - (text_[0] == '^'));
        return;
    }
    if ((flags & static_cast<unsigned>(Scintilla::FindOption::RegExp)) && text_.size() > 1 && text_.back() == '$' &&
        text_[text_.size() - 2] != '\\')
        flags_ |= SearchForLineEnd;
    bookmarkForFindAll_ = (flags & Bookmark) && (flags & FromFindAll);
    Continue();
}

void OccurrenceMarking::FindAll(std::string text, Scintilla::FindOption flags, bool multiline, bool bookmarks) {
    auto findFlags = static_cast<unsigned>(flags);
    if (!(findFlags & static_cast<unsigned>(Scintilla::FindOption::RegExp | Scintilla::FindOption::MatchCase)) &&
        !CaseSensitiveSelection(text))
        findFlags |= static_cast<unsigned>(Scintilla::FindOption::MatchCase);
    Start(false, findFlags | FromFindAll | (multiline ? Multiline : 0) | (bookmarks ? Bookmark : 0), std::move(text));
}

std::optional<size_t> OccurrenceMarking::FindAllCount() const {
    return flags_ & FromFindAll ? std::optional(matchCount_) : std::nullopt;
}

void OccurrenceMarking::Continue() {
    using Clock = std::chrono::steady_clock;
    const auto begin = Clock::now();
    const auto previousCount = matchCount_;
    const auto length = editor_.Send(Message::GetLength);
    auto position = stopPosition_;
    auto limit = std::min(length, stopPosition_ + incrementSize_ * MeasuredSize + static_cast<intptr_t>(text_.size()));
    if (limit < length) {
        limit = editor_.Send(Message::PositionFromLine, editor_.Send(Message::LineFromPosition, limit) + 1);
        if (limit + MeasuredSize >= length) limit = length;
    }
    if (flags_ & Multiline) position = std::max(position - static_cast<intptr_t>(text_.size()) + 1, lastMatch_);
    auto stop = position;
    std::array<Range, 240> ranges{};
    size_t index = 0;
    editor_.Send(Message::SetIndicatorCurrent, 8);
    const auto mark = [&] {
        for (size_t i = 0; i < index; ++i) {
            const auto& range = ranges[i];
            editor_.Send(Message::IndicatorFillRange, range.start, range.length);
            if (!(flags_ & Bookmark)) continue;
            auto line = editor_.Send(Message::LineFromPosition, range.start);
            const auto last =
                flags_ & Multiline ? editor_.Send(Message::LineFromPosition, range.start + range.length) : line;
            line = std::max(line, bookmarkLine_ + 1);
            for (; line <= last; ++line)
                editor_.Send(Message::MarkerAdd, line, 0);
            bookmarkLine_ = last;
        }
        index = 0;
    };
    while (position < limit && std::chrono::duration<double, std::milli>(Clock::now() - begin).count() < TimeSlot) {
        Scintilla::TextToFindFull find{{position, limit}, text_.c_str(), static_cast<intptr_t>(text_.size()), {0, 0}};
        const auto found = editor_.Send(Message::FindTextFull, flags_, reinterpret_cast<intptr_t>(&find));
        if (found < 0) {
            stop = limit;
            break;
        }
        ++matchCount_;
        const auto count = find.chrgText.cpMax - found;
        if (!count) {
            position = editor_.Send(Message::PositionAfter, found);
            continue;
        }
        if (index && found == position && !(flags_ & SearchForLineEnd))
            ranges[index - 1].length += count;
        else {
            ranges[index++] = {found, count};
            if (index == ranges.size()) mark();
        }
        position = find.chrgText.cpMax;
    }
    mark();
    stop = std::max(stop, position);
    pending_ = stop < length;
    if (pending_) {
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        const auto durationOne = MeasuredSize * elapsed / (stop - stopPosition_);
        duration_ = std::max(0.25 * durationOne + 0.75 * duration_, 1.0);
        incrementSize_ = 1 + static_cast<int>(TimeSlot / duration_);
        timer_.StartOnce(1);
    }
    lastMatch_ = position;
    stopPosition_ = stop;
    if (!pending_ || previousCount != matchCount_) {
        auto* event = new wxCommandEvent(wxEVT_EDITOR_STATE_CHANGED, editor_.GetId());
        event->SetEventObject(&editor_);
        wxQueueEvent(&editor_, event);
    }
}
