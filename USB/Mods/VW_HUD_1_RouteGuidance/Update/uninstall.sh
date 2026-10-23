#!/bin/ksh
# Restore original smartphone_integrator.json and remove dio_cluster.so.
J=/mnt/system/etc/eso/production/smartphone_integrator.json
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system
if grep -q '"carplay"' $MOD_PATH/backup/smartphone_integrator.json 2>/dev/null; then
    mount -uw /mnt/system
    cp -v $MOD_PATH/backup/smartphone_integrator.json $J
    echo "json restored, LD_PRELOAD lines left: $(grep -c LD_PRELOAD $J)"
else
    # no backup on this drive: remove our entries in place (dio_cluster.so and cl_probe.so are deleted below)
    SO=/mnt/app/eso/bin/apps/cluster/dio_cluster.so
    sed -e "s#\"LD_PRELOAD=$SO[^\"]*\", \"DIO_CLUSTER_LOG=1\", ##" $J > /tmp/si_new.json
    if ! grep -q "dio_cluster" /tmp/si_new.json && grep -q '"carplay"' /tmp/si_new.json && [ "$(wc -l < /tmp/si_new.json)" = "$(wc -l < $J)" ]; then
        mount -uw /mnt/system; cp -v /tmp/si_new.json $J; echo "json: our LD_PRELOAD entries removed in place"
    else
        echo "no json backup on USB and in-place edit failed, json not touched"
    fi
fi
mount -uw /mnt/app
rm -fv /mnt/app/eso/bin/apps/cluster/dio_cluster.so
rmdir /mnt/app/eso/bin/apps/cluster 2>/dev/null
sync
echo "done"
