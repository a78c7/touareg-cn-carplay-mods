#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "aeabi_shim.h"
__attribute__((visibility("hidden"), used)) double __aeabi_ul2d(unsigned long long x) {
    volatile uint32_t hi = (uint32_t)(x >> 32), lo = (uint32_t)x;
    return (double)hi * 4294967296.0 + (double)lo;
}
__attribute__((visibility("hidden"), used)) float __aeabi_ul2f(unsigned long long x) {
    volatile uint32_t hi = (uint32_t)(x >> 32), lo = (uint32_t)x;
    return (float)((double)hi * 4294967296.0 + (double)lo);
}
__attribute__((visibility("hidden"), used)) double __aeabi_l2d(long long x) {
    if (x >= 0) {
        uint64_t u = (uint64_t)x;
        volatile uint32_t hi = (uint32_t)(u >> 32), lo = (uint32_t)u;
        return (double)hi * 4294967296.0 + (double)lo;
    } else {
        uint64_t u = (uint64_t)(-(x + 1)) + 1ULL;
        volatile uint32_t hi = (uint32_t)(u >> 32), lo = (uint32_t)u;
        return -((double)hi * 4294967296.0 + (double)lo);
    }
}
__attribute__((visibility("hidden"), used)) long long __aeabi_d2lz(double x) {
    union { double d; uint64_t u; } v;
    v.d = x;
    int neg = (int)(v.u >> 63);
    int exp = (int)((v.u >> 52) & 0x7ffU) - 1023;
    uint64_t mant = (v.u & 0x000fffffffffffffULL) | 0x0010000000000000ULL;
    if (exp < 0) return 0;
    if (exp >= 63) return neg ? (-9223372036854775807LL - 1LL) : 9223372036854775807LL;
    uint64_t mag = (exp >= 52) ? (mant << (exp - 52)) : (mant >> (52 - exp));
    return neg ? -(long long)mag : (long long)mag;
}
/* QuickJS default path references malloc_usable_size; runtime uses JS_NewRuntime2 tracked allocator. */
__attribute__((visibility("hidden"), used)) size_t malloc_usable_size(void *ptr) {
    (void)ptr;
    return 0;
}
FILE *popen(const char *c, const char *m) { (void)c; (void)m; return NULL; }
int pclose(FILE *f) { (void)f; return -1; }
