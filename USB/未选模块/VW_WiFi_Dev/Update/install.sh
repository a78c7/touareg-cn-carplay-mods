#!/bin/ksh
# VW_WiFi_Dev - MH2p 无线自由开发 (bcm2 手机热点 SSH + touareg.local mDNS + 8080 手机配置页)
# ModKit Update 安装脚本（通用 QNX 6.6 MH2p，不修改任何原厂只读系统文件）

export PATH=/sbin:/usr/sbin:/bin:/usr/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin:$PATH
D=/mnt/app/eso/bin/apps/wifi_dev
SSH_DIR=/mnt/app/root/.ssh
AK=$SSH_DIR/authorized_keys

echo "=== VW_WiFi_Dev install ($RELEASE_VERSION) ==="
echo "=== Free & Open Source: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL) ==="

# 1) 先确保 /mnt/app 已挂载，再执行只读体检与基础组件校验（不锁死单一小版本）
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app

# 只读体检：在 ModKit 覆写 servicemgrmibhigh 与写入密钥前检查改装店遗留锁死脚本及陌生 SSH 公钥（不写闪存、不阻断安装）
preflight_readonly_check() {
    _app="${PREFLIGHT_APP_ROOT:-/mnt/app}"
    _ota="${PREFLIGHT_OTA_ROOT:-/mnt/ota}"
    _bin="$_app/eso/bin"
    _entry="$_bin/servicemgrmibhigh"
    _entry0="$_bin/servicemgrmibhigh0"
    _ak="$_app/root/.ssh/authorized_keys"
    _warn=0
    echo "--- preflight (read-only) ---"

    if [ -f "$_entry" ]; then
        _m=$(dd if="$_entry" bs=1 skip=1 count=3 2>/dev/null)
        if [ "$_m" = "ELF" ]; then
            echo "entry: stock ELF"
        elif grep -q "modkit_persist" "$_entry" 2>/dev/null; then
            echo "entry: standard ModKit script"
        else
            _warn=1
            _sz=$(ls -l "$_entry" 2>/dev/null | awk 'NR==1 { print $5 + 0 }')
            echo "WARN: suspected shop / third-party servicemgrmibhigh (${_sz:-?} bytes, missing modkit_persist)"
            awk '
            NF > 0 && shown < 15 {
                sub(/\r$/, "")
                low = tolower($0)
                if (low ~ /(private key|password|passwd|psk|secret|token)/) print "  | [REDACTED]"
                else print "  | " $0
                shown++
            }' "$_entry" 2>/dev/null || true
        fi
    else
        echo "entry: missing ($_entry)"
    fi

    if [ -f "$_entry0" ]; then
        _m0=$(dd if="$_entry0" bs=1 skip=1 count=3 2>/dev/null)
        if [ "$_m0" != "ELF" ]; then
            _warn=1
            echo "WARN: servicemgrmibhigh0 is NOT ELF (may trigger ModKit error: unknown patch state)"
        fi
    fi

    if [ -e "$_bin/del" ] || [ -e "$_bin/shadow" ]; then
        _warn=1
        echo "WARN: suspected shop anti-tamper files (del/shadow) detected -- boot entry may be restored ~2min after boot (see Issue #10)"
        [ -e "$_bin/del" ] && ls -l "$_bin/del" 2>/dev/null || true
        [ -e "$_bin/shadow" ] && ls -l "$_bin/shadow" 2>/dev/null || true
    fi

    if [ -f "$_ota/modkit/Logs/_ModKit_Persist.log" ]; then
        echo "persist log: present"
    else
        echo "persist log: absent (normal on first install)"
    fi
    if [ -d "$_ota/modkit/Mods" ]; then
        _mods=""
        for _d in "$_ota/modkit/Mods"/*; do
            [ -d "$_d" ] || continue
            _bn="${_d##*/}"
            _mods="$_mods $_bn"
            case "$_bn" in
                VW_*|AppleCarPlay|AndroidAuto) ;;
                *)
                    if [ -f "$_d/Persist/install.sh" ]; then
                        if grep -qi "sshd" "$_d/Persist/install.sh" 2>/dev/null || grep -qi "challenge" "$_d/Persist/install.sh" 2>/dev/null; then
                            _warn=1
                            echo "WARN: third-party Persist mod '$_bn' references sshd/challenge"
                        fi
                    fi
                    ;;
            esac
        done
        [ -n "$_mods" ] && echo "existing mods:$_mods"
    fi

    if [ -f "$_ak" ]; then
        _ext=$(awk '
        {
            sub(/\r$/, "")
            if ($0 ~ /^[ \t]*$/ || $0 ~ /^[ \t]*#/) next
            if ($0 ~ /VW_WiFi_Dev_Managed/) next
            cnt++
            ki = 1
            for (i = 1; i <= NF; i++) {
                if ($i ~ /^(ssh-|ecdsa-|sk-)/) { ki = i; break }
            }
            ktype = $ki
            kbody = (ki + 1 <= NF) ? $(ki + 1) : ""
            tail = (length(kbody) > 8) ? substr(kbody, length(kbody) - 7) : kbody
            cmt = ""
            for (i = ki + 2; i <= NF; i++) cmt = (cmt == "" ? $i : cmt " " $i)
            printf "  key#%d: %s ...%s %s\n", cnt, ktype, tail, cmt
        }
        ' "$_ak" 2>/dev/null)
        if [ -n "$_ext" ]; then
            _warn=1
            echo "WARN: non-managed SSH public key(s) found in authorized_keys (verify before enabling :2012 SSH):"
            echo "$_ext"
        fi
    fi

    if [ "$_warn" -eq 0 ]; then
        echo "preflight: clean"
    else
        echo "preflight: warnings found (read-only check, continuing install; see docs/07 sec.12 & Issue #10)"
    fi
    echo "--- end preflight ---"
}
preflight_readonly_check || true
if [ ! -x /mnt/app/armle/usr/sbin/wpa_supplicant-2.5-wapi ]; then
    echo "abort: wpa_supplicant-2.5-wapi not found"
    exit 0
fi
if [ ! -f "$MOD_PATH/touareg_netd" ]; then
    echo "abort: touareg_netd missing on USB"
    exit 0
fi

SSH_MODE=1
if [ -f "$MOD_PATH/no_ssh" ] || [ ! -f "$MOD_PATH/authorized_keys" ]; then
    SSH_MODE=0
fi
if [ "$SSH_MODE" -eq 1 ]; then
    if [ ! -x /mnt/app/armle/usr/sbin/sshd ]; then
        echo "abort: sshd not found"
        exit 0
    fi
    if [ ! -f "$MOD_PATH/ssh_host_ed25519_key" ]; then
        echo "abort: host key missing - run configurator first"
        exit 0
    fi
fi

if [ ! -f "$MOD_PATH/wpa.conf" ] && [ ! -f "$D/wpa.conf" ]; then
    echo "abort: wpa.conf missing - run configurator first"
    exit 0
fi
mount -uw /mnt/app

mkdir -p "$D"
chmod 700 "$D"

# 2) 安装 touareg_netd（同分区 .new -> mv 原子替换）
cp -v "$MOD_PATH/touareg_netd" "$D/touareg_netd.new" && chmod 755 "$D/touareg_netd.new" && mv -f "$D/touareg_netd.new" "$D/touareg_netd"

# 3) 安装管理口令 web.pin（兼容旧版平滑升级：U 盘提供则覆盖，否则保留车机既有口令；
#    若旧版未配置过 web.pin，由 touareg_netd 首次启动自动生成 6 位随机口令并在仪表屏显示）
if [ -s "$MOD_PATH/web.pin" ]; then
    cp -v "$MOD_PATH/web.pin" "$D/web.pin.new" && chmod 600 "$D/web.pin.new" && mv -f "$D/web.pin.new" "$D/web.pin"
    rm -f "$D/web.pin.unconfirmed"
elif [ -s "$D/web.pin" ]; then
    echo "info: keeping existing web.pin on head unit"
else
    echo "info: no web.pin found; touareg_netd will auto-generate a random 6-digit PIN on first boot"
fi

# 4) 安装 Wi-Fi 热点配置（如 U 盘有配置则覆盖并备份旧版）
if [ -f "$MOD_PATH/wpa.conf" ]; then
    [ -f "$D/wpa.conf" ] && cp -f "$D/wpa.conf" "$D/wpa.conf.bak"
    cp -v "$MOD_PATH/wpa.conf" "$D/wpa.conf.new" && chmod 600 "$D/wpa.conf.new" && mv -f "$D/wpa.conf.new" "$D/wpa.conf"
else
    echo "warn: wpa.conf missing on USB (keeping existing)"
fi

# 5) 按模式配置终端开发开关与密钥
if [ "$SSH_MODE" -eq 1 ]; then
    rm -f "$D/no_ssh"
    if [ ! -f "$D/ssh_host_ed25519_key" ]; then
        cp -v "$MOD_PATH/ssh_host_ed25519_key" "$D/ssh_host_ed25519_key.new" && chmod 600 "$D/ssh_host_ed25519_key.new" && mv -f "$D/ssh_host_ed25519_key.new" "$D/ssh_host_ed25519_key"
        [ -f "$MOD_PATH/ssh_host_ed25519_key.pub" ] && cp -v "$MOD_PATH/ssh_host_ed25519_key.pub" "$D/ssh_host_ed25519_key.pub" && chmod 644 "$D/ssh_host_ed25519_key.pub"
    else
        echo "keep existing host key"
    fi
    mkdir -p "$SSH_DIR"
    chmod 700 "$SSH_DIR"
    mkdir -p "$MOD_PATH/Backup"
    if [ -f "$AK" ] && [ ! -f "$MOD_PATH/Backup/authorized_keys.orig" ]; then
        cp -f "$AK" "$MOD_PATH/Backup/authorized_keys.orig"
    fi
    if [ -f "$AK" ]; then
        grep -v "VW_WiFi_Dev_Managed" "$AK" > "$AK.new" 2>/dev/null || : > "$AK.new"
    else
        : > "$AK.new"
    fi
    # QNX 6.6 固件无 tr 命令，统一使用原生 awk 去 \r、跳空行与注释并写入公钥
    awk '{
        sub(/\r$/, "")
        if ($0 ~ /^[ \t]*$/ || $0 ~ /^[ \t]*#/) next
        if ($0 ~ /VW_WiFi_Dev_Managed/) print $0
        else print $0 " VW_WiFi_Dev_Managed"
    }' "$MOD_PATH/authorized_keys" >> "$AK.new"
    chmod 600 "$AK.new"
    mv -f "$AK.new" "$AK"
    ak_n=$(awk 'END{print NR+0}' "$AK")
    echo "ssh authorized_keys installed (lines=$ak_n)"
else
    : > "$D/no_ssh"
    if [ -f "$AK" ]; then
        grep -v "VW_WiFi_Dev_Managed" "$AK" > "$AK.new" 2>/dev/null || : > "$AK.new"
        chmod 600 "$AK.new"
        mv -f "$AK.new" "$AK"
    fi
    echo "console-only mode (8080)"
fi

ls /mnt/crashlogs/core/io-pkt-v6-hc*.core.gz 2>/dev/null | awk 'END{print NR+0}' > "$D/iopkt_base_cores"
echo "v23.3" > "$D/ota_version"
rm -f "$D/disable"
sync
# 注意：绝不能在此执行 mount -ur /mnt/app，上游 modkit_install.sh 后续仍需写入 /mnt/app/eso/bin/servicemgrmibhigh
echo "=== VW_WiFi_Dev install done ==="
