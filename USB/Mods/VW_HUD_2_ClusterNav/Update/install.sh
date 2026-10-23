#!/bin/ksh
# VW Touareg CN (MH2p_CN_VWG36_P2878): CarPlay route guidance -> instrument cluster + HUD, step 2.
# The jar keeps the file name VWClusterNav_v6.jar; content is v17 (also used by VW_HUD_3_CarPlayCluster: display switching, info box data).
# Needs step 1 (VW_HUD_1_RouteGuidance: dio_cluster.so preloaded into dio_manager) already installed.
# - copies VWClusterNav_v6.jar (Java 1.4 class files, version 48) to /mnt/app/eso/hmi/lsd/jars/, removes older versions
# - registers it as HMI bundle 97, started LAST in the Lateload group (61,62,95,97)
# bundles.properties is backed up to the USB first (stock copy); uninstall.sh restores it.
B=/mnt/app/eso/hmi/lsd/config/bundles.properties
JD=/mnt/app/eso/hmi/lsd/jars
JAR=VWClusterNav_v6.jar
SO=/mnt/app/eso/bin/apps/cluster/dio_cluster.so
SI=/mnt/system/etc/eso/production/smartphone_integrator.json
L_STOCK='^Bundle.Startup.Lateload = 61,62,95$'
L_V1='^Bundle.Startup.Lateload = 61,62,95,97$'
L_V2='^Bundle.Startup.Lateload = 97,61,62,95$'

if [ "$RELEASE_VERSION" != "MH2p_CN_VWG36_P2878" ]; then
    echo "abort: built for MH2p_CN_VWG36_P2878, this unit is $RELEASE_VERSION"; exit 0
fi
touch $MOD_PATH/test.txt
if [[ ! -e $MOD_PATH/test.txt ]]; then echo "abort: USB is write protected"; exit 0; fi
rm -f $MOD_PATH/test.txt
[[ ! -f $MOD_PATH/$JAR ]] && { echo "abort: $JAR missing on USB"; exit 0; }

[[ ! -e "/mnt/app/eso" ]] && mount -t qnx6 /dev/mnanda0t177.1 /mnt/app
[[ ! -e "/mnt/system/etc" ]] && mount -o noatime,nosuid,noexec /dev/fs0p1 /mnt/system

[[ ! -f $SO ]] && { echo "abort: $SO not found - install VW_HUD_1_RouteGuidance first"; exit 0; }
if ! grep -q "LD_PRELOAD=$SO" $SI; then echo "abort: dio_cluster.so not preloaded in $SI"; exit 0; fi
echo "step 1 present: OK"

mkdir -p $MOD_PATH/backup
if [[ ! -e $MOD_PATH/backup/bundles.properties ]]; then
    if grep -q '^Bundle.Name.97 = VWClusterNav$' $B; then
        # already installed from another drive: rebuild the stock file for the backup
        echo "bundles.properties already patched by an earlier install - rebuilding the stock version as backup"
        sed -e 's/^Bundle.Startup.Lateload = 61,62,95,97$/Bundle.Startup.Lateload = 61,62,95/' -e 's/^Bundle.Startup.Lateload = 97,61,62,95$/Bundle.Startup.Lateload = 61,62,95/' \
            -e '/^Bundle.Name.97 = VWClusterNav$/d' -e '/^Bundle.Activator.97 = vwcn.Activator$/d' -e '/^# VWClusterNav: /d' $B > $MOD_PATH/backup/bundles.properties
    else
        cp -v $B $MOD_PATH/backup/bundles.properties
    fi
fi
if ! grep -q "$L_STOCK" $MOD_PATH/backup/bundles.properties; then
    echo "abort: USB backup is not the stock bundles.properties"; exit 0
fi

mount -uw /mnt/app

# 1) jar
mkdir -p $JD
for old in $JD/VWClusterNav_*.jar; do [[ -f $old && "${old##*/}" != "$JAR" ]] && rm -fv $old; done
cp -v $MOD_PATH/$JAR $JD/$JAR
chmod 644 $JD/$JAR
ls -l $JD

# 2) bundles.properties: rebuild from the stock backup every time (idempotent)
S=$MOD_PATH/backup/bundles.properties
if grep -q '^Bundle.Name.97' $S; then echo "abort: bundle id 97 used in stock file"; exit 0; fi
sed -e 's/^Bundle.Startup.Lateload = 61,62,95$/Bundle.Startup.Lateload = 61,62,95,97/' $S > /tmp/bp_new
echo "" >> /tmp/bp_new
echo "# VWClusterNav: CarPlay route guidance -> cluster/HUD (USB mod VW_ClusterNav)" >> /tmp/bp_new
echo "Bundle.Name.97 = VWClusterNav" >> /tmp/bp_new
echo "Bundle.Activator.97 = vwcn.Activator" >> /tmp/bp_new
if [ "$(grep -c "$L_V1" /tmp/bp_new)" != "1" ]; then echo "abort: edit check 1 failed"; exit 0; fi
if [ "$(grep -c '^Bundle.Activator.97 = vwcn.Activator$' /tmp/bp_new)" != "1" ]; then echo "abort: edit check 2 failed"; exit 0; fi
if [ "$(grep -c '^Bundle.Name.97 = VWClusterNav$' /tmp/bp_new)" != "1" ]; then echo "abort: edit check 3 failed"; exit 0; fi
if [ "$(grep -c '^Bundle\.' /tmp/bp_new)" != "$(( $(grep -c '^Bundle\.' $S) + 2 ))" ]; then echo "abort: edit check 4 failed"; exit 0; fi
cp -v /tmp/bp_new $B
sync
echo "now:"
grep -n 'Startup.Lateload' $B
grep -n 'Name.97' $B
grep -n 'Activator.97' $B
sync
echo "done"
