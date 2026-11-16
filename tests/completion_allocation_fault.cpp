#include "completion_allocation_fault.h"
#include <cstdlib>
#include <new>

namespace {
struct State {
    bool armed = false;
    bool injected = false;
    std::size_t targetSize = 0;
};

State& CurrentState() noexcept {
    thread_local State state;
    return state;
}
} // namespace

namespace CompletionAllocationFault {
ScopedFailure::ScopedFailure(std::size_t size) noexcept {
    auto& state = CurrentState();
    state.targetSize = size;
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
    if (state.armed && (state.targetSize == 0 || size == state.targetSize)) {
        state.armed = false; // Cleanup and diagnostics must be able to allocate.
        state.injected = true;
        throw std::bad_alloc();
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
