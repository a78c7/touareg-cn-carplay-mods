#!/bin/ksh
# VW_ShopBypassARollback - 变体 A 绕过方案一键原样还原模块（VW_ShopBypassA 配套后悔药）
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL)
#
# 用途：
#   将 servicemgrmibhigh0.real（原厂 493KB ELF）原子还原回 servicemgrmibhigh0，
#   恢复 U 盘刷机兼容性（还原后上游 modkit_install.sh 可正常识别原厂 ELF，不再报 unknown patch state）。
#   即使更换了 U 盘，只要车机上的 servicemgrmibhigh0.real 验明为原厂 ELF 且 servicemgrmibhigh0 为 VW_ShopBypassA
#   即可安全还原；若使用同一根带 ShopBackupA/orig/MANIFEST.txt 的 U 盘，还会额外做固件号与 cksum 双重核对。

echo "=== VW_ShopBypassARollback (restore stock servicemgrmibhigh0, Issue #10/#17) ==="

_APP="${BYPASS_APP_ROOT:-/mnt/app}"
_SKIP_MOUNT="${BYPASS_SKIP_MOUNT:-0}"

# 1) 固件校验
case "$RELEASE_VERSION" in
    MH2p_CN_VWG36_P*) ;;
    *)
        echo "abort: built for CN VW Touareg P-series (MH2p_CN_VWG36_P*), this unit is '${RELEASE_VERSION:-unknown}'"
        exit 0
        ;;
esac
echo "firmware: $RELEASE_VERSION"

# 2) 定位 U 盘根目录
USB_ROOT="$MEDIA_PATH"
if [ -z "$USB_ROOT" ] && [ -n "$MOD_PATH" ]; then
    _t1="${MOD_PATH%/Update}"
    _t2="${_t1%/*}"
    USB_ROOT="${_t2%/Mods}"
fi
if [ -n "$USB_ROOT" ] && [ -d "$USB_ROOT/Mods/VW_ShopBypassA" ] && [ -d "$USB_ROOT/Mods/VW_ShopBypassARollback" ]; then
    echo "abort: DO NOT place VW_ShopBypassA and VW_ShopBypassARollback in Mods/ at the same time!"
    exit 0
fi

if [ "$_SKIP_MOUNT" != "1" ]; then
    [ ! -e "$_APP/eso" ] && mount -t qnx6 /dev/mnanda0t177.1 "$_APP" 2>/dev/null || true
fi

# ---- 工具函数 ----
is_symlink() {
    case "$(ls -ld "$1" 2>/dev/null)" in
        l*) return 0 ;;
        *)  return 1 ;;
    esac
}

is_regular_nonlink() {
    [ -f "$1" ] || return 1
    is_symlink "$1" && return 1
    return 0
}

is_elf() {
    is_regular_nonlink "$1" || return 1
    [ "$(dd if="$1" bs=1 skip=1 count=3 2>/dev/null)" = "ELF" ]
}

file_size() {
    is_regular_nonlink "$1" || { echo 0; return 1; }
    ls -l "$1" 2>/dev/null | awk 'NR==1 { print $5 + 0 }'
}

file_cksum() {
    if command -v cksum >/dev/null 2>&1; then
        cksum "$1" 2>/dev/null | awk 'NR==1 { print $1 ":" $2 }'
    else
        echo "nocrc:$(file_size "$1")"
    fi
}

files_match() {
    _fa="$1"
    _fb="$2"
    is_regular_nonlink "$_fa" && is_regular_nonlink "$_fb" || return 1
    _sa=$(file_size "$_fa")
    _sb=$(file_size "$_fb")
    [ -n "$_sa" ] && [ "$_sa" -gt 0 ] && [ "$_sa" = "$_sb" ] || return 1
    if command -v cmp >/dev/null 2>&1; then
        cmp -s "$_fa" "$_fb" || return 1
    elif command -v cksum >/dev/null 2>&1; then
        [ "$(file_cksum "$_fa")" = "$(file_cksum "$_fb")" ] || return 1
    fi
    return 0
}

manifest_val() {
    awk -F= -v k="$2" '$1 == k { sub(/\r$/, "", $2); print $2; exit }' "$1" 2>/dev/null
}

_BIN="$_APP/eso/bin"
_ENTRY0="$_BIN/servicemgrmibhigh0"
_ENTRY0_REAL="$_BIN/servicemgrmibhigh0.real"

echo "--- [1/2] Verifying stock servicemgrmibhigh0.real integrity ---"

# 幂等：如果已经是原厂 ELF 且无 .real，直接退出
if is_elf "$_ENTRY0" && [ ! -e "$_ENTRY0_REAL" ]; then
    echo "already restored: servicemgrmibhigh0 is stock ELF ($(file_size "$_ENTRY0") B). Nothing to do."
    exit 0
fi

if ! is_elf "$_ENTRY0_REAL"; then
    echo "abort: $_ENTRY0_REAL is missing or NOT an ELF binary! Refusing to touch servicemgrmibhigh0."
    exit 0
fi
_real_sz=$(file_size "$_ENTRY0_REAL")
if [ "$_real_sz" -lt 400000 ]; then
    echo "abort: $_ENTRY0_REAL size (${_real_sz} B) is unexpectedly small (<400000 B). Nothing touched."
    exit 0
fi

# 若存在 U 盘原始清单，则叠加固件号与大小/校验和双重校验
MF=""
if [ -n "$USB_ROOT" ] && [ -f "$USB_ROOT/ShopBackupA/orig/MANIFEST.txt" ]; then
    MF="$USB_ROOT/ShopBackupA/orig/MANIFEST.txt"
    _bk_fw=$(manifest_val "$MF" "RELEASE_VERSION")
    if [ -n "$_bk_fw" ] && [ "$_bk_fw" != "$RELEASE_VERSION" ]; then
        echo "abort: USB backup firmware ($_bk_fw) does not match this unit ($RELEASE_VERSION)!"
        exit 0
    fi
    _exp_sz=$(manifest_val "$MF" "SERVICEMGR0_SIZE")
    if [ -n "$_exp_sz" ] && [ "$_exp_sz" != "$_real_sz" ]; then
        echo "abort: servicemgrmibhigh0.real size mismatch (expected=$_exp_sz actual=$_real_sz)!"
        exit 0
    fi
    _exp_crc=$(manifest_val "$MF" "SERVICEMGR0_CKSUM")
    if [ -n "$_exp_crc" ] && command -v cksum >/dev/null 2>&1; then
        case "$_exp_crc" in
            nocrc:*) ;;
            *)
                _cur_crc=$(file_cksum "$_ENTRY0_REAL")
                if [ "$_exp_crc" != "$_cur_crc" ]; then
                    echo "abort: servicemgrmibhigh0.real checksum mismatch!"
                    exit 0
                fi
                ;;
        esac
    fi
    echo "verified against USB manifest: $_ENTRY0_REAL (${_real_sz} B)"
else
    echo "verified on-car ELF backup: $_ENTRY0_REAL (${_real_sz} B)"
fi

echo "--- [2/2] Atomically restoring stock servicemgrmibhigh0 ---"
if [ "$_SKIP_MOUNT" != "1" ]; then
    mount -uw "$_APP"
fi

cp -f "$_ENTRY0_REAL" "$_ENTRY0.rb"
if ! is_elf "$_ENTRY0.rb" || ! files_match "$_ENTRY0_REAL" "$_ENTRY0.rb"; then
    rm -f "$_ENTRY0.rb"
    echo "abort: staging copy of servicemgrmibhigh0.real failed verification! Keeping existing wrapper."
    exit 0
fi
chmod 755 "$_ENTRY0.rb"
mv -f "$_ENTRY0.rb" "$_ENTRY0"
sync

if ! is_elf "$_ENTRY0" || ! files_match "$_ENTRY0_REAL" "$_ENTRY0"; then
    echo "abort: post-move verification failed; keeping $_ENTRY0_REAL for safety."
    exit 0
fi

rm -f "$_ENTRY0_REAL"
sync
echo "restored stock ELF $_ENTRY0 ($(file_size "$_ENTRY0") B) and removed $_ENTRY0_REAL"
echo "=== VW_ShopBypassARollback done: standard USB ModKit flashing is now unlocked again ==="
