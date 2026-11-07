#pragma once

#include <chrono>

namespace TestClock {
// Native polling budgets must survive corrections to the system's UTC clock.
class StopWatch {
public:
    void Start() { started_ = std::chrono::steady_clock::now(); }
    long Time() const {
        return static_cast<long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_).count());
    }

private:
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
};
} // namespace TestClock
