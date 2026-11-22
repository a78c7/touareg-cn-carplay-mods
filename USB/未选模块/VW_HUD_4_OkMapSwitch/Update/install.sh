#!/bin/ksh
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods (PolyForm Noncommercial 1.0.0 - DO NOT SELL)
echo "=== Free & Open Source: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL) ==="
# VW Touareg CN P-series (MH2p_CN_VWG36_P*): Enable steering-wheel OK double-tap to switch CarPlay <-> native map (step 4).

D=/mnt/app/eso/bin/apps/cluster

case "$RELEASE_VERSION" in
    MH2p_CN_VWG36_P*) ;;
    *) echo "abort: built for CN VW Touareg P-series (MH2p_CN_VWG36_P*), this unit is $RELEASE_VERSION"; exit 0 ;;
esac

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
if [[ ! -d "$D" ]]; then
    echo "abort: $D not found - install VW_HUD_3_CarPlayCluster first"; exit 0
fi

mount -uw /mnt/app
rm -fv "$D/switch_off" "$D/triple_off"
sync
echo "VW_HUD_4_OkMapSwitch installed (OK double-tap map switch enabled)"
