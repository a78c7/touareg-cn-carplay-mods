#!/bin/ksh
# VW Touareg CN (MH2p_CN_VWG36_P2878): CarPlay map on the instrument cluster, step 3.
# Needs VW_HUD_1_RouteGuidance (dio_cluster.so preloaded into dio_manager) and VW_HUD_2_ClusterNav.
# - cl_probe.so : preloaded into dio_manager next to dio_cluster.so; advertises a cluster display to the iPhone
#                 (altScreen), receives + decrypts the 2nd video stream -> /cluster_h264_shm
# - cl_render   : H.264 decoder/renderer for the cluster window (based on fifthBro/mh2p-cluster cluster.c, CC BY-NC-SA 4.0)
# - clusterd    : starts cl_render while the stream is live, switches the cluster back to the stock map when it ends
# - dash.conf   : info box on the cluster map (which values, order, position)
# - the cluster display switching used by clusterd lives in VWClusterNav_v6.jar (installed by VW_HUD_2_ClusterNav)
# smartphone_integrator.json is backed up to the USB first; uninstall.sh removes only our LD_PRELOAD entry.
D=/mnt/app/eso/bin/apps/cluster
SI=/mnt/system/etc/eso/production/smartphone_integrator.json
DIO=$D/dio_cluster.so
PR=$D/cl_probe.so

if [ "$RELEASE_VERSION" != "MH2p_CN_VWG36_P2878" ]; then
    echo "abort: built for MH2p_CN_VWG36_P2878, this unit is $RELEASE_VERSION"; exit 0
fi
touch $MOD_PATH/test.txt
if [[ ! -e $MOD_PATH/test.txt ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f $MOD_PATH/test.txt
for f in cl_probe.so cl_render clusterd cl.conf; do
    [[ ! -f $MOD_PATH/$f ]] && { echo "abort: $f missing on USB"; exit 0; }
done

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system

[[ ! -f $DIO ]] && { echo "abort: $DIO not found - install VW_HUD_1_RouteGuidance first"; exit 0; }
if ! grep -q "LD_PRELOAD=$DIO" $SI; then echo "abort: dio_cluster.so not preloaded in $SI - install VW_HUD_1_RouteGuidance first"; exit 0; fi
echo "step 1 present: OK"

mkdir -p $MOD_PATH/backup
[[ ! -e $MOD_PATH/backup/smartphone_integrator.json ]] && cp -v $SI $MOD_PATH/backup/smartphone_integrator.json

mount -uw /mnt/app
mkdir -p $D
# 1) binaries (copy then rename: a running dio_manager keeps the old inode)
for f in cl_probe.so cl_render clusterd; do
    cp -v $MOD_PATH/$f $D/$f.new && chmod 755 $D/$f.new && mv -f $D/$f.new $D/$f
done
# settings (car position per navigation app etc.): keep an existing cl.conf, always refresh the reference copy
cp -v $MOD_PATH/cl.conf $D/cl.conf.default && chmod 644 $D/cl.conf.default
[[ ! -f $D/cl.conf ]] && cp -v $MOD_PATH/cl.conf $D/cl.conf && chmod 644 $D/cl.conf
[[ -f $MOD_PATH/cl.sh ]] && cp -v $MOD_PATH/cl.sh $D/cl.sh && chmod 755 $D/cl.sh
# info box items/order/position (v17): the USB copy is the owner's choice, always installed
[[ -f $MOD_PATH/dash.conf ]] && cp -v $MOD_PATH/dash.conf $D/dash.conf && chmod 644 $D/dash.conf
rm -f $D/disable
ls -l $D

# 2) json: append cl_probe.so to the dio_cluster.so LD_PRELOAD entry
if grep -q "$PR" $SI; then
    echo "json already patched"
else
    sed -e "s#\"LD_PRELOAD=$DIO\"#\"LD_PRELOAD=$DIO:$PR\"#" $SI > /tmp/si_new.json
    if [ "$(grep -c "LD_PRELOAD=$DIO:$PR\"" /tmp/si_new.json)" != "1" ]; then echo "abort: json edit check failed (json not changed)"; exit 0; fi
    if [ "$(wc -l < /tmp/si_new.json)" != "$(wc -l < $SI)" ]; then echo "abort: line count changed (json not changed)"; exit 0; fi
    mount -uw /mnt/system
    cp -v /tmp/si_new.json $SI
    echo "patched:"; grep -o '"LD_PRELOAD=[^"]*"' $SI
fi

# 3) remove the hand-installed development copy of the boot script, if any
rm -rf /mnt/ota/modkit/Mods/VW_CarPlayCluster 2>/dev/null
sync
echo "done"
