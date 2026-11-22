#!/bin/ksh
# VW Touareg CN P-series (MH2p_CN_VWG36_P*): CarPlay map on the instrument cluster, step 3.
# FREE & OPEN SOURCE: https://github.com/a78c7/touareg-cn-carplay-mods (PolyForm Noncommercial 1.0.0 - DO NOT SELL)
echo "=== Free & Open Source: https://github.com/a78c7/touareg-cn-carplay-mods (DO NOT SELL) ==="
# Needs VW_HUD_1_RouteGuidance (dio_cluster.so preloaded into dio_manager) and VW_HUD_2_ClusterNav.
# - cl_probe.so : preloaded into dio_manager next to dio_cluster.so; advertises a cluster display to the iPhone
#                 (altScreen), dynamically captures HomeKit pairing pointer, receives + decrypts stream 111 -> /cluster_h264_shm
# - cl_render   : H.264 decoder/renderer for the cluster window (based on fifthBro/mh2p-cluster cluster.c, CC BY-NC-SA 4.0)
# - clusterd    : starts cl_render while the stream is live, switches the cluster back to the stock map when it ends
# - dash.conf   : info box on the cluster map (which values, order, position)
# - the cluster display switching used by clusterd lives in VWClusterNav_v6.jar (installed by VW_HUD_2_ClusterNav)
# smartphone_integrator.json is backed up to the USB first; uninstall.sh removes only our LD_PRELOAD entry.
# same line count, non-empty (awk: the ModKit USB install environment has no wc)
same_lines() { a=$(awk 'END{print NR}' "$1" 2>/dev/null); b=$(awk 'END{print NR}' "$2" 2>/dev/null); [ -n "$a" ] && [ "$a" = "$b" ]; }
D=/mnt/app/eso/bin/apps/cluster
SI=/mnt/system/etc/eso/production/smartphone_integrator.json
DIO=$D/dio_cluster.so
PR=$D/cl_probe.so

case "$RELEASE_VERSION" in
    MH2p_CN_VWG36_P*) ;;
    *) echo "abort: built for CN VW Touareg P-series (MH2p_CN_VWG36_P*), this unit is $RELEASE_VERSION"; exit 0 ;;
esac
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
HAD_CLUSTER=0
[[ -f $D/dash.conf ]] && HAD_CLUSTER=1
mkdir -p $D
# 1) binaries (copy then rename: a running dio_manager keeps the old inode)
for f in cl_probe.so cl_render clusterd; do
    cp -v $MOD_PATH/$f $D/$f.new && chmod 755 $D/$f.new && mv -f $D/$f.new $D/$f
done
# settings (car position per navigation app etc.): keep an existing cl.conf, always refresh the reference copy
cp -v $MOD_PATH/cl.conf $D/cl.conf.default && chmod 644 $D/cl.conf.default
[[ ! -f $D/cl.conf ]] && cp -v $MOD_PATH/cl.conf $D/cl.conf && chmod 644 $D/cl.conf
[[ -f $MOD_PATH/cl.sh ]] && cp -v $MOD_PATH/cl.sh $D/cl.sh && chmod 755 $D/cl.sh
# info box items/order/position (v17+): if upgrading an existing install while VW_HUD_4_OkMapSwitch is "skip",
# preserve existing trip_base and existing switch_off; if VW_HUD_4_OkMapSwitch is also in Mods/, it runs right after step 3.
if [[ -f $MOD_PATH/dash.conf ]]; then
    cp -v $MOD_PATH/dash.conf $D/dash.conf.new
    if [ "$HAD_CLUSTER" = "1" ] && [[ -f $D/dash.conf ]]; then
        oldv=$(awk '/^trip_base=/{ sub(/\r$/, ""); v=$0 } END{ print v }' $D/dash.conf 2>/dev/null || true)
        newv=$(awk '/^trip_base=/{ sub(/\r$/, ""); v=$0 } END{ print v }' $D/dash.conf.new 2>/dev/null || true)
        if [ -n "$oldv" ] && [ "$newv" = "trip_base=0" ]; then
            awk -v kv="$oldv" 'index($0, "trip_base=") == 1 { print kv; next } { print }' \
                $D/dash.conf.new > $D/dash.conf.tmp && mv -f $D/dash.conf.tmp $D/dash.conf.new
        fi
    fi
    chmod 644 $D/dash.conf.new && mv -f $D/dash.conf.new $D/dash.conf
fi
# Clean first-time install: OK double-tap map switch defaults to OFF unless VW_HUD_4_OkMapSwitch is installed
if [ "$HAD_CLUSTER" = "0" ]; then
    if [[ -f $MOD_PATH/switch_off ]]; then
        : > $D/switch_off && chmod 644 $D/switch_off
    else
        rm -f $D/switch_off
    fi
fi
# v23 60fps AI custom theme engine (opt-in via configurator; absent -> stay on battle-tested v22.1 path)
# Auto-enables v23_mode if a non-empty themes_user/my_theme.js is deployed without an explicit v23_off marker.
if [[ -f $MOD_PATH/themes_user/my_theme.js ]]; then
    mkdir -p $D/themes_user
    cp -v $MOD_PATH/themes_user/my_theme.js $D/themes_user/my_theme.js && chmod 644 $D/themes_user/my_theme.js
fi
if [[ -f $MOD_PATH/v23_mode ]]; then
    : > $D/v23_mode && chmod 644 $D/v23_mode
    echo "v23_mode: enabled (Beta opt-in, 3-layer fuse active)"
elif [[ -s $MOD_PATH/themes_user/my_theme.js && ! -f $MOD_PATH/v23_off ]]; then
    : > $D/v23_mode && chmod 644 $D/v23_mode
    echo "v23_mode: auto-enabled (custom themes_user/my_theme.js present, 3-layer fuse active)"
else
    rm -f $D/v23_mode
fi
rm -f $D/disable $D/safe_mode $D/triple_off $D/tire_ref.state /tmp/ultra_theme /tmp/media_cp.state /tmp/rdk_probe.log /tmp/v23_fallback_v22 /tmp/v23_theme_loading
# v22.0: record zero-wear crash baselines for 3-crash safe-mode fuse
ls /mnt/crashlogs/core/dio_manager*.core.gz 2>/dev/null | awk 'END{print NR+0}' > $D/dio_base_cores
ls /mnt/crashlogs/core/cl_render*.core.gz /mnt/crashlogs/core/clusterd*.core.gz 2>/dev/null | awk 'END{print NR+0}' > $D/render_base_cores
ls -l $D

# 2) json: append cl_probe.so to the dio_cluster.so LD_PRELOAD entry
if grep -q "$PR" $SI; then
    echo "json already patched"
else
    sed -e "s#\"LD_PRELOAD=$DIO\"#\"LD_PRELOAD=$DIO:$PR\"#" $SI > /tmp/si_new.json
    if [ "$(grep -c "LD_PRELOAD=$DIO:$PR\"" /tmp/si_new.json)" != "1" ]; then echo "abort: json edit check failed (json not changed)"; exit 0; fi
    if ! same_lines /tmp/si_new.json $SI; then echo "abort: line count changed (json not changed)"; exit 0; fi
    mount -uw /mnt/system
    cp -v /tmp/si_new.json $SI
    echo "patched:"; grep LD_PRELOAD $SI
fi

# 3) remove legacy pre-v19 Persist directory so two Persist loops never run side-by-side
[[ ! -e "/mnt/ota" ]] && mount -t qnx6 /dev/mnanda0t177.16 /mnt/ota 2>/dev/null
mount -uw /mnt/ota 2>/dev/null || true
rm -rf /mnt/ota/modkit/Mods/VW_CarPlayCluster 2>/dev/null
sync
echo "done"
