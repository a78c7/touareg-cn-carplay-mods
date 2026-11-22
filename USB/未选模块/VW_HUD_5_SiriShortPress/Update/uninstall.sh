#!/bin/ksh
# VW Touareg CN P-series: Disable steering-wheel voice short press -> Siri.

D=/mnt/app/eso/bin/apps/cluster

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app
if [[ -d "$D" ]]; then
    rm -fv "$D/siri_short"
fi
sync
echo "VW_HUD_5_SiriShortPress uninstalled (stock voice key behavior restored)"
