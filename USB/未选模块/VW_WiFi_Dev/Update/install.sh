#!/bin/ksh
# VW_WiFi_Dev - MH2p 无线自由开发 (bcm2 手机热点 SSH + touareg.local mDNS + 8080 手机配置页)
# ModKit Update 安装脚本（通用 QNX 6.6 MH2p，不修改任何原厂只读系统文件）

export PATH=/sbin:/usr/sbin:/bin:/usr/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin:$PATH
D=/mnt/app/eso/bin/apps/wifi_dev
SSH_DIR=/mnt/app/root/.ssh
AK=$SSH_DIR/authorized_keys

echo "=== VW_WiFi_Dev install ($RELEASE_VERSION) ==="
echo "=== Free & Open Source: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL) ==="

# 1) 先确保 /mnt/app 已挂载，再校验车机基础组件（不锁死单一小版本）
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
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
    tr -d '\r' < "$MOD_PATH/authorized_keys" | while IFS= read -r line || [ -n "$line" ]; do
        [ -z "$line" ] && continue
        case "$line" in \#*) continue ;; esac
        echo "$line VW_WiFi_Dev_Managed" >> "$AK.new"
    done
    chmod 600 "$AK.new"
    mv -f "$AK.new" "$AK"
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
echo "v23.1" > "$D/ota_version"
rm -f "$D/disable"
sync
# 注意：绝不能在此执行 mount -ur /mnt/app，上游 modkit_install.sh 后续仍需写入 /mnt/app/eso/bin/servicemgrmibhigh
echo "=== VW_WiFi_Dev install done ==="
