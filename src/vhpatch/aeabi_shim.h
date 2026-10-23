/* QNX 6.6 libc has no __aeabi_mem* helpers that clang may emit; provide them locally. */
#include <stddef.h>
#include <string.h>
#define AEABI_HIDDEN __attribute__((visibility("hidden"), used))
static void *(*volatile aeabi_mc_)(void *, const void *, size_t) = memcpy;
static void *(*volatile aeabi_mm_)(void *, const void *, size_t) = memmove;
static void *(*volatile aeabi_ms_)(void *, int, size_t) = memset;
AEABI_HIDDEN void __aeabi_memcpy(void *d, const void *s, size_t n) { aeabi_mc_(d, s, n); }
AEABI_HIDDEN void __aeabi_memcpy4(void *d, const void *s, size_t n) { aeabi_mc_(d, s, n); }
AEABI_HIDDEN void __aeabi_memcpy8(void *d, const void *s, size_t n) { aeabi_mc_(d, s, n); }
AEABI_HIDDEN void __aeabi_memmove(void *d, const void *s, size_t n) { aeabi_mm_(d, s, n); }
AEABI_HIDDEN void __aeabi_memset(void *d, size_t n, int c) { aeabi_ms_(d, c, n); }
AEABI_HIDDEN void __aeabi_memclr(void *d, size_t n) { aeabi_ms_(d, 0, n); }
AEABI_HIDDEN void __aeabi_memclr4(void *d, size_t n) { aeabi_ms_(d, 0, n); }
AEABI_HIDDEN void __aeabi_memclr8(void *d, size_t n) { aeabi_ms_(d, 0, n); }
