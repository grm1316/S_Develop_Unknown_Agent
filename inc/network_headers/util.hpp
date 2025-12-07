#ifndef UTIL_HPP
#define UTIL_HPP

#include <cstdint>

#ifdef __linux__
inline uint16_t MAKEWORD(uint8_t& a, uint8_t& b) {
    //return (static_cast<uint16_t>(a) | (static_cast<uint16_t>(b) << 8));
    return (static_cast<uint16_t>(a) << 8 | (static_cast<uint16_t>(b)));
}
#endif

#ifdef _WIN32
inline void* MALLOC(size_t size) {
    return HeapAlloc(GetProcessHeap(), 0, size);
}

inline void FREE(LPVOID ptr) {
    HeapFree(GetProcessHeap(), 0, ptr);
}
#endif


#endif // UTIL_HPP
