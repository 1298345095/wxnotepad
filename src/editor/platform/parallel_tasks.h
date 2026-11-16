#pragma once
#include <exception>
#include <future>
#include <memory>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ParallelTasks {
namespace Detail {
// Keep the work and its output buffers alive until every started task has
// finished, including when another task or a later launch fails.
template <class Work, class Launch> void RunAsync(unsigned count, Work& work, Launch launch) {
    std::vector<std::future<void>> futures;
    futures.reserve(count);
    std::exception_ptr failure;
    try {
        for (unsigned index = 0; index < count; ++index)
            futures.push_back(launch([&work] { work(); }));
    } catch (...) {
        failure = std::current_exception();
    }
    for (auto& future : futures) {
        try {
            future.get();
        } catch (...) {
            if (!failure) failure = std::current_exception();
        }
    }
    if (failure) std::rethrow_exception(failure);
}

#ifdef _WIN32
template <class Work> struct NativeBatch {
    Work& work;
    SRWLOCK failureLock = SRWLOCK_INIT;
    std::exception_ptr failure{};

    static void CALLBACK Invoke(PTP_CALLBACK_INSTANCE, void* context, PTP_WORK) noexcept {
        auto& batch = *static_cast<NativeBatch*>(context);
        try {
            batch.work();
        } catch (...) {
            // C callbacks cannot transport a C++ exception. Retain it until the
            // caller has joined every callback and released the native work.
            AcquireSRWLockExclusive(&batch.failureLock);
            if (!batch.failure) batch.failure = std::current_exception();
            ReleaseSRWLockExclusive(&batch.failureLock);
        }
    }
};

struct JoinNativeWork {
    void operator()(PTP_WORK work) const noexcept {
        WaitForThreadpoolWorkCallbacks(work, FALSE);
        CloseThreadpoolWork(work);
    }
};

template <class Work, class Create> void RunNative(unsigned count, Work& work, Create create) {
    NativeBatch<Work> batch{work};
    std::unique_ptr<TP_WORK, JoinNativeWork> native(create(&NativeBatch<Work>::Invoke, &batch));
    if (!native)
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateThreadpoolWork");
    for (unsigned index = 0; index < count; ++index)
        SubmitThreadpoolWork(native.get());
    native.reset();
    if (batch.failure) std::rethrow_exception(batch.failure);
}
#endif
} // namespace Detail

template <class Work> void Run(unsigned count, Work work) {
#ifdef _WIN32
    Detail::RunNative(count, work, [](PTP_WORK_CALLBACK callback, void* context) {
        return CreateThreadpoolWork(callback, context, nullptr);
    });
#else
    Detail::RunAsync(count, work, [](auto task) { return std::async(std::launch::async, task); });
#endif
}
} // namespace ParallelTasks
