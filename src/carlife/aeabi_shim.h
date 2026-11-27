/*
 * aeabi_shim.h — QNX 6.6 ARMv7 libc 缺失的 clang __aeabi_* 辅助函数静态补齐
 * 适用于 carlife_probe.so、carlife_clusterd、vhpatch_carlife 共用。
 * 所有符号均设为 hidden，绝不污染进程动态符号表。
 *
 * Licensed under PolyForm Noncommercial 1.0.0 + LICENSE.ADDITIONAL-TERMS.md.
 */
#ifndef AEABI_SHIM_H
#define AEABI_SHIM_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define AEABI_HIDDEN __attribute__((visibility("hidden"), used))

static void *(*volatile aeabi_mc_)(void *, const void *, size_t) = memcpy;
static void *(*volatile aeabi_mm_)(void *, const void *, size_t) = memmove;
static void *(*volatile aeabi_ms_)(void *, int, size_t) = memset;

AEABI_HIDDEN void __aeabi_memcpy(void *d, const void *s, size_t n) { aeabi_mc_(d, s, n); }
AEABI_HIDDEN void __aeabi_memcpy4(void *d, const void *s, size_t n) { aeabi_mc_(d, s, n); }
AEABI_HIDDEN void __aeabi_memcpy8(void *d, const void *s, size_t n) { aeabi_mc_(d, s, n); }

AEABI_HIDDEN void __aeabi_memmove(void *d, const void *s, size_t n) { aeabi_mm_(d, s, n); }
AEABI_HIDDEN void __aeabi_memmove4(void *d, const void *s, size_t n) { aeabi_mm_(d, s, n); }
AEABI_HIDDEN void __aeabi_memmove8(void *d, const void *s, size_t n) { aeabi_mm_(d, s, n); }

AEABI_HIDDEN void __aeabi_memset(void *d, size_t n, int c) { aeabi_ms_(d, c, n); }
AEABI_HIDDEN void __aeabi_memset4(void *d, size_t n, int c) { aeabi_ms_(d, c, n); }
AEABI_HIDDEN void __aeabi_memset8(void *d, size_t n, int c) { aeabi_ms_(d, c, n); }

AEABI_HIDDEN void __aeabi_memclr(void *d, size_t n) { aeabi_ms_(d, 0, n); }
AEABI_HIDDEN void __aeabi_memclr4(void *d, size_t n) { aeabi_ms_(d, 0, n); }
AEABI_HIDDEN void __aeabi_memclr8(void *d, size_t n) { aeabi_ms_(d, 0, n); }

#ifdef AEABI_SHIM_WITH_FLOAT
AEABI_HIDDEN double __aeabi_ul2d(unsigned long long x) {
    volatile uint32_t hi = (uint32_t)(x >> 32);
    volatile uint32_t lo = (uint32_t)x;
    return (double)hi * 4294967296.0 + (double)lo;
}

AEABI_HIDDEN float __aeabi_ul2f(unsigned long long x) {
    volatile uint32_t hi = (uint32_t)(x >> 32);
    volatile uint32_t lo = (uint32_t)x;
    return (float)((double)hi * 4294967296.0 + (double)lo);
}

AEABI_HIDDEN double __aeabi_l2d(long long x) {
    if (x >= 0) {
        uint64_t u = (uint64_t)x;
        volatile uint32_t hi = (uint32_t)(u >> 32);
        volatile uint32_t lo = (uint32_t)u;
        return (double)hi * 4294967296.0 + (double)lo;
    } else {
        uint64_t u = (uint64_t)(-(x + 1)) + 1ULL;
        volatile uint32_t hi = (uint32_t)(u >> 32);
        volatile uint32_t lo = (uint32_t)u;
        return -((double)hi * 4294967296.0 + (double)lo);
    }
}

AEABI_HIDDEN long long __aeabi_d2lz(double x) {
    union {
        double d;
        uint64_t u;
    } v;
    v.d = x;
    int neg = (int)(v.u >> 63);
    int exp = (int)((v.u >> 52) & 0x7FFu) - 1023;
    uint64_t mant = (v.u & 0x000FFFFFFFFFFFFFULL) | 0x0010000000000000ULL;
    if (exp < 0) return 0;
    if (exp >= 63) return neg ? (-9223372036854775807LL - 1LL) : 9223372036854775807LL;
    uint64_t mag = (exp >= 52) ? (mant << (exp - 52)) : (mant >> (52 - exp));
    return neg ? -(long long)mag : (long long)mag;
}
#endif

#endif /* AEABI_SHIM_H */
