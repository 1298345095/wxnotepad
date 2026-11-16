#include "measurement_workers.h"
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {
constexpr int Iterations = 64;
class RegressionFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
void Require(bool condition, const char* message) {
    if (!condition) throw RegressionFailure(message);
}

void SuccessfulBatch() {
    std::atomic<int> launched = 0;
    std::array<int, MeasurementWorkers::Count> completed{};
    MeasurementWorkers::Run(
        [&launched, &completed](int worker) {
            Require(launched == MeasurementWorkers::Count, "all workers must start before the barrier opens");
            for (int iteration = 0; iteration < Iterations; ++iteration)
                ++completed[worker];
        },
        [&launched](auto task) {
            ++launched;
            return std::jthread(std::move(task));
        });
    for (const auto count : completed)
        Require(count == Iterations, "every worker completes all 64 iterations before returning");
}

void FailedStart(int failAfter) {
    int launched = 0;
    std::atomic<int> completed = 0;
    const auto error = std::make_error_code(std::errc::resource_unavailable_try_again);
    try {
        MeasurementWorkers::Run([&completed](int) { ++completed; },
                                [&launched, failAfter, error](auto task) {
                                    if (launched == failAfter) throw std::system_error(error);
                                    ++launched;
                                    return std::jthread(std::move(task));
                                });
    } catch (const std::system_error& failure) {
        Require(failure.code() == error, "retain the original thread-start failure");
        Require(launched == failAfter && completed == failAfter,
                "all started workers leave the barrier and join before the launch failure propagates");
        return;
    }
    throw RegressionFailure("thread creation failure must propagate");
}

void WorkerFailure() {
    const auto original = std::make_exception_ptr(RegressionFailure("worker failure"));
    std::array<int, MeasurementWorkers::Count> completed{};
    try {
        MeasurementWorkers::Run([&completed, &original](int worker) {
            for (int iteration = 0; iteration < Iterations; ++iteration)
                ++completed[worker];
            if (worker == 2) std::rethrow_exception(original);
        });
    } catch (const RegressionFailure& failure) {
        Require(std::string_view(failure.what()) == "worker failure",
                "transport the original worker exception type and message");
        for (const auto count : completed)
            Require(count == Iterations, "join every other worker before propagating a work failure");
        return;
    }
    throw RegressionFailure("worker exception must propagate");
}
} // namespace

int main() try {
    SuccessfulBatch();
    for (const int started : {0, 1, 3})
        FailedStart(started);
    WorkerFailure();
    SuccessfulBatch();
    std::puts("PASS: measurement workers join after partial startup and preserve work failures");
    return 0;
} catch (const std::exception& failure) {
    std::fprintf(stderr, "FAIL: %s\n", failure.what());
    return 1;
}
