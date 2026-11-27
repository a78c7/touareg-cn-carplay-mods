#!/bin/ksh
# VW_ShopAudit - MH2p 只读配置与历史改装残留取证模块（供 Issue #10 排查使用）
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods
#
# 设计原则：
# 1. 本脚本严格只读车机分区：不修改/不删除/不改名车机任何文件，不执行 mount -uw/mount -ur 车机分区。
# 2. 在 ModKit Update 阶段运行（早于上游 modkit_install.sh 覆写 servicemgrmibhigh），抓取覆写前的原始现场。
# 3. 严格脱敏：绝不读取 SSH 私钥、host key、wpa.conf、web.pin；shadow 仅保留账户名与哈希前缀/长度；
#    authorized_keys 仅输出算法、尾 8 位与注释；不复制大众原厂二进制或完整 viewhandler.zip。
# 4. 仅依赖 QNX 6.6 RAM disk 原生工具（ksh / dd / awk / ls / mkdir / cp / rm / sync），不用 wc/find/stat/sed -i。

echo "=== VW_ShopAudit v4 (read-only) ==="

# 允许本地沙盒测试通过环境变量重定向路径（真车默认标准分区路径）
_APP="${AUDIT_APP_ROOT:-/mnt/app}"
_OTA="${AUDIT_OTA_ROOT:-/mnt/ota}"
_SYS="${AUDIT_SYSTEM_ROOT:-/mnt/system}"
_PERSIST="${AUDIT_PERSIST_ROOT:-/mnt/persist_new}"
_CRASH="${AUDIT_CRASH_ROOT:-/mnt/crashlogs}"
_MISC="${AUDIT_MISC_ROOT:-/mnt/misc1}"
_SWUP="${AUDIT_SWUP_ROOT:-/mnt/swup}"
_SKIP_MOUNT="${AUDIT_SKIP_MOUNT:-0}"

# 1) 定位 U 盘根目录并确认可写
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

touch "$USB_ROOT/.audit_write_test" 2>/dev/null
if [ ! -e "$USB_ROOT/.audit_write_test" ]; then
    echo "abort: USB drive $USB_ROOT is write protected"
    exit 0
fi
rm -f "$USB_ROOT/.audit_write_test"

# 2) 创建独立自增输出目录 ShopAudit/audit_N（不覆盖历史采集）
AUDIT_BASE="$USB_ROOT/ShopAudit"
mkdir -p "$AUDIT_BASE"
_n=1
while [ -d "$AUDIT_BASE/audit_$_n" ]; do
    _n=$((_n + 1))
done
OUT="$AUDIT_BASE/audit_$_n"
mkdir -p "$OUT"
echo "output dir: $OUT"

# 3) 确保车机分区已只读可读（若未挂载则尝试挂载，绝不执行 mount -uw 或 mount -ur）
if [ "$_SKIP_MOUNT" != "1" ]; then
    [ ! -e "$_APP/eso" ] && mount -t qnx6 /dev/mnanda0t177.1 "$_APP" 2>/dev/null || true
    [ ! -e "$_SYS/etc" ] && mount -o noatime,nosuid,noexec /dev/fs0p1 "$_SYS" 2>/dev/null || true
    [ ! -e "$_OTA" ] && mount -t qnx6 /dev/mnanda0t177.16 "$_OTA" 2>/dev/null || true
fi

# ---- 工具函数 ----

is_elf() {
    [ -f "$1" ] || return 1
    [ "$(dd if="$1" bs=1 skip=1 count=3 2>/dev/null)" = "ELF" ]
}

file_size_bytes() {
    [ -f "$1" ] || { echo 0; return 1; }
    ls -l "$1" 2>/dev/null | awk 'NR==1 { print $5 + 0 }'
}

# 判断是否为适合扫描/打印的非敏感小文本文件（<= 64 KB，排除二进制与密钥/密码文件）
is_safe_small_text() {
    _f="$1"
    [ -f "$_f" ] || return 1
    _bn="${_f##*/}"
    case "$_bn" in
        *.so|*.so.*|*.jar|*.jxe|*.zip|*.gz|*.tar|*.jpg|*.jpeg|*.png|*.bmp|*.ttf|*.otf|*.bin|*.core|*.elf|*.o|*.a|*.db|*.sqlite)
            return 1 ;;
        *id_rsa*|*id_ed25519*|*id_ecdsa*|*id_dsa*|*_key|*.key|*.pem|*.p12|wpa.conf*|web.pin*|shadow*|*passwd*|*secret*)
            return 1 ;;
    esac
    is_elf "$_f" && return 1
    _sz=$(file_size_bytes "$_f")
    [ -n "$_sz" ] && [ "$_sz" -gt 0 ] && [ "$_sz" -le 65536 ] || return 1
    return 0
}

# 脱敏输出文本文件前 N 行（绝不输出私钥块或密码/口令/PSK 行）
dump_sanitized() {
    _f="$1"
    _max="${2:-80}"
    [ -f "$_f" ] || { echo "  (missing: $_f)"; return 0; }
    awk -v maxl="$_max" '
    BEGIN { redacted_key = 0; shown = 0 }
    {
        total++
        if (redacted_key) next
        line = $0
        sub(/\r$/, "", line)
        low = tolower(line)
        if (low ~ /begin .*private key/) {
            print "  | [REDACTED_PRIVATE_KEY_BLOCK]"
            redacted_key = 1
            next
        }
        if (shown >= maxl) next
        if (low ~ /(^|[^a-z])(psk|password|passwd|web\.pin|secret|api_key|token)[ \t]*[:=]/) {
            print "  | [REDACTED_SENSITIVE_LINE]"
        } else {
            printf "  | %s\n", line
        }
        shown++
    }
    END {
        if (total == 0) print "  (empty file)"
        else if (total > maxl) printf "  ... (truncated: %d total lines, showed first %d)\n", total, maxl
    }
    ' "$_f" 2>/dev/null || true
}

# 脱敏解析 shadow 文件（仅保留用户名、哈希前缀/长度与时间字段，绝不输出哈希本体）
dump_shadow_sanitized() {
    _f="$1"
    [ -f "$_f" ] || { echo "  (missing: $_f)"; return 0; }
    awk -F: '
    {
        line = $0
        sub(/\r$/, "", line)
        if (line == "") next
        n = split(line, a, ":")
        if (n < 2) {
            printf "  | [non-colon line, len=%d]\n", length(line)
            next
        }
        h = a[2]
        hlen = length(h)
        if (h == "" || h == "*" || h == "!" || h == "x" || h == "!!") {
            hsum = h
        } else if (substr(h, 1, 1) == "$") {
            # e.g. $6$... or $1$...
            hsum = "<hash:prefix=" substr(h, 1, 3) ",len=" hlen ">"
        } else if (substr(h, 1, 1) == "@") {
            hsum = "<hash:prefix=@,len=" hlen ">"
        } else {
            hsum = "<hash:len=" hlen ">"
        }
        rest = ""
        for (i = 3; i <= n; i++) rest = rest ":" a[i]
        printf "  | %s:%s%s\n", a[1], hsum, rest
    }
    ' "$_f" 2>/dev/null || true
}

# 脱敏解析 authorized_keys（仅输出条数、算法、尾 8 位、注释）
dump_ak_sanitized() {
    _f="$1"
    if [ ! -f "$_f" ]; then
        echo "ssh authorized_keys: none ($_f not found)"
        return 0
    fi
    awk '
    BEGIN { total = 0; managed = 0; ext = 0 }
    /^[ \t]*$/ || /^[ \t]*#/ { next }
    {
        sub(/\r$/, "")
        total++
        if (index($0, "VW_WiFi_Dev_Managed") > 0) {
            managed++
        } else {
            ext++
            ktype = $1
            kbody = $2
            klen = length(kbody)
            ktail = (klen > 8 ? substr(kbody, klen - 7) : kbody)
            kcmt = ""
            for (i = 3; i <= NF; i++) kcmt = (kcmt == "" ? $i : kcmt " " $i)
            ext_list[ext] = ktype " ..." ktail (kcmt != "" ? " (" kcmt ")" : "")
        }
    }
    END {
        if (total == 0) {
            print "ssh authorized_keys: empty (0 active keys)"
        } else if (ext > 0) {
            printf "WARN: %s contains %d non-VW_WiFi_Dev SSH public key(s) (total=%d, managed=%d)\n", FILENAME, ext, total, managed
            for (i = 1; i <= ext; i++) printf "  external_key[%d]: %s\n", i, ext_list[i]
        } else {
            printf "ssh authorized_keys: OK (%d VW_WiFi_Dev managed key(s), 0 external)\n", managed
        }
    }
    ' "$_f" 2>/dev/null || true
}

# 在指定目录的一层文本文件中极速搜索还原/劫持关键词（脱敏、带行号、单文件最多 15 条）
# 性能优化：单次 ls -la + awk 批量筛出 0 < size <= 65536 且非二进制/密钥后缀的小文件（含隐藏文件），
# 避免在 QNX 6.6 上对大目录内几十上百个 ELF 逐个 fork dd/ls 触发 SWDL 看门狗超时。
scan_dir_keywords() {
    _dir="$1"
    [ -d "$_dir" ] || return 0
    _cands=$(ls -la "$_dir" 2>/dev/null | awk '
    /^-/ {
        sz = $5 + 0
        if (sz <= 0 || sz > 65536) next
        name = $NF
        if (name == "." || name == "..") next
        low = tolower(name)
        if (low ~ /\.(so|so\.[0-9]+|jar|jxe|zip|gz|tar|jpg|jpeg|png|bmp|ttf|otf|bin|core|elf|o|a|db|sqlite|pak|crt|der|pem|p12|key)$/) next
        if (low ~ /(id_rsa|id_ed25519|id_ecdsa|id_dsa|_key|wpa\.conf|web\.pin|shadow|passwd|secret)/) next
        print name
    }')
    [ -z "$_cands" ] && return 0
    for _bn in $_cands; do
        _item="$_dir/$_bn"
        [ -f "$_item" ] || continue
        is_elf "$_item" && continue
        awk -v fn="$_item" '
        BEGIN { hits = 0 }
        {
            line = $0
            sub(/\r$/, "", line)
            low = tolower(line)
            if (low ~ /servicemgrmibhigh|challenge|sshd|authorized_keys|\/eso\/bin\/del|\/eso\/bin\/shadow|\/mnt\/app\/eso\/bin\/shadow|cp[ \t]+.*(del|shadow|servicemgrmibhigh)|crontab|slay[ \t]+-9|sleep[ \t]+(60|90|100|120|150|180|240|300)|mount[ \t]+-uw|autorunner|\/eso\/hmi\/lsd\/\.j/) {
                hits++
                if (hits == 1) printf "[HIT] %s\n", fn
                if (hits <= 15) {
                    if (low ~ /(^|[^a-z])(psk|password|passwd|secret|token)[ \t]*[:=]/) {
                        printf "  L%d: [REDACTED_SENSITIVE_LINE]\n", NR
                    } else {
                        printf "  L%d: %s\n", NR, line
                    }
                }
            }
        }
        END {
            if (hits > 15) printf "  ... (%d total matching lines)\n", hits
        }
        ' "$_item" 2>/dev/null || true
    done
}

# 记录告警到汇总列表
WARN_FILE="$OUT/.warns.tmp"
: > "$WARN_FILE"
add_warn() {
    echo "WARN: $1" >> "$WARN_FILE"
}

# ==============================================================================
# [1/5] 启动入口现场 (/mnt/app/eso/bin)
# ==============================================================================
F1="$OUT/01_boot_entry.txt"
{
    echo "=== 01. Boot Entry & $_APP/eso/bin Audit ==="
    _bin="$_APP/eso/bin"
    _entry=$_bin/servicemgrmibhigh
    _entry0=$_bin/servicemgrmibhigh0
    _orgapp=$_bin/servicemgrmibhigh_orgapp

    echo ""
    echo "-- Key entry files (ls -l) --"
    for _f in "$_bin"/servicemgrmibhigh* "$_bin/del" "$_bin/shadow" "$_bin"/runMMX* "$_bin/autorunner" "$_APP/eso/hmi/lsd/.j" "$_OTA/challenge.pub"; do
        [ -e "$_f" ] && ls -l "$_f" 2>/dev/null || true
    done

    _entry_elf=0
    _entry0_elf=0
    is_elf "$_entry" && _entry_elf=1
    is_elf "$_entry0" && _entry0_elf=1

    echo ""
    echo "-- Entry classification --"
    if [ ! -f "$_entry" ]; then
        echo "status: MISSING ($_entry)"
        add_warn "servicemgrmibhigh missing"
    elif [ "$_entry_elf" = "1" ]; then
        echo "status: stock ELF (clean system)"
    elif grep -q "modkit_persist" "$_entry" 2>/dev/null; then
        echo "status: standard ModKit script"
    else
        echo "status: NON-MODKIT THIRD-PARTY SCRIPT!"
        add_warn "servicemgrmibhigh is a non-ModKit third-party script (blocks modkit_persist.sh)"
    fi

    if [ "$_entry_elf" = "0" ] && [ -f "$_entry0" ]; then
        if [ "$_entry0_elf" = "1" ]; then
            echo "servicemgrmibhigh0: stock ELF (OK)"
        else
            echo "servicemgrmibhigh0: NOT ELF! (wrapper or non-binary)"
            add_warn "servicemgrmibhigh0 is NOT ELF (upstream modkit_install.sh will report 'unknown patch state')"
        fi
    elif [ "$_entry_elf" = "0" ] && [ ! -f "$_entry0" ]; then
        echo "servicemgrmibhigh0: MISSING (upstream modkit_install.sh will abort with 'unknown patch state')"
        add_warn "servicemgrmibhigh0 is MISSING while servicemgrmibhigh is non-ELF (blocks ModKit Persist)"
        if [ -f "$_orgapp" ]; then
            if is_elf "$_orgapp"; then
                echo "servicemgrmibhigh_orgapp: ELF ($(file_size_bytes "$_orgapp") B)"
            else
                echo "servicemgrmibhigh_orgapp: NOT ELF ($(file_size_bytes "$_orgapp") B)"
            fi
        fi
    fi

    # 商业双件套关键检测：dio_manager.org / 异常小体积 dio_manager 与 .j/autorunner/challenge.pub
    _dio="$_bin/apps/dio_manager"
    _dio_org="$_bin/apps/dio_manager.org"
    echo ""
    echo "-- dio_manager & shop payload check (Issue #16 variant B) --"
    if [ -e "$_dio_org" ]; then
        _dtag="non-ELF"; is_elf "$_dio" && _dtag="ELF"
        _dotag="non-ELF"; is_elf "$_dio_org" && _dotag="ELF"
        echo "dio_manager     : $_dtag ($(file_size_bytes "$_dio") B)"
        echo "dio_manager.org : $_dotag ($(file_size_bytes "$_dio_org") B)"
        add_warn "dio_manager.org present (shop dio_manager replacement detected; must restore stock dio_manager together with boot entry)"
    elif [ -f "$_dio" ]; then
        _dsz=$(file_size_bytes "$_dio")
        echo "dio_manager     : $(is_elf "$_dio" && echo ELF || echo non-ELF) (${_dsz} B)"
        if [ -n "$_dsz" ] && [ "$_dsz" -gt 0 ] && [ "$_dsz" -lt 500000 ]; then
            add_warn "dio_manager size unusually small (${_dsz} B < 500000; stock is ~958KB)"
        fi
    fi
    if [ -e "$_APP/eso/hmi/lsd/.j" ] || [ -e "$_OTA/challenge.pub" ] || [ -e "$_bin/autorunner" ]; then
        add_warn "shop payload/hook files detected (.j / challenge.pub / autorunner)"
        for _sp in "$_APP/eso/hmi/lsd/.j" "$_OTA/challenge.pub" "$_bin/autorunner"; do
            [ -e "$_sp" ] && echo "  [$(is_elf "$_sp" && echo ELF || echo non-ELF)] $(ls -ld "$_sp" 2>/dev/null)"
        done
    fi

    if [ "$_entry_elf" = "0" ] && [ -f "$_entry" ]; then
        echo ""
        echo "-- servicemgrmibhigh content (sanitized, first 60 lines) --"
        dump_sanitized "$_entry" 60
        if grep -q "challenge" "$_entry" 2>/dev/null || grep -q "sshd" "$_entry" 2>/dev/null; then
            add_warn "servicemgrmibhigh references challenge/sshd (typical commercial-shop hook)"
        fi
    fi

    if [ -f "$_entry0" ] && [ "$_entry0_elf" = "0" ]; then
        echo ""
        echo "-- servicemgrmibhigh0 content (sanitized, first 60 lines) --"
        dump_sanitized "$_entry0" 60
    fi

    if [ -e "$_bin/del" ]; then
        add_warn "suspicious companion file present: $_bin/del"
        echo ""
        echo "-- $_bin/del content (sanitized, first 60 lines) --"
        if is_elf "$_bin/del"; then
            echo "  (ELF binary)"
        else
            dump_sanitized "$_bin/del" 60
        fi
    fi

    if [ -e "$_bin/shadow" ]; then
        add_warn "suspicious companion file present: $_bin/shadow"
        echo ""
        echo "-- $_bin/shadow structure (password hashes redacted) --"
        dump_shadow_sanitized "$_bin/shadow"
    fi

    echo ""
    echo "-- Shell scripts (*.sh) in $_bin (ls -l) --"
    _any_sh=0
    for _f in "$_bin"/*.sh; do
        [ -e "$_f" ] || continue
        ls -l "$_f" 2>/dev/null || true
        _any_sh=1
    done
    [ "$_any_sh" = "0" ] && echo "(none)"

    # 深度寻源（防商业包用 touch -r 伪装原厂时间戳）：单次扫描关键启动脚本异常关键词与目录全列表
    echo ""
    echo "-- $_bin full directory listing (ls -la, catch hidden / non-.sh hooks) --"
    ls -la "$_bin" 2>/dev/null || true

    echo ""
    echo "-- Key boot scripts anomaly check (runMMX/runHMI/runLSD/java/lsd.sh) --"
    for _ksh in "$_bin/runMMX.sh" "$_bin/runHMI.sh" "$_bin/runLSD.sh" "$_bin/java.sh" "$_APP/eso/hmi/lsd/lsd.sh" "$_APP/eso/hmi/lsd/lsd_d.sh"; do
        [ -f "$_ksh" ] || continue
        awk -v fn="$_ksh" '
        BEGIN { hits = 0 }
        {
            line = $0; sub(/\r$/, "", line)
            low = tolower(line)
            if (low ~ /servicemgrmibhigh|modkit|slay|challenge|sshd|\/eso\/bin\/del|\/eso\/bin\/shadow|mount[ \t]+-uw|autorunner|\/eso\/hmi\/lsd\/\.j|sleep[ \t]+(60|90|100|120|150|180|240|300)/) {
                hits++
                if (hits == 1) printf "[SCRIPT-HIT] %s\n", fn
                if (hits <= 20) printf "  L%d: %s\n", NR, line
            }
        }
        END {
            printf "  (%s: %d lines, %d keyword hits)\n", fn, NR, hits
        }
        ' "$_ksh" 2>/dev/null || true
    done

    echo ""
    echo "-- $_APP/eso/hmi/lsd top-level (ls -la, catch hidden payloads like .j) --"
    ls -la "$_APP/eso/hmi/lsd" 2>/dev/null || echo "(not accessible)"

    echo ""
    echo "-- shadow file metadata comparison (hashes redacted) --"
    for _shf in "$_bin/shadow" "$_APP/etc/shadow" "$_SYS/etc/shadow"; do
        [ -e "$_shf" ] && ls -l "$_shf" 2>/dev/null || true
    done
} > "$F1" 2>&1
sync

# ==============================================================================
# [3/5] SSH 残留安全审计 (/mnt/app/root/.ssh)
# ==============================================================================
F3="$OUT/03_ssh_audit.txt"
{
    echo "=== 03. SSH Residual Key & Config Audit (Read-Only, Private Keys Never Read) ==="
    echo ""
    echo "-- $_APP/root/.ssh directory listing --"
    if [ -d "$_APP/root/.ssh" ]; then
        ls -la "$_APP/root/.ssh" 2>/dev/null || true
    else
        echo "($_APP/root/.ssh does not exist)"
    fi

    echo ""
    echo "-- authorized_keys sanitized summary --"
    _ak_out=$(dump_ak_sanitized "$_APP/root/.ssh/authorized_keys")
    echo "$_ak_out"
    case "$_ak_out" in
        WARN:*) add_warn "${_ak_out#WARN: }" ;;
    esac

    echo ""
    echo "-- sshd_config key directives (if present) --"
    _any_sc=0
    for _sc in "$_APP/etc/ssh/sshd_config" "$_SYS/etc/ssh/sshd_config" /etc/ssh/sshd_config; do
        [ -f "$_sc" ] || continue
        _any_sc=1
        ls -l "$_sc" 2>/dev/null || true
        awk '
        /^[ \t]*#/ || /^[ \t]*$/ { next }
        {
            low = tolower($0)
            if (low ~ /port|permitrootlogin|passwordauthentication|authorizedkeysfile|pubkeyauthentication|hostkey/) {
                printf "  | %s\n", $0
            }
        }
        ' "$_sc" 2>/dev/null || true
    done
    [ "$_any_sc" = "0" ] && echo "(no sshd_config found at checked paths)"
} > "$F3" 2>&1
sync

# ==============================================================================
# [4/5] CarPlay / 仪表 / 全屏配置状态 (含 P2853 核对)
# ==============================================================================
F4="$OUT/04_carplay_cluster_fs.txt"
{
    echo "=== 04. CarPlay, Cluster, FullScreen & Crashlogs State ==="

    _si="$_SYS/etc/eso/production/smartphone_integrator.json"
    echo ""
    echo "-- smartphone_integrator.json --"
    if [ -f "$_si" ]; then
        ls -l "$_si" 2>/dev/null || true
        awk '
        {
            lines[NR] = $0
            low = tolower($0)
            if (low ~ /"carplay"|ld_preload|dio_cluster|cl_probe/) mark[NR] = 1
        }
        END {
            printf "total lines: %d\n", NR
            for (i = 1; i <= NR; i++) {
                show = 0
                for (d = -2; d <= 5; d++) if (mark[i - d]) show = 1
                if (show) printf "  L%d: %s\n", i, lines[i]
            }
        }
        ' "$_si" 2>/dev/null || true
    else
        echo "(not found: $_si)"
    fi

    _bp="$_APP/eso/hmi/lsd/config/bundles.properties"
    echo ""
    echo "-- bundles.properties --"
    if [ -f "$_bp" ]; then
        ls -l "$_bp" 2>/dev/null || true
        awk '
        {
            line = $0; sub(/\r$/, "", line)
            all[NR] = line
            if (line ~ /^Bundle\.Startup\.Lateload/ || line ~ /^Bundle\.(Name|Activator)\.(9[0-9]|[1-9][0-9][0-9])/) {
                printf "  L%d: %s\n", NR, line
            }
        }
        END {
            printf "total lines: %d\n", NR
            print "-- tail 20 lines --"
            start = (NR > 20 ? NR - 19 : 1)
            for (i = start; i <= NR; i++) printf "  L%d: %s\n", i, all[i]
        }
        ' "$_bp" 2>/dev/null || true
    else
        echo "(not found: $_bp)"
    fi

    echo ""
    echo "-- $_APP/eso/hmi/lsd/jars (full ls -la) --"
    if [ -d "$_APP/eso/hmi/lsd/jars" ]; then
        ls -la "$_APP/eso/hmi/lsd/jars" 2>/dev/null || true
    else
        echo "(jars dir not found)"
    fi

    _info="$_APP/eso/hmi/lsd/Resources/skin3/info.txt"
    echo ""
    echo "-- skin3/info.txt (CarPlay canvas) --"
    if [ -f "$_info" ]; then
        ls -l "$_info" 2>/dev/null || true
        awk 'tolower($0) ~ /carplay/ { sub(/\r$/, ""); printf "  L%d: %s\n", NR, $0 }' "$_info" 2>/dev/null || true
    else
        echo "(not found: $_info)"
    fi

    _vh="$_APP/eso/hmi/lsd/Resources/skin3/viewhandler.zip"
    echo ""
    echo "-- skin3/viewhandler.zip (metadata only; OEM zip NOT copied) --"
    if [ -f "$_vh" ]; then
        ls -l "$_vh" 2>/dev/null || true
        if command -v cksum >/dev/null 2>&1; then
            echo "cksum: $(cksum "$_vh" 2>/dev/null)"
        fi
        if command -v unzip >/dev/null 2>&1; then
            echo "-- jxe entries (unzip -v) --"
            unzip -v "$_vh" 2>/dev/null | awk 'NR<=3 || /\.jxe/ { print "  " $0 }'
        else
            echo "(unzip not available in RAM disk; size/mtime recorded above)"
        fi
    else
        echo "(not found: $_vh)"
    fi

    echo ""
    echo "-- $_APP/eso/bin/apps/cluster (ls -la & conf) --"
    if [ -d "$_APP/eso/bin/apps/cluster" ]; then
        ls -la "$_APP/eso/bin/apps/cluster" 2>/dev/null || true
        for _bf in "$_APP/eso/bin/apps/cluster/dio_base_cores" "$_APP/eso/bin/apps/cluster/j9_base_crashes" "$_APP/eso/bin/apps/cluster/render_base_cores"; do
            [ -f "$_bf" ] && echo "  baseline $_bf = $(cat "$_bf" 2>/dev/null)"
        done
        for _cf in "$_APP/eso/bin/apps/cluster/cl.conf" "$_APP/eso/bin/apps/cluster/dash.conf"; do
            [ -f "$_cf" ] || continue
            echo "-- $_cf --"
            dump_sanitized "$_cf" 60
        done
    else
        echo "(not installed)"
    fi

    echo ""
    echo "-- $_APP/eso/bin/apps/wifi_dev (filenames only; secrets NOT read) --"
    if [ -d "$_APP/eso/bin/apps/wifi_dev" ]; then
        ls -la "$_APP/eso/bin/apps/wifi_dev" 2>/dev/null || true
    else
        echo "(not installed)"
    fi

    echo ""
    echo "-- $_CRASH (java & core filenames, up to 40 each) --"
    for _cd in "$_CRASH/java" "$_CRASH/core"; do
        echo "[$_cd]"
        if [ -d "$_cd" ]; then
            ls -l "$_cd" 2>/dev/null | awk 'NR<=41 { print "  " $0 } END { if (NR > 41) printf "  ... (%d total lines)\n", NR }'
        else
            echo "  (not present)"
        fi
    done
} > "$F4" 2>&1
sync

# ==============================================================================
# [2/5] 排查“开机 2 分钟还原源”线索 (/mnt/ota, /mnt/app, /mnt/system)
# ==============================================================================
F2="$OUT/02_restore_hunt.txt"
{
    echo "=== 02. Auto-Restore Source & Persistence Hunt ==="
    _ota="$_OTA/modkit"

    echo ""
    echo "-- $_OTA top-level (ls -la) --"
    ls -la "$_OTA" 2>/dev/null || echo "(not accessible)"

    echo ""
    echo "-- $_OTA/{system,app,ot} & $_PERSIST/fec (2-level listing incl. .pers, filenames & sizes only) --"
    for _od in "$_OTA/system" "$_OTA/app" "$_OTA/app/.pers" "$_OTA/ot" "$_PERSIST/fec"; do
        echo "[$_od]"
        if [ -d "$_od" ]; then
            ls -la "$_od" 2>/dev/null || true
            for _subd in "$_od"/*; do
                [ -d "$_subd" ] || continue
                echo "  [$_subd]"
                ls -la "$_subd" 2>/dev/null || true
            done
        else
            echo "  (absent)"
        fi
    done

    echo ""
    echo "-- /mnt/persist top-level (ls -la, if mounted) --"
    if [ -d /mnt/persist ]; then
        ls -la /mnt/persist 2>/dev/null || true
    else
        echo "(not mounted / absent)"
    fi

    echo ""
    echo "-- $_ota top-level & Logs (ls -la) --"
    if [ -d "$_ota" ]; then
        ls -la "$_ota" 2>/dev/null || true
        echo ""
        echo "-- $_ota/Logs (ls -la) --"
        ls -la "$_ota/Logs" 2>/dev/null || echo "(no Logs dir)"
        if [ -f "$_ota/Logs/_ModKit_Persist.log" ]; then
            echo ""
            echo "-- _ModKit_Persist.log (last 80 lines) --"
            awk '{ buf[NR % 80] = $0; n = NR } END { start = (n > 80 ? n - 79 : 1); for (i = start; i <= n; i++) print "  | " buf[i % 80] }' "$_ota/Logs/_ModKit_Persist.log" 2>/dev/null || true
        elif [ -f "$_ota/modkit_persist.sh" ]; then
            echo "WARN: modkit_persist.sh exists, but _ModKit_Persist.log is MISSING!"
            add_warn "modkit_persist.sh exists from prior install, but _ModKit_Persist.log is MISSING"
        fi
    else
        echo "($_ota does not exist)"
    fi

    echo ""
    echo "-- $_ota/Mods/* (Persist & Post scripts) --"
    if [ -d "$_ota/Mods" ]; then
        ls -la "$_ota/Mods" 2>/dev/null || true
        for _d in "$_ota/Mods"/*; do
            [ -d "$_d" ] || continue
            _m="${_d##*/}"
            echo ""
            echo "[Mod: $_m]"
            ls -la "$_d" 2>/dev/null || true
            for _sub in Persist/install.sh Post/install.sh; do
                _s="$_d/$_sub"
                [ -f "$_s" ] || continue
                ls -l "$_s" 2>/dev/null || true
                # 检测敏感/可疑关键词
                _hits=$(awk '
                    { low = tolower($0)
                      if (low ~ /sshd/) h["sshd"]=1
                      if (low ~ /challenge/) h["challenge"]=1
                      if (low ~ /servicemgrmibhigh/) h["servicemgrmibhigh"]=1
                      if (low ~ /shadow/) h["shadow"]=1
                    }
                    END { for (k in h) printf "%s ", k }
                ' "$_s" 2>/dev/null)
                if [ -n "$_hits" ]; then
                    echo "  keywords: $_hits"
                    if [ "$_m" != "VW_WiFi_Dev" ] || [ "$_hits" != "sshd " ]; then
                        add_warn "existing ModKit script $_m/$_sub references: $_hits"
                    fi
                fi
                # 第三方模块或含可疑词的脚本脱敏输出前 80 行
                case "$_m" in
                    VW_HUD_*|VW_WiFi_Dev|VW_FullScreenCarPlay|VW_NoSimPopup|VW_NoBtAuthPopup|VW_Wireless5GHzFix|AppleCarPlay|AndroidAuto)
                        echo "  (known module; sanitized head 25 lines)"
                        dump_sanitized "$_s" 25 ;;
                    *)
                        echo "  (THIRD-PARTY / UNKNOWN module; sanitized up to 100 lines)"
                        add_warn "unknown third-party ModKit persist module found: $_m"
                        dump_sanitized "$_s" 100 ;;
                esac
            done
        done
    else
        echo "($_ota/Mods not present)"
    fi

    echo ""
    echo "-- $_APP/eso/bin/apps top-level (ls -la) --"
    ls -la "$_APP/eso/bin/apps" 2>/dev/null || echo "(not accessible)"

    echo ""
    echo "-- $_APP/root top-level (ls -la) --"
    ls -la "$_APP/root" 2>/dev/null || echo "(not accessible)"

    echo ""
    echo "-- Cron locations check --"
    _any_cron=0
    for _c in "$_APP/etc/crontab" "$_SYS/etc/crontab" "$_APP/var/spool/cron" "$_APP/var/spool/cron/crontabs"; do
        if [ -e "$_c" ]; then
            ls -la "$_c" 2>/dev/null || true
            [ -f "$_c" ] && dump_sanitized "$_c" 40
            _any_cron=1
        fi
    done
    [ "$_any_cron" = "0" ] && echo "(no standard cron files found)"

    echo ""
    echo "-- Keyword scan across common startup/config directories (fast pre-filtered <= 64KB) --"
    for _scan_d in \
        "$_APP/eso/bin" \
        "$_APP/eso/bin/apps" \
        "$_APP/eso/hmi/lsd" \
        "$_APP/eso/hmi/lsd/config" \
        "$_APP/eso/config/production" \
        "$_APP/root" \
        "$_APP/etc" \
        "$_APP/etc/scripts" \
        "$_APP/etc/rc.d" \
        "$_APP/armle/etc" \
        "$_SYS" \
        "$_SYS/etc" \
        "$_SYS/etc/autorunner" \
        "$_SYS/etc/config" \
        "$_SYS/etc/eso" \
        "$_SYS/etc/eso/production" \
        "$_OTA" \
        "$_OTA/.boot" \
        "$_OTA/app" \
        "$_OTA/app/.pers" \
        "$_OTA/system" \
        "$_OTA/ot" \
        "$_OTA/modkit" \
        "$_PERSIST" \
        "$_MISC"
    do
        scan_dir_keywords "$_scan_d"
    done
    for _ota_sub in "$_OTA/app"/* "$_OTA/system"/* "$_OTA/ot"/*; do
        [ -d "$_ota_sub" ] || continue
        scan_dir_keywords "$_ota_sub"
    done

    echo ""
    echo "-- v4 Deep Hunt: autorunner, /mnt/app/etc/scripts, /mnt/system, /mnt/ota/.boot, /mnt/persist_new, /mnt/misc1, /mnt/app top-level --"
    for _ld in \
        "$_SYS/etc/autorunner" \
        "$_APP/etc/scripts" \
        "$_SYS" \
        "$_SYS/etc" \
        "$_SYS/etc/config" \
        "$_SYS/etc/db" \
        "$_SYS/etc/eso" \
        "$_SYS/etc/eso/production" \
        "$_APP" \
        "$_APP/etc" \
        "$_APP/eso/config/production" \
        "$_APP/eso/hmi/lsd/config" \
        "$_OTA/.boot" \
        "$_PERSIST" \
        "$_MISC" \
        "$_SWUP"
    do
        echo "[$_ld]"
        [ -d "$_ld" ] && ls -la "$_ld" 2>/dev/null || echo "  (absent)"
    done

    echo ""
    echo "-- v4 Sanitized text dump of $_SYS/etc/autorunner/* & $_APP/etc/scripts/* (<= 32KB non-ELF) --"
    for _dd in "$_SYS/etc/autorunner" "$_APP/etc/scripts" "$_OTA/.boot"; do
        [ -d "$_dd" ] || continue
        _dcands=$(ls -la "$_dd" 2>/dev/null | awk '
        /^-/ {
            sz = $5 + 0
            if (sz <= 0 || sz > 32768) next
            name = $NF
            if (name == "." || name == "..") next
            low = tolower(name)
            if (low ~ /\.(so|jar|jxe|zip|gz|tar|bin|core|elf|db|sqlite|crt|der|pem|key|p12)$/) next
            if (low ~ /(id_rsa|id_ed25519|_key|wpa\.conf|web\.pin|shadow|passwd|secret)/) next
            print name
        }')
        for _fn in $_dcands; do
            _fp="$_dd/$_fn"
            [ -f "$_fp" ] || continue
            if is_elf "$_fp"; then
                echo "[ELF] $_fp ($(file_size_bytes "$_fp") B)"
            else
                echo "[FILE] $_fp ($(file_size_bytes "$_fp") B)"
                dump_sanitized "$_fp" 60
            fi
        done
    done

    echo ""
    echo "-- v4 Fast non-2020/2019/2018 file scan in $_APP/{bin,sbin,usr/bin,armle/bin,armle/sbin,armle/etc,armle/usr/bin} --"
    for _bd in "$_APP/bin" "$_APP/sbin" "$_APP/usr/bin" "$_APP/armle/bin" "$_APP/armle/sbin" "$_APP/armle/etc" "$_APP/armle/usr/bin"; do
        [ -d "$_bd" ] || continue
        _odd=$(ls -la "$_bd" 2>/dev/null | awk '
        /^-/ {
            # QNX ls -la 列 8 为年份（如 2020）或 HH:MM（如 00:04 表示当年/开机时钟）
            if ($8 != "2020" && $8 != "2019" && $8 != "2018" && $8 != "2017") {
                print "  " $0
            }
        }')
        if [ -n "$_odd" ]; then
            echo "[$_bd]"
            printf '%s\n' "$_odd"
        fi
    done

    for _app_sub in "$_APP/eso/bin/apps"/*; do
        [ -d "$_app_sub" ] || continue
        _an="${_app_sub##*/}"
        case "$_an" in
            cluster|wifi_dev) continue ;;
        esac
        scan_dir_keywords "$_app_sub"
    done
} > "$F2" 2>&1
sync

# ==============================================================================
# [5/5] 汇总总览 00_summary.txt + 一体化 REPORT.txt
# ==============================================================================
F0="$OUT/00_summary.txt"
_rv="$RELEASE_VERSION"
if [ -z "$_rv" ] && [ -x /mnt/app/armle/usr/bin/pc ]; then
    _rv=$(/mnt/app/armle/usr/bin/pc b:46924065:401 2>/dev/null | cut -c 61- | sed ':a;N;$!ba;s/\n//g' | sed -e 's/\.//g' -e 's/ //g')
fi

{
    echo "=== 00. VW_ShopAudit Summary ==="
    echo "RELEASE_VERSION  : ${_rv:-(unknown)}"
    echo "REGION / OEM     : ${REGION:-?} / ${OEM:-?} (TYPE=${TYPE:-?})"
    echo "RELEASE_TYPE/SW  : ${RELEASE_TYPE:-?} / ${SOFTWARE_VERSION:-?}"
    echo "UNAME            : $(uname -a 2>/dev/null || echo unknown)"
    echo "OUTPUT_DIR       : $OUT"
    echo ""
    echo "-- Warnings Summary --"
    if [ -s "$WARN_FILE" ]; then
        cat "$WARN_FILE"
    else
        echo "(no warnings detected)"
    fi
    echo ""
    echo "-- Mounts (read-only view) --"
    mount 2>/dev/null || true
} > "$F0" 2>&1

rm -f "$WARN_FILE"

# 合并生成单文件 REPORT.txt，方便车友直接传单个文本文件或整目录打包
{
    cat "$F0"
    echo ""
    echo "================================================================"
    cat "$F1"
    echo ""
    echo "================================================================"
    cat "$F2"
    echo ""
    echo "================================================================"
    cat "$F3"
    echo ""
    echo "================================================================"
    cat "$F4"
} > "$OUT/REPORT.txt" 2>/dev/null

sync

# 把摘要打印到 ModKit Update 日志方便双保险查看
cat "$F0"
echo "=== VW_ShopAudit done: please pack $OUT (or REPORT.txt) ==="
