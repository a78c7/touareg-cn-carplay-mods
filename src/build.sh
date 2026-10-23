#!/bin/sh
# Cross-build the native parts for the MH2p head unit (QNX 6.6, ARMv7 Tegra K1) on Linux with clang + GNU ld.
# Needs:
#   - clang (tested 18), arm-linux-gnueabi binutils (ld)
#   - QNX 6.5/6.6 ARMv7 sysroot: https://github.com/luka-dev/qnx65-armv7-toolchain  (sdp/target/qnx6)
#   - QNX screen headers (screen/screen.h) for cl_render, e.g. from the same toolchain repo
#   - zlib source (https://github.com/madler/zlib, tested 1.3.1) for vhpatch: ZLIB_SRC=/path/to/zlib
# usage: QNX_TARGET=/path/to/sdp/target/qnx6 SCREEN_INC=/path/with/screen_h ZLIB_SRC=/path/to/zlib ./build.sh
# QNX 6.6 libc has no __aeabi_memcpy/memclr/ul2d (clang emits them) -> aeabi_shim.h provides them.
# Link with --hash-style=both (the QNX loader needs DT_HASH); lld-linked .so made dlsym(RTLD_NEXT) fail.
set -e
: ${QNX_TARGET:?set QNX_TARGET to .../sdp/target/qnx6}
: ${SCREEN_INC:=$QNX_TARGET/usr/include}
L=$QNX_TARGET/armle-v7/lib; UL=$QNX_TARGET/armle-v7/usr/lib
CF="--target=armv7a-none-eabi -mfloat-abi=softfp -mfpu=neon -mthumb -fPIC -O2 -D__QNX__ -D__QNXNTO__ -D__ARM__ -D__LITTLEENDIAN__ -nostdinc -isystem $(clang -print-resource-dir)/include -isystem $SCREEN_INC -isystem $QNX_TARGET/usr/include -fno-stack-protector -Wno-format"
EXE_START="--dynamic-linker=/usr/lib/ldqnx.so.2 $L/crt1.o $L/crti.o $L/crtbegin.o"
EXE_END="$L/crtend.o $L/crtn.o"
cd "$(dirname "$0")"; mkdir -p out

# 1) cl_probe.so - LD_PRELOAD into dio_manager
clang $CF -fvisibility=hidden -c cluster-probe/cl_probe.c -o out/cl_probe.o
arm-linux-gnueabi-ld --hash-style=both -shared -soname cl_alt.so -o out/cl_probe.so -L$L -L$UL out/cl_probe.o -lsocket -lc

# 2) clusterd
clang $CF -fvisibility=hidden -c clusterd/clusterd.c -o out/clusterd.o
arm-linux-gnueabi-ld --hash-style=both -o out/clusterd $EXE_START -L$L -L$UL out/clusterd.o -lc $EXE_END

# 3) cl_render (fifthBro cluster.c, CC BY-NC-SA 4.0) - screen_blit path, NvMedia decoder loaded with dlopen
#    libscreen/libEGL/libGLESv2/libz must resolve to the head unit's own libraries at run time: link against
#    stub libraries with the same soname (do NOT use --unresolved-symbols=ignore-all, that crashes).
clang $CF -DUSE_SCREEN_BLIT -DUSE_DASH -Icl_render -include cl_render/glchar.h -w -c cl_render/cluster.c -o out/cluster.o
clang $CF -fvisibility=hidden -c cl_render/shim.c -o out/shim.o
for lib in libscreen.so.1 libEGL.so.1 libGLESv2.so.2 libz.so.2; do
    [ -f out/$lib ] || { echo "need stub out/$lib (copy the real one from the head unit or build a stub with that soname)"; exit 1; }
done
arm-linux-gnueabi-ld --hash-style=both -o out/cl_render $EXE_START -L$L -L$UL out/cluster.o out/shim.o \
    out/libscreen.so.1 out/libEGL.so.1 out/libz.so.2 out/libGLESv2.so.2 -lm -lc $EXE_END

# 4) vhpatch - applies / reverts the full-screen CarPlay byte patch on the car's own viewhandler.zip (zlib built in)
if [ -n "$ZLIB_SRC" ]; then
    for f in adler32 crc32 deflate inflate inffast inftrees trees zutil; do
        clang $CF -w -I"$ZLIB_SRC" -c "$ZLIB_SRC/$f.c" -o out/z_$f.o
    done
    clang $CF -I"$ZLIB_SRC" -c vhpatch/vhpatch.c -o out/vhpatch.o
    arm-linux-gnueabi-ld --hash-style=both -o out/vhpatch $EXE_START -L$L -L$UL out/vhpatch.o out/z_*.o -lc $EXE_END
fi
echo "built: out/cl_probe.so out/clusterd out/cl_render ${ZLIB_SRC:+out/vhpatch}"
