#ifndef QNX_CLEAN_MATH_H
#define QNX_CLEAN_MATH_H
#include_next <math.h>
#include <stdint.h>

/*
 * 清除 QNX 6.5 Dinkumware 私有符号宏（_Sin / _LDclass / _LDsign 等），
 * 改用纯 IEEE-754 位运算 + 标准 C99 函数，确保在 QNX 6.6 实车 libm.so.2 上零私有 ABI 风险。
 */
static inline uint64_t qnx_d2u(double x) {
    union { double d; uint64_t u; } v;
    v.d = x;
    return v.u;
}
#undef isnan
#define isnan(x) (((qnx_d2u((double)(x)) & 0x7fffffffffffffffULL) > 0x7ff0000000000000ULL))
#undef isinf
#define isinf(x) (((qnx_d2u((double)(x)) & 0x7fffffffffffffffULL) == 0x7ff0000000000000ULL))
#undef isfinite
#define isfinite(x) (((qnx_d2u((double)(x)) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL))
#undef signbit
#define signbit(x) ((int)(qnx_d2u((double)(x)) >> 63))
#undef sin
#undef cos
extern double (sin)(double);
extern double (cos)(double);
#endif
