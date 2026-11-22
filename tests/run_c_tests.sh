#!/bin/sh
# Build and run all host C unit tests with AddressSanitizer + UndefinedBehaviorSanitizer.
set -e
cd "$(dirname "$0")/.."

CC=${CC:-cc}
SAN_FLAGS="-Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"

TMPDIR_TESTS=$(mktemp -d /tmp/touareg_c_tests.XXXXXX)
trap 'rm -rf "$TMPDIR_TESTS"' EXIT

echo "[1/3] test_wired_rg (ASan + UBSan)..."
$CC $SAN_FLAGS -Isrc/cluster-probe/wired \
    src/cluster-probe/wired/cl_wired_rg.c src/cluster-probe/wired/test_wired_rg.c \
    -lpthread -o "$TMPDIR_TESTS/test_wired_rg"
"$TMPDIR_TESTS/test_wired_rg"

echo "[2/3] test_ota (SHA-256 + Ed25519 + OTA security + diag redaction, ASan + UBSan)..."
$CC $SAN_FLAGS -I. -Isrc/touareg_netd \
    tests/test_ota.c -o "$TMPDIR_TESTS/test_ota"
"$TMPDIR_TESTS/test_ota"

echo "[3/3] test_netd (fail-closed PIN + per-IP rate limit + HTTP routing, ASan + UBSan)..."
$CC $SAN_FLAGS -I. -Isrc/common -Isrc/touareg_netd \
    tests/test_netd.c -lpthread -o "$TMPDIR_TESTS/test_netd"
"$TMPDIR_TESTS/test_netd"

echo "ALL C UNIT TESTS PASSED (ASan + UBSan clean)."
