#!/bin/ksh
# Remove the CarPlay cluster map: our LD_PRELOAD entry and binaries. dio_cluster.so / the HUD jar stay (steps 1/2).
# same line count, non-empty (awk: the ModKit USB install environment has no wc)
same_lines() { a=$(awk 'END{print NR}' "$1" 2>/dev/null); b=$(awk 'END{print NR}' "$2" 2>/dev/null); [ -n "$a" ] && [ "$a" = "$b" ]; }
D=/mnt/app/eso/bin/apps/cluster
SI=/mnt/system/etc/eso/production/smartphone_integrator.json
PR=$D/cl_probe.so
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system
if grep -q ":$PR" $SI; then
    sed -e "s#:$PR##" $SI > /tmp/si_new.json
    if [ "$(grep -c "$PR" /tmp/si_new.json)" = "0" ] && same_lines /tmp/si_new.json $SI; then
        mount -uw /mnt/system; cp -v /tmp/si_new.json $SI; echo "json: cl_probe.so removed"
    else
        echo "json edit check failed, json not changed"
    fi
else
    echo "json: cl_probe.so not present"
fi
mount -uw /mnt/app
rm -fv $D/cl_probe.so $D/cl_render $D/clusterd $D/disable $D/switch_off $D/triple_off $D/ka_off $D/zoom_flip $D/v23_mode $D/gles60_mode $D/tire_ref.state $D/cl.conf $D/cl.conf.default $D/cl.sh $D/dash.conf /tmp/ultra_theme /tmp/media_cp.state /tmp/rdk_probe.log /tmp/theme_live.js /tmp/theme_status.json /tmp/v23_fallback_v22 /tmp/v23_theme_loading
rm -rf $D/bak_v14 $D/bak_v15 $D/bak_pre_v21 $D/v15 $D/dm $D/themes_user
rmdir $D 2>/dev/null
[[ ! -e "/mnt/ota" ]] && mount -t qnx6 /dev/mnanda0t177.16 /mnt/ota 2>/dev/null
mount -uw /mnt/ota 2>/dev/null || true
rm -rf /mnt/ota/modkit/Mods/VW_CarPlayCluster 2>/dev/null
sync
echo "done"
