// Portability support for the pinned Notepad4 core on Linux.
#pragma once
#include <cstdint>
inline unsigned char _BitScanReverse(unsigned long* index,unsigned int value) {
    if(!value) return 0; *index=31-__builtin_clz(value); return 1;
}
inline unsigned char _bittest(const long* address,long index) {
    return (*reinterpret_cast<const uint32_t*>(address)>>index)&1;
}
inline unsigned char _bittestandset(long* address,long index) {
    auto& value=*reinterpret_cast<uint32_t*>(address); const bool old=(value>>index)&1; value|=1u<<index; return old;
}
inline unsigned char _bittestandreset(long* address,long index) {
    auto& value=*reinterpret_cast<uint32_t*>(address); const bool old=(value>>index)&1; value&=~(1u<<index); return old;
}
