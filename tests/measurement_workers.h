#pragma once

#include <array>
#include <barrier>
#include <exception>
#include <thread>
#include <utility>

namespace MeasurementWorkers {
inline constexpr int Count = 4;

template <typename Work, typename Launch> void Run(const Work& work, const Launch& launch) {
    std::array<std::exception_ptr, Count> failures{};
    std::barrier start(Count);
    std::array<std::jthread, Count> workers;
    int started = 0;
    try {
        for (; started < Count; ++started) {
            workers[started] = launch([&start, &work, &failures, worker = started]() noexcept {
                start.arrive_and_wait();
                try {
                    work(worker);
                } catch (...) {
                    failures[worker] = std::current_exception();
                }
            });
        }
    } catch (...) {
        // Release the started workers before their jthreads join during unwinding.
        for (int missing = started; missing < Count; ++missing)
            start.arrive_and_drop();
        throw;
    }
    for (auto& worker : workers)
        worker.join();
    for (const auto& failure : failures)
        if (failure) std::rethrow_exception(failure);
}

template <typename Work> void Run(const Work& work) {
    Run(work, [](auto task) { return std::jthread(std::move(task)); });
}
} // namespace MeasurementWorkers
