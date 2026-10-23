#!/bin/ksh
# Restore stock bundles.properties and remove VWClusterNav jar.
B=/mnt/app/eso/hmi/lsd/config/bundles.properties
[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
mount -uw /mnt/app
if grep -q '^Bundle.Startup.Lateload' $MOD_PATH/backup/bundles.properties 2>/dev/null; then
    cp -v $MOD_PATH/backup/bundles.properties $B
    echo "bundles.properties restored"; grep -n 'Startup.Lateload' $B
else
    echo "no backup on USB - removing our lines in place"
    sed -e 's/^Bundle.Startup.Lateload = 61,62,95,97$/Bundle.Startup.Lateload = 61,62,95/' -e 's/^Bundle.Startup.Lateload = 97,61,62,95$/Bundle.Startup.Lateload = 61,62,95/' -e '/^Bundle.Name.97 = VWClusterNav$/d' -e '/^Bundle.Activator.97 = vwcn.Activator$/d' $B > /tmp/bp_new
    grep -q '^Bundle.Startup.Lateload' /tmp/bp_new && cp -v /tmp/bp_new $B
fi
rm -fv /mnt/app/eso/hmi/lsd/jars/VWClusterNav_*.jar
sync
echo "done"
