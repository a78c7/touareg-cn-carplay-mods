#!/bin/ksh
# Runs at every boot (ModKit Persist). Switch files for cl_probe.so / VWClusterNav and the cluster daemon.
# Disable without uninstalling: create an empty file /mnt/app/eso/bin/apps/cluster/disable
D=/mnt/app/eso/bin/apps/cluster
touch /tmp/cl_probe_on /tmp/cl_alt_on /tmp/vwcn_noRG
echo none > /tmp/cl_alt_cmd
chmod 666 /tmp/cl_alt_cmd
export LD_LIBRARY_PATH=/mnt/app/armle/lib:/mnt/app/armle/usr/lib:/mnt/app/armle/graphics:/eso/lib:/lib:/usr/lib
export CL_ZOOM=0
if [ -e $D/disable ]; then
    rm -f /tmp/cl_alt_on /tmp/vwcn_noRG
    echo "carplay cluster disabled"
elif [ -x $D/clusterd ] && ! pidin -p clusterd 2>/dev/null | grep -q clusterd; then
    ( trap "" HUP; exec $D/clusterd $D/cl_render capture=h264 xres=1920 yres=639 mode=stretch ) </dev/null >/dev/null 2>&1 &
    echo "clusterd started"
fi
# every minute: cap the RAM logs; copy them to a USB drive that has a "cluster_logs" folder (troubleshooting)
(
while true; do
    sleep 60
    for f in /tmp/cl_probe.log /tmp/clusterd.log; do
        if [[ -f $f ]]; then
            set -- $(ls -l $f)
            [[ -n "$5" && "$5" -gt 1000000 ]] && : > $f
        fi
    done
    for m in /fs/usb0_0 /fs/usb1_0 /fs/sda0 /fs/sdb0; do
        if [[ -d $m/cluster_logs ]]; then
            mount -uw $m 2>/dev/null
            # v19.5: per-boot folder chosen by VW_HUD_2 (boot_N); without it (HUD_2 not installed) choose it here
            if [[ ! -f /tmp/cl_bootdir && ! -f /tmp/vwcn_persist.log ]]; then
                n=1; while [[ -d $m/cluster_logs/boot_$n ]]; do n=$((n + 1)); done
                mkdir $m/cluster_logs/boot_$n 2>/dev/null && echo boot_$n > /tmp/cl_bootdir
            fi
            [[ -f /tmp/cl_bootdir ]] && cp -f /tmp/cl_probe.log /tmp/clusterd.log /tmp/cl_render_dbg /tmp/cl_car_dbg $m/cluster_logs/$(cat /tmp/cl_bootdir)/ 2>/dev/null
            sync
        fi
    done
done
) </dev/null >/dev/null 2>&1 &
