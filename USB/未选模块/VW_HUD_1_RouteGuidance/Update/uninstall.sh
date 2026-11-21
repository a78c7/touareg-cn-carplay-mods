#!/bin/ksh
# Restore original smartphone_integrator.json and remove dio_cluster.so.
# same line count, non-empty (awk: the ModKit USB install environment has no wc)
same_lines() { a=$(awk 'END{print NR}' "$1" 2>/dev/null); b=$(awk 'END{print NR}' "$2" 2>/dev/null); [ -n "$a" ] && [ "$a" = "$b" ]; }
J=/mnt/system/etc/eso/production/smartphone_integrator.json
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system
# 优先就地精准移除 LD_PRELOAD，避免用旧备份整文件覆写冲掉后装的 VW_Wireless5GHzFix 等配置
SO=/mnt/app/eso/bin/apps/cluster/dio_cluster.so
sed -e "s#\"LD_PRELOAD=$SO[^\"]*\", \"DIO_CLUSTER_LOG=1\", ##" $J > /tmp/si_new.json
if ! grep -q "dio_cluster" /tmp/si_new.json && grep -q '"carplay"' /tmp/si_new.json && same_lines /tmp/si_new.json $J; then
    mount -uw /mnt/system
    cp -v /tmp/si_new.json $J.new && mv -f $J.new $J
    echo "json: our LD_PRELOAD entries removed in place"
elif grep -q '"carplay"' $MOD_PATH/backup/smartphone_integrator.json 2>/dev/null; then
    mount -uw /mnt/system
    cp -v $MOD_PATH/backup/smartphone_integrator.json $J.new && mv -f $J.new $J
    echo "json restored from backup, LD_PRELOAD lines left: $(grep -c LD_PRELOAD $J)"
else
    echo "in-place edit and backup restore both skipped, json not touched"
fi
rm -f /tmp/si_new.json
mount -uw /mnt/app
rm -fv /mnt/app/eso/bin/apps/cluster/dio_cluster.so
rmdir /mnt/app/eso/bin/apps/cluster 2>/dev/null
sync
echo "done"
