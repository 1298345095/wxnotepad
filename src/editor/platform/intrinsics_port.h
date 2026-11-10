// Portability support for the pinned Notepad4 core on Linux.
#pragma once
#include <cstdint>
#include <cstring>
inline unsigned char _BitScanReverse(unsigned long* index, unsigned int value) {
    if (!value) return 0;
    *index = 31 - __builtin_clz(value);
    return 1;
}
inline unsigned char _bittest(const long* address, long index) {
    uint32_t value;
    std::memcpy(&value, address, sizeof(value));
    return (value >> index) & 1;
}
inline unsigned char _bittestandset(long* address, long index) {
    uint32_t value;
    std::memcpy(&value, address, sizeof(value));
    const bool old = (value >> index) & 1;
    value |= 1u << index;
    std::memcpy(address, &value, sizeof(value));
    return old;
}
inline unsigned char _bittestandreset(long* address, long index) {
    uint32_t value;
    std::memcpy(&value, address, sizeof(value));
    const bool old = (value >> index) & 1;
    value &= ~(1u << index);
    std::memcpy(address, &value, sizeof(value));
    return old;
}
