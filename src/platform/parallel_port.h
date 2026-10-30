// Portability support for the pinned Notepad4 core on Linux.
#pragma once
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <shared_mutex>
#include <thread>
#include <type_traits>
#define USE_STD_ASYNC_FUTURE 1
#define USE_WIN32_PTP_WORK 0
struct CoreDeadline {std::chrono::steady_clock::time_point due;};
using HANDLE=void*;
struct LARGE_INTEGER {int64_t QuadPart;};
struct SYSTEM_INFO {unsigned dwNumberOfProcessors;};
inline void GetNativeSystemInfo(SYSTEM_INFO* info) {info->dwNumberOfProcessors=std::max(1u,std::thread::hardware_concurrency());}
constexpr unsigned ALL_PROCESSOR_GROUPS=0xffff;
inline unsigned GetActiveProcessorCount(unsigned) {return std::max(1u,std::thread::hardware_concurrency());}
inline HANDLE CreateWaitableTimer(void*,bool,void*) {return new CoreDeadline;}
inline void CloseHandle(HANDLE timer) {delete static_cast<CoreDeadline*>(timer);}
inline void SetWaitableTimer(HANDLE timer,const LARGE_INTEGER* due,int,void*,void*,bool) {
    static_cast<CoreDeadline*>(timer)->due=std::chrono::steady_clock::now()+std::chrono::nanoseconds(-due->QuadPart*100);
}
namespace Scintilla::Internal {
using NativeMutex=std::shared_mutex;
template<class Mutex> using LockGuard=std::lock_guard<Mutex>;
struct HeapPointerFreer {
    template<class T> void operator()(T* ptr) const noexcept {std::free(ptr);}
    template<class T> requires std::is_unbounded_array_v<T>
    static std::unique_ptr<T,HeapPointerFreer> make_unique(size_t size) noexcept {
        return std::unique_ptr<T,HeapPointerFreer>{static_cast<std::remove_extent_t<T>*>(std::calloc(size,sizeof(std::remove_extent_t<T>)))};
    }
};
inline bool WaitableTimerExpired(HANDLE timer) noexcept {return std::chrono::steady_clock::now()>=static_cast<CoreDeadline*>(timer)->due;}
}
