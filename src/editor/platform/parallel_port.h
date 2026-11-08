// Portability support for the pinned Notepad4 core on Linux.
#pragma once
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <new>
#include <shared_mutex>
#include <thread>
#include <type_traits>
#define USE_STD_ASYNC_FUTURE 1
#define USE_WIN32_PTP_WORK 0
struct CoreDeadline {
    std::chrono::steady_clock::time_point due;
};
using HANDLE = void*;
struct LARGE_INTEGER {
    int64_t QuadPart;
};
struct SYSTEM_INFO {
    unsigned dwNumberOfProcessors;
};
inline void GetNativeSystemInfo(SYSTEM_INFO* info) {
    info->dwNumberOfProcessors = std::max(1u, std::thread::hardware_concurrency());
}
constexpr unsigned ALL_PROCESSOR_GROUPS = 0xffff;
inline unsigned GetActiveProcessorCount(unsigned) {
    return std::max(1u, std::thread::hardware_concurrency());
}
// EditModel only creates unnamed timers without attributes or completion callbacks.
inline HANDLE CreateWaitableTimer(std::nullptr_t, bool, std::nullptr_t) {
    return new CoreDeadline;
}
inline void CloseHandle(HANDLE timer) {
    delete static_cast<CoreDeadline*>(timer);
}
inline void SetWaitableTimer(HANDLE timer, const LARGE_INTEGER* due, int, std::nullptr_t, std::nullptr_t, bool) {
    static_cast<CoreDeadline*>(timer)->due =
        std::chrono::steady_clock::now() + std::chrono::nanoseconds(-due->QuadPart * 100);
}
namespace Scintilla::Internal {
using NativeMutex = std::shared_mutex;
template <class Mutex> using LockGuard = std::lock_guard<Mutex>;
struct HeapPointerFreer {
    template <class T> void operator()(T* ptr) const noexcept { std::default_delete<T[]>{}(ptr); }
    template <class T>
        requires std::is_unbounded_array_v<T>
    static std::unique_ptr<T, HeapPointerFreer> make_unique(size_t size) noexcept {
        try {
            return std::unique_ptr<T, HeapPointerFreer>{std::make_unique<T>(size).release()};
        } catch (const std::bad_alloc&) {
            // The pinned line-layout allocation contract returns null on failure.
            return {};
        }
    }
};
inline bool WaitableTimerExpired(HANDLE timer) noexcept {
    return std::chrono::steady_clock::now() >= static_cast<CoreDeadline*>(timer)->due;
}
} // namespace Scintilla::Internal
