#include "completion_allocation_fault.h"
#include <cstdlib>
#include <new>
#include <utility>
#include <vector>

namespace {
struct State {
    bool armed = false;
    bool injected = false;
    std::size_t targetSize = 0;
    std::size_t skipMatches = 0;
    std::size_t* observedSize = nullptr;
};

State& CurrentState() noexcept {
    thread_local State state;
    return state;
}

class ScopedAllocationObservation final {
public:
    ScopedAllocationObservation() noexcept : previous_(std::exchange(CurrentState().observedSize, &largestSize_)) {}
    ~ScopedAllocationObservation() { CurrentState().observedSize = previous_; }
    ScopedAllocationObservation(const ScopedAllocationObservation&) = delete;
    ScopedAllocationObservation& operator=(const ScopedAllocationObservation&) = delete;
    ScopedAllocationObservation(ScopedAllocationObservation&&) = delete;
    ScopedAllocationObservation& operator=(ScopedAllocationObservation&&) = delete;
    std::size_t LargestSize() const noexcept { return largestSize_; }

private:
    std::size_t largestSize_ = 0;
    std::size_t* previous_;
};
} // namespace

namespace CompletionAllocationFault {
std::size_t ByteVectorAllocationSize(std::size_t size) {
    // The allocator can request alignment storage beyond the vector's payload.
    ScopedAllocationObservation observation;
    const std::vector<std::byte> buffer(size);
    return observation.LargestSize();
}

ScopedFailure::ScopedFailure(std::size_t size, std::size_t skipMatches) noexcept {
    auto& state = CurrentState();
    state.targetSize = size;
    state.skipMatches = skipMatches;
    state.injected = false;
    state.armed = true;
}

ScopedFailure::~ScopedFailure() {
    CurrentState().armed = false;
}

bool ScopedFailure::Injected() const noexcept {
    return CurrentState().injected;
}
} // namespace CompletionAllocationFault

void* operator new(std::size_t size) {
    auto& state = CurrentState();
    if (state.observedSize && size > *state.observedSize) *state.observedSize = size;
    if (state.armed && (state.targetSize == 0 || size == state.targetSize)) {
        if (state.skipMatches) {
            --state.skipMatches;
        } else {
            state.armed = false; // Cleanup and diagnostics must be able to allocate.
            state.injected = true;
            throw std::bad_alloc();
        }
    }
    for (;;) {
        if (void* allocation = std::malloc(size ? size : 1)) return allocation;
        const auto handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* allocation) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation, std::size_t) noexcept {
    std::free(allocation);
}
