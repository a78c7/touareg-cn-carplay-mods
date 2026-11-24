#!/bin/ksh
# VW_WiFi_Dev 开机自启守护脚本（ModKit Persist - 稳健防崩版）
# 停用而不卸载：创建空文件 /mnt/app/eso/bin/apps/wifi_dev/disable

export PATH=/sbin:/usr/sbin:/bin:/usr/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin:$PATH
export LD_LIBRARY_PATH=/mnt/app/armle/lib:/mnt/app/armle/usr/lib:/eso/lib:/lib:/usr/lib

D=/mnt/app/eso/bin/apps/wifi_dev
LOG=/tmp/wifi_dev_persist.log
WPA_BIN=/mnt/app/armle/usr/sbin/wpa_supplicant-2.5-wapi
HAPD_BIN=/mnt/app/armle/usr/sbin/hostapd-2.5-wapi
PFCTL=/mnt/app/armle/sbin/pfctl
ED_KEY=$D/ssh_host_ed25519_key

if [ -e "$D/disable" ]; then
    echo "VW_WiFi_Dev disabled" > "$LOG"
    exit 0
fi

# 防止重复拉起多个守护实例
if [ -f /tmp/wifi_dev_guard.pid ]; then
    oldp=$(cat /tmp/wifi_dev_guard.pid 2>/dev/null)
    if [ -n "$oldp" ] && kill -0 "$oldp" 2>/dev/null; then
        exit 0
    fi
fi

echo "=== VW_WiFi_Dev Persist boot ===" > "$LOG"

# 熔断保护（零闪存写入）：若安装后出现了新的 io-pkt-v6-hc 崩溃 core，自动停止自启，绝不陷入重启循环
if [ -f "$D/iopkt_base_cores" ]; then
    base_c=$(cat "$D/iopkt_base_cores" 2>/dev/null)
    cur_c=$(ls /mnt/crashlogs/core/io-pkt-v6-hc*.core.gz 2>/dev/null | awk 'END{print NR+0}')
    if [ -n "$base_c" ] && [ "$cur_c" -gt "$base_c" ]; then
        echo "FUSE TRIPPED: new io-pkt crash detected (base=$base_c cur=$cur_c), aborting" >> "$LOG"
        exit 0
    fi
fi

# 1) 启动/升级 sshd 为 Ed25519 + 原配置 RSA（不重复传 -h rsa，消除 duplicated 告警，且不踢断现有会话）
ensure_sshd() {
    [ -e "$D/no_ssh" ] && return 0
    if [ -f "$ED_KEY" ]; then
        if pidin arg 2>/dev/null | grep "[/]usr/sbin/sshd" | grep -q "ssh_host_ed25519_key"; then
            return 0
        fi
        for p in $(pidin arg 2>/dev/null | awk '/\/usr\/sbin\/sshd/ && !/-R/ {print $1}'); do
            kill "$p" 2>/dev/null
        done
        sleep 1
        /usr/sbin/sshd -p 2012 -h "$ED_KEY" -o PasswordAuthentication=no -o ChallengeResponseAuthentication=no -o PermitRootLogin=without-password </dev/null >> "$LOG" 2>&1
        echo "sshd started (ed25519 key-only, port 2012)" >> "$LOG"
    else
        if ! pidin arg 2>/dev/null | grep -q "[/]usr/sbin/sshd"; then
            /usr/sbin/sshd -p 2012 -o PasswordAuthentication=no -o ChallengeResponseAuthentication=no -o PermitRootLogin=without-password </dev/null >> "$LOG" 2>&1
        fi
    fi
}

# 2) 安全加载 bcm2 pf 规则（限次加载，避免频繁刷新 altq 冲击 io-pkt-v6-hc；
#    按实车网卡状态选底表，兼容有 ecm0、无 ecm0 及走 pf.mlan0.conf 的同平台 MH2p 车型）
pf_tries=0
ensure_pf_bcm2() {
    cur_pf=$("$PFCTL" -sr 2>/dev/null)
    if echo "$cur_pf" | grep -q "pass in quick on bcm2.*port = 8080"; then
        if [ -e "$D/no_ssh" ] || echo "$cur_pf" | grep -q "pass in quick on bcm2.*port = 2012"; then
            return 0
        fi
    fi
    if [ "$pf_tries" -ge 3 ]; then
        return 1
    fi
    if ifconfig ecm0 >/dev/null 2>&1; then
        pf_candidates="/etc/pf.ecm0.conf /etc/pf.conf /etc/pf.mlan0.conf"
    elif echo "$cur_pf" | grep -q "on bcm2"; then
        pf_candidates="/etc/pf.mlan0.conf /etc/pf.conf /etc/pf.ecm0.conf"
    else
        pf_candidates="/etc/pf.conf /etc/pf.mlan0.conf /etc/pf.ecm0.conf"
    fi
    tcp_ports="{2012, 8080}"
    [ -e "$D/no_ssh" ] && tcp_ports="8080"
    pf_tries=$((pf_tries + 1))
    for base_pf in $pf_candidates; do
        [ ! -f "$base_pf" ] && continue
        awk -v tp="$tcp_ports" '
        !done && /block/ && index($0, "$wlan_if") > 0 {
            print "pass out quick on bcm2 all keep state"
            print "pass in quick on bcm2 inet proto udp from any port bootp to any port bootpc keep state"
            print "pass in quick on bcm2 inet proto icmp from any to (bcm2) keep state"
            print "pass in quick on bcm2 inet proto tcp from any to (bcm2) port " tp " keep state"
            print "pass in quick on bcm2 inet proto udp from any to 224.0.0.251 port 5353"
            print "pass in quick on bcm2 inet proto udp from any to (bcm2) port 5353"
            done = 1
        }
        { print }
        ' "$base_pf" > /tmp/pf_bcm2.conf
        if grep -q "pass in quick on bcm2" /tmp/pf_bcm2.conf && "$PFCTL" -nf /tmp/pf_bcm2.conf >> "$LOG" 2>&1; then
            if "$PFCTL" -f /tmp/pf_bcm2.conf >> "$LOG" 2>&1; then
                echo "pf bcm2 loaded (#$pf_tries base=$base_pf)" >> "$LOG"
                return 0
            fi
        fi
    done
    echo "pf bcm2 dry-run failed (#$pf_tries)" >> "$LOG"
    return 1
}

stop_bcm2_dhcp() {
    for p in $(pidin arg 2>/dev/null | awk '/dhcp\.client/ && /bcm2/ {print $1}'); do
        kill -9 "$p" 2>/dev/null
    done
}

# 2.5) RSDB 并发保活：wpa_supplicant 在 bcm2 启动时会重置博通 qwdi 状态导致 bcm0 5GHz AP 退出，
# 补拉原厂 hostapd-2.5-wapi 后 bcm2 几秒内自动重连，实现 5GHz 无线 CarPlay AP (bcm0) 与 2.4GHz STA (bcm2) 真并发。
hapd_tries=0
ensure_hostapd_bcm0() {
    [ -e "$D/no_ap_restore" ] && return 0
    [ ! -s /tmp/ap.config.bcm0 ] && return 0
    [ ! -x "$HAPD_BIN" ] && return 0
    if pidin arg 2>/dev/null | grep -q "[h]ostapd-2.5-wapi"; then
        return 0
    fi
    if [ "$hapd_tries" -ge 3 ]; then
        return 1
    fi
    hapd_tries=$((hapd_tries + 1))
    ( trap "" HUP; exec "$HAPD_BIN" /tmp/ap.config.bcm0 >/tmp/hostapd_bcm0.log 2>&1 ) </dev/null &
    echo "hostapd restored on bcm0 (#$hapd_tries)" >> "$LOG"
}

# 后台独立守护循环
(
    trap "" HUP
    echo $$ > /tmp/wifi_dev_guard.pid

    # 第 1 步：立刻把 sshd 准备好（不碰无线和 pf，零风险）
    sleep 3
    ensure_sshd

    # 第 2 步：等待原车整机开机完成（startupFinishedFlag）+ 额外等 25 秒，
    # 确保原厂博通 Wi-Fi 固件、hostapd、ecm0 4G 拨号与 pf 初始化全部彻底尘埃落定！
    w=0
    while [ $w -lt 60 ]; do
        [ -e /tmp/startupFinishedFlag ] && break
        sleep 1
        w=$((w + 1))
    done
    sleep 25
    [ -e "$D/disable" ] && exit 0

    ensure_pf_bcm2

    # 第 3 步：单实例拉起 wpa_supplicant（严格防止双开撞 qwdi 驱动！）
    if [ -s "$D/wpa.conf" ]; then
        if ! pidin arg 2>/dev/null | grep -q "[w]pa_supplicant-2.5-wapi"; then
            ( trap "" HUP; exec "$WPA_BIN" -D qwdi -i bcm2 -c "$D/wpa.conf" >/tmp/wpa_bcm2.log 2>&1 ) </dev/null &
            echo "wpa_supplicant started on bcm2" >> "$LOG"
        fi
    fi

    # 第 3.5 步：等 wpa_supplicant 初始化完 qwdi 后，检查并恢复 bcm0 5GHz AP（无线 CarPlay）
    sleep 3
    ensure_hostapd_bcm0

    # 第 4 步：单实例拉起 touareg_netd (mDNS touareg.local + bcm2:8080 Web)
    if [ -x "$D/touareg_netd" ]; then
        if ! pidin arg 2>/dev/null | grep -q "[t]ouareg_netd"; then
            ( trap "" HUP; exec "$D/touareg_netd" >/tmp/touareg_netd.log 2>&1 ) </dev/null &
            echo "touareg_netd started" >> "$LOG"
        fi
    fi

    was_active=0
    last_dhcp_ts=0
    tick=0

    while true; do
        [ -e "$D/disable" ] && break

        # 每 30 秒检查一次 pf（限 3 次以内）与守护存活；绝不频繁刷新 pf
        if [ $((tick % 5)) -eq 0 ]; then
            ensure_pf_bcm2
            if [ -x "$D/touareg_netd" ] && ! pidin arg 2>/dev/null | grep -q "[t]ouareg_netd"; then
                ( trap "" HUP; exec "$D/touareg_netd" >/tmp/touareg_netd.log 2>&1 ) </dev/null &
                echo "touareg_netd restarted" >> "$LOG"
            fi
        fi

        # 检查 bcm2 关联状态与 IPv4（换热点时自动清旧 IP 并重新 DHCP，-n -R 不碰原车 ecm0）
        ifout=$(ifconfig bcm2 2>/dev/null)
        if echo "$ifout" | grep -q "status: active"; then
            cur_ip=$(echo "$ifout" | awk '/inet / && !/inet6/ {print $2; exit}')
            if [ "$was_active" -eq 0 ] || [ -z "$cur_ip" ] || [ "$cur_ip" = "0.0.0.0" ]; then
                now=$((tick * 6))
                if [ $((now - last_dhcp_ts)) -ge 15 ] || [ "$was_active" -eq 0 ]; then
                    ensure_pf_bcm2
                    stop_bcm2_dhcp
                    ( trap "" HUP; exec /usr/sbin/dhcp.client -h touareg -H -n -R -t 1 -i bcm2 >/tmp/dhcp_bcm2.log 2>&1 ) </dev/null &
                    last_dhcp_ts=$now
                    echo "bcm2 active -> dhcp requested (prev_ip=$cur_ip)" >> "$LOG"
                fi
            fi
            was_active=1
        else
            if [ "$was_active" -eq 1 ]; then
                stop_bcm2_dhcp
                old_ip=$(echo "$ifout" | awk '/inet / && !/inet6/ {print $2; exit}')
                if [ -n "$old_ip" ] && [ "$old_ip" != "0.0.0.0" ]; then
                    ifconfig bcm2 delete "$old_ip" 2>/dev/null
                fi
                echo "bcm2 disconnected (cleared $old_ip)" >> "$LOG"
                was_active=0
            fi
        fi

        # 控制内存日志大小 <= 512 KB
        if [ $((tick % 10)) -eq 0 ]; then
            for lf in /tmp/wpa_bcm2.log /tmp/hostapd_bcm0.log /tmp/touareg_netd.log /tmp/dhcp_bcm2.log "$LOG"; do
                if [ -f "$lf" ]; then
                    set -- $(ls -l "$lf")
                    [ -n "$5" ] && [ "$5" -gt 524288 ] && : > "$lf"
                fi
            done
        fi

        tick=$((tick + 1))
        sleep 6
    done
) </dev/null >/dev/null 2>&1 &

echo "VW_WiFi_Dev guard spawned"
