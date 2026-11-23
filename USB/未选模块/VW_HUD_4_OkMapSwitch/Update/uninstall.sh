#!/bin/ksh
# VW Touareg CN P-series (MH2p_CN_VWG36_P*): Disable steering-wheel OK double-tap map switch.

D=/mnt/app/eso/bin/apps/cluster

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app
if [[ -d "$D" ]]; then
    : > "$D/switch_off" && chmod 644 "$D/switch_off"
    rm -f "$D/triple_off"
fi
sync
echo "VW_HUD_4_OkMapSwitch uninstalled (OK double-tap map switch disabled)"
