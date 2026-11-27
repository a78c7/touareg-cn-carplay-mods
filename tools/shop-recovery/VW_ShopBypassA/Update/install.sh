#!/bin/ksh
# VW_ShopBypassA - 变体 A 商业防升级还原（109B servicemgrmibhigh + del/shadow）安全绕过模块
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL)
#
# 适用场景（Issue #10 / Issue #17 实车 REPORT.txt 实锤）：
#   1) /mnt/app/eso/bin/servicemgrmibhigh 每次正常开机后都会被底层 /ifs 或 RCC 还原回 109B 商业脚本
#      （仅执行 /eso/bin/servicemgrmibhigh0 & 与 slay -9 challenge/sshd，永远不拉起 modkit_persist.sh）
#   2) /mnt/app/eso/bin/servicemgrmibhigh0 为原厂 493KB ELF 且从未被暗桩还原
#   3) /mnt/app/eso/bin/apps/dio_manager 为原厂 ~958KB ELF（无 dio_manager.org；若为 #16 变体 B 严禁运行本包）
#
# 安全设计原则：
#   - 严苛前置校验：仅放行国行 P 系；拒绝变体 B（dio_manager.org 或小体积 dio_manager、servicemgrmibhigh_orgapp）；
#     核验原厂 servicemgrmibhigh0（>=400KB ELF）验明正身后才动任何文件；校验不通过车机零改动。
#   - 零窗口原子保护：先将 servicemgrmibhigh0 完整拷贝为 servicemgrmibhigh0.real 并逐字节校验落盘，
#     确认 servicemgrmibhigh0.real 为完好原厂 ELF 后，才原子替换 servicemgrmibhigh0 为轻量 wrapper；
#     wrapper 立即 exec 原厂 servicemgrmibhigh0.real "$@"，并在后台防重入拉起 /mnt/ota/modkit/modkit_persist.sh。
#   - 严格遵循 QNX 6.6 约束：仅用 ksh/dd/awk/grep/ls/cp/mv/rm/chmod/sync；不在 /tmp 建子目录（用 noclobber 普通文件锁）；
#     绝不执行 mount -ur /mnt/app；配合 VW_ShopBypassARollback 可随时 1 分钟原样还原。

echo "=== VW_ShopBypassA (Variant A safe bypass, Issue #10/#17) ==="

_APP="${BYPASS_APP_ROOT:-/mnt/app}"
_OTA="${BYPASS_OTA_ROOT:-/mnt/ota}"
_PERSIST="${BYPASS_PERSIST_ROOT:-/mnt/persist_new}"
_SKIP_MOUNT="${BYPASS_SKIP_MOUNT:-0}"

# 1) 固件校验（仅放行国行 15 寸 P 系）
case "$RELEASE_VERSION" in
    MH2p_CN_VWG36_P*) ;;
    *)
        echo "abort: built for CN VW Touareg P-series (MH2p_CN_VWG36_P*), this unit is '${RELEASE_VERSION:-unknown}'"
        exit 0
        ;;
esac
echo "firmware: $RELEASE_VERSION"

# 2) 定位 U 盘根目录并确认可写
USB_ROOT="$MEDIA_PATH"
if [ -z "$USB_ROOT" ] && [ -n "$MOD_PATH" ]; then
    _t1="${MOD_PATH%/Update}"
    _t2="${_t1%/*}"
    USB_ROOT="${_t2%/Mods}"
fi
if [ -z "$USB_ROOT" ] || [ ! -d "$USB_ROOT" ]; then
    echo "abort: cannot determine writable USB root (MEDIA_PATH=$MEDIA_PATH MOD_PATH=$MOD_PATH)"
    exit 0
fi

# 防呆：禁止同时放绕过包与回滚包
if [ -d "$USB_ROOT/Mods/VW_ShopBypassA" ] && [ -d "$USB_ROOT/Mods/VW_ShopBypassARollback" ]; then
    echo "abort: DO NOT place VW_ShopBypassA and VW_ShopBypassARollback in Mods/ at the same time!"
    exit 0
fi
if [ -d "$USB_ROOT/Mods/VW_ShopTakeover" ] || [ -d "$USB_ROOT/Mods/VW_ShopRollback" ]; then
    echo "abort: DO NOT mix Variant A (VW_ShopBypassA) with Variant B (VW_ShopTakeover/VW_ShopRollback)!"
    exit 0
fi

touch "$USB_ROOT/.bypassa_write_test" 2>/dev/null
if [ ! -e "$USB_ROOT/.bypassa_write_test" ]; then
    echo "abort: USB drive $USB_ROOT is write protected"
    exit 0
fi
rm -f "$USB_ROOT/.bypassa_write_test"

# 3) 确保车机分区已挂载（绝不执行 mount -ur /mnt/app）
if [ "$_SKIP_MOUNT" != "1" ]; then
    [ ! -e "$_APP/eso" ] && mount -t qnx6 /dev/mnanda0t177.1 "$_APP" 2>/dev/null || true
    [ ! -e "$_OTA" ] && mount -t qnx6 /dev/mnanda0t177.16 "$_OTA" 2>/dev/null || true
fi

# ---- 工具函数（纯 QNX 6.6 兼容，不用 tr/wc/stat/find/sed -i/local） ----

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

# ---- 目标路径 ----
_BIN="$_APP/eso/bin"
_APPS="$_APP/eso/bin/apps"
_ENTRY="$_BIN/servicemgrmibhigh"
_ENTRY0="$_BIN/servicemgrmibhigh0"
_ENTRY0_REAL="$_BIN/servicemgrmibhigh0.real"
_ORGAPP="$_BIN/servicemgrmibhigh_orgapp"
_DIO="$_APPS/dio_manager"
_DIO_ORG="$_APPS/dio_manager.org"

echo "--- [1/4] Preflight verification (read-only) ---"

# 4) 拒绝变体 B 双件套（#16 必须走 VW_ShopTakeover）
if [ -e "$_ORGAPP" ] || [ -e "$_DIO_ORG" ]; then
    echo "abort: Variant B files (servicemgrmibhigh_orgapp / dio_manager.org) detected! Use VW_ShopTakeover instead. Nothing touched."
    exit 0
fi
_dio_sz=$(file_size "$_DIO")
if ! is_elf "$_DIO" || [ "$_dio_sz" -lt 800000 ]; then
    echo "abort: dio_manager is missing, non-ELF, or unexpectedly small (${_dio_sz} B < 800000 B). Nothing touched."
    exit 0
fi

# 5) 幂等检测：若已完成 VW_ShopBypassA 部署且原厂 servicemgrmibhigh0.real 完好，安全退出
if is_regular_nonlink "$_ENTRY0" && ! is_elf "$_ENTRY0" && grep -q "VW_ShopBypassA" "$_ENTRY0" 2>/dev/null; then
    if is_elf "$_ENTRY0_REAL" && [ "$(file_size "$_ENTRY0_REAL")" -ge 400000 ]; then
        echo "already bypassed: VW_ShopBypassA wrapper + stock ELF servicemgrmibhigh0.real ($(file_size "$_ENTRY0_REAL") B). Nothing to do."
        exit 0
    fi
fi

# 6) 核验当前确为变体 A 现场（非 ELF 商业入口 + 原厂 ELF servicemgrmibhigh0）
if is_elf "$_ENTRY"; then
    echo "abort: clean stock ELF servicemgrmibhigh detected — no commercial lock to bypass! Nothing touched."
    exit 0
fi
if ! is_regular_nonlink "$_ENTRY"; then
    echo "abort: $_ENTRY missing or not a regular file. Nothing touched."
    exit 0
fi
if grep -q "servicemgrmibhigh_orgapp" "$_ENTRY" 2>/dev/null; then
    echo "abort: $_ENTRY references servicemgrmibhigh_orgapp (Variant B). Use VW_ShopTakeover instead. Nothing touched."
    exit 0
fi
if ! grep -q "servicemgrmibhigh0" "$_ENTRY" 2>/dev/null; then
    echo "abort: $_ENTRY does not invoke servicemgrmibhigh0 (unknown boot entry). Nothing touched."
    exit 0
fi
if ! is_elf "$_ENTRY0"; then
    echo "abort: $_ENTRY0 is missing or NOT an ELF binary! Refusing to touch boot chain."
    exit 0
fi
_entry0_sz=$(file_size "$_ENTRY0")
if [ "$_entry0_sz" -lt 400000 ]; then
    echo "abort: $_ENTRY0 size (${_entry0_sz} B) is unexpectedly small (<400000 B). Nothing touched."
    exit 0
fi
if [ -e "$_ENTRY0_REAL" ] && ! is_elf "$_ENTRY0_REAL"; then
    echo "abort: $_ENTRY0_REAL already exists and is NOT an ELF binary. Nothing touched."
    exit 0
fi
echo "verified stock servicemgrmibhigh0 (ELF, ${_entry0_sz} B) & stock dio_manager (ELF, ${_dio_sz} B)"

# ---- 阶段一：备份原入口脚本与元数据到 U 盘（绝不覆盖首次备份，绝不拷原厂二进制到 U 盘） ----
echo "--- [2/4] Backing up entry metadata to USB ---"
BK_ORIG="$USB_ROOT/ShopBackupA/orig"
if [ -f "$BK_ORIG/MANIFEST.txt" ]; then
    echo "existing original backup found at $BK_ORIG (preserving intact)"
else
    mkdir -p "$BK_ORIG"
    cp -f "$_ENTRY" "$BK_ORIG/servicemgrmibhigh"
    if ! files_match "$_ENTRY" "$BK_ORIG/servicemgrmibhigh"; then
        echo "abort: USB backup verification failed for servicemgrmibhigh! Nothing touched."
        exit 0
    fi
    _mf="$BK_ORIG/MANIFEST.txt"
    {
        echo "# VW_ShopBypassA Original Backup Manifest"
        echo "RELEASE_VERSION=$RELEASE_VERSION"
        echo "SERVICEMGR_SIZE=$(file_size "$BK_ORIG/servicemgrmibhigh")"
        echo "SERVICEMGR_CKSUM=$(file_cksum "$BK_ORIG/servicemgrmibhigh")"
        echo "SERVICEMGR0_SIZE=$_entry0_sz"
        echo "SERVICEMGR0_CKSUM=$(file_cksum "$_ENTRY0")"
    } > "$_mf"
    sync
    echo "USB backup manifest verified and synced: $BK_ORIG"
fi

# ---- 阶段二：挂载可写、补齐 modkit_persist.sh 并原子切换 ----
if [ "$_SKIP_MOUNT" != "1" ]; then
    mount -uw "$_APP"
    mount -uw "$_OTA" 2>/dev/null || true
fi

echo "--- [3/4] Ensuring /mnt/ota/modkit/modkit_persist.sh is ready ---"
mkdir -p "$_OTA/modkit/Logs" 2>/dev/null || true
if [ ! -f "$_OTA/modkit/modkit_persist.sh" ]; then
    if [ -f "$_PERSIST/modkit_persist.sh" ]; then
        cp -f "$_PERSIST/modkit_persist.sh" "$_OTA/modkit/modkit_persist.sh"
        chmod 755 "$_OTA/modkit/modkit_persist.sh"
        echo "installed modkit_persist.sh from $_PERSIST"
    else
        echo "WARN: $_OTA/modkit/modkit_persist.sh not found yet (install mods first if needed)"
    fi
else
    chmod 755 "$_OTA/modkit/modkit_persist.sh" 2>/dev/null || true
    echo "verified $_OTA/modkit/modkit_persist.sh ($(file_size "$_OTA/modkit/modkit_persist.sh") B)"
fi

echo "--- [4/4] Staging servicemgrmibhigh0.real & installing Variant A wrapper ---"
# 先将原厂 ELF 完整落盘为 servicemgrmibhigh0.real 并逐字节校验，绝不留下空窗期
if is_elf "$_ENTRY0_REAL" && files_match "$_ENTRY0" "$_ENTRY0_REAL"; then
    echo "servicemgrmibhigh0.real already matches stock ELF, keeping as-is"
else
    cp -f "$_ENTRY0" "$_ENTRY0_REAL.new"
    if ! is_elf "$_ENTRY0_REAL.new" || ! files_match "$_ENTRY0" "$_ENTRY0_REAL.new"; then
        rm -f "$_ENTRY0_REAL.new"
        echo "abort: staging copy of servicemgrmibhigh0 -> servicemgrmibhigh0.real failed verification! Nothing touched."
        exit 0
    fi
    chmod 755 "$_ENTRY0_REAL.new"
    mv -f "$_ENTRY0_REAL.new" "$_ENTRY0_REAL"
    sync
    if ! is_elf "$_ENTRY0_REAL" || ! files_match "$_ENTRY0" "$_ENTRY0_REAL"; then
        rm -f "$_ENTRY0_REAL"
        echo "abort: post-move verification of servicemgrmibhigh0.real failed! Original servicemgrmibhigh0 untouched."
        exit 0
    fi
    echo "verified stock ELF backup on flash: $_ENTRY0_REAL ($(file_size "$_ENTRY0_REAL") B)"
fi

# 写入轻量防重入 wrapper（仅用 QNX 6.6 /bin/sh 原生语法，不在 /tmp 建子目录，立即 exec 原厂二进制）
cat > "$_ENTRY0.new" <<'EOF'
#!/bin/sh
# VW_ShopBypassA wrapper (Issue #10/#17 Variant A safe bypass)
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods
_D="${0%/*}"
case "$_D" in /*) ;; *) _D="/eso/bin" ;; esac
(
    if ( set -C; : > /tmp/.vw_bypass_a_ran ) 2>/dev/null; then
        if ! grep -q "modkit_persist" "$_D/servicemgrmibhigh" 2>/dev/null; then
            if [ -e /mnt/ota/modkit/modkit_persist.sh ]; then
                mkdir -p /mnt/ota/modkit/Logs 2>/dev/null
                if [ -d /mnt/ota/modkit/Logs ]; then
                    /bin/ksh /mnt/ota/modkit/modkit_persist.sh > /mnt/ota/modkit/Logs/_ModKit_Persist.log 2>&1
                else
                    /bin/ksh /mnt/ota/modkit/modkit_persist.sh > /tmp/_ModKit_Persist.log 2>&1
                fi
            fi
        fi
    fi
) >/dev/null 2>&1 &
exec "$_D/servicemgrmibhigh0.real" "$@"
EOF

if ! grep -q "servicemgrmibhigh0.real" "$_ENTRY0.new" 2>/dev/null; then
    rm -f "$_ENTRY0.new"
    echo "abort: wrapper staging sanity check failed! Original servicemgrmibhigh0 untouched."
    exit 0
fi
chmod 755 "$_ENTRY0.new"
mv -f "$_ENTRY0.new" "$_ENTRY0"
sync

echo "installed VW_ShopBypassA wrapper at $_ENTRY0 ($(file_size "$_ENTRY0") B)"
echo "=== VW_ShopBypassA done (NOTE: 'error: unknown patch state' at end of _ModKit_Update.log is EXPECTED!) ==="
echo "=== On next boot, ModKit Post/Persist will start automatically (1 auto-reboot if Post scripts pending) ==="
