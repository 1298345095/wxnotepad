#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct NativeByteRange {
    intptr_t start;
    intptr_t end;
    bool operator==(const NativeByteRange&) const = default;
};
using NativeByteRanges = std::vector<NativeByteRange>;
using NativeByteSnapshot = std::shared_ptr<const NativeByteRanges>;

// Provenance of bytes copied unchanged from a native ANSI buffer. The core
// owns text and undo actions; this records only the raw portions of each action.
class NativeByteHistory {
public:
    NativeByteHistory();
    NativeByteSnapshot Snapshot() const { return current_; }
    void ExpectInsertion(intptr_t position, intptr_t length, NativeByteRanges ranges);
    void FinishInsertion();
    void BeforeChange(int action, bool collecting, bool undo, bool redo, bool insertion, intptr_t position,
                      intptr_t length);
    void AfterChange(bool insertion, intptr_t position, intptr_t length);
    void ClearHistory();
    void Reset();

private:
    struct ExpectedInsertion {
        intptr_t position;
        intptr_t length;
        NativeByteSnapshot ranges;
    };
    NativeByteSnapshot empty_;
    NativeByteSnapshot current_;
    std::map<int, NativeByteSnapshot> actions_;
    std::map<int, NativeByteSnapshot>::node_type preparedAction_;
    std::unique_ptr<ExpectedInsertion> expected_;
    NativeByteSnapshot changed_;
    std::shared_ptr<NativeByteRanges> next_;
    bool active_ = false;
    void Insert(intptr_t position, intptr_t length);
    void Delete(intptr_t position, intptr_t length);
};

// Convert unmarked text runs, and copy marked bytes literally. Positions are
// absolute core-byte offsets; a selection supplies its document start offset.
std::string NativeBytesFromRanges(std::string_view text, intptr_t start, const NativeByteRanges& ranges);
