#!/bin/sh
# 本地沙盒端到端自测脚本：完整模拟真实车机 P2838 + 上游 modkit_install.sh 入口判定流程
# 覆盖 7 大场景：
#   1) 正常首次接管（校验备份落盘、dio_manager 还原、rsa-key-20210318 清除、servicemgrmibhigh0 建立、ModKit 接管成功）
#   2) 重复刷接管包幂等（绝不覆盖首次原始备份）
#   3) 防呆拦截：同时把接管包和回滚包放进 Mods/ 时直接中止
#   4) 异常拦截：servicemgrmibhigh_orgapp 非 ELF / 过小时中止、车机零改动
#   5) 异常拦截：dio_manager.org 缺失或过小时中止、车机零改动
#   6) 一键回滚（校验原 350B 脚本与 109KB dio_manager 完整恢复、移除 servicemgrmibhigh0 后 modkit_install.sh 触发保护性 unknown patch state 不反覆写、且不塞回陌生 SSH 公钥）
#   7) 回滚后再次重新接管（完整闭环）
set -eu

BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
AUDIT_SH="$BASE_DIR/VW_ShopAudit/Update/install.sh"
TAKEOVER_SH="$BASE_DIR/VW_ShopTakeover/Update/install.sh"
ROLLBACK_SH="$BASE_DIR/VW_ShopRollback/Update/install.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

# 0) 语法与 QNX 6.6 工具禁令静态检查
echo "=== [0] Static syntax & QNX 6.6 compatibility check ==="
for f in "$AUDIT_SH" "$TAKEOVER_SH" "$ROLLBACK_SH"; do
    sh -n "$f"
    if command -v ksh >/dev/null 2>&1; then
        ksh -n "$f"
    fi
    python3 - "$f" <<'PY'
import re, sys
p = sys.argv[1]
raw = open(p, 'rb').read()
assert b'\r\n' not in raw, f"{p}: CRLF found"
txt = raw.decode('utf-8')
for ln, line in enumerate(txt.splitlines(), 1):
    s = line.strip()
    if s.startswith('#'):
        continue
    assert not re.search(r'\bmount\s+-ur\s+/mnt/app\b', s), f"{p}:{ln}: forbidden mount -ur /mnt/app"
    assert not re.search(r'(^|[|;&$(`\s])(tr|wc|stat|find|mktemp)\s+', s), f"{p}:{ln}: forbidden QNX missing tool: {s}"
    assert not re.search(r'\bsed\s+-i\b', s), f"{p}:{ln}: forbidden sed -i"
    assert not re.search(r'(^|[;&\s])local\s+', s), f"{p}:{ln}: avoid non-portable local"
PY
done
pass "syntax + LF + QNX 6.6 tool constraints"

# 生成逼真 ELF 头 + 固定大小的测试二进制
mk_elf() {
    python3 - "$1" "$2" "$3" <<'PY'
import sys
path, size, fill = sys.argv[1], int(sys.argv[2]), sys.argv[3].encode()
hdr = b'\x7fELF\x01\x01\x01\x00'
data = hdr + (fill * ((size - len(hdr)) // len(fill) + 1))[:size - len(hdr)]
with open(path, 'wb') as f:
    f.write(data)
PY
}

# 模拟上游 modkit_install.sh 末尾入口挂载逻辑（与 USB/Data/.../modkit_install.sh L86-143 一致）
run_upstream_modkit_tail() {
    _app="$1"
    _ota="$2"
    _entry="$_app/eso/bin/servicemgrmibhigh"
    _entry0="$_app/eso/bin/servicemgrmibhigh0"
    _is_elf() {
        [ -f "$1" ] && [ "$(dd if="$1" bs=1 skip=1 count=3 2>/dev/null)" = "ELF" ]
    }
    if _is_elf "$_entry"; then
        cp -f "$_entry" "$_entry0"
    elif _is_elf "$_entry0"; then
        :
    else
        echo "error: unknown patch state"
        return 1
    fi
    cat > "$_entry" <<'EOF'
#!/bin/sh
/eso/bin/servicemgrmibhigh0 &
if [[ -e /mnt/ota/modkit/modkit_persist.sh ]]; then
    /bin/ksh /mnt/ota/modkit/modkit_persist.sh > /mnt/ota/modkit/Logs/_ModKit_Persist.log 2>&1 &
fi
EOF
    chmod 755 "$_entry"
    mkdir -p "$_ota/modkit/Logs"
    echo "#!/bin/ksh" > "$_ota/modkit/modkit_persist.sh"
    chmod 755 "$_ota/modkit/modkit_persist.sh"
    echo "setup ModKit persistence"
    return 0
}

# 构建逼真的 Issue #16 车机环境（完全复刻 REPORT.txt）
setup_car() {
    _root="$1"
    rm -rf "$_root"
    mkdir -p "$_root/app/eso/bin/apps/cluster" \
             "$_root/app/eso/hmi/lsd" \
             "$_root/app/root/.ssh" \
             "$_root/ota/system" "$_root/ota/app" "$_root/ota/ot" \
             "$_root/persist_new/fec" \
             "$_root/usb/Mods/VW_ShopTakeover/Update"

    cat > "$_root/app/eso/bin/servicemgrmibhigh" <<'EOF'
#!/bin/sh
ln -sP /mnt/ota/challenge.pub /ifs/challenge.pub >/dev/null 2>&1
ln -sP /eso/bin/autorunner /usr/bin/autorunner >/dev/null 2>&1
rm -f /tmp/.j.ready
on -p 63 /eso/hmi/lsd/.j &
/eso/bin/servicemgrmibhigh_orgapp &
(
    while true; do
        slay -f sshd >/dev/null 2>&1
        slay -f challenge >/dev/null 2>&1
        sleep 3
    done
) &
EOF
    chmod 755 "$_root/app/eso/bin/servicemgrmibhigh"
    mk_elf "$_root/app/eso/bin/servicemgrmibhigh_orgapp" 493952 "ORGAPP"
    mk_elf "$_root/app/eso/bin/apps/dio_manager" 109874 "SHOPDIO"
    mk_elf "$_root/app/eso/bin/apps/dio_manager.org" 958480 "STOCKDIO"
    mk_elf "$_root/app/eso/hmi/lsd/.j" 65536 "SHOPJ"
    mk_elf "$_root/app/eso/bin/autorunner" 12345 "AUTORUN"
    echo "PUBKEYDATA" > "$_root/ota/challenge.pub"
    echo "0" > "$_root/app/eso/bin/apps/cluster/dio_base_cores"
    echo "dummy" > "$_root/persist_new/fec/granted.fecs"
    printf 'ssh-rsa AAAAB3NzaC1yc2EAAAABJQAAAQEAtesTkeyDataemImkw== rsa-key-20210318\n' > "$_root/app/root/.ssh/authorized_keys"
    chmod 600 "$_root/app/root/.ssh/authorized_keys"
}

# ---- 场景 1：正常首次接管 ----
echo "=== [1] Normal first-time takeover ==="
S1="$TMP/s1"
setup_car "$S1"
OUT1=$(RELEASE_VERSION="MH2p_CN_VWG36_P2838" \
    MEDIA_PATH="$S1/usb" \
    MOD_PATH="$S1/usb/Mods/VW_ShopTakeover/Update" \
    TAKEOVER_APP_ROOT="$S1/app" \
    TAKEOVER_OTA_ROOT="$S1/ota" \
    TAKEOVER_PERSIST_ROOT="$S1/persist_new" \
    TAKEOVER_SKIP_MOUNT=1 \
    sh "$TAKEOVER_SH")
echo "$OUT1"
grep -q "VW_ShopTakeover done" <<EOF
$OUT1
EOF
cmp -s "$S1/app/eso/bin/apps/dio_manager" "$S1/app/eso/bin/apps/dio_manager.org" || fail "dio_manager not restored to stock"
cmp -s "$S1/app/eso/bin/servicemgrmibhigh0" "$S1/app/eso/bin/servicemgrmibhigh_orgapp" || fail "servicemgrmibhigh0 not staged from orgapp"
[ -f "$S1/usb/ShopBackup/orig/MANIFEST.txt" ] || fail "MANIFEST.txt missing"
[ -f "$S1/usb/ShopBackup/orig/servicemgrmibhigh" ] || fail "servicemgrmibhigh backup missing"
[ -f "$S1/usb/ShopBackup/orig/dio_manager" ] || fail "dio_manager backup missing"
[ -f "$S1/usb/ShopBackup/orig/authorized_keys" ] || fail "authorized_keys backup missing"
! grep -q "rsa-key-20210318" "$S1/app/root/.ssh/authorized_keys" || fail "shop SSH key not removed"
[ -f "$S1/app/eso/hmi/lsd/.j" ] || fail ".j should NOT be deleted"
[ -f "$S1/app/eso/bin/apps/dio_manager.org" ] || fail "dio_manager.org should NOT be deleted"
[ -f "$S1/app/eso/bin/servicemgrmibhigh_orgapp" ] || fail "servicemgrmibhigh_orgapp should NOT be deleted"
# 模拟紧接着执行上游 modkit_install.sh 末尾
TAIL1=$(run_upstream_modkit_tail "$S1/app" "$S1/ota")
echo "modkit tail: $TAIL1"
grep -q "modkit_persist" "$S1/app/eso/bin/servicemgrmibhigh" || fail "ModKit failed to install standard entry"
pass "first-time takeover + ModKit handoff succeeded"

# ---- 场景 2：重复刷接管包（幂等，不覆盖原备份） ----
echo "=== [2] Idempotent re-run ==="
ORIG_DIO_CRC=$(cksum "$S1/usb/ShopBackup/orig/dio_manager")
OUT2=$(RELEASE_VERSION="MH2p_CN_VWG36_P2838" \
    MEDIA_PATH="$S1/usb" \
    MOD_PATH="$S1/usb/Mods/VW_ShopTakeover/Update" \
    TAKEOVER_APP_ROOT="$S1/app" \
    TAKEOVER_OTA_ROOT="$S1/ota" \
    TAKEOVER_PERSIST_ROOT="$S1/persist_new" \
    TAKEOVER_SKIP_MOUNT=1 \
    sh "$TAKEOVER_SH")
echo "$OUT2"
echo "$OUT2" | grep -q "already taken over" || fail "idempotent detection failed"
[ "$(cksum "$S1/usb/ShopBackup/orig/dio_manager")" = "$ORIG_DIO_CRC" ] || fail "original backup was overwritten!"
pass "idempotent re-run preserved original backup"

# ---- 场景 3：同时放接管包和回滚包（防呆拦截） ----
echo "=== [3] Guard against placing Takeover and Rollback together ==="
S3="$TMP/s3"
setup_car "$S3"
mkdir -p "$S3/usb/Mods/VW_ShopRollback/Update"
OUT3=$(RELEASE_VERSION="MH2p_CN_VWG36_P2838" \
    MEDIA_PATH="$S3/usb" \
    MOD_PATH="$S3/usb/Mods/VW_ShopTakeover/Update" \
    TAKEOVER_APP_ROOT="$S3/app" \
    TAKEOVER_OTA_ROOT="$S3/ota" \
    TAKEOVER_PERSIST_ROOT="$S3/persist_new" \
    TAKEOVER_SKIP_MOUNT=1 \
    sh "$TAKEOVER_SH")
echo "$OUT3" | grep -q "DO NOT place VW_ShopTakeover and VW_ShopRollback" || fail "dual-mod guard failed"
[ ! -e "$S3/app/eso/bin/servicemgrmibhigh0" ] || fail "car modified despite dual-mod guard"
pass "dual-mod conflict guard"

# ---- 场景 4：异常拦截（servicemgrmibhigh_orgapp 非 ELF） ----
echo "=== [4] Abort when servicemgrmibhigh_orgapp is not ELF ==="
S4="$TMP/s4"
setup_car "$S4"
echo "not an elf" > "$S4/app/eso/bin/servicemgrmibhigh_orgapp"
OUT4=$(RELEASE_VERSION="MH2p_CN_VWG36_P2838" \
    MEDIA_PATH="$S4/usb" \
    MOD_PATH="$S4/usb/Mods/VW_ShopTakeover/Update" \
    TAKEOVER_APP_ROOT="$S4/app" \
    TAKEOVER_OTA_ROOT="$S4/ota" \
    TAKEOVER_PERSIST_ROOT="$S4/persist_new" \
    TAKEOVER_SKIP_MOUNT=1 \
    sh "$TAKEOVER_SH")
echo "$OUT4" | grep -q "NOT an ELF binary" || fail "orgapp non-ELF guard failed"
[ ! -e "$S4/app/eso/bin/servicemgrmibhigh0" ] || fail "servicemgrmibhigh0 created despite bad orgapp"
pass "corrupt orgapp safely aborted"

# ---- 场景 5：异常拦截（dio_manager.org 缺失） ----
echo "=== [5] Abort when dio_manager.org is missing ==="
S5="$TMP/s5"
setup_car "$S5"
rm -f "$S5/app/eso/bin/apps/dio_manager.org"
OUT5=$(RELEASE_VERSION="MH2p_CN_VWG36_P2838" \
    MEDIA_PATH="$S5/usb" \
    MOD_PATH="$S5/usb/Mods/VW_ShopTakeover/Update" \
    TAKEOVER_APP_ROOT="$S5/app" \
    TAKEOVER_OTA_ROOT="$S5/ota" \
    TAKEOVER_PERSIST_ROOT="$S5/persist_new" \
    TAKEOVER_SKIP_MOUNT=1 \
    sh "$TAKEOVER_SH")
echo "$OUT5" | grep -q "dio_manager.org is missing or NOT ELF" || fail "missing dio_manager.org guard failed"
[ ! -e "$S5/app/eso/bin/servicemgrmibhigh0" ] || fail "partial takeover occurred despite missing dio_manager.org"
pass "missing dio_manager.org safely aborted with zero changes"

# ---- 场景 6：一键回滚 + 验证 ModKit 保护性 unknown patch state 不反覆写 ----
echo "=== [6] One-click rollback from S1 ==="
rm -rf "$S1/usb/Mods/VW_ShopTakeover"
mkdir -p "$S1/usb/Mods/VW_ShopRollback/Update"
OUT6=$(RELEASE_VERSION="MH2p_CN_VWG36_P2838" \
    MEDIA_PATH="$S1/usb" \
    MOD_PATH="$S1/usb/Mods/VW_ShopRollback/Update" \
    TAKEOVER_APP_ROOT="$S1/app" \
    TAKEOVER_OTA_ROOT="$S1/ota" \
    TAKEOVER_SKIP_MOUNT=1 \
    sh "$ROLLBACK_SH")
echo "$OUT6"
echo "$OUT6" | grep -q "VW_ShopRollback done" || fail "rollback did not complete"
cmp -s "$S1/app/eso/bin/servicemgrmibhigh" "$S1/usb/ShopBackup/orig/servicemgrmibhigh" || fail "servicemgrmibhigh not restored"
cmp -s "$S1/app/eso/bin/apps/dio_manager" "$S1/usb/ShopBackup/orig/dio_manager" || fail "dio_manager not restored"
[ ! -e "$S1/app/eso/bin/servicemgrmibhigh0" ] || fail "servicemgrmibhigh0 not removed on rollback"
[ ! -e "$S1/ota/modkit/modkit_persist.sh" ] || fail "modkit_persist.sh not removed on rollback"
! grep -q "rsa-key-20210318" "$S1/app/root/.ssh/authorized_keys" || fail "rollback should NOT re-inject unknown SSH key"
# 确认紧接着跑 modkit_install.sh 末尾时会报 unknown patch state 且不会覆写还原好的商业脚本！
if run_upstream_modkit_tail "$S1/app" "$S1/ota" >/dev/null 2>&1; then
    fail "modkit_install.sh should have aborted with unknown patch state after rollback!"
fi
cmp -s "$S1/app/eso/bin/servicemgrmibhigh" "$S1/usb/ShopBackup/orig/servicemgrmibhigh" || fail "modkit tail overwrote rolled-back script!"
pass "rollback + ModKit protection verified"

# ---- 场景 7：回滚后再次接管（全闭环验证） ----
echo "=== [7] Re-takeover after rollback ==="
rm -rf "$S1/usb/Mods/VW_ShopRollback"
mkdir -p "$S1/usb/Mods/VW_ShopTakeover/Update"
OUT7=$(RELEASE_VERSION="MH2p_CN_VWG36_P2838" \
    MEDIA_PATH="$S1/usb" \
    MOD_PATH="$S1/usb/Mods/VW_ShopTakeover/Update" \
    TAKEOVER_APP_ROOT="$S1/app" \
    TAKEOVER_OTA_ROOT="$S1/ota" \
    TAKEOVER_PERSIST_ROOT="$S1/persist_new" \
    TAKEOVER_SKIP_MOUNT=1 \
    sh "$TAKEOVER_SH")
echo "$OUT7" | grep -q "preserving intact" || fail "re-takeover should preserve original backup"
echo "$OUT7" | grep -q "VW_ShopTakeover done" || fail "re-takeover failed"
run_upstream_modkit_tail "$S1/app" "$S1/ota" >/dev/null
grep -q "modkit_persist" "$S1/app/eso/bin/servicemgrmibhigh" || fail "re-takeover ModKit handoff failed"
cmp -s "$S1/app/eso/bin/apps/dio_manager" "$S1/app/eso/bin/apps/dio_manager.org" || fail "re-takeover dio_manager mismatch"
pass "full round-trip (takeover -> rollback -> re-takeover) verified"

# ---- 场景 8：VW_ShopAudit 严格只读摸底测试 ----
echo "=== [8] VW_ShopAudit read-only audit on Issue #16 variant B ==="
S8="$TMP/s8"
setup_car "$S8"
mkdir -p "$S8/sys/etc/eso/production" "$S8/crash/core" "$S8/usb/Mods/VW_ShopAudit/Update"
_pre_sm_crc=$(cksum "$S8/app/eso/bin/servicemgrmibhigh")
_pre_dio_crc=$(cksum "$S8/app/eso/bin/apps/dio_manager")
RELEASE_VERSION="MH2p_CN_VWG36_P2838" \
    REGION="CN" OEM="VW" TYPE="G36" RELEASE_TYPE="P" SOFTWARE_VERSION="2838" \
    MEDIA_PATH="$S8/usb" \
    MOD_PATH="$S8/usb/Mods/VW_ShopAudit/Update" \
    AUDIT_APP_ROOT="$S8/app" \
    AUDIT_OTA_ROOT="$S8/ota" \
    AUDIT_SYSTEM_ROOT="$S8/sys" \
    AUDIT_PERSIST_ROOT="$S8/persist_new" \
    AUDIT_CRASH_ROOT="$S8/crash" \
    AUDIT_SKIP_MOUNT=1 \
    sh "$AUDIT_SH" >/dev/null
RPT="$S8/usb/ShopAudit/audit_1/REPORT.txt"
[ -f "$RPT" ] || fail "REPORT.txt not generated"
grep -q "dio_manager.org present" "$RPT" || fail "REPORT.txt missing dio_manager.org warning"
grep -q "servicemgrmibhigh0 is MISSING" "$RPT" || fail "REPORT.txt missing servicemgrmibhigh0 warning"
grep -q "shop payload/hook files detected" "$RPT" || fail "REPORT.txt missing .j/autorunner warning"
grep -q "rsa-key-20210318" "$RPT" || fail "REPORT.txt missing external SSH key warning"
grep -q "granted.fecs" "$RPT" || fail "REPORT.txt missing fec listing"
[ "$(cksum "$S8/app/eso/bin/servicemgrmibhigh")" = "$_pre_sm_crc" ] || fail "VW_ShopAudit modified servicemgrmibhigh!"
[ "$(cksum "$S8/app/eso/bin/apps/dio_manager")" = "$_pre_dio_crc" ] || fail "VW_ShopAudit modified dio_manager!"
pass "VW_ShopAudit read-only audit + zero mutations verified"

echo ""
echo "ALL 8 SANDBOX SCENARIOS PASSED!"
