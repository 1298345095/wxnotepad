#pragma once
#include <cstddef>

namespace CompletionAllocationFault {
std::size_t ByteVectorAllocationSize(std::size_t size);

class ScopedFailure final {
public:
    explicit ScopedFailure(std::size_t size = 0, std::size_t skipMatches = 0) noexcept;
    ~ScopedFailure();
    ScopedFailure(const ScopedFailure&) = delete;
    ScopedFailure& operator=(const ScopedFailure&) = delete;
    ScopedFailure(ScopedFailure&&) = delete;
    ScopedFailure& operator=(ScopedFailure&&) = delete;
    bool Injected() const noexcept;
};
} // namespace CompletionAllocationFault
