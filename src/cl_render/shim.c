#include <stdio.h>
#include <stdint.h>
#include "aeabi_shim.h"
__attribute__((visibility("hidden"), used)) double __aeabi_ul2d(unsigned long long x) {
    volatile uint32_t hi = (uint32_t)(x >> 32), lo = (uint32_t)x;
    return (double)hi * 4294967296.0 + (double)lo;
}
FILE *popen(const char *c, const char *m) { (void)c; (void)m; return NULL; }
int pclose(FILE *f) { (void)f; return -1; }
