#!/bin/ksh
# VW_ShopRollback - P2838 商业改装一键原样回滚模块（VW_ShopTakeover 配套后悔药）
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL)
#
# 用途：
#   如果在运行 VW_ShopTakeover 停掉旧商业包后，发现原有功能或授权存在异常，
#   只需保留包含 ShopBackup/orig/ 备份目录的同一根 U 盘，清空 Mods/ 后仅放入本模块刷一次即可：
#     1) 核对 U 盘 ShopBackup/orig/ 中的备份文件完整无损，并确认车机 servicemgrmibhigh_orgapp 仍为原厂 ELF；
#     2) 将商业版 dio_manager 原子还原回 /mnt/app/eso/bin/apps/dio_manager；
#     3) 将原 350B 商业启动脚本还原回 /mnt/app/eso/bin/servicemgrmibhigh；
#     4) 移除接管时新建的 servicemgrmibhigh0 与 /mnt/ota/modkit/modkit_persist.sh
#        （注：移除 servicemgrmibhigh0 后，紧接着执行的上游 modkit_install.sh 会报 'error: unknown patch state'
#         并自动中止退出——这是刻意设计的安全机制，用来确保刚还原的旧脚本不会被 ModKit 再次覆写！）
#   安全说明：出于安全考虑，本回滚脚本默认不把来路不明的 2021 SSH 公钥重新塞回 authorized_keys
#   （原商业包开机也会每 3 秒 slay sshd，不影响旧商业包任何功能；原公钥备份仍完好存于 U 盘）。

echo "=== VW_ShopRollback (restore original pre-takeover backup, Issue #16) ==="

_APP="${TAKEOVER_APP_ROOT:-/mnt/app}"
_OTA="${TAKEOVER_OTA_ROOT:-/mnt/ota}"
_SKIP_MOUNT="${TAKEOVER_SKIP_MOUNT:-0}"

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
if [ -z "$USB_ROOT" ] || [ ! -d "$USB_ROOT" ]; then
    echo "abort: cannot determine USB root (MEDIA_PATH=$MEDIA_PATH MOD_PATH=$MOD_PATH)"
    exit 0
fi

# 防呆：禁止同时放置接管包与回滚包
if [ -d "$USB_ROOT/Mods/VW_ShopTakeover" ] && [ -d "$USB_ROOT/Mods/VW_ShopRollback" ]; then
    echo "abort: DO NOT place VW_ShopTakeover and VW_ShopRollback in Mods/ at the same time!"
    exit 0
fi

if [ "$_SKIP_MOUNT" != "1" ]; then
    [ ! -e "$_APP/eso" ] && mount -t qnx6 /dev/mnanda0t177.1 "$_APP" 2>/dev/null || true
    [ ! -e "$_OTA" ] && mount -t qnx6 /dev/mnanda0t177.16 "$_OTA" 2>/dev/null || true
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

# ---- 校验 U 盘备份完整性 ----
BK_ORIG="$USB_ROOT/ShopBackup/orig"
MF="$BK_ORIG/MANIFEST.txt"
_BIN="$_APP/eso/bin"
_APPS="$_APP/eso/bin/apps"
_ENTRY="$_BIN/servicemgrmibhigh"
_ENTRY0="$_BIN/servicemgrmibhigh0"
_ORGAPP="$_BIN/servicemgrmibhigh_orgapp"
_DIO="$_APPS/dio_manager"

echo "--- [1/4] Verifying USB backup & target integrity ---"

if [ ! -f "$MF" ]; then
    echo "abort: backup manifest $MF not found! Please use the SAME USB drive that ran VW_ShopTakeover."
    exit 0
fi

_bk_fw=$(manifest_val "$MF" "RELEASE_VERSION")
if [ -n "$_bk_fw" ] && [ "$_bk_fw" != "$RELEASE_VERSION" ]; then
    echo "abort: backup firmware ($_bk_fw) does not match this unit ($RELEASE_VERSION)! Refusing cross-unit restore."
    exit 0
fi

# 确认车机上 servicemgrmibhigh_orgapp 仍完好（旧商业脚本依赖它拉起车机服务）
if ! is_elf "$_ORGAPP" || [ "$(file_size "$_ORGAPP")" -lt 400000 ]; then
    echo "abort: $_ORGAPP is missing or invalid ELF on car! Refusing to restore script that invokes it."
    exit 0
fi

# 校验备份的 servicemgrmibhigh 脚本
_BK_ENTRY="$BK_ORIG/servicemgrmibhigh"
if ! is_regular_nonlink "$_BK_ENTRY" || is_elf "$_BK_ENTRY"; then
    echo "abort: backup $_BK_ENTRY missing or invalid."
    exit 0
fi
if ! grep -q "servicemgrmibhigh_orgapp" "$_BK_ENTRY" 2>/dev/null; then
    echo "abort: backup $_BK_ENTRY does not reference servicemgrmibhigh_orgapp."
    exit 0
fi
_exp_sm_sz=$(manifest_val "$MF" "SERVICEMGR_SIZE")
_cur_sm_sz=$(file_size "$_BK_ENTRY")
if [ -n "$_exp_sm_sz" ] && [ "$_exp_sm_sz" != "$_cur_sm_sz" ]; then
    echo "abort: backup servicemgrmibhigh size mismatch (expected=$_exp_sm_sz actual=$_cur_sm_sz)."
    exit 0
fi
_exp_sm_crc=$(manifest_val "$MF" "SERVICEMGR_CKSUM")
if [ -n "$_exp_sm_crc" ] && command -v cksum >/dev/null 2>&1; then
    case "$_exp_sm_crc" in
        nocrc:*) ;;
        *)
            _cur_sm_crc=$(file_cksum "$_BK_ENTRY")
            if [ "$_exp_sm_crc" != "$_cur_sm_crc" ]; then
                echo "abort: backup servicemgrmibhigh checksum mismatch!"
                exit 0
            fi
            ;;
    esac
fi
echo "verified backup servicemgrmibhigh (${_cur_sm_sz} B)"

# 校验备份的商业版 dio_manager（若当时备份过）
_BK_DIO="$BK_ORIG/dio_manager"
_HAS_BK_DIO=0
_exp_dio_sz=$(manifest_val "$MF" "DIO_SIZE")
if [ -n "$_exp_dio_sz" ]; then
    _HAS_BK_DIO=1
    if ! is_regular_nonlink "$_BK_DIO"; then
        echo "abort: manifest records dio_manager backup but $_BK_DIO is missing!"
        exit 0
    fi
    _cur_dio_sz=$(file_size "$_BK_DIO")
    if [ "$_exp_dio_sz" != "$_cur_dio_sz" ]; then
        echo "abort: backup dio_manager size mismatch (expected=$_exp_dio_sz actual=$_cur_dio_sz)."
        exit 0
    fi
    _exp_dio_crc=$(manifest_val "$MF" "DIO_CKSUM")
    if [ -n "$_exp_dio_crc" ] && command -v cksum >/dev/null 2>&1; then
        case "$_exp_dio_crc" in
            nocrc:*) ;;
            *)
                _cur_dio_crc=$(file_cksum "$_BK_DIO")
                if [ "$_exp_dio_crc" != "$_cur_dio_crc" ]; then
                    echo "abort: backup dio_manager checksum mismatch!"
                    exit 0
                fi
                ;;
        esac
    fi
    echo "verified backup dio_manager (${_cur_dio_sz} B)"
fi

# ---- 执行回滚 ----
if [ "$_SKIP_MOUNT" != "1" ]; then
    mount -uw "$_APP"
    mount -uw "$_OTA" 2>/dev/null || true
fi

# 步骤 2/4：恢复原商业版 dio_manager
echo "--- [2/4] Restoring backed-up dio_manager ---"
if [ "$_HAS_BK_DIO" = "1" ]; then
    cp -f "$_BK_DIO" "$_DIO.rb"
    if ! files_match "$_BK_DIO" "$_DIO.rb"; then
        rm -f "$_DIO.rb"
        echo "abort: failed to stage dio_manager backup on /mnt/app!"
        exit 0
    fi
    chmod 755 "$_DIO.rb"
    mv -f "$_DIO.rb" "$_DIO"
    sync
    echo "restored dio_manager ($(file_size "$_DIO") B)"
else
    echo "skipped (dio_manager was not replaced during takeover)"
fi

# 步骤 3/4：恢复原商业启动脚本 servicemgrmibhigh，并在验证通过后移除 servicemgrmibhigh0
echo "--- [3/4] Restoring backed-up servicemgrmibhigh & removing servicemgrmibhigh0 ---"
cp -f "$_BK_ENTRY" "$_ENTRY.rb"
if ! files_match "$_BK_ENTRY" "$_ENTRY.rb"; then
    rm -f "$_ENTRY.rb"
    echo "abort: failed to stage servicemgrmibhigh backup on /mnt/app!"
    exit 0
fi
chmod 755 "$_ENTRY.rb"
mv -f "$_ENTRY.rb" "$_ENTRY"
sync

if ! files_match "$_BK_ENTRY" "$_ENTRY"; then
    echo "abort: post-move verification of servicemgrmibhigh failed; keeping servicemgrmibhigh0 for safety."
    exit 0
fi

# 确认还原后的 servicemgrmibhigh 与原厂 servicemgrmibhigh_orgapp 均完整后，移除接管时新建的 servicemgrmibhigh0
# 这样后续 modkit_install.sh 会报 'error: unknown patch state' 并中止，不会再覆写刚还原的脚本！
if [ -e "$_ENTRY0" ]; then
    rm -f "$_ENTRY0"
    echo "removed $_ENTRY0 (expected: modkit_install.sh will report 'unknown patch state' next so restored entry stays intact)"
fi

# 步骤 4/4：停用 ModKit persist 入口（避免残留）
echo "--- [4/4] Disabling modkit_persist.sh ---"
if [ -f "$_OTA/modkit/modkit_persist.sh" ]; then
    rm -f "$_OTA/modkit/modkit_persist.sh"
    echo "removed $_OTA/modkit/modkit_persist.sh"
else
    echo "modkit_persist.sh already absent"
fi

sync
echo "=== VW_ShopRollback done (NOTE: 'error: unknown patch state' at end of _ModKit_Update.log is EXPECTED & protects your rollback) ==="
echo "=== Tip: if you previously installed VW_HUD_1~5, also uninstall VW_HUD_1~5 via Configurator to avoid conflict with .j ==="
