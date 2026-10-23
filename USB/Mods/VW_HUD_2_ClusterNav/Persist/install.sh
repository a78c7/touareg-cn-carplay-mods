#!/bin/ksh
# Runs at every boot (early, from the ModKit service-manager hook).
# 1) Pre-create /tmp/cluster_cp.state world-writable. dio_manager first loads the hook as root
#    (creates the file 0644) and loads it again after dropping privileges -> "PPS: FAILED to open"
#    and the state file stays empty. A 0666 file created here first fixes that.
# 2) Background loop: keep it 0666, cap the RAM logs, restart dio_manager once (only in the first
#    3 minutes after boot) if the hook still reports the open failure, copy logs to a USB drive
#    that has a "cluster_logs" folder.
ST=/tmp/cluster_cp.state
PL=/tmp/vwcn_persist.log
[[ ! -e $ST ]] && : > $ST
chmod 666 $ST
echo "boot: $(ls -l $ST)" > $PL
(
restarted=0
t=0
while true; do
    chmod 666 $ST 2>/dev/null
    for f in /tmp/dio_cluster.log /tmp/vwcn.log; do
        if [[ -f $f ]]; then
            set -- $(ls -l $f)
            if [[ -n "$5" && "$5" -gt 2000000 ]]; then : > $f; echo "t=$t truncated $f" >> $PL; fi
        fi
    done
    if [[ $restarted = 0 && $t -lt 180 && -f /tmp/dio_cluster.log ]]; then
        if grep -q 'FAILED to open /tmp/cluster_cp.state' /tmp/dio_cluster.log; then
            restarted=1
            echo "t=$t hook could not open state file -> slay dio_manager (smartphone_integrator restarts it)" >> $PL
            ls -l $ST >> $PL
            slay -f dio_manager >> $PL 2>&1
        fi
    fi
    if [[ ! -e /tmp/vwcn_seen && -f /tmp/vwcn.log ]]; then : > /tmp/vwcn_seen; echo "t=$t /tmp/vwcn.log appeared" >> $PL; fi
    for m in /fs/usb0_0 /fs/usb1_0 /fs/sda0 /fs/sdb0; do
        if [[ -d $m/cluster_logs ]]; then
            D=$m/cluster_logs
            mount -uw $m 2>/dev/null
            cp -f /tmp/vwcn.log /tmp/vwcn_persist.log /tmp/dio_cluster.log $ST /tmp/Performance.txt /tmp/disableReasons $D/ 2>/dev/null
            ls -l /dev/shmem/ > $D/tmp_list.txt 2>&1
            ls -l /mnt/app/eso/hmi/lsd/jars /eso/hmi/lsd/jars > $D/jars_list.txt 2>&1
            grep -n 'Startup.Lateload' /mnt/app/eso/hmi/lsd/config/bundles.properties > $D/bundles_97.txt 2>&1
            grep -n 'Activator.97' /mnt/app/eso/hmi/lsd/config/bundles.properties >> $D/bundles_97.txt 2>&1
            ls -l /eso /eso/hmi/lsd/config/bundles.properties >> $D/bundles_97.txt 2>&1
            pidin -p dio_manager arg > $D/pidin_dio_manager.txt 2>&1
            pidin arg > $D/pidin_all_arg.txt 2>&1
            pidin -p j9 env > $D/pidin_j9_env.txt 2>&1
            ls -lR /mnt/crashlogs/java > $D/crash_java.txt 2>&1
            ls -l /mnt/crashlogs > $D/crash_list.txt 2>&1
            sloginfo > $D/slog.txt 2>&1
            echo "t=$t" > $D/last_copy.txt
            date >> $D/last_copy.txt
            sync
        fi
    done
    sleep 5
    t=$((t + 5))
done
) > /dev/null 2>&1 &
echo "VW_ClusterNav persist loop started"
