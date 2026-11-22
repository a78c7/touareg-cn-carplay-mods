#!/bin/sh
# Build and run all host C unit tests with AddressSanitizer + UndefinedBehaviorSanitizer.
set -e
cd "$(dirname "$0")/.."

CC=${CC:-cc}
SAN_FLAGS="-Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"

TMPDIR_TESTS=$(mktemp -d /tmp/touareg_c_tests.XXXXXX)
trap 'rm -rf "$TMPDIR_TESTS"' EXIT

echo "[1/4] test_wired_rg (ASan + UBSan)..."
$CC $SAN_FLAGS -Isrc/cluster-probe/wired \
    src/cluster-probe/wired/cl_wired_rg.c src/cluster-probe/wired/test_wired_rg.c \
    -lpthread -o "$TMPDIR_TESTS/test_wired_rg"
"$TMPDIR_TESTS/test_wired_rg"

echo "[2/4] test_ota (SHA-256 + Ed25519 + OTA security + diag redaction, ASan + UBSan)..."
$CC $SAN_FLAGS -I. -Isrc/touareg_netd \
    tests/test_ota.c -o "$TMPDIR_TESTS/test_ota"
"$TMPDIR_TESTS/test_ota"

echo "[3/4] test_netd (fail-closed PIN + per-IP rate limit + HTTP routing, ASan + UBSan)..."
$CC $SAN_FLAGS -I. -Isrc/common -Isrc/touareg_netd \
    tests/test_netd.c -lpthread -o "$TMPDIR_TESTS/test_netd"
"$TMPDIR_TESTS/test_netd"

echo "[4/4] test_v23_theme (QuickJS 60fps sandbox + 7 fuses + cold-start + 72KB/140KB + night, ASan + UBSan)..."
for qf in quickjs dtoa libregexp libunicode cutils; do
    $CC -O1 -g -fwrapv -DNDEBUG -DQJS_NO_ATOMICS -DCONFIG_VERSION=\"2026-06-04\" \
        -fno-omit-frame-pointer -w \
        -Isrc/cl_render/quickjs -c "src/cl_render/quickjs/$qf.c" -o "$TMPDIR_TESTS/qjs_$qf.o"
done
$CC $SAN_FLAGS -DNDEBUG -DQJS_NO_ATOMICS -Isrc/cl_render \
    -c tests/test_v23_theme.c -o "$TMPDIR_TESTS/test_v23_theme.o"
$CC -fsanitize=address,undefined \
    "$TMPDIR_TESTS/test_v23_theme.o" "$TMPDIR_TESTS"/qjs_*.o \
    -lm -o "$TMPDIR_TESTS/test_v23_theme"
"$TMPDIR_TESTS/test_v23_theme"

echo "ALL C UNIT TESTS PASSED (ASan + UBSan clean)."
