#include "editor/native_byte_history.h"
#include "editor/platform/native_encoding.h"
#include <algorithm>

namespace {
void Append(NativeByteRanges& output, intptr_t start, intptr_t end) {
    if (start == end) return;
    if (!output.empty() && output.back().end == start)
        output.back().end = end;
    else
        output.push_back({start, end});
}
} // namespace

NativeByteHistory::NativeByteHistory() : empty_(std::make_shared<const NativeByteRanges>()), current_(empty_) {
}

void NativeByteHistory::ExpectInsertion(intptr_t position, intptr_t length, NativeByteRanges ranges) {
    if (ranges.empty()) return;
    expected_ = std::make_unique<ExpectedInsertion>(position, length,
                                                    std::make_shared<const NativeByteRanges>(std::move(ranges)));
    active_ = true;
}

void NativeByteHistory::FinishInsertion() {
    expected_.reset();
    next_.reset();
    changed_.reset();
    preparedAction_ = {};
    if (current_->empty() && actions_.empty()) active_ = false;
}

void NativeByteHistory::BeforeChange(int action, bool collecting, bool undo, bool redo, bool insertion,
                                     intptr_t position, intptr_t length) {
    next_.reset();
    preparedAction_ = {};
    if (!active_) return;
    changed_ = empty_;
    if (undo || redo) {
        const auto found = actions_.find(undo ? action - 1 : action);
        if (found != actions_.end()) changed_ = found->second;
    } else {
        if (insertion) {
            if (expected_ && expected_->position == position && expected_->length == length)
                changed_ = expected_->ranges;
        } else {
            auto removed = std::make_shared<NativeByteRanges>();
            for (const auto& range : *current_) {
                const auto start = std::max(position, range.start);
                const auto end = std::min(position + length, range.end);
                if (start < end) Append(*removed, start - position, end - position);
            }
            if (!removed->empty()) changed_ = std::move(removed);
        }
    }
    // Allocate before the core mutates text. The post-edit hook only fills the
    // reserved vector and publishes it, before optional host callbacks run.
    next_ = std::make_shared<NativeByteRanges>();
    next_->reserve(current_->size() + changed_->size() + 1);
    if (!undo && !redo && collecting) {
        std::map<int, NativeByteSnapshot> prepared;
        prepared.emplace(action, changed_);
        preparedAction_ = prepared.extract(action);
    }
}

void NativeByteHistory::Insert(intptr_t position, intptr_t length) {
    bool inserted = false;
    const auto insert = [&] {
        if (inserted) return;
        for (const auto& range : *changed_)
            Append(*next_, position + range.start, position + range.end);
        inserted = true;
    };
    for (const auto& range : *current_) {
        if (range.end <= position) {
            Append(*next_, range.start, range.end);
        } else if (range.start < position) {
            Append(*next_, range.start, position);
            insert();
            Append(*next_, position + length, range.end + length);
        } else {
            insert();
            Append(*next_, range.start + length, range.end + length);
        }
    }
    insert();
}

void NativeByteHistory::Delete(intptr_t position, intptr_t length) {
    for (const auto& range : *current_) {
        if (range.end <= position)
            Append(*next_, range.start, range.end);
        else if (range.start >= position + length)
            Append(*next_, range.start - length, range.end - length);
        else {
            if (range.start < position) Append(*next_, range.start, position);
            if (range.end > position + length) Append(*next_, position, range.end - length);
        }
    }
}

void NativeByteHistory::AfterChange(bool insertion, intptr_t position, intptr_t length) {
    if (!next_) return;
    if (insertion)
        Insert(position, length);
    else
        Delete(position, length);
    current_ = next_->empty() ? empty_ : std::move(next_);
    next_.reset();
    if (!preparedAction_.empty()) {
        actions_.erase(actions_.lower_bound(preparedAction_.key()), actions_.end());
        actions_.insert(std::move(preparedAction_));
    }
}

void NativeByteHistory::ClearHistory() {
    actions_.clear();
    next_.reset();
    changed_.reset();
    preparedAction_ = {};
    active_ = !current_->empty();
}

void NativeByteHistory::Reset() {
    current_ = empty_;
    expected_.reset();
    ClearHistory();
}

std::string NativeBytesFromRanges(std::string_view text, intptr_t start, const NativeByteRanges& ranges) {
    std::string result;
    intptr_t position = start;
    const auto end = start + static_cast<intptr_t>(text.size());
    for (const auto& range : ranges) {
        const auto first = std::max(start, range.start);
        const auto last = std::min(end, range.end);
        if (first >= last) continue;
        if (position < first)
            result += NativeDefaultEncodingBytes(
                text.substr(static_cast<size_t>(position - start), static_cast<size_t>(first - position)));
        result += text.substr(static_cast<size_t>(first - start), static_cast<size_t>(last - first));
        position = last;
    }
    if (position < end) result += NativeDefaultEncodingBytes(text.substr(static_cast<size_t>(position - start)));
    return result;
}
