#!/bin/ksh
# VW_WiFi_Dev 一键完全卸载（精准还原，不影响已有 mh2p-ssh-access 公钥）

D=/mnt/app/eso/bin/apps/wifi_dev
AK=/mnt/app/root/.ssh/authorized_keys

echo "=== VW_WiFi_Dev uninstall ==="
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app

slay -f touareg_netd 2>/dev/null
slay -f wpa_supplicant-2.5-wapi 2>/dev/null
for p in $(pidin arg | awk '/dhcp\.client/ && /bcm2/ {print $1}'); do kill -9 "$p" 2>/dev/null; done

rm -rf "$D"

# 只移除本模块追加的公钥行，保留原有其他公钥
if [ -f "$AK" ]; then
    grep -v "VW_WiFi_Dev_Managed" "$AK" > "$AK.new" 2>/dev/null || : > "$AK.new"
    chmod 600 "$AK.new"
    mv -f "$AK.new" "$AK"
fi

sync
mount -ur /mnt/app 2>/dev/null
echo "=== VW_WiFi_Dev uninstall done ==="
