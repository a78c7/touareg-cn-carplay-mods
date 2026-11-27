#!/bin/sh
# Cross-build the Baidu CarLife enhancement native binaries for the VW Touareg CR7 MH2p head unit
# (QNX 6.6, ARMv7 Tegra K1) on Linux x86_64 with clang + GNU arm-linux-gnueabi-ld.
#
# Outputs:
#   - out/carlife_probe.so : LD_PRELOAD enhancement probe for /carlife/carlife
#   - out/vhpatch_carlife  : On-car in-place patcher/reverter for skin3/viewhandler.zip (static zlib)
#
# Requirements:
#   - clang (tested 14/18) and arm-linux-gnueabi binutils (ld, strip)
#   - QNX 6.5/6.6 ARMv7 sysroot: https://github.com/luka-dev/qnx65-armv7-toolchain (sdp/target/qnx6)
#   - zlib source (https://github.com/madler/zlib, tested 1.3.1) for vhpatch_carlife: ZLIB_SRC=/path/to/zlib
#
# Usage:
#   QNX_TARGET=/path/to/sdp/target/qnx6 ZLIB_SRC=/path/to/zlib-1.3.1 sh ./build.sh
#
# Key cross-compilation constraints:
#   1. QNX 6.6 libc lacks clang-emitted __aeabi_* helpers -> aeabi_shim.h supplies hidden static shims.
#   2. Must link with GNU arm-linux-gnueabi-ld and --hash-style=both (emits both DT_HASH and DT_GNU_HASH;
#      never use lld, otherwise QNX ldqnx dlsym(RTLD_NEXT) fails at runtime).
#
# Licensed under PolyForm Noncommercial 1.0.0 + LICENSE.ADDITIONAL-TERMS.md.
set -e

: "${QNX_TARGET:?set QNX_TARGET to .../sdp/target/qnx6}"

L="$QNX_TARGET/armle-v7/lib"
UL="$QNX_TARGET/armle-v7/usr/lib"
RES_INC="$(clang -print-resource-dir)/include"

CF="--target=armv7a-none-eabi -mfloat-abi=softfp -mfpu=neon -mthumb -fPIC -O2 -D__QNX__ -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ -nostdinc -I. -isystem $RES_INC -isystem $QNX_TARGET/usr/include -fno-stack-protector -Wno-format"
EXE_START="--dynamic-linker=/usr/lib/ldqnx.so.2 $L/crt1.o $L/crti.o $L/crtbegin.o"
EXE_END="$L/crtend.o $L/crtn.o"

cd "$(dirname "$0")"
OUT_DIR="${OUT_DIR:-./out}"
mkdir -p "$OUT_DIR"

echo "==> [1/2] Building $OUT_DIR/carlife_probe.so ..."
clang $CF -fvisibility=hidden -c carlife_probe.c -o "$OUT_DIR/carlife_probe.o"
arm-linux-gnueabi-ld --hash-style=both -shared -soname carlife_probe.so \
    -o "$OUT_DIR/carlife_probe.so" -L"$L" -L"$UL" "$OUT_DIR/carlife_probe.o" -lc
arm-linux-gnueabi-strip --strip-unneeded "$OUT_DIR/carlife_probe.so"

if [ -n "${ZLIB_SRC:-}" ]; then
    echo "==> [2/2] Building $OUT_DIR/vhpatch_carlife (embedded zlib) ..."
    for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do
        clang $CF -w -I"$ZLIB_SRC" -c "$ZLIB_SRC/$f.c" -o "$OUT_DIR/z_$f.o"
    done
    clang $CF -I"$ZLIB_SRC" -c vhpatch_carlife.c -o "$OUT_DIR/vhpatch_carlife.o"
    arm-linux-gnueabi-ld --hash-style=both -o "$OUT_DIR/vhpatch_carlife" \
        $EXE_START -L"$L" -L"$UL" "$OUT_DIR/vhpatch_carlife.o" "$OUT_DIR"/z_*.o -lc $EXE_END
    arm-linux-gnueabi-strip "$OUT_DIR/vhpatch_carlife"
else
    echo "==> [2/2] Skipping vhpatch_carlife (ZLIB_SRC not set)"
fi

rm -f "$OUT_DIR"/*.o
ls -lh "$OUT_DIR"
