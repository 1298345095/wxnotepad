#pragma once
#include <exception>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace ParallelTasks {
namespace Detail {
// Keep the work and its output buffers alive until every started task has
// finished, including when another task or a later launch fails.
template <class Work, class Launch> void RunThreads(unsigned count, Work& work, Launch launch) {
    // Slots outlive the thread owners even if joining exits exceptionally.
    // Each worker writes only its own slot; the caller reads after joining.
    std::vector<std::exception_ptr> failures(count);
    std::vector<std::jthread> threads;
    threads.reserve(count);
    std::exception_ptr startupFailure;
    try {
        for (unsigned index = 0; index < count; ++index)
            threads.push_back(launch([&work, &failures, index] {
                try {
                    work();
                } catch (...) {
                    failures[index] = std::current_exception();
                }
            }));
    } catch (...) {
        startupFailure = std::current_exception();
    }
    for (auto& thread : threads)
        thread.join();
    if (startupFailure) std::rethrow_exception(startupFailure);
    for (const auto& failure : failures)
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
    Detail::RunThreads(count, work, [](auto task) { return std::jthread(std::move(task)); });
#endif
}
} // namespace ParallelTasks
