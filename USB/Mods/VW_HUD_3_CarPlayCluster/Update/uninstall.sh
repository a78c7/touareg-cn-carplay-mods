#!/bin/ksh
# Remove the CarPlay cluster map: our LD_PRELOAD entry and binaries. dio_cluster.so / the HUD jar stay (steps 1/2).
D=/mnt/app/eso/bin/apps/cluster
SI=/mnt/system/etc/eso/production/smartphone_integrator.json
PR=$D/cl_probe.so
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system
if grep -q ":$PR" $SI; then
    sed -e "s#:$PR##" $SI > /tmp/si_new.json
    if [ "$(grep -c "$PR" /tmp/si_new.json)" = "0" ] && [ "$(wc -l < /tmp/si_new.json)" = "$(wc -l < $SI)" ]; then
        mount -uw /mnt/system; cp -v /tmp/si_new.json $SI; echo "json: cl_probe.so removed"
    else
        echo "json edit check failed, json not changed"
    fi
else
    echo "json: cl_probe.so not present"
fi
mount -uw /mnt/app
rm -fv $D/cl_probe.so $D/cl_render $D/clusterd $D/disable $D/cl.conf $D/cl.conf.default $D/cl.sh
rm -rf $D/bak_v14 $D/v15
rmdir $D 2>/dev/null
rm -rf /mnt/ota/modkit/Mods/VW_CarPlayCluster 2>/dev/null
sync
echo "done"
