#!/bin/ksh
# VW Touareg CN (MH2p_CN_VWG36_P2878): Enable steering-wheel OK double-tap to switch CarPlay <-> native map (step 4).

D=/mnt/app/eso/bin/apps/cluster

if [ "$RELEASE_VERSION" != "MH2p_CN_VWG36_P2878" ]; then
    echo "abort: built for MH2p_CN_VWG36_P2878, this unit is $RELEASE_VERSION"; exit 0
fi

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
if [[ ! -d "$D" ]]; then
    echo "abort: $D not found - install VW_HUD_3_CarPlayCluster first"; exit 0
fi

mount -uw /mnt/app
rm -fv "$D/switch_off" "$D/triple_off"
sync
echo "VW_HUD_4_OkMapSwitch installed (OK double-tap map switch enabled)"
