#!/bin/ksh
# VW_ShopTakeover - P2838 商业改装双件套（servicemgrmibhigh_orgapp + dio_manager.org）安全接管模块
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL)
#
# 适用场景（Issue #16 实车 REPORT.txt 实锤）：
#   1) /mnt/app/eso/bin/servicemgrmibhigh 被替换为商业脚本（拉起 /eso/hmi/lsd/.j + 每 3 秒强杀 sshd/challenge）
#      且原厂二进制被改名为 /mnt/app/eso/bin/servicemgrmibhigh_orgapp（缺少 servicemgrmibhigh0）
#   2) /mnt/app/eso/bin/apps/dio_manager 被替换为 109KB 商业壳，原厂 958KB 程序被改名为 dio_manager.org
#   3) /mnt/app/root/.ssh/authorized_keys 残留来路不明的 rsa-key-20210318 外部公钥
#
# 安全设计原则：
#   - 严苛前置校验：原厂 servicemgrmibhigh_orgapp（>=400KB ELF）与 dio_manager.org（>=800KB ELF）
#     全部验明正身后才动任何文件；任何校验不通过立刻中止，车机零改动。
#   - 先备份、验完落盘再动刀：将原 350B 脚本、商业版 dio_manager、authorized_keys 备份到 U 盘
#     ShopBackup/orig/ 并逐项核对大小/校验和；绝不覆盖首次原始备份。
#   - 零破坏原则：保留车机上原有的 servicemgrmibhigh_orgapp、dio_manager.org、.j、challenge.pub、
#     autorunner 不删除，仅恢复现役入口并交回上游 ModKit；配合 VW_ShopRollback 可随时 1 分钟原样回滚。
#   - 严格遵循 QNX 6.6 RAM disk 约束：仅用 ksh/dd/awk/grep/ls/cp/mv/rm/chmod/sync，
#     绝不提前执行 mount -ur /mnt/app（避免阻断后续 modkit_install.sh 挂载 ModKit 入口）。

echo "=== VW_ShopTakeover (P2838 safe backup & takeover, Issue #16) ==="

# 允许本地沙盒自测通过环境变量重定向根路径（真车未设置时默认使用标准车机路径）
_APP="${TAKEOVER_APP_ROOT:-/mnt/app}"
_OTA="${TAKEOVER_OTA_ROOT:-/mnt/ota}"
_PERSIST="${TAKEOVER_PERSIST_ROOT:-/mnt/persist_new}"
_SKIP_MOUNT="${TAKEOVER_SKIP_MOUNT:-0}"

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

# 防呆：禁止同时将接管包与回滚包放进 Mods/
if [ -d "$USB_ROOT/Mods/VW_ShopTakeover" ] && [ -d "$USB_ROOT/Mods/VW_ShopRollback" ]; then
    echo "abort: DO NOT place VW_ShopTakeover and VW_ShopRollback in Mods/ at the same time!"
    exit 0
fi

touch "$USB_ROOT/.takeover_write_test" 2>/dev/null
if [ ! -e "$USB_ROOT/.takeover_write_test" ]; then
    echo "abort: USB drive $USB_ROOT is write protected"
    exit 0
fi
rm -f "$USB_ROOT/.takeover_write_test"

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
_ORGAPP="$_BIN/servicemgrmibhigh_orgapp"
_DIO="$_APPS/dio_manager"
_DIO_ORG="$_APPS/dio_manager.org"
_AK="$_APP/root/.ssh/authorized_keys"

echo "--- [1/5] Preflight verification (read-only) ---"

# 4) 幂等检测：如果已经完成接管（标准 ModKit + 原厂 dio_manager），直接安全退出
_entry_is_elf=0
_entry_is_modkit=0
is_elf "$_ENTRY" && _entry_is_elf=1
if [ "$_entry_is_elf" = "0" ] && is_regular_nonlink "$_ENTRY" && grep -q "modkit_persist" "$_ENTRY" 2>/dev/null; then
    _entry_is_modkit=1
fi
_dio_sz=$(file_size "$_DIO")
_dio_is_elf=0
is_elf "$_DIO" && _dio_is_elf=1

if [ "$_entry_is_modkit" = "1" ] && is_elf "$_ENTRY0" && [ "$_dio_is_elf" = "1" ] && [ "$_dio_sz" -ge 800000 ]; then
    echo "already taken over: standard ModKit entry + ELF servicemgrmibhigh0 + stock-size dio_manager (${_dio_sz} B). Nothing to do."
    exit 0
fi

if [ "$_entry_is_elf" = "1" ] && [ "$_dio_is_elf" = "1" ] && [ "$_dio_sz" -ge 800000 ]; then
    echo "clean stock system detected: no shop hook to remove. Nothing to do."
    exit 0
fi

# 5) 严格核验当前确为目标商业包现场
if [ "$_entry_is_modkit" = "0" ]; then
    if [ "$_entry_is_elf" = "1" ]; then
        echo "abort: unexpected state - servicemgrmibhigh is stock ELF while dio_manager is small (${_dio_sz} B). Nothing touched."
        exit 0
    fi
    if ! is_regular_nonlink "$_ENTRY"; then
        echo "abort: $_ENTRY missing or not a regular file. Nothing touched."
        exit 0
    fi
    if ! grep -q "servicemgrmibhigh_orgapp" "$_ENTRY" 2>/dev/null; then
        echo "abort: $_ENTRY does not reference servicemgrmibhigh_orgapp (unknown third-party variant). Nothing touched."
        exit 0
    fi
    if ! is_elf "$_ORGAPP"; then
        echo "abort: $_ORGAPP is missing or NOT an ELF binary! Refusing to proceed."
        exit 0
    fi
    _orgapp_sz=$(file_size "$_ORGAPP")
    if [ "$_orgapp_sz" -lt 400000 ]; then
        echo "abort: $_ORGAPP size (${_orgapp_sz} B) is unexpectedly small (<400000 B). Nothing touched."
        exit 0
    fi
    echo "verified stock entry backup: $_ORGAPP (ELF, ${_orgapp_sz} B)"

    if [ -e "$_ENTRY0" ] && ! is_elf "$_ENTRY0"; then
        echo "abort: $_ENTRY0 already exists and is NOT an ELF binary. Nothing touched."
        exit 0
    fi
fi

# 核验 dio_manager 与 dio_manager.org
_NEED_RESTORE_DIO=0
if [ "$_dio_is_elf" = "0" ] || [ "$_dio_sz" -lt 800000 ]; then
    _NEED_RESTORE_DIO=1
    if ! is_elf "$_DIO_ORG"; then
        echo "abort: $_DIO is non-stock (${_dio_sz} B) but $_DIO_ORG is missing or NOT ELF! Nothing touched."
        exit 0
    fi
    _dio_org_sz=$(file_size "$_DIO_ORG")
    if [ "$_dio_org_sz" -lt 800000 ]; then
        echo "abort: $_DIO_ORG size (${_dio_org_sz} B) is unexpectedly small (<800000 B). Nothing touched."
        exit 0
    fi
    echo "verified stock dio_manager backup: $_DIO_ORG (ELF, ${_dio_org_sz} B) vs current shop wrapper (${_dio_sz} B)"
else
    echo "dio_manager already stock-sized ELF (${_dio_sz} B), will keep untouched"
fi

# ---- 阶段一：记录只读补充勘测 + 先完整备份到 U 盘 ----
echo "--- [2/5] Recording read-only audit & creating USB backup ---"

BK_BASE="$USB_ROOT/ShopBackup"
BK_ORIG="$BK_BASE/orig"
mkdir -p "$BK_BASE"

# 补充只读摸底日志（绝不拷贝原厂或商业黑盒二进制，仅记元数据供复核）
EXTRA_LOG="$BK_BASE/audit_extra.txt"
{
    echo "=== VW_ShopTakeover Extra Read-Only Audit ==="
    echo "RELEASE_VERSION : $RELEASE_VERSION"
    echo ""
    echo "-- Key binaries & shop payloads (metadata only) --"
    for _p in "$_ENTRY" "$_ENTRY0" "$_ORGAPP" "$_DIO" "$_DIO_ORG" \
              "$_APP/eso/hmi/lsd/.j" "$_BIN/autorunner" "$_OTA/challenge.pub"; do
        if [ -e "$_p" ]; then
            _tag="non-ELF"
            is_elf "$_p" && _tag="ELF"
            echo "  [$_tag] $(ls -ld "$_p" 2>/dev/null)"
        else
            echo "  [absent] $_p"
        fi
    done
    echo ""
    echo "-- /mnt/ota subdirs (ls -la) --"
    for _d in "$_OTA/system" "$_OTA/app" "$_OTA/ot"; do
        echo "[$_d]"
        [ -d "$_d" ] && ls -la "$_d" 2>/dev/null || echo "  (absent)"
    done
    echo ""
    echo "-- /mnt/persist_new/fec (filenames & sizes only, contents NOT read) --"
    if [ -d "$_PERSIST/fec" ]; then
        ls -la "$_PERSIST/fec" 2>/dev/null || true
    else
        echo "  (not mounted / absent)"
    fi
    echo ""
    echo "-- cluster crash baselines (if installed) --"
    for _bf in "$_APPS/cluster/dio_base_cores" "$_APPS/cluster/j9_base_crashes" "$_APPS/cluster/render_base_cores" "$_APPS/cluster/siri_short"; do
        if [ -f "$_bf" ]; then
            echo "  $_bf = $(cat "$_bf" 2>/dev/null)"
        fi
    done
    _cur_dio_cores=$(ls /mnt/crashlogs/core/dio_manager*.core.gz 2>/dev/null | awk 'END{print NR+0}')
    echo "  current /mnt/crashlogs/core/dio_manager*.core.gz count = ${_cur_dio_cores:-0}"
} > "$EXTRA_LOG" 2>&1

# 若首次原始备份尚不存在，则完整备份并逐项比对（严禁覆写已有首次原始备份）
if [ -f "$BK_ORIG/MANIFEST.txt" ]; then
    echo "existing original backup found at $BK_ORIG (preserving intact, not overwriting)"
else
    mkdir -p "$BK_ORIG"
    _bk_ok=1
    _mf="$BK_ORIG/MANIFEST.txt"
    {
        echo "# VW_ShopTakeover Original Backup Manifest"
        echo "RELEASE_VERSION=$RELEASE_VERSION"
    } > "$_mf"

    if [ "$_entry_is_modkit" = "0" ]; then
        cp -f "$_ENTRY" "$BK_ORIG/servicemgrmibhigh"
        if ! files_match "$_ENTRY" "$BK_ORIG/servicemgrmibhigh"; then
            echo "abort: backup verification failed for servicemgrmibhigh! Nothing touched."
            _bk_ok=0
        else
            echo "SERVICEMGR_SIZE=$(file_size "$BK_ORIG/servicemgrmibhigh")" >> "$_mf"
            echo "SERVICEMGR_CKSUM=$(file_cksum "$BK_ORIG/servicemgrmibhigh")" >> "$_mf"
            echo "backed up servicemgrmibhigh ($(file_size "$BK_ORIG/servicemgrmibhigh") B)"
        fi
    fi

    if [ "$_bk_ok" = "1" ] && [ "$_NEED_RESTORE_DIO" = "1" ]; then
        cp -f "$_DIO" "$BK_ORIG/dio_manager"
        if ! files_match "$_DIO" "$BK_ORIG/dio_manager"; then
            echo "abort: backup verification failed for dio_manager! Nothing touched."
            _bk_ok=0
        else
            echo "DIO_SIZE=$(file_size "$BK_ORIG/dio_manager")" >> "$_mf"
            echo "DIO_CKSUM=$(file_cksum "$BK_ORIG/dio_manager")" >> "$_mf"
            echo "backed up shop dio_manager ($(file_size "$BK_ORIG/dio_manager") B)"
        fi
    fi

    if [ "$_bk_ok" = "1" ] && is_regular_nonlink "$_AK"; then
        cp -f "$_AK" "$BK_ORIG/authorized_keys"
        if ! files_match "$_AK" "$BK_ORIG/authorized_keys"; then
            echo "abort: backup verification failed for authorized_keys! Nothing touched."
            _bk_ok=0
        else
            echo "AK_SIZE=$(file_size "$BK_ORIG/authorized_keys")" >> "$_mf"
            echo "AK_CKSUM=$(file_cksum "$BK_ORIG/authorized_keys")" >> "$_mf"
            echo "backed up authorized_keys ($(file_size "$BK_ORIG/authorized_keys") B)"
        fi
    fi

    sync
    if [ "$_bk_ok" != "1" ]; then
        rm -f "$_mf"
        exit 0
    fi
    echo "USB backup verified and synced: $BK_ORIG"
fi

# ---- 阶段二：挂载可写并原子切换 ----
if [ "$_SKIP_MOUNT" != "1" ]; then
    mount -uw "$_APP"
fi

# 步骤 3/5：将原厂 dio_manager.org 原子恢复为 dio_manager（保留 dio_manager.org 原封不动）
echo "--- [3/5] Restoring stock dio_manager ---"
_RESTORED_DIO_NOW=0
if [ "$_NEED_RESTORE_DIO" = "1" ]; then
    cp -f "$_DIO_ORG" "$_DIO.new"
    if ! is_elf "$_DIO.new" || ! files_match "$_DIO_ORG" "$_DIO.new"; then
        rm -f "$_DIO.new"
        echo "abort: staging copy of dio_manager.org failed verification! Nothing touched."
        exit 0
    fi
    chmod 755 "$_DIO.new"
    mv -f "$_DIO.new" "$_DIO"
    sync
    if ! is_elf "$_DIO" || ! files_match "$_DIO_ORG" "$_DIO"; then
        echo "ERROR: dio_manager post-move verify failed, attempting auto-rollback of dio_manager!"
        cp -f "$BK_ORIG/dio_manager" "$_DIO.rb" && chmod 755 "$_DIO.rb" && mv -f "$_DIO.rb" "$_DIO"
        sync
        exit 0
    fi
    _RESTORED_DIO_NOW=1
    echo "restored stock dio_manager ($(file_size "$_DIO") B); dio_manager.org preserved"
else
    echo "skipped (already stock-sized)"
fi

# 步骤 4/5：精准移除残留的 2021 外部 SSH 公钥（rsa-key-20210318），不动托管公钥
echo "--- [4/5] Cleaning residual shop SSH key (rsa-key-20210318) ---"
if is_regular_nonlink "$_AK"; then
    if grep -q "rsa-key-20210318" "$_AK" 2>/dev/null; then
        awk '
        {
            line = $0
            sub(/\r$/, "", line)
            if (index(line, "rsa-key-20210318") > 0 && index(line, "VW_WiFi_Dev_Managed") == 0) next
            print $0
        }
        ' "$_AK" > "$_AK.new"
        chmod 600 "$_AK.new"
        mv -f "$_AK.new" "$_AK"
        echo "removed rsa-key-20210318 from authorized_keys (backup saved on USB)"
    else
        echo "rsa-key-20210318 not present in authorized_keys (nothing removed)"
    fi
else
    echo "authorized_keys absent (clean)"
fi

# 步骤 5/5：从 servicemgrmibhigh_orgapp 建立原厂 servicemgrmibhigh0，交由后续 modkit_install.sh 自动挂载标准入口
echo "--- [5/5] Staging stock servicemgrmibhigh0 for ModKit handoff ---"
if is_elf "$_ENTRY0"; then
    echo "servicemgrmibhigh0 already ELF ($(file_size "$_ENTRY0") B), keeping as-is"
else
    cp -f "$_ORGAPP" "$_ENTRY0.new"
    if ! is_elf "$_ENTRY0.new" || ! files_match "$_ORGAPP" "$_ENTRY0.new"; then
        rm -f "$_ENTRY0.new"
        if [ "$_RESTORED_DIO_NOW" = "1" ] && [ -f "$BK_ORIG/dio_manager" ]; then
            echo "rolling back dio_manager to keep pre-takeover state consistent..."
            cp -f "$BK_ORIG/dio_manager" "$_DIO.rb" && chmod 755 "$_DIO.rb" && mv -f "$_DIO.rb" "$_DIO" && sync
        fi
        echo "abort: staging copy of servicemgrmibhigh_orgapp failed verification!"
        exit 0
    fi
    chmod 755 "$_ENTRY0.new"
    mv -f "$_ENTRY0.new" "$_ENTRY0"
    sync
    if ! is_elf "$_ENTRY0" || ! files_match "$_ORGAPP" "$_ENTRY0"; then
        rm -f "$_ENTRY0"
        if [ "$_RESTORED_DIO_NOW" = "1" ] && [ -f "$BK_ORIG/dio_manager" ]; then
            echo "rolling back dio_manager to keep pre-takeover state consistent..."
            cp -f "$BK_ORIG/dio_manager" "$_DIO.rb" && chmod 755 "$_DIO.rb" && mv -f "$_DIO.rb" "$_DIO" && sync
        fi
        echo "abort: servicemgrmibhigh0 post-move verification failed!"
        exit 0
    fi
    echo "created stock ELF $_ENTRY0 ($(file_size "$_ENTRY0") B); servicemgrmibhigh_orgapp preserved"
fi

# 保留 /mnt/app 为可写（供紧接着执行的 modkit_install.sh 覆盖 servicemgrmibhigh 为标准 ModKit 入口）
sync
echo "=== VW_ShopTakeover done: modkit_install.sh will now install standard ModKit persist entry ==="
