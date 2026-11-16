#include "editor/platform/parallel_tasks.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <latch>
#include <new>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
constexpr unsigned WorkerCount = 4;
constexpr std::array Inputs{3, 7, 11, 19};
constexpr std::array Expected{9, 49, 121, 361};

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

enum class FailureKind { Allocation, NativeResource };

[[noreturn]] void ThrowFailure(FailureKind kind) {
    if (kind == FailureKind::Allocation) throw std::bad_alloc();
    throw std::system_error(std::make_error_code(std::errc::io_error), "native measurement fixture");
}

void CheckFailure(const std::exception_ptr& failure, FailureKind kind) {
    Require(static_cast<bool>(failure), "a failed worker must prevent consuming its incomplete output");
    try {
        std::rethrow_exception(failure);
    } catch (const std::bad_alloc&) {
        Require(kind == FailureKind::Allocation, "allocation failure keeps its original exception type");
    } catch (const std::system_error& error) {
        Require(kind == FailureKind::NativeResource && error.code() == std::errc::io_error &&
                    std::string_view(error.what()).contains("native measurement fixture"),
                "native resource failure keeps its original error code and context");
    }
}

void SuccessfulRetry() {
    std::array<int, WorkerCount> positions{};
    std::atomic<unsigned> next = 0;
    ParallelTasks::Run(WorkerCount, [&next, &positions] {
        const unsigned index = next.fetch_add(1);
        positions[index] = Inputs[index] * Inputs[index];
    });
    Require(next == WorkerCount && positions == Expected,
            "a new batch fills all output after the preceding failed batch has joined");
}

void WorkerFailure(FailureKind kind) {
    std::latch failureStarted(1);
    std::latch releaseRemaining(1);
    std::promise<void> returned;
    auto returnState = returned.get_future();
    std::exception_ptr failure;
    std::atomic<unsigned> started = 0;
    std::atomic<unsigned> finished = 0;
    std::array<int, WorkerCount> positions{};
    auto work = [kind, &started, &finished, &failureStarted, &releaseRemaining, &positions] {
        const unsigned index = started.fetch_add(1);
        struct Completion {
            std::atomic<unsigned>& finished;
            explicit Completion(std::atomic<unsigned>& count) : finished(count) {}
            ~Completion() { ++finished; }
            Completion(const Completion&) = delete;
            Completion& operator=(const Completion&) = delete;
            Completion(Completion&&) = delete;
            Completion& operator=(Completion&&) = delete;
        };
        Completion completion{finished};
        if (index == 0) {
            failureStarted.count_down();
            ThrowFailure(kind);
        }
        releaseRemaining.wait();
        positions[index] = Inputs[index] * Inputs[index];
    };
    std::jthread caller([&work, &failure, &returned] {
        try {
            ParallelTasks::Run(WorkerCount, work);
        } catch (...) {
            failure = std::current_exception();
        }
        returned.set_value();
    });
    failureStarted.wait();
    // Other callbacks cannot finish until this thread releases them. Propagating
    // now would let the caller destroy buffers still used by those callbacks.
    const bool returnedEarly = returnState.wait_for(std::chrono::milliseconds(30)) == std::future_status::ready;
    releaseRemaining.count_down();
    caller.join();
    Require(!returnedEarly, "a worker failure waits for outstanding work before reaching the caller");
    Require(started == WorkerCount && finished == WorkerCount,
            "all queued callbacks finish before the failed batch returns");
    for (unsigned index = 1; index < WorkerCount; ++index)
        Require(positions[index] == Expected[index], "other workers finish writing their live output buffers");
    CheckFailure(failure, kind);
    SuccessfulRetry();
}

void PartialStartFailure(unsigned failAfter) {
    unsigned launched = 0;
    std::atomic<unsigned> completed = 0;
    std::latch launchFailed(1);
    std::latch releaseRemaining(1);
    std::promise<void> returned;
    auto returnState = returned.get_future();
    std::exception_ptr failure;
    auto work = [&releaseRemaining, &completed] {
        releaseRemaining.wait();
        ++completed;
        throw std::bad_alloc();
    };
    const auto error = std::make_error_code(std::errc::resource_unavailable_try_again);
    std::jthread caller([failAfter, error, &work, &launched, &launchFailed, &failure, &returned] {
        try {
            ParallelTasks::Detail::RunThreads(WorkerCount, work,
                                              [failAfter, error, &launched, &launchFailed](auto task) {
                                                  if (launched == failAfter) {
                                                      launchFailed.count_down();
                                                      throw std::system_error(error, "launch fixture");
                                                  }
                                                  std::jthread worker(std::move(task));
                                                  ++launched;
                                                  return worker;
                                              });
        } catch (...) {
            failure = std::current_exception();
        }
        returned.set_value();
    });
    launchFailed.wait();
    const bool returnedEarly = returnState.wait_for(std::chrono::milliseconds(30)) == std::future_status::ready;
    releaseRemaining.count_down();
    caller.join();
    Require(failAfter == 0 || !returnedEarly,
            "partial startup failure waits for already running workers and their output storage");
    Require(static_cast<bool>(failure), "partial startup failure must reach the caller");
    try {
        std::rethrow_exception(failure);
    } catch (const std::system_error& startupFailure) {
        Require(startupFailure.code() == error,
                "the startup failure remains primary when an earlier worker also fails");
        Require(launched == failAfter && completed == failAfter,
                "every successfully started task joins before partial startup failure propagates");
        return;
    }
    throw std::runtime_error("partial startup failure must reach the caller");
}

#ifdef _WIN32
void NativeCreationFailure() {
    unsigned invoked = 0;
    auto work = [&invoked] { ++invoked; };
    try {
        ParallelTasks::Detail::RunNative(WorkerCount, work, [](PTP_WORK_CALLBACK, void*) -> PTP_WORK {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            return nullptr;
        });
    } catch (const std::system_error& failure) {
        Require(failure.code().value() == ERROR_NOT_ENOUGH_MEMORY && invoked == 0,
                "native work creation failure preserves GetLastError and never submits a null work item");
        return;
    }
    throw std::runtime_error("native work creation failure must reach the caller");
}
#endif
} // namespace

int main() try {
    SuccessfulRetry();
    WorkerFailure(FailureKind::Allocation);
    WorkerFailure(FailureKind::NativeResource);
    for (const unsigned failAfter : {0U, 1U, 3U})
        PartialStartFailure(failAfter);
#ifdef _WIN32
    NativeCreationFailure();
#endif
    SuccessfulRetry();
    std::puts("PASS: production parallel tasks join before propagating resource failures and permit retry");
    return 0;
} catch (const std::exception& failure) {
    std::fprintf(stderr, "FAIL: %s\n", failure.what());
    return 1;
}
