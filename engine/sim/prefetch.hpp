// A hint to the cache: load the memory at p before it is read (docs/bugs.md B59). It changes no result, only how long
// the reads wait; long unit lists walked in list order miss the cache on almost every unit without it.
#pragma once

#include <cstddef>

#if defined(_MSC_VER) && !defined(__clang__)
#include <xmmintrin.h>
#endif

namespace antanta::sim {

inline void Prefetch(const void* p) {
#if defined(_MSC_VER) && !defined(__clang__)
    _mm_prefetch(static_cast<const char*>(p), _MM_HINT_T0);
#else
    __builtin_prefetch(p);
#endif
}

// The cache lines [from, to) bytes into the object at p.
inline void Prefetch_Lines(const void* p, std::size_t from, std::size_t to) {
    for (std::size_t i = from; i < to; i += 64) Prefetch(static_cast<const char*>(p) + i);
}

}  // namespace antanta::sim
