#include "editor/platform/intrinsics_port.h"
#include <array>
#include <limits>
#include <memory>
#include <cstdio>
#include <stdexcept>

namespace {
constexpr uint32_t GuardWord = 0xa5c3f017;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void CheckWord(uint32_t* word) {
    auto* address = reinterpret_cast<long*>(word);
    for (long bit = 0; bit < std::numeric_limits<uint32_t>::digits; ++bit) {
        const uint32_t mask = uint32_t{1} << bit;
        *word = 0;
        const auto unset = _bittest(address, bit);
        const auto resetUnset = _bittestandreset(address, bit);
        Require(!unset && !resetUnset && *word == 0, "an unset bit reads and resets as false");
        const auto firstSet = _bittestandset(address, bit);
        Require(!firstSet && *word == mask, "first set returns the prior bit and changes 32 bits");
        const auto set = _bittest(address, bit);
        const auto repeatedSet = _bittestandset(address, bit);
        Require(set && repeatedSet && *word == mask, "repeated set retains the bit and reports its prior state");
        const auto resetSet = _bittestandreset(address, bit);
        Require(resetSet && *word == 0, "reset reports and clears the original bit");
        unsigned long highest = 0;
        const auto scanned = _BitScanReverse(&highest, mask);
        Require(scanned && highest == static_cast<unsigned long>(bit),
                "reverse scanning returns every original 32-bit index");
    }
}
} // namespace

int main() try {
    // VectorISA casts uint32_t storage to the MSVC-compatible long* signature.
    // Linux long is wider: accesses must still touch exactly four bytes.
    alignas(long) std::array<uint32_t, 2> adjacent = {0, GuardWord};
    CheckWord(adjacent.data());
    Require(adjacent[1] == GuardWord, "neighboring storage is unchanged");
    const auto single = std::make_unique<uint32_t>(0);
    CheckWord(single.get());
    unsigned long index = 0;
    Require(!_BitScanReverse(&index, 0), "zero has no set bit");
    std::puts("PASS: original 32-bit bit operations retain state and memory boundaries on LP64");
    return 0;
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
