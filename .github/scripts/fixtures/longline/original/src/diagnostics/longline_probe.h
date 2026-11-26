#pragma once

// Temporary Linux diagnosis only. One GUI thread writes fixed-size records to
// shared mapped storage; CTest's original SIGQUIT timeout retains unfinished
// spans. No callbacks, dispatch, file flushing, or per-character hooks are added.
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

namespace LonglineProbe {
enum class Kind : std::uint64_t {
    ScenarioBegin = 1, ScenarioEnd, Stage,
    ShortcutBegin = 10, ShortcutEnd, FocusBegin, FocusEnd,
    RequestBegin, RequestEnd, DispatchBegin, DispatchEnd, Release,
    FinalDispatchBegin, FinalDispatchEnd,
    DrawBegin = 30, DrawEnd, Viewport, PaintBegin, PaintEnd,
    CaretTickBegin = 40, CaretTickEnd, IdleBegin, IdleEnd,
    NativeIdleAdded = 50, NativeIdleRemoved,
    MapShown = 60, MapAcknowledged, MapWaitBegin, MapWaitEnd,
};

struct Header {
    char magic[8];
    std::uint64_t version, recordBytes, capacity, published, dropped, pid, reserved;
};
struct Record {
    std::uint64_t sequence;
    std::int64_t wallNs, threadCpuNs;
    std::uint64_t kind, shortcut, span;
    std::int64_t a, b, c, d;
};
inline constexpr std::size_t Capacity = 8192;
struct Storage { Header header; Record records[Capacity]; };
static_assert(sizeof(Header) == 64 && sizeof(Record) == 80);

inline Storage* storage = nullptr;
inline bool active = false;
inline std::uint64_t currentShortcut = 0;

inline std::int64_t Clock(clockid_t id) noexcept {
    timespec value{};
    return clock_gettime(id, &value) == 0 ? value.tv_sec * INT64_C(1000000000) + value.tv_nsec : -1;
}

inline std::uint64_t Event(Kind kind, std::uint64_t span = 0,
                           std::int64_t a = 0, std::int64_t b = 0,
                           std::int64_t c = 0, std::int64_t d = 0) noexcept {
    if (!active) return 0;
    auto& header = storage->header;
    const auto index = header.published;
    if (index == Capacity) { ++header.dropped; return 0; }
    auto& row = storage->records[index];
    row.wallNs = Clock(CLOCK_MONOTONIC);
    row.threadCpuNs = Clock(CLOCK_THREAD_CPUTIME_ID);
    row.kind = static_cast<std::uint64_t>(kind);
    row.shortcut = currentShortcut;
    row.span = span;
    row.a = a; row.b = b; row.c = c; row.d = d;
    // Commit the row before publishing its count. A timeout can leave at most
    // one unpublished row, which the offline reader deliberately ignores.
    std::atomic_ref(row.sequence).store(index + 1, std::memory_order_release);
    std::atomic_ref(header.published).store(index + 1, std::memory_order_release);
    return index + 1;
}

inline std::uint64_t Start() {
    const char* path = std::getenv("WXNOTEPAD_PAINT_TRACE");
    if (!path || !*path) return 0;
    if (storage) throw std::runtime_error("longline observation may start only once per process");
    const int fd = open(path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) throw std::runtime_error(std::string("open longline trace: ") + std::strerror(errno));
    if (ftruncate(fd, static_cast<off_t>(sizeof(Storage))) != 0) {
        const int error = errno;
        close(fd);
        throw std::runtime_error(std::string("size longline trace: ") + std::strerror(error));
    }
    void* memory = mmap(nullptr, sizeof(Storage), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    const int error = errno;
    close(fd);
    if (memory == MAP_FAILED)
        throw std::runtime_error(std::string("map longline trace: ") + std::strerror(error));
    storage = static_cast<Storage*>(memory);
    std::memset(storage, 0, sizeof(Storage));
    std::memcpy(storage->header.magic, "WXLP275", 8);
    storage->header.version = 1;
    storage->header.recordBytes = sizeof(Record);
    storage->header.capacity = Capacity;
    storage->header.pid = static_cast<std::uint64_t>(getpid());
    active = true;
    return Event(Kind::ScenarioBegin);
}

class Scenario final {
    std::uint64_t begin_;
public:
    Scenario() : begin_(Start()) {}
    ~Scenario() {
        if (begin_) Event(Kind::ScenarioEnd, begin_, std::uncaught_exceptions());
        active = false;
    }
};

class Scope final {
    Kind end_;
    std::uint64_t begin_;
public:
    Scope(Kind begin, Kind end, std::int64_t a = 0, std::int64_t b = 0,
          std::int64_t c = 0, std::int64_t d = 0) noexcept
        : end_(end), begin_(Event(begin, 0, a, b, c, d)) {}
    ~Scope() { if (begin_) Event(end_, begin_, std::uncaught_exceptions()); }
    std::uint64_t Id() const noexcept { return begin_; }
};

class Shortcut final {
    std::uint64_t previous_;
    std::uint64_t begin_;
public:
    Shortcut(int key, bool control, bool alt, bool shift) noexcept
        : previous_(currentShortcut),
          begin_(Event(Kind::ShortcutBegin, 0, key, control, alt, shift)) {
        if (begin_) currentShortcut = begin_;
    }
    ~Shortcut() {
        if (begin_) Event(Kind::ShortcutEnd, begin_, std::uncaught_exceptions());
        currentShortcut = previous_;
    }
};

inline void Stage(const char* label) noexcept {
    if (!active) return;
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const char* c = label; *c; ++c) {
        hash ^= static_cast<unsigned char>(*c);
        hash *= UINT64_C(1099511628211);
    }
    Event(Kind::Stage, 0, static_cast<std::int64_t>(hash));
}
} // namespace LonglineProbe
