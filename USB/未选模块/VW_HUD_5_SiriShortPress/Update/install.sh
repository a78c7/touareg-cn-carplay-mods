#!/bin/ksh
# VW Touareg CN P-series (MH2p_CN_VWG36_P*): Enable short press of steering-wheel voice key to trigger Siri (step 5).
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods (PolyForm Noncommercial 1.0.0 - DO NOT SELL)
# Needs VW_HUD_2_ClusterNav (VWClusterNav_v6.jar v22.0+) installed.

echo "=== Free & Open Source: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL) ==="
D=/mnt/app/eso/bin/apps/cluster

case "$RELEASE_VERSION" in
    MH2p_CN_VWG36_P*) ;;
    *) echo "abort: built for CN VW Touareg P-series (MH2p_CN_VWG36_P*), this unit is $RELEASE_VERSION"; exit 0 ;;
esac

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app
mkdir -p "$D"
: > "$D/siri_short" && chmod 644 "$D/siri_short"
sync
echo "VW_HUD_5_SiriShortPress installed (steering-wheel voice short press -> Siri enabled)"
